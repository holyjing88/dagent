#include "dml_common.h"

#include "tagent_mng_proto_util.h"

#include <sys/stat.h>

static TAGENTMNGUTIL g_stTagentUtil;
static int g_iTagentUtilInited = 0;

static int dml_ftp_util_init(DMLENV *pstEnv)
{
    if (g_iTagentUtilInited) return 0;
    tagent_mng_util_set_logcat(pstEnv->pstLogCat);
    if (0 != tagent_mng_util_init(&g_stTagentUtil))
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "tagent_mng_util_init failed");
        return -1;
    }
    g_iTagentUtilInited = 1;
    return 0;
}

static int dml_ftp_download_one(const char *pszFtpIp, int iPort, const char *pszUser,
                                const char *pszPasswd, const char *pszSrc,
                                const char *pszDst)
{
    /* 使用 curl 下载 ftp 文件；确保目标目录存在 */
    char szDir[512];
    snprintf(szDir, sizeof(szDir), "%s", pszDst);
    char *p = strrchr(szDir, '/');
    if (NULL != p)
    {
        *p = '\0';
        if (szDir[0] != '\0')
        {
            char szMkdir[600];
            snprintf(szMkdir, sizeof(szMkdir), "mkdir -p \"%s\"", szDir);
            system(szMkdir);
        }
    }

    char szCmd[2048];
    snprintf(szCmd, sizeof(szCmd),
             "curl -s -f -o \"%s\" \"ftp://%s:%s@%s:%d%s\"",
             pszDst, pszUser, pszPasswd, pszFtpIp, iPort, pszSrc);

    int iRet = system(szCmd);
    if (0 == iRet) return 0;

    /* 无密码场景重试 */
    snprintf(szCmd, sizeof(szCmd),
             "curl -s -f -o \"%s\" \"ftp://%s:%d%s\"",
             pszDst, pszFtpIp, iPort, pszSrc);
    iRet = system(szCmd);
    return iRet;
}

int dml_ftp_handle_req(DMLENV *pstEnv, TAGENTPLUGIN *pstPlugin,
                       char *pszBuff, int iBuff, TAGENTPLUGINHEAD *pstHead)
{
    if (0 != dml_ftp_util_init(pstEnv)) return -1;

    AGENTMNGPKG stReq;
    memset(&stReq, 0, sizeof(stReq));
    if (0 != tagent_mng_util_unpack_pkg(&g_stTagentUtil, &stReq, pszBuff, iBuff,
                                        TDR_METALIB_TAGENTMNGPROTO_VERSION))
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "tagent_mng_util_unpack_pkg failed");
        return -1;
    }

    LPFTPDOWNLOADFILEREQ pstReq = &stReq.stPkg.stFtpDownloadFileReq;
    LPFTPDOWNLOADFILERES pstRes = &stReq.stPkg.stFtpDownloadFileRes;

    tlog_info(pstEnv->pstLogCat, 0, 0,
              "ftp download req callback(%d) count(%d) server(%s:%d)",
              pstReq->iCallBackId, pstReq->iCount, pstReq->szFtpSvrIp, pstReq->iFtpSvrPort);

    stReq.iSelector = TAGENT_ID_MSG_FTP_DOWNLOAD_FILE_RES;
    pstRes->iCallBackId = pstReq->iCallBackId;
    pstRes->iCount = pstReq->iCount;

    for (int i = 0; i < pstReq->iCount && i < MAX_FTP_FILE_NUM; i++)
    {
        LPDLFILEREQ pstFile = &pstReq->astFileArr[i];
        LPDLFILERSP pstRsp = &pstRes->astResult[i];
        STRNCPY(pstRsp->szSourceFile, pstFile->szSourceFile, sizeof(pstRsp->szSourceFile));

        int iRet = dml_ftp_download_one(pstReq->szFtpSvrIp, pstReq->iFtpSvrPort,
                                        pstReq->szFtpUser, pstReq->szFtpPasswd,
                                        pstFile->szSourceFile, pstFile->szTargetFile);
        pstRsp->iRetCode = iRet;
        if (0 != iRet)
        {
            snprintf(pstRsp->szError, sizeof(pstRsp->szError),
                     "download %s failed ret(%d)", pstFile->szSourceFile, iRet);
        }
        else
        {
            tlog_info(pstEnv->pstLogCat, 0, 0, "ftp download %s -> %s succ",
                      pstFile->szSourceFile, pstFile->szTargetFile);
        }
    }

    char szNet[40960];
    int iNetLen = (int)sizeof(szNet);
    if (0 != tagent_mng_util_pack_pkg(&g_stTagentUtil, szNet, &iNetLen, &stReq,
                                      TDR_METALIB_TAGENTMNGPROTO_VERSION))
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "tagent_mng_util_pack_pkg failed");
        return -1;
    }

    /* 回送 FTP 下载结果给插件 */
    TAGENTPLUGINHEAD stRspHead;
    memset(&stRspHead, 0, sizeof(stRspHead));
    stRspHead.uiAppid = pstHead->uiAppid;
    stRspHead.uiBusid = pstHead->uiBusid;
    stRspHead.uMsgCmd = TAGENT_ID_MSG_FTP_DOWNLOAD_FILE_RES;
    stRspHead.cMsgVer = 0;

    if (NULL != pstPlugin && NULL != pstPlugin->recv)
    {
        return pstPlugin->recv(pstPlugin, szNet, iNetLen, &stRspHead);
    }
    return -1;
}
