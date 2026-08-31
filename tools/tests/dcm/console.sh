#!/bin/bash
# 启动 tcmconsole (前台交互式控制台)
# 前置条件: 先运行 ./start_all.sh 启动 tbusmgr/tcenterd/tconnd/tcmcenter/dmltagent
DCM_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DCM_DIR/tcmconsole/bin"
exec ./tcmconsole --id=2.4.1 --conf-file=../cfg/tcmconsole.xml --tlogconf=../cfg/tcmconsole_log.xml start
