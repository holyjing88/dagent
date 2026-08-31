#ifndef _DML_COMMON_H_
#define _DML_COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include "tapp/tapp.h"
#include "tlog/tlog.h"
#include "tbus/tbus.h"
#include "tdr/tdr.h"
#include "pal/pal.h"
#include "taa/tagentapi.h"
#include "tagentpluginapi/plugin.h"

extern "C" {
#include "tagent_centerd_proto.h"
extern unsigned char g_szMetalib_tagent_centerd_proto[];
}

/* tagent 主程序与 mod_procmng 之间的 FTP 下载协议命令字（tagent_procmng_proto.xml，
 * 其中定义为枚举，此处仅为 dml_main.cpp 提供字面常量，避免引入整个协议头） */
#define DML_MSG_FTP_DOWNLOAD_FILE_REQ  0xaf00
#define DML_MSG_FTP_DOWNLOAD_FILE_RES  0xaf01

#define DML_MAX_PLUGIN_NUM      64
#define DML_MAX_HOSTNAME_LEN     128
#define DML_MAX_NETDEVICE        8
#define DML_RECV_BUFF_LEN        (64 * 1024)
#define DML_DEFAULT_BUSINESS_ID  0
#define DML_DEFAULT_TICK_US      500000
#define DML_HEARTBEAT_GAP_SEC    5
#define DML_RECONNECT_GAP_SEC    3

typedef struct tagDmlTagentConf
{
    char szMaster[64];          /* ip:port */
    char szMasterIp[64];
    int  iMasterPort;
    char szSlave[64];           /* ip:port */
    char szSlaveIp[64];
    int  iSlavePort;
    char szLib[256];            /* plugin so dir */
    char szCfg[256];            /* cfg dir */
    int  iTickUs;               /* main loop tick in us */
    int  iBusinessID;
} DMLTAGENTCONF;

typedef struct tagDmlPlugin
{
    TAGENTPLUGIN stPlugin;
    char szName[64];            /* module name, e.g. mod_procmng */
    void *pvHandle;             /* dlopen handle */
    int  iLoaded;
} DMLPLUGIN;

typedef struct tagDmlEnv
{
    TAPPCTX *pstAppCtx;
    LPTLOGCATEGORYINST pstLogCat;

    DMLTAGENTCONF stConf;

    DMLPLUGIN astPlugin[DML_MAX_PLUGIN_NUM];
    int iPluginNum;

    LPTDRMETA pstCenterdMeta;

    LPEXCHANGEMNG pstExchangeMng;

    int  iCenterdSock;
    int  iConnected;            /* 1: connected to tcenterd */
    int  iMasterActive;         /* 1: currently on master, 0: slave */

    time_t tLastHeartbeat;
    time_t tLastReconnect;
    uint32_t dwPingSeq;
} DMLENV;

extern DMLENV g_stEnv;

/* dml_conf.cpp */
int dml_load_conf(DMLTAGENTCONF *pstConf, const char *pszConfFile);

/* dml_plugin.cpp */
int dml_load_plugins(DMLENV *pstEnv);
int dml_cleanup_plugins(DMLENV *pstEnv);
int dml_plugin_timer(DMLENV *pstEnv);
TAGENTPLUGIN *dml_find_plugin(DMLENV *pstEnv, unsigned int uiAppid);

/* dml_centerd.cpp */
int dml_centerd_init(DMLENV *pstEnv);
int dml_centerd_connect(DMLENV *pstEnv);
int dml_centerd_proc(DMLENV *pstEnv);
int dml_centerd_send(DMLENV *pstEnv, TAGENTPKGHEAD *pstHead, TAGENTCENTERDMSGBODY *pstBody);
int dml_centerd_close(DMLENV *pstEnv);
void dml_centerd_heartbeat(DMLENV *pstEnv);

/* dml_ftp.cpp */
int dml_ftp_handle_req(DMLENV *pstEnv, TAGENTPLUGIN *pstPlugin,
                       char *pszBuff, int iBuff, TAGENTPLUGINHEAD *pstHead);

/* plugin send/logging provided to mod_*.so (dml_main.cpp) */
int dml_plugin_send(TAGENTPLUGIN *p, char *ptr, size_t size, TAGENTPLUGINHEAD *pstHead);
int dml_plugin_logging(TAGENTPLUGIN *p, int priority, int id, int cls, const char *fmt, ...);

#endif /* _DML_COMMON_H_ */
