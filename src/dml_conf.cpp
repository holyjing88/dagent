#include "dml_common.h"

static void dml_trim(char *sz)
{
    char *p = sz;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != sz) memmove(sz, p, strlen(p) + 1);
    int len = (int)strlen(sz);
    while (len > 0 && (sz[len - 1] == ' ' || sz[len - 1] == '\t' ||
                       sz[len - 1] == '\r' || sz[len - 1] == '\n'))
    {
        sz[--len] = '\0';
    }
}

static int dml_get_xml_value(const char *pszContent, const char *pszTag,
                             char *pszOut, int iOutLen)
{
    char szOpen[96];
    char szClose[96];
    snprintf(szOpen, sizeof(szOpen), "<%s>", pszTag);
    snprintf(szClose, sizeof(szClose), "</%s>", pszTag);

    const char *p = strstr(pszContent, szOpen);
    if (NULL == p) return -1;
    p += strlen(szOpen);
    const char *q = strstr(p, szClose);
    if (NULL == q) return -1;

    int len = (int)(q - p);
    if (len >= iOutLen) len = iOutLen - 1;
    memcpy(pszOut, p, len);
    pszOut[len] = '\0';
    dml_trim(pszOut);
    return 0;
}

static int dml_parse_addr(const char *pszAddr, char *pszIp, int iIpLen, int *piPort)
{
    char szBuf[64];
    snprintf(szBuf, sizeof(szBuf), "%s", pszAddr);
    dml_trim(szBuf);

    char *p = strchr(szBuf, ':');
    if (NULL == p) return -1;
    *p = '\0';
    snprintf(pszIp, iIpLen, "%s", szBuf);
    dml_trim(pszIp);
    *piPort = atoi(p + 1);
    if (*piPort <= 0 || *piPort > 65535) return -1;
    return 0;
}

int dml_load_conf(DMLTAGENTCONF *pstConf, const char *pszConfFile)
{
    if (NULL == pstConf || NULL == pszConfFile) return -1;

    FILE *fp = fopen(pszConfFile, "r");
    if (NULL == fp)
    {
        printf("dml_load_conf: open %s failed: %s\n", pszConfFile, strerror(errno));
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long lSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (lSize <= 0 || lSize > 1024 * 1024)
    {
        fclose(fp);
        return -1;
    }

    char *pszBuf = (char *)malloc(lSize + 1);
    if (NULL == pszBuf)
    {
        fclose(fp);
        return -1;
    }
    fread(pszBuf, 1, lSize, fp);
    pszBuf[lSize] = '\0';
    fclose(fp);

    pstConf->iTickUs = DML_DEFAULT_TICK_US;
    pstConf->iBusinessID = DML_DEFAULT_BUSINESS_ID;
    snprintf(pstConf->szLib, sizeof(pstConf->szLib), "../lib");
    snprintf(pstConf->szCfg, sizeof(pstConf->szCfg), "../cfg");

    char szVal[256];
    if (0 == dml_get_xml_value(pszBuf, "master", szVal, sizeof(szVal)))
    {
        if (0 == dml_parse_addr(szVal, pstConf->szMasterIp, sizeof(pstConf->szMasterIp),
                                &pstConf->iMasterPort))
        {
            snprintf(pstConf->szMaster, sizeof(pstConf->szMaster), "%s", szVal);
        }
    }
    if (0 == dml_get_xml_value(pszBuf, "slave", szVal, sizeof(szVal)))
    {
        if (0 == dml_parse_addr(szVal, pstConf->szSlaveIp, sizeof(pstConf->szSlaveIp),
                                &pstConf->iSlavePort))
        {
            snprintf(pstConf->szSlave, sizeof(pstConf->szSlave), "%s", szVal);
        }
    }
    if (0 == dml_get_xml_value(pszBuf, "lib", szVal, sizeof(szVal)))
    {
        snprintf(pstConf->szLib, sizeof(pstConf->szLib), "%s", szVal);
    }
    if (0 == dml_get_xml_value(pszBuf, "cfg", szVal, sizeof(szVal)))
    {
        snprintf(pstConf->szCfg, sizeof(pstConf->szCfg), "%s", szVal);
    }
    if (0 == dml_get_xml_value(pszBuf, "tick", szVal, sizeof(szVal)))
    {
        double f = atof(szVal);
        if (f > 0) pstConf->iTickUs = (int)f;
    }

    free(pszBuf);

    if (pstConf->szMasterIp[0] == '\0')
    {
        printf("dml_load_conf: master addr not configured, use default 127.0.0.1:8899\n");
        snprintf(pstConf->szMaster, sizeof(pstConf->szMaster), "127.0.0.1:8899");
        snprintf(pstConf->szMasterIp, sizeof(pstConf->szMasterIp), "127.0.0.1");
        pstConf->iMasterPort = 8899;
    }
    if (pstConf->szSlaveIp[0] == '\0')
    {
        snprintf(pstConf->szSlave, sizeof(pstConf->szSlave), "%s", pstConf->szMaster);
        snprintf(pstConf->szSlaveIp, sizeof(pstConf->szSlaveIp), "%s", pstConf->szMasterIp);
        pstConf->iSlavePort = pstConf->iMasterPort;
    }

    return 0;
}
