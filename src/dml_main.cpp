#include "dml_common.h"

#include <stdarg.h>
#include <signal.h>

#define DML_MAJOR 0
#define DML_MINOR 1
#define DML_REV   0
#define DML_BUILD  0

DMLENV g_stEnv;

static TAPPCTX gs_stAppCtx;

/* plugin send: 由宿主提供给 mod_*.so 的发送接口 */
int dml_plugin_send(TAGENTPLUGIN *p, char *ptr, size_t size, TAGENTPLUGINHEAD *pstHead)
{
    if (NULL == pstHead) return -1;

    if (pstHead->uMsgCmd == TAGEND_PLUGIN_MSG_TRANSFER)
    {
        TAGENTPKGHEAD stHead;
        memset(&stHead, 0, sizeof(stHead));
        stHead.wMsgCmd = TAGENT_ID_MSG_TRANSFER;
        stHead.dwAppid = pstHead->uiAppid;
        stHead.dwBusid = pstHead->uiBusid;
        stHead.bType = ID_CMD_UP;
        stHead.ulSourceIP = pstHead->uiSourceIP;
        stHead.ulDestIP = pstHead->uiDestIP;

        TAGENTCENTERDMSGBODY stBody;
        memset(&stBody, 0, sizeof(stBody));
        if (size > sizeof(stBody.stTransmit.szBuff))
        {
            size = sizeof(stBody.stTransmit.szBuff);
        }
        memcpy(stBody.stTransmit.szBuff, ptr, size);
        stBody.stTransmit.dwLen = (uint32_t)size;

        return dml_centerd_send(&g_stEnv, &stHead, &stBody);
    }
    else if (pstHead->uMsgCmd == DML_MSG_FTP_DOWNLOAD_FILE_REQ)
    {
        return dml_ftp_handle_req(&g_stEnv, p, ptr, (int)size, pstHead);
    }

    tlog_error(g_stEnv.pstLogCat, 0, 0, "dml_plugin_send: unsupported msgcmd(%u)", pstHead->uMsgCmd);
    return -1;
}

int dml_plugin_logging(TAGENTPLUGIN *p, int priority, int id, int cls, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    tlog_log_va_list(g_stEnv.pstLogCat, priority, (unsigned int)id,
                     (unsigned int)cls, fmt, ap);
    va_end(ap);
    return 0;
}

static int dml_init(TAPPCTX *a_pstAppCtx, void *pvArg)
{
    DMLENV *pstEnv = &g_stEnv;
    memset(pstEnv, 0, sizeof(*pstEnv));
    pstEnv->pstAppCtx = a_pstAppCtx;

    tapp_get_category(NULL, &pstEnv->pstLogCat);
    if (NULL == pstEnv->pstLogCat)
    {
        printf("Error: tapp_get_category failed\n");
        return -1;
    }

    if (0 != dml_load_conf(&pstEnv->stConf, a_pstAppCtx->pszConfFile))
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "load conf %s failed",
                   a_pstAppCtx->pszConfFile ? a_pstAppCtx->pszConfFile : "");
        return -1;
    }

    tlog_info(pstEnv->pstLogCat, 0, 0,
              "conf master(%s) slave(%s) lib(%s) cfg(%s) tick(%d)",
              pstEnv->stConf.szMaster, pstEnv->stConf.szSlave,
              pstEnv->stConf.szLib, pstEnv->stConf.szCfg, pstEnv->stConf.iTickUs);

    if (0 != dml_centerd_init(pstEnv))
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "dml_centerd_init failed");
        return -1;
    }

    if (0 != dml_load_plugins(pstEnv))
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "dml_load_plugins failed");
    }

    dml_centerd_connect(pstEnv);

    return 0;
}

static int dml_proc(TAPPCTX *a_pstAppCtx, void *pvArg)
{
    return dml_centerd_proc(&g_stEnv);
}

static int dml_tick(TAPPCTX *a_pstAppCtx, void *pvArg)
{
    dml_centerd_proc(&g_stEnv);
    dml_centerd_heartbeat(&g_stEnv);
    dml_plugin_timer(&g_stEnv);
    return 0;
}

static int dml_reload(TAPPCTX *a_pstAppCtx, void *pvArg)
{
    DMLENV *pstEnv = &g_stEnv;
    tlog_info(pstEnv->pstLogCat, 0, 0, "reload conf %s",
              a_pstAppCtx->pszConfFile ? a_pstAppCtx->pszConfFile : "");

    if (NULL != a_pstAppCtx->pszConfFile)
    {
        DMLTAGENTCONF stConf;
        memset(&stConf, 0, sizeof(stConf));
        if (0 == dml_load_conf(&stConf, a_pstAppCtx->pszConfFile))
        {
            pstEnv->stConf = stConf;
        }
    }
    return 0;
}

static int dml_fini(TAPPCTX *a_pstAppCtx, void *pvArg)
{
    DMLENV *pstEnv = &g_stEnv;

    dml_centerd_close(pstEnv);
    dml_cleanup_plugins(pstEnv);

    if (NULL != pstEnv->pstExchangeMng)
    {
        agent_api_destroy(pstEnv->pstExchangeMng);
        pstEnv->pstExchangeMng = NULL;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int iRet = 0;
    void *pvArg = &g_stEnv;

    memset(&g_stEnv, 0, sizeof(g_stEnv));
    memset(&gs_stAppCtx, 0, sizeof(gs_stAppCtx));

    gs_stAppCtx.argc = argc;
    gs_stAppCtx.argv = argv;
    gs_stAppCtx.iTimer = 500; /* 500ms, 对应 tagent.xml 默认 tick=500000us */
    gs_stAppCtx.iEpollWait = 100; /* 主循环 epoll 超时(ms)，避免无 tbus fd 时空转 */

    gs_stAppCtx.pfnInit = (PFNTAPPFUNC)dml_init;
    gs_stAppCtx.pfnFini = (PFNTAPPFUNC)dml_fini;
    gs_stAppCtx.pfnProc = (PFNTAPPFUNC)dml_proc;
    gs_stAppCtx.pfnTick = (PFNTAPPFUNC)dml_tick;
    gs_stAppCtx.pfnReload = (PFNTAPPFUNC)dml_reload;

    gs_stAppCtx.iNoLoadConf = 1; /* tagent.xml 为自定义 XML，自行解析 */
    gs_stAppCtx.uiVersion = TAPP_MAKE_VERSION(DML_MAJOR, DML_MINOR, DML_REV, DML_BUILD);

    iRet = tapp_def_init(&gs_stAppCtx, pvArg);
    if (iRet < 0)
    {
        printf("Error: tapp_def_init failed, ret: %d\n", iRet);
        return iRet;
    }

    iRet = tapp_def_mainloop(&gs_stAppCtx, pvArg);

    tapp_def_fini(&gs_stAppCtx, pvArg);

    return iRet;
}
