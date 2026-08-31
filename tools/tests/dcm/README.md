# DCM 测试环境 (dmltagent 端到端验收)

本目录是一个**自包含的 TCM 测试环境**，用于在不依赖生产环境的情况下，完整启动
`tbusmgr + tcenterd + tconnd + tcmcenter + dmltagent`，并可通过 `tcmconsole` 连接
`tcmcenter` 进行控制台操作验收。

## 一、目录结构

```
dcm/
├── start_all.sh           # 一键启动全部服务 (推荐)
├── stop_all.sh            # 一键停止全部
├── status.sh              # 查看运行状态 / 验收结果
├── console.sh             # 启动 tcmconsole 控制台 (前台交互)
├── README.md              # 本文档
├── tbusmgr                # tbus 管理工具 (初始化 GCIM 共享内存)
├── tbusmgr.xml            # tbus 通道配置 (GCIM key=1000)
├── tcenterd/              # 中心守护 (tbus 0.0.2.1, 监听 0.0.0.0:8899)
├── tconnd/                # 连接接入 (tbus 0.0.3.1, 监听 0.0.0.0:9010)
│   └── cfg/tcm_proto.tdr  # tcm 协议元数据 (tconnd 解包用)
├── tcmcenter/             # 配置管理中心 (tbus 0.0.1.1, Master)
│   ├── cfg/{tcmcenter.xml, tcmcenter_log.xml, host.xml, proc.xml,
│   │        proc_deploy.xml, bus_relation.xml, access_whitelist.xml}
│   ├── lib/mmogxyz_config.so
│   └── conf/
└── tcmconsole/            # 运维控制台 (连接 tconnd 127.0.0.1:9010)
```

> dmltagent 被测程序位于 `../../../release/`（即 `dmltagent/release/`），
> 由 `start_all.sh` 自动引用，无需复制到本目录。

## 二、启动步骤

### 方式 A：一键启动（推荐）

```bash
cd dmltagent/tools/tests/dcm
./start_all.sh
```

### 方式 B：手动逐步启动

```bash
cd dmltagent/tools/tests/dcm

# 1) 初始化 tbus GCIM
./tbusmgr --conf-file=./tbusmgr.xml -W

# 2) 启动 tcenterd (0.0.2.1, :8899)
cd tcenterd/bin
./tcenterd --id 0.0.2.1 --bus-key 1000 --tlogconf ../cfg/tcenterd_log.xml \
    --conf-file ../cfg/tcenterd.xml -D start

# 3) 启动 tconnd (0.0.3.1, :9010)
cd ../../tconnd/bin
./tconnd --id=0.0.3.1 --use-bus --bus-key=1000 --conf-file=../cfg/tconnd.xml \
    --tlogconf=../cfg/tconnd_log.xml -D start

# 4) 启动 tcmcenter (0.0.1.1)
cd ../../tcmcenter/bin
./tcmcenter --id=0.0.1.1 --bus-key=1000 --conf-file=../cfg/tcmcenter.xml \
    --tlogconf=../cfg/tcmcenter_log.xml --conf-format=3 -D start

# 5) 启动 dmltagent
cd ../../../release/bin
./dmltagent --tlogconf=../cfg/tagent_log.xml --conf-file=../cfg/tagent.xml \
    --log-file=../log/dmltagent -D start
```

## 三、测试验收

启动完成后执行：

```bash
cd dmltagent/tools/tests/dcm
./status.sh
```

验收要点：

1. **进程存活**：`tcenterd`、`tconnd`、`tcmcenter`、`dmltagent` 四个进程均在。
2. **端口监听**：`8899`(tcenterd/Agent) 与 `9010`(tconnd/控制台) 均 LISTEN。
3. **Agent 注册成功**：`dmltagent/release/log/tagent.log` 中出现：
   ```
   load plugin mod_procmng appid(4) succ
   load plugin mod_tbusconfig appid(2) succ
   connected to tcenterd master(127.0.0.1:8899)
   ```
4. **全链路通**：`dcm/tcmcenter/log/tcmcenter.log` 中出现
   `CmdID=REQ_GET_CONF` 与 `send one msg to tbus agent(ip: 127.0.0.1)`。
5. **心跳正常**：`dmltagent/release/log/tagent.log` 中周期出现 `recv PONG`。

## 四、使用 tcmconsole 连接 tcmcenter

```bash
cd dmltagent/tools/tests/dcm
./console.sh
```

控制台连接路径：`tcmconsole → tconnd(127.0.0.1:9010) → tcmcenter(tbus 0.0.1.1)`。
进入控制台后可用 `help` 查看支持的命令（如 `listhost` / `listproc` 等），
通过控制台即可下发进程列表查询等操作（受控进程未部署，仅验证链路与命令交互）。

## 五、停止步骤

```bash
cd dmltagent/tools/tests/dcm
./stop_all.sh
```

## 六、注意事项

1. 若反复启动后 tbus 共享内存冲突，可清理残留共享内存后重启：
   ```bash
   ipcrm -M 1000 2>/dev/null   # 清理 GCIM 共享内存 (按需)
   ```
2. 本环境未部署受控业务进程，控制台可用于**链路连通性与命令交互验收**，
   进程启停/配置下发等需配合真实业务进程。
3. `tcmcenter.xml` 中 `CenterdAddr=0.0.2.1`、`TconndAddr=0.0.3.1`，
   `tconnd.xml` 中 Serializer 指向 `tcmcenter=0.0.1.1`，与 `tbusmgr.xml`
   通道地址一致；如需改 bus-key 请同步修改 tbusmgr.xml 与各 start 脚本。
