#!/bin/bash
# 查看 TCM 测试环境运行状态
echo "=== 进程状态 ==="
ps -ef | grep -E 'tcenterd|tconnd|tcmcenter|dmltagent' | grep -v grep || echo "  (无进程运行)"

echo ""
echo "=== tcenterd 监听端口 8899 (Agent 接入) ==="
ss -tlnp 2>/dev/null | grep 8899 || netstat -tlnp 2>/dev/null | grep 8899 || echo "  (未监听 8899)"

echo ""
echo "=== tconnd 监听端口 9010 (tcmconsole 接入) ==="
ss -tlnp 2>/dev/null | grep 9010 || netstat -tlnp 2>/dev/null | grep 9010 || echo "  (未监听 9010)"

echo ""
echo "=== dmltagent 最近日志 ==="
DCM_DIR="$(cd "$(dirname "$0")" && pwd)"
RELEASE_DIR="$(cd "$DCM_DIR/../../../release" && pwd)"
tail -5 "$RELEASE_DIR/log/tagent.log" 2>/dev/null || echo "  (无 dmltagent 日志)"

echo ""
echo "=== tcmcenter 是否收到 agent 请求 (REQ_GET_CONF) ==="
grep -E 'REQ_GET_CONF|ProcessGetTbusConfReq' "$DCM_DIR/tcmcenter/log/tcmcenter.log" 2>/dev/null | tail -3 || echo "  (暂无，若 dmltagent 已启动，稍等数秒后再查)"
