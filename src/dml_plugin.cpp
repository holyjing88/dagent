#include "dml_common.h"

#include <dirent.h>
#include <dlfcn.h>

typedef int (*PFNPLUGININIT)(TAGENTPLUGIN *p);
typedef void (*PFNSETTAPPCTX)(void *p);
typedef void (*PFNSETCONFPARAM)(void *p);

static int dml_plugin_load_one(DMLENV *pstEnv, const char *pszSoPath, const char *pszBaseName)
{
    if (pstEnv->iPluginNum >= DML_MAX_PLUGIN_NUM) return -1;

    char szInit[96];
    snprintf(szInit, sizeof(szInit), "%s_plugin_init", pszBaseName);

    void *pvHandle = dlopen(pszSoPath, RTLD_NOW | RTLD_GLOBAL);
    if (NULL == pvHandle)
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "dlopen %s failed: %s", pszSoPath, dlerror());
        return -1;
    }

    PFNPLUGININIT pfnInit = (PFNPLUGININIT)dlsym(pvHandle, szInit);
    if (NULL == pfnInit)
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "dlsym %s failed: %s", szInit, dlerror());
        dlclose(pvHandle);
        return -1;
    }

    DMLPLUGIN *pstP = &pstEnv->astPlugin[pstEnv->iPluginNum];
    memset(pstP, 0, sizeof(*pstP));
    snprintf(pstP->szName, sizeof(pstP->szName), "%s", pszBaseName);
    pstP->pvHandle = pvHandle;

    TAGENTPLUGIN *p = &pstP->stPlugin;
    memset(p, 0, sizeof(*p));
    p->send = dml_plugin_send;
    p->logging = dml_plugin_logging;
    p->log = pstEnv->pstLogCat;
    p->data = NULL;

    if (0 != pfnInit(p))
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "%s plugin_init failed", pszBaseName);
        dlclose(pvHandle);
        return -1;
    }

    PFNSETTAPPCTX pfnSetTappCtx = (PFNSETTAPPCTX)dlsym(pvHandle, "mod_set_tappctx");
    if (NULL != pfnSetTappCtx && NULL != pstEnv->pstAppCtx)
    {
        pfnSetTappCtx((void *)pstEnv->pstAppCtx);
    }

    PFNSETCONFPARAM pfnSetConf = (PFNSETCONFPARAM)dlsym(pvHandle, "mod_set_config_param");
    if (NULL != pfnSetConf)
    {
        /* TODO: 解析 tagent.xml 中插件配置并透传 PROCMNGCONF 结构 */
        pfnSetConf(NULL);
    }

    if (NULL != p->init)
    {
        int iRet = p->init(p);
        if (0 != iRet)
        {
            tlog_error(pstEnv->pstLogCat, 0, 0, "plugin %s init failed ret(%d)", p->name, iRet);
            dlclose(pvHandle);
            return -1;
        }
    }

    pstP->iLoaded = 1;
    pstEnv->iPluginNum++;

    tlog_info(pstEnv->pstLogCat, 0, 0, "load plugin %s appid(%u) succ", p->name ? p->name : pszBaseName, p->id);
    printf("load plugin %s appid(%u) succ\n", p->name ? p->name : pszBaseName, p->id);
    return 0;
}

int dml_load_plugins(DMLENV *pstEnv)
{
    DIR *pDir = opendir(pstEnv->stConf.szLib);
    if (NULL == pDir)
    {
        tlog_error(pstEnv->pstLogCat, 0, 0, "opendir %s failed: %s", pstEnv->stConf.szLib, strerror(errno));
        return -1;
    }

    struct dirent *pstEnt = NULL;
    char szSoPath[512];
    while (NULL != (pstEnt = readdir(pDir)))
    {
        if (0 == strncmp(pstEnt->d_name, "mod_", 4) &&
            0 == strncmp(pstEnt->d_name + strlen(pstEnt->d_name) - 3, ".so", 3))
        {
            char szBase[128];
            snprintf(szBase, sizeof(szBase), "%s", pstEnt->d_name);
            szBase[strlen(szBase) - 3] = '\0';  /* strip .so */

            snprintf(szSoPath, sizeof(szSoPath), "%s/%s", pstEnv->stConf.szLib, pstEnt->d_name);
            dml_plugin_load_one(pstEnv, szSoPath, szBase);
        }
    }
    closedir(pDir);
    return 0;
}

int dml_cleanup_plugins(DMLENV *pstEnv)
{
    for (int i = 0; i < pstEnv->iPluginNum; i++)
    {
        DMLPLUGIN *pstP = &pstEnv->astPlugin[i];
        if (pstP->iLoaded)
        {
            if (NULL != pstP->stPlugin.cleanup)
            {
                pstP->stPlugin.cleanup(&pstP->stPlugin);
            }
            if (NULL != pstP->pvHandle)
            {
                dlclose(pstP->pvHandle);
            }
            pstP->iLoaded = 0;
        }
    }
    pstEnv->iPluginNum = 0;
    return 0;
}

int dml_plugin_timer(DMLENV *pstEnv)
{
    for (int i = 0; i < pstEnv->iPluginNum; i++)
    {
        DMLPLUGIN *pstP = &pstEnv->astPlugin[i];
        if (pstP->iLoaded && NULL != pstP->stPlugin.timer)
        {
            pstP->stPlugin.timer(&pstP->stPlugin);
        }
    }
    return 0;
}

TAGENTPLUGIN *dml_find_plugin(DMLENV *pstEnv, unsigned int uiAppid)
{
    for (int i = 0; i < pstEnv->iPluginNum; i++)
    {
        DMLPLUGIN *pstP = &pstEnv->astPlugin[i];
        if (pstP->iLoaded && pstP->stPlugin.id == uiAppid)
        {
            return &pstP->stPlugin;
        }
    }
    return NULL;
}
