#include "dml_common.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <ifaddrs.h>
#include <netdb.h>

/* single-threaded stream receive buffer */
static char g_szStreamBuf[DML_RECV_BUFF_LEN * 2];
static int  g_iStreamLen = 0;

static int dml_set_nonblock(int iSock)
{
    int iFlags = fcntl(iSock, F_GETFL, 0);
    if (iFlags < 0) return -1;
    return fcntl(iSock, F_SETFL, iFlags | O_NONBLOCK);
}

static int dml_tcp_connect(const char *pszIp, int iPort, int iTimeoutMs)
{
    struct sockaddr_in stAddr;
    memset(&stAddr, 0, sizeof(stAddr));
    stAddr.sin_family = AF_INET;
    stAddr.sin_port = htons((unsigned short)iPort);
    if (inet_pton(AF_INET, pszIp, &stAddr.sin_addr) != 1)
    {
        return -1;
    }

    int iSock = socket(AF_INET, SOCK_STREAM, 0);
    if (iSock < 0) return -1;

    dml_set_nonblock(iSock);

    int iRet = connect(iSock, (struct sockaddr *)&stAddr, sizeof(stAddr));
    if (iRet < 0 && errno != EINPROGRESS)
    {
        close(iSock);
        return -1;
    }

    struct pollfd stPoll;
    stPoll.fd = iSock;
    stPoll.events = POLLOUT;
    stPoll.revents = 0;
    iRet = poll(&stPoll, 1, iTimeoutMs);
    if (iRet <= 0)
    {
        close(iSock);
        return -1;
    }

    int iErr = 0;
    socklen_t iErrLen = sizeof(iErr);
    getsockopt(iSock, SOL_SOCKET, SO_ERROR, &iErr, &iErrLen);
    if (iErr != 0)
    {
        close(iSock);
        return -1;
    }

    return iSock;
}

int dml_centerd_init(DMLENV *pstEnv)
{
    pstEnv->iCenterdSock = -1;
    pstEnv->iConnected = 0;
    pstEnv->iMasterActive = 0;
    pstEnv->dwPingSeq = 0;
    pstEnv->tLastHeartbeat = time(NULL);
    pstEnv->tLastReconnect = 0;

    pstEnv->pstCenterdMeta = tdr_get_meta_by_name(
        (LPTDRMETALIB)g_szMetalib_tagent_centerd_proto, "TAGENTCENTERDMSG");
    if (NULL == pstEnv->pstCenterdMeta)
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "failed to get meta TAGENTCENTERDMSG");
        return -1;
    }

    /* init exchange shared memory for agent_api (business process register) */
    if (0 != agent_api_init(&pstEnv->pstExchangeMng))
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "agent_api_init failed");
        pstEnv->pstExchangeMng = NULL;
    }

    g_iStreamLen = 0;
    return 0;
}

int dml_centerd_send(DMLENV *pstEnv, TAGENTPKGHEAD *pstHead, TAGENTCENTERDMSGBODY *pstBody)
{
    if (pstEnv->iCenterdSock < 0) return -1;

    TAGENTCENTERDMSG stMsg;
    memset(&stMsg, 0, sizeof(stMsg));
    stMsg.stHead = *pstHead;
    stMsg.stHead.wMagic = TAGENT_MSG_MAGIC;
    stMsg.stHead.bMsgVer = TDR_METALIB_TAGENT_CENTERD_PROTO_VERSION;
    if (NULL != pstBody)
    {
        stMsg.stBody = *pstBody;
    }

    char szNet[DML_RECV_BUFF_LEN];
    TDRDATA stNet;
    TDRDATA stHost;
    stHost.pszBuff = (char *)&stMsg;
    stHost.iBuff = (int)sizeof(stMsg);
    stNet.pszBuff = szNet;
    stNet.iBuff = (int)sizeof(szNet);

    int iRet = tdr_hton(pstEnv->pstCenterdMeta, &stNet, &stHost,
                        TDR_METALIB_TAGENT_CENTERD_PROTO_VERSION);
    if (0 != iRet)
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "tdr_hton failed ret(%d)", iRet);
        return iRet;
    }

    /* ensure head.wLen = total packed size */
    uint16_t wLen = htons((uint16_t)stNet.iBuff);
    memcpy(stNet.pszBuff, &wLen, sizeof(wLen));

    iRet = send(pstEnv->iCenterdSock, stNet.pszBuff, stNet.iBuff, 0);
    if (iRet < 0)
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "send to centerd failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int dml_collect_local_ips(char aszIp[DML_MAX_NETDEVICE][16], int *piCount)
{
    int iCount = 0;
    struct ifaddrs *pstIfa = NULL;
    if (0 != getifaddrs(&pstIfa)) return -1;

    for (struct ifaddrs *p = pstIfa; p != NULL && iCount < DML_MAX_NETDEVICE; p = p->ifa_next)
    {
        if (NULL == p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        void *pv = &((struct sockaddr_in *)p->ifa_addr)->sin_addr;
        inet_ntop(AF_INET, pv, aszIp[iCount], 16);
        if (0 != strncmp(aszIp[iCount], "127.", 4))
        {
            iCount++;
        }
    }
    freeifaddrs(pstIfa);
    *piCount = iCount;
    return 0;
}

static int dml_send_hello(DMLENV *pstEnv)
{
    TAGENTCENTERDMSGBODY stBody;
    memset(&stBody, 0, sizeof(stBody));

    char szHost[DML_MAX_HOSTNAME_LEN] = {0};
    gethostname(szHost, sizeof(szHost) - 1);

    stBody.stHelloReq.wHostNameLen = (uint16_t)strlen(szHost);
    if (stBody.stHelloReq.wHostNameLen >= TAGENT_HSOT_NAME_LEN)
    {
        stBody.stHelloReq.wHostNameLen = TAGENT_HSOT_NAME_LEN - 1;
    }
    memcpy(stBody.stHelloReq.szHostName, szHost, stBody.stHelloReq.wHostNameLen);

    char aszIp[DML_MAX_NETDEVICE][16];
    int iCount = 0;
    dml_collect_local_ips(aszIp, &iCount);
    stBody.stHelloReq.bNet = (unsigned char)iCount;
    for (int i = 0; i < iCount && i < 8; i++)
    {
        struct in_addr stAddr;
        inet_pton(AF_INET, aszIp[i], &stAddr);
        stBody.stHelloReq.netdevice[i] = (uint32_t)stAddr.s_addr;
    }

    TAGENTPKGHEAD stHead;
    memset(&stHead, 0, sizeof(stHead));
    stHead.wMsgCmd = TAGENT_ID_MSG_HELLO;
    stHead.bType = ID_CMD_UP;

    return dml_centerd_send(pstEnv, &stHead, &stBody);
}

static int dml_collect_appinfos(DMLENV *pstEnv, APPINFOS *pstInfos)
{
    memset(pstInfos, 0, sizeof(*pstInfos));

    /* 先注册本机加载的插件 appid（mod_procmng=4 / mod_tbusconfig=2 等） */
    for (int i = 0; i < pstEnv->iPluginNum && pstInfos->dwCount < TCENTERD_MAX_BUSNISSID_PER_PACK; i++)
    {
        pstInfos->astInfoList[pstInfos->dwCount].dwAppid = pstEnv->astPlugin[i].stPlugin.id;
        pstInfos->astInfoList[pstInfos->dwCount].dwBusid = (uint32_t)pstEnv->stConf.iBusinessID;
        tlog_info(pstEnv->pstLogCat, 0, 0, "register appid(%u) busid(%u)",
                  pstInfos->astInfoList[pstInfos->dwCount].dwAppid,
                  pstInfos->astInfoList[pstInfos->dwCount].dwBusid);
        pstInfos->dwCount++;
    }

    /* 追加 Exchange 共享内存中业务进程注册的 appid/busid（去重） */
    if (NULL != pstEnv->pstExchangeMng)
    {
        LPEXCHANGEBLOCK pstBlock = NULL;
        int iLen = 0;
        if (0 == agent_api_get_blocks(pstEnv->pstExchangeMng, &pstBlock, &iLen) &&
            NULL != pstBlock && iLen > 0)
        {
            for (int i = 0; i < iLen && pstInfos->dwCount < TCENTERD_MAX_BUSNISSID_PER_PACK; i++)
            {
                int j = 0;
                for (j = 0; j < (int)pstInfos->dwCount; j++)
                {
                    if (pstInfos->astInfoList[j].dwAppid == pstBlock[i].uiAppid &&
                        pstInfos->astInfoList[j].dwBusid == pstBlock[i].uiBusid)
                    {
                        break;
                    }
                }
                if (j == (int)pstInfos->dwCount)
                {
                    pstInfos->astInfoList[pstInfos->dwCount].dwAppid = pstBlock[i].uiAppid;
                    pstInfos->astInfoList[pstInfos->dwCount].dwBusid = pstBlock[i].uiBusid;
                    tlog_info(pstEnv->pstLogCat, 0, 0, "register exchange appid(%u) busid(%u)",
                              pstBlock[i].uiAppid, pstBlock[i].uiBusid);
                    pstInfos->dwCount++;
                }
            }
            free(pstBlock);
        }
    }
    return 0;
}

static int dml_send_register(DMLENV *pstEnv)
{
    TAGENTCENTERDMSGBODY stBody;
    memset(&stBody, 0, sizeof(stBody));
    dml_collect_appinfos(pstEnv, &stBody.stAgentRegister);

    TAGENTPKGHEAD stHead;
    memset(&stHead, 0, sizeof(stHead));
    stHead.wMsgCmd = TAGENT_ID_MSG_AGENTREGISTER_NEW;
    stHead.bType = ID_CMD_UP;

    tlog_info(pstEnv->pstLogCat, 0, 0, "register %u appid/busid to tcenterd",
              stBody.stAgentRegister.dwCount);
    return dml_centerd_send(pstEnv, &stHead, &stBody);
}

int dml_centerd_connect(DMLENV *pstEnv)
{
    if (pstEnv->iCenterdSock >= 0)
    {
        close(pstEnv->iCenterdSock);
        pstEnv->iCenterdSock = -1;
    }
    pstEnv->iConnected = 0;

    /* try master first */
    int iSock = dml_tcp_connect(pstEnv->stConf.szMasterIp, pstEnv->stConf.iMasterPort, 2000);
    pstEnv->iMasterActive = 1;
    if (iSock < 0)
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "connect master(%s:%d) failed",
                   pstEnv->stConf.szMasterIp, pstEnv->stConf.iMasterPort);
        iSock = dml_tcp_connect(pstEnv->stConf.szSlaveIp, pstEnv->stConf.iSlavePort, 2000);
        pstEnv->iMasterActive = 0;
        if (iSock < 0)
        {
            tlog_error(pstEnv->pstLogCat, 0, 0, "connect slave(%s:%d) failed",
                       pstEnv->stConf.szSlaveIp, pstEnv->stConf.iSlavePort);
            return -1;
        }
    }

    pstEnv->iCenterdSock = iSock;
    pstEnv->iConnected = 1;
    g_iStreamLen = 0;

    dml_send_hello(pstEnv);
    dml_send_register(pstEnv);

    tlog_info(pstEnv->pstLogCat, 0, 0, "connected to tcenterd %s(%s:%d)",
              pstEnv->iMasterActive ? "master" : "slave",
              pstEnv->iMasterActive ? pstEnv->stConf.szMasterIp : pstEnv->stConf.szSlaveIp,
              pstEnv->iMasterActive ? pstEnv->stConf.iMasterPort : pstEnv->stConf.iSlavePort);
    return 0;
}

int dml_centerd_close(DMLENV *pstEnv)
{
    if (pstEnv->iCenterdSock >= 0)
    {
        close(pstEnv->iCenterdSock);
        pstEnv->iCenterdSock = -1;
    }
    pstEnv->iConnected = 0;
    g_iStreamLen = 0;
    return 0;
}

/* dispatch a received TRANSFER message to plugin */
static int dml_dispatch_transfer(DMLENV *pstEnv, TAGENTCENTERDMSG *pstMsg)
{
    TAGENTPLUGINHEAD stHead;
    memset(&stHead, 0, sizeof(stHead));
    stHead.uiAppid = pstMsg->stHead.dwAppid;
    stHead.uiBusid = pstMsg->stHead.dwBusid;
    stHead.uiSourceIP = pstMsg->stHead.ulSourceIP;
    stHead.uiDestIP = pstMsg->stHead.ulDestIP;
    stHead.uMsgCmd = pstMsg->stHead.wMsgCmd;
    stHead.cMsgVer = pstMsg->stHead.bMsgVer;

    TAGENTPLUGIN *pstPlugin = dml_find_plugin(pstEnv, stHead.uiAppid);
    if (NULL == pstPlugin || NULL == pstPlugin->recv)
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "no plugin for appid(%u)", stHead.uiAppid);
        return -1;
    }

    return pstPlugin->recv(pstPlugin, pstMsg->stBody.stTransmit.szBuff,
                           pstMsg->stBody.stTransmit.dwLen, &stHead);
}

static int dml_handle_pong(DMLENV *pstEnv, TAGENTCENTERDMSG *pstMsg)
{
    tlog_debug(pstEnv->pstLogCat, 0, 0, "recv PONG(%u)", pstMsg->stBody.dwPong);
    return 0;
}

static int dml_handle_msg(DMLENV *pstEnv, TAGENTCENTERDMSG *pstMsg)
{
    switch (pstMsg->stHead.wMsgCmd)
    {
    case TAGENT_ID_MSG_TRANSFER:
        return dml_dispatch_transfer(pstEnv, pstMsg);
    case TAGENT_ID_MSG_PONG:
        return dml_handle_pong(pstEnv, pstMsg);
    case TAGENT_ID_MSG_PING:
    {
        TAGENTPKGHEAD stHead;
        memset(&stHead, 0, sizeof(stHead));
        stHead.wMsgCmd = TAGENT_ID_MSG_PONG;
        stHead.bType = ID_CMD_UP;
        TAGENTCENTERDMSGBODY stBody;
        memset(&stBody, 0, sizeof(stBody));
        stBody.dwPong = pstMsg->stBody.dwPing;
        return dml_centerd_send(pstEnv, &stHead, &stBody);
    }
    default:
        tlog_debug(pstEnv->pstLogCat, 0, 0, "recv msgCmd(%u) ignored", pstMsg->stHead.wMsgCmd);
        return 0;
    }
}

/* parse complete frames from stream buffer */
static int dml_parse_stream(DMLENV *pstEnv)
{
    while (g_iStreamLen >= (int)sizeof(TAGENTPKGHEAD))
    {
        uint16_t wLen = 0;
        memcpy(&wLen, g_szStreamBuf, sizeof(wLen));
        wLen = ntohs(wLen);
        if (wLen < sizeof(TAGENTPKGHEAD) || wLen > DML_RECV_BUFF_LEN)
        {
            tlog_error(pstEnv->pstLogCat, 0, 0, "invalid frame len(%u), reset stream", wLen);
            g_iStreamLen = 0;
            return -1;
        }
        if (g_iStreamLen < (int)wLen) return 0;

        TAGENTCENTERDMSG stMsg;
        memset(&stMsg, 0, sizeof(stMsg));
        TDRDATA stNet;
        TDRDATA stHost;
        stNet.pszBuff = g_szStreamBuf;
        stNet.iBuff = (int)wLen;
        stHost.pszBuff = (char *)&stMsg;
        stHost.iBuff = (int)sizeof(stMsg);
        int iRet = tdr_ntoh(pstEnv->pstCenterdMeta, &stHost, &stNet,
                            TDR_METALIB_TAGENT_CENTERD_PROTO_VERSION);
        if (0 != iRet)
        {
            tlog_error(pstEnv->pstLogCat, 0, 0, "tdr_ntoh failed ret(%d)", iRet);
            g_iStreamLen = 0;
            return -1;
        }

        dml_handle_msg(pstEnv, &stMsg);

        int iLeft = g_iStreamLen - (int)wLen;
        if (iLeft > 0) memmove(g_szStreamBuf, g_szStreamBuf + wLen, iLeft);
        g_iStreamLen = iLeft;
    }
    return 0;
}

int dml_centerd_proc(DMLENV *pstEnv)
{
    time_t tNow = time(NULL);

    if (!pstEnv->iConnected)
    {
        if (tNow - pstEnv->tLastReconnect >= DML_RECONNECT_GAP_SEC)
        {
            pstEnv->tLastReconnect = tNow;
            dml_centerd_connect(pstEnv);
        }
        usleep(10000); /* 未连接时睡眠 10ms，避免主循环空转 */
        return 0;
    }

    struct pollfd stPoll;
    stPoll.fd = pstEnv->iCenterdSock;
    stPoll.events = POLLIN;
    stPoll.revents = 0;
    int iRet = poll(&stPoll, 1, 100); /* 100ms 超时，空闲时让出 CPU */
    if (iRet <= 0) return 0;

    char szTmp[DML_RECV_BUFF_LEN];
    iRet = recv(pstEnv->iCenterdSock, szTmp, sizeof(szTmp), 0);
    if (iRet <= 0)
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "recv failed(%d), close connection", iRet);
        dml_centerd_close(pstEnv);
        return -1;
    }

    if (g_iStreamLen + iRet >= (int)sizeof(g_szStreamBuf))
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "stream buffer overflow, reset");
        g_iStreamLen = 0;
        return -1;
    }
    memcpy(g_szStreamBuf + g_iStreamLen, szTmp, iRet);
    g_iStreamLen += iRet;

    return dml_parse_stream(pstEnv);
}

void dml_centerd_heartbeat(DMLENV *pstEnv)
{
    if (!pstEnv->iConnected) return;

    time_t tNow = time(NULL);
    if (tNow - pstEnv->tLastHeartbeat < DML_HEARTBEAT_GAP_SEC) return;
    pstEnv->tLastHeartbeat = tNow;

    TAGENTPKGHEAD stHead;
    memset(&stHead, 0, sizeof(stHead));
    stHead.wMsgCmd = TAGENT_ID_MSG_PING;
    stHead.bType = ID_CMD_UP;

    TAGENTCENTERDMSGBODY stBody;
    memset(&stBody, 0, sizeof(stBody));
    stBody.dwPing = ++pstEnv->dwPingSeq;

    dml_centerd_send(pstEnv, &stHead, &stBody);
}
