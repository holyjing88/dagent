# dmltagent 技术设计文档

> 文档版本：v1.1（修正版）
> 日期：2026-08-31
> 目标：以 dmltagent 重写 tagent 主程序框架（宿主进程），复用 TSF4G 的 tapp/tbus/taa/tdr/tlog 库，做到与 tcenterd / tcmcenter 及既有 `mod_*.so` 插件完全协议兼容，最终替换生产环境的 `tagent` 二进制。

---

## 0. 关键结论（v1.1 修正）

通过对 GOpen_tcm 源码、现网 tagent/tcenterd 二进制（`strings`/`nm`）、以及部署包内
《TSF4G-Agent-Centerd-General.doc》的交叉分析，并**从 tcenterd 二进制中反解出完整协议元数据**
`tagent_centerd_proto.xml`，得到以下关键结论：

1. **tagent 是 C++ 程序**（动态链接 `libstdc++`）。
2. **tagent 不通过 `centerdapi`（libcenterdapi.a）连接 tcenterd**。`centerdapi` 是 **Master（tcmcenter）侧** 的客户端 API（基于 tbus）。
3. **tagent 通过自有的 `tagent_centerd_proto` 协议（TDR）走 TCP 连接 tcenterd**（`tagent.xml` 中 `master/slave` 即 tcenterd 的 `ip:port`）。
4. 该协议源码（`tagent_centerd_proto.xml/.h/.c`）不在任何现有源码树中，但 **已成功从 tcenterd 二进制内嵌的 metalib（`g_szMetalib_tagent_centerd_proto`）反解并还原为 XML**（见 `tagent_centerd_proto.xml`），可用 `tdr` 工具重新生成 C/C++ 协议代码。

---

## 1. 背景与目标

TCM（Tencent Center Manager）是腾讯 TSF4G 解决方案的基础系统之一，用于业务进程集中部署与控制。

`tagent` 是部署在每台业务机上的“agent 宿主进程”，是 TCM 管控链路的最后一级执行者。

当前 `tagent` 只有预编译二进制，无源码。`dmltagent` 的目标是：**依据 GOpen_tcm 源码中的接口头文件、协议定义、插件源码，以及反解出的 `tagent_centerd_proto` 协议，重写 tagent 主程序框架，产出功能等价、协议兼容的 `dmltagent`，用于替换 `tagent`。**

### 1.1 范围边界（本期）

| 项 | 是否纳入 | 说明 |
|----|----|----|
| tagent 宿主框架（main/配置/插件/消息分发/daemon） | ✅ 纳入 | 本次核心，C++ 实现 |
| tcenterd 通信（tagent_centerd_proto over TCP） | ✅ 纳入 | 反解协议 + 自实现 TCP 客户端 |
| 共享内存交换（agent_api） | ✅ 纳入 | 复用 libtaa.a |
| FTP 下载能力 | ✅ 纳入 | tagent 主程序内实现 |
| `mod_procmng.so` / `mod_collect.so` / `mod_tbusconfig.so` | ❌ 不重写 | 复用 GOpen_tcm 已编译产物 |
| tcmcenter / tcenterd / tbusmgr | ❌ 不重写 | 复用现网 |

---

## 2. 现状分析（tagent 功能需求）

### 2.1 TCM 数据流与 tagent 角色

```
 +-----------+  (tbus/centerdapi)   +-----------+  (tagent_centerd_proto/TCP)  +------------+
 | tcmcenter | <==================> | tcenterd  | <=========================> |   tagent   |
 | (集中配置) |                      | (中心守护) |     master/slave ip:port      | (主机代理)  |
 +-----------+                      +-----------+                             +-----+------+
                                                                                    |
                                                                        dlopen  mod_*.so
                                                                                    |
                                   +-----------------+--------------+--------------+
                                   | mod_procmng.so  | mod_tbusconfig.so | mod_collect.so |
                                   +-----------------+------------------+-----------------+
                                                                                    |
                                                        共享内存 agent_api (libtaa.a)
                                                                                    |
                                                                         +------------------+
                                                                         |  业务进程 (busAgent)|
                                                                         +------------------+
```

tagent 职责：**TCP 协议客户端 + 插件宿主 + 共享内存交换 + 进程生命周期管理**。

### 2.2 tagent 功能需求清单

| # | 功能 | 说明 | 接口/证据 |
|---|------|------|------|
| F1 | tapp 框架集成 | daemon、主循环 tick、命令行(start/stop/reload/control)、信号 | `tapp.h` TAPPCTX 回调；部署脚本 |
| F2 | 配置解析 | `tagent.xml`(master/slave ip:port、lib、cfg、tick) | 现网 `cfg/tagent.xml` |
| F3 | tcenterd 连接 | TCP 连接 master/slave、断线重连、主备切换 | `tagent_centerd_proto` + 二进制字符串 |
| F4 | 注册 | HELLO 上报主机名/IP，AGENTREGISTER_NEW 上报 appid/busid | 协议 0x00/0x07 |
| F5 | 心跳 | PING/PONG | 协议 4086/4087 |
| F6 | 插件加载/生命周期 | dlopen mod_*.so → plugin_init → init/cleanup/timer/recv | `plugin.h` |
| F7 | 消息收发/分发 | 收 TRANSFER 按 appid 分发插件 recv | 协议 65520 |
| F8 | 插件 send 实现 | 实现 p->send（TRANSFER→发 tcenterd；FTP_REQ→FTP 模块） | `mod_promng.c:6517-6589` |
| F9 | 插件 logging/log | p->logging、p->log | `plugin.h` |
| F10 | tapp 上下文注入 | 调 `mod_set_tappctx(pstTappCtx)` | `mod_promng.c:9724` |
| F11 | 插件配置注入 | 调 `mod_set_config_param(...)` | `mod_promng.c:9731` |
| F12 | 文件传输 | FILEUPDATE/FILEREQUEST/FILEPUSH/FILEREPORT | 协议 0x0a-0x0d |
| F13 | FTP 下载 | tagent 主程序实现 FTP 客户端 | `tagent_procmng_proto.xml` |
| F14 | 模块管理 | MOD_ADD/DEL/LIST（运行期升级 mod_*.so） | 协议 4080-4085 |

### 2.3 插件接口契约（plugin.h）

`TAGENTPLUGIN` 字段分工（**插件填 vs 宿主填**）：

| 字段 | 谁填充 | 含义 |
|------|------|------|
| `name`/`version` | 插件 | 模块名/版本 |
| `init`/`cleanup`/`timer`/`recv` | 插件 | 生命周期与收包回调 |
| `send` | **宿主** | 插件发消息（转发 tcenterd 或触发 FTP） |
| `logging` | **宿主** | 插件写日志 |
| `data` | 插件 | 插件私有环境 |
| `log` | **宿主** | tlog 日志分类 |
| `id` | 插件 | appid（ID_APPID_*） |

`TAGENTPLUGINHEAD`：`uiAppid/uiBusid/uiSourceIP/uiDestIP/uMsgCmd/cMsgVer`。

插件发消息约定（由 `mod_promng.c` 反推）：
- `uMsgCmd = TAGEND_PLUGIN_MSG_TRANSFER(65520)`：转发给 tcmcenter 的普通协议包 → 宿主封装为 `TAGENT_ID_MSG_TRANSFER` 发 tcenterd。
- `uMsgCmd = TAGENT_ID_MSG_FTP_DOWNLOAD_FILE_REQ(0xaf00)`：请求宿主执行 FTP 下载 → 宿主回 `TAGENT_ID_MSG_FTP_DOWNLOAD_FILE_RES(0xaf01)`。

### 2.4 appid 约定

```c
ID_APPID_CENTERD 0x00 | ID_APPID_DIRTY 0x01 | ID_APPID_BUSCONFIG 0x02 | ID_APPID_TCONND 0x03
ID_APPID_PROCMNG 0x04 | ID_APPID_COLLECT 0x05 | ID_APPID_TMAC 0xff
```

插件与 appid：`mod_procmng`(4)、`mod_collect`(5)、`mod_tbusconfig`(2)、`mod_tdirty`(1，可选)。

### 2.5 部署结构与命令行

```
tagent/{ bin/{tagent,start/stop/reload/control_*.sh}, cfg/{tagent.xml,tagent_log.xml}, lib/mod_*.so, docs/ }
```

- 启动：`./tagent --tlogconf=../cfg/tagent_log.xml --conf-file=../cfg/tagent.xml --log-file=../log/tagent -D start`
- 停止/重载/控制：`./tagent stop|reload|control`
- `tagent.xml`：`<master>ip:port</master>`、`<slave>ip:port</slave>`、`<lib>`、`<cfg>`、`<tick>`（微秒）

---

## 3. 总体设计

### 3.1 dmltagent 定位

C++ 程序，只重写 tagent 宿主框架，复用 TSF4G 库：
- `libtapp.a`（应用框架）、`libtaa.a`（共享内存交换 agent_api）、`libtbus.a`（总线）、`libtdr.a`（协议）、`libtlog.a`（日志）、`libpal.a`/`libcomm.a`（基础）
- 复用 `libtcmutil.a`（`tcm_util_*`、`tagent_mng_util_*`、`FileMd5`）
- 复用插件 `mod_procmng.so`/`mod_collect.so`/`mod_tbusconfig.so`

**不复用** `libcenterdapi.a`（那是 Master 侧）；tcenterd 通信改为**自实现 `tagent_centerd_proto` over TCP**。

### 3.2 总体架构

```
                  dmltagent（C++ 宿主进程）
+----------------------------------------------------------------+
| main() / tapp 集成（daemon、命令行、信号、主循环 tick）            |
|   ├── 配置解析（tagent.xml → DMLTAGENTCONF）                     |
|   ├── 日志（tlog 初始化/分类）                                    |
|   ├── tcenterd TCP 客户端（tagent_centerd_proto：连接/HELLO/注册/   |
|   │       心跳/重连/主备切换/收发）                                |
|   ├── 插件管理器（dlopen / plugin_init / init / timer / cleanup）  |
|   ├── 消息分发器（收 TRANSFER → 按 appid 分发插件 recv）            |
|   ├── 插件 send 实现（TRANSFER→发 tcenterd；FTP_REQ→FTP 模块）      |
|   ├── FTP 下载模块                                                |
|   └── 共享内存交换（agent_api 就绪，供插件调用 libtaa.a）            |
+----------------------------------------------------------------+
        | tagent_centerd_proto(TCP)   | agent_api(共享内存)   | dlopen
        v                            v                        v
   tcenterd(master/slave)        业务进程 busAgent        mod_*.so 插件
```

---

## 4. tagent_centerd_proto 协议（反解还原）

> 元数据已还原为 `tagent_centerd_proto.xml`（metalib `tagent_centerd_proto`，version 1），
> 可用 `tdr -C/-H/-P` 重新生成 C/C++ 代码。

### 4.1 包结构

```
TAGENTCENTERDMSG  {  head(tagentpkghead)  +  body(tagentcenterdmsgbody select=head.msgCmd) }
```

`tagentpkghead`（定长 24 字节）：

| 字段 | 类型 | 说明 |
|------|------|------|
| len | ushort | 整包长度（sizeinfo） |
| msgCmd | ushort | 命令字（TAGENTMSGCMD） |
| appid | uint | 功能 ID |
| busid | uint | 业务 ID |
| msgVer | uchar | 协议版本 |
| type | uchar | ID_CMD_DISPATCH(0)/UP(1)/DOWN(2) |
| magic | ushort | `TAGENT_MSG_MAGIC=34969`(0x8899) |
| SourceIP | ip | 源 IP |
| DestIP | ip | 目的 IP |

### 4.2 命令字（TAGENTMSGCMD）

| 命令 | 值 | 方向 | body |
|------|----|----|------|
| HELLO | 0 | agent→centerd | tagentprotoipconfig(主机名+IP列表) |
| AGENTREGISTER | 1 | agent→centerd | transmit(老版本) |
| MASTEREGISTER | 2 | master→centerd | uint(appid) |
| CENTERDCASCADE | 3 | centerd↔centerd | CascadeInfo |
| LOCATIONREQ | 4 | agent→centerd | transmit |
| LOCATIONRES | 5 | centerd→agent | tagentprotolocationres(master,slave) |
| SNAPSHOTREPORT | 6 | centerd→? | snapshot |
| AGENTREGISTER_NEW | 7 | agent→centerd | AppInfos(count,AppInfo{Appid,Busid}[]) |
| FILEUPDATE | 10 | master→agent | createfilereq(md5,len,fileName,fileLen) |
| FILEREQUEST | 11 | agent→master | filerequest(fileOffset,len,fileName) |
| FILEPUSH | 12 | master→agent | filepush(fileOffset,len,fileName,segSize,buff) |
| FILEREPORT | 13 | agent→master | fileReport(md5,len,fileName,result) |
| PASS | 204 | — | transmit |
| INNERMNG | 238 | — | tagentprotoinnermng(appid,dst,len,fileName) |
| CHROMO | 255 | — | transmit |
| MOD_ADD_REQ/RES | 4080/4081 | master↔agent | tagentmodaddreq / int |
| MOD_DEL_REQ/RES | 4082/4083 | master↔agent | tagentmoddelreq / int |
| MOD_LIST_REQ/RES | 4084/4085 | master↔agent | int / tagentmodlistres |
| PING | 4086 | agent→centerd | uint(seq) |
| PONG | 4087 | centerd→agent | uint(seq) |
| LIST_AGENT_REQ/RES | 4088/4089 | — | uint / listAgentRes |
| **TRANSFER** | **65520** | 双向 | **tagentbuffer(len,buff[40960])**（承载业务消息） |

### 4.3 连接与注册流程（tagent 侧）

1. TCP connect 到 `master(ip:port)`，失败则 `slave`；断线定时重连。
2. 连接后发送 **HELLO(0)**：主机名 + 网卡 IP 列表。
3. 发送 **AGENTREGISTER_NEW(7)**：`AppInfos{count, {appid,busid}[]}`（上报本机加载的各插件 appid 及 busid）。
4. 之后收发 **TRANSFER(65520)** 承载业务消息；周期 **PING(4086)** 并等待 **PONG(4087)**。

> `TRANSFER` 的 `type` 语义沿用 .doc：DISPATCH(广播下发)、UP(上行)、DOWN(上行返回)。

### 4.4 消息帧（TCP 收包）

定长头 24 字节 → 解析 `head.len` → 读剩余 `len-24` 字节 body。用 TDR `tdr_ntoh`/`tdr_hton` 对 `TAGENTCENTERDMSG` 编解码。

---

## 5. 详细设计

### 5.1 主程序框架（tapp 集成）

复用 `TAPPCTX` 回调：`pfnArgv`（解析命令行）、`pfnInit`（配置/tlog/TCP客户端/插件初始化）、`pfnProc`（主循环收包分发）、`pfnTick`（插件 timer + 心跳）、`pfnStop/pfnQuit`（清理）、`pfnFini`（释放）。

参照 tcmcenter 的 `main`（`tcmcenter.c:112-165`）使用 `tapp_def_init/tapp_def_mainloop/tapp_def_fini`。

### 5.2 配置解析

```c
typedef struct { char szMaster[64]; char szSlave[64]; char szLib[256]; char szCfg[256]; int iTickUs; } DMLTAGENTCONF;
```
解析 `<master>/<slave>/<lib>/<cfg>/<tick>`，`master/slave` 拆 `ip`+`port`。

插件配置透传：读取 `tagent.xml` 扩展字段，调 `mod_set_config_param(void*)` 透传（结构见 `mod_promng.h` 的 `PROCMNGCONF`）。

### 5.3 tcenterd TCP 客户端

- 非阻塞 socket + epoll（或 select），或独立线程收发。
- 收包：定长头 → body 拼接。
- 发送：`tdr_hton` 打包 `TAGENTCENTERDMSG` → send。
- 主备：master 连接失败/心跳超时 → 切 slave；备恢复切回。

### 5.4 插件加载与生命周期

```
扫描 <lib>/mod_*.so：
  dlopen → dlsym("<name>_plugin_init") → plugin_init(&p)
  宿主填 p.send/p.logging/p.log；调可选 mod_set_tappctx / mod_set_config_param
  p.init(&p)；按 p.id 登记 appid→plugin 映射
卸载：cleanup → dlclose
```

插件名约定：`mod_procmng`→`mod_procmng_plugin_init`、`mod_collect`→`mod_collect_plugin_init`、`mod_tbusconfig`→`mod_tbusconfig_plugin_init`。

### 5.5 消息分发

- 收 TRANSFER(65520)：解出 `tagentbuffer.buff`，按 `head.appid` 查插件，调 `plugin->recv(plugin, buff, len, &TAGENTPLUGINHEAD)`。
- 插件 `send`：`uMsgCmd==TAGEND_PLUGIN_MSG_TRANSFER` → 封装 `TRANSFER` 发 tcenterd；`uMsgCmd==TAGENT_ID_MSG_FTP_DOWNLOAD_FILE_REQ` → FTP 模块。

### 5.6 FTP 下载

复用 `tagent_mng_util_*`（libtcmutil.a）+ `tagent_procmng_proto.xml`，主程序实现 FTP 客户端（优先 libcurl）。

### 5.7 日志

tlog 读取 `tagent_log.xml`，创建分类赋给 `p->log`；`p->logging` 转发 tlog。

---

## 6. 目录结构

```
dmltagent/
├── design.md                       # 本文档
├── tagent_centerd_proto.xml        # 反解还原的协议元数据
├── Makefile
├── src/
│   ├── dml_main.cpp                # main + tapp 回调
│   ├── dml_conf.cpp/.h             # tagent.xml 解析
│   ├── dml_centerd.cpp/.h          # tagent_centerd_proto TCP 客户端
│   ├── dml_plugin.cpp/.h           # 插件管理
│   ├── dml_dispatch.cpp/.h         # 消息分发 + send/logging 实现
│   ├── dml_ftp.cpp/.h              # FTP 下载
│   └── dml_log.cpp/.h              # 日志
├── proto/                          # tdr 生成的 tagent_centerd_proto.c/.h
├── cfg/{tagent.xml,tagent_log.xml}
├── bin/*.sh
└── lib/                            # 部署 mod_*.so
```

---

## 7. 构建与依赖

| 依赖 | 位置 | 能力 |
|------|------|------|
| libtapp.a / libtaa.a / libtbus.a / libtdr.a / libtlog.a / libpal.a / libcomm.a | tsf4g_proj/lib | 框架/共享内存/总线/协议/日志 |
| libtcmutil.a | GOpen_tcm/lib | tcm_util_*/tagent_mng_util_*/FileMd5 |
| libcurl（可选） | 系统 | FTP |
| tdr 工具 | tsf4g_proj/tools/tdr | 由 XML 生成协议代码 |

```
# 生成协议代码
tdr -C -o proto/tagent_centerd_proto.c tagent_centerd_proto.xml
tdr -H -o proto/tagent_centerd_proto.h tagent_centerd_proto.xml
# 编译
g++ -o dmltagent src/*.cpp proto/*.c \
    libtcmutil.a libtaa.a libtapp.a libtbus.a libtdr.a libtlog.a libtloghelp.a \
    libcomm.a libpal.a libtsf4g.a -lpthread -ldl -lrt -lm -lstdc++
```

---

## 8. 替换 tagent 方案

1. 编译 `dmltagent`，放入 `tagent/bin/`（原 `tagent` 备份为 `tagent.bak`）。
2. 启动脚本 `./tagent` 改为 `./dmltagent`（或直接命名 `tagent`）。
3. `cfg/tagent.xml`、`cfg/tagent_log.xml`、`lib/mod_*.so` 不变。
4. 启动 → 观察 HELLO/注册/心跳 → tcmconsole 验证进程启停/检查/传文件/FTP。

---

## 9. 测试方案

1. 单元：配置解析、协议 pack/unpack 往返、FTP（本地 FTP server）。
2. 集成：`autotest/tcm_test/output` 环境替换 tagent 跑通启停/检查/传文件。
3. 兼容性回归：与原 tagent 并跑对比协议字节流/时序。
4. 稳定性：长跑观察内存/句柄/自动拉起。

---

## 10. 风险与后续

| 风险 | 缓解 |
|------|------|
| 连接/注册时序细节（HELLO 字段、重连节奏） | 以二进制字符串 + .doc + 抓包核对 |
| `mod_set_config_param` 结构需精确 | 解析 `mod_promng.h` 的 `PROCMNGCONF` 补齐 |
| tapp 回调签名细节 | 参照 tcmcenter `main` 对齐 |
| 现网 tagent 隐藏行为 | 以协议+插件接口为事实标准，灰度替换 |

---

## 附录 A：关键文件/资料索引

| 内容 | 路径 |
|------|------|
| 插件接口 | `GOpen_tcm/include/apps/tagentpluginapi/plugin.h` |
| 共享内存交换 API | `GOpen_tcm/include/apps/taa/tagentapi.h` |
| 进程管理协议 | `GOpen_tcm/src/protocol/tcm_procmng_proto.xml` |
| FTP 下载协议 | `GOpen_tcm/src/protocol/tagent_procmng_proto.xml` |
| tagent↔tcenterd 协议 | `dmltagent/tagent_centerd_proto.xml`（本仓库，反解还原） |
| 进程管理插件 | `GOpen_tcm/src/mod_procmng/mod_promng.c` |
| center 侧 agent 处理 | `GOpen_tcm/src/configmngcenter/tcm_procmng_agent.cpp` |
| 协议工具 | `GOpen_tcm/src/util_lib/`（tcm_proto_util.c、tagent_mng_proto_util.c） |
| TSF4G SDK | `/data/home/holyjing/gcloudservice/tsf4g_proj` |
| Agent-Centerd 设计书 | 现网 tagent/docs/TSF4G-Agent-Centerd-General.doc |
