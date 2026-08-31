#!/bin/bash
# 完整启动 TCM 测试环境 + dmltagent
# 启动顺序: tbusmgr(初始化GCIM) -> tcenterd -> tconnd -> tcmcenter -> dmltagent
DCM_DIR="$(cd "$(dirname "$0")" && pwd)"
RELEASE_DIR="$(cd "$DCM_DIR/../../../release" && pwd)"
cd "$DCM_DIR"

echo "=== [1/5] 初始化 tbus GCIM (bus-key=1000) ==="
./tbusmgr --conf-file=./tbusmgr.xml -W

echo "=== [2/5] 启动 tcenterd (tbus 0.0.2.1, 监听 0.0.0.0:8899) ==="
(cd tcenterd/bin && ./tcenterd --id 0.0.2.1 --bus-key 1000 --tlogconf ../cfg/tcenterd_log.xml --conf-file ../cfg/tcenterd.xml -D start > /dev/null 2>&1)
sleep 1

echo "=== [3/5] 启动 tconnd (tbus 0.0.3.1, 监听 0.0.0.0:9010) ==="
(cd tconnd/bin && ./tconnd --id=0.0.3.1 --use-bus --bus-key=1000 --conf-file=../cfg/tconnd.xml --tlogconf=../cfg/tconnd_log.xml -D start > /dev/null 2>&1)
sleep 1

echo "=== [4/5] 启动 tcmcenter (tbus 0.0.1.1, Master) ==="
mkdir -p "$DCM_DIR/tcmcenter/conf/confcreated" "$DCM_DIR/tcmcenter/conf/tbusconf" \
         "$DCM_DIR/tcmcenter/conf/deploy" "$DCM_DIR/tcmcenter/conf/HostConf" \
         "$DCM_DIR/tcmcenter/conf/tcmdump" "$DCM_DIR/tcmcenter/conf/bindir" \
         "$DCM_DIR/tcmcenter/conf/tools_dir" "$DCM_DIR/tcmcenter/conf/proc_status"
(cd tcmcenter/bin && ./tcmcenter --id=0.0.1.1 --bus-key=1000 --conf-file=../cfg/tcmcenter.xml --tlogconf=../cfg/tcmcenter_log.xml --conf-format=3 -D start > /dev/null 2>&1)
sleep 2

echo "=== [5/5] 启动 dmltagent (连接 tcenterd 127.0.0.1:8899) ==="
(cd "$RELEASE_DIR/bin" && ./dmltagent --tlogconf=../cfg/tagent_log.xml --conf-file=../cfg/tagent.xml --log-file=../log/dmltagent -D start > /dev/null 2>&1)
sleep 1

echo ""
echo "=== 全部启动完成 ==="
"$DCM_DIR/status.sh"
