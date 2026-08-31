#!/bin/bash
# 停止全部进程 (逆序: dmltagent -> tcmcenter -> tconnd -> tcenterd)
DCM_DIR="$(cd "$(dirname "$0")" && pwd)"
RELEASE_DIR="$(cd "$DCM_DIR/../../../release" && pwd)"

echo "=== 停止 dmltagent ==="
(cd "$RELEASE_DIR/bin" && ./dmltagent stop) 2>/dev/null || echo "  dmltagent 未运行"

echo "=== 停止 tcmcenter ==="
(cd "$DCM_DIR/tcmcenter/bin" && ./tcmcenter --id=0.0.1.1 stop) 2>/dev/null || echo "  tcmcenter 未运行"

echo "=== 停止 tconnd ==="
(cd "$DCM_DIR/tconnd/bin" && ./tconnd --id=0.0.3.1 stop) 2>/dev/null || echo "  tconnd 未运行"

echo "=== 停止 tcenterd ==="
(cd "$DCM_DIR/tcenterd/bin" && ./tcenterd --id 0.0.2.1 stop) 2>/dev/null || echo "  tcenterd 未运行"

echo "=== 停止完成 ==="
