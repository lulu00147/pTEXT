# pTEXT v0.1

Android 17 纯文本阅读器。C11 + Java 11，NativeActivity，arm64-v8a。

## 构建

Windows，仓库根需 `tools/`。

| 组件 | 版本 |
|---|---|
| NDK | r30 |
| CMake | 4.5.0 |
| Build Tools | 37.0.0 |
| JDK | 25 |
| Platform | 37.2 |
| Ninja | 1.13.2 |

```bat
build.bat
```

产物：`pTEXT.apk`。minSdk 37，targetSdk 37，compileSdk 37.2；versionCode 1，versionName 0.1。

`stb_truetype.h` 构建时下载，不入库：

- v1.28
- commit `9f755b604db3492df2cbeb39bf84b81e3e3932e3`
- SHA-256 `428cfe93520f2aed5e413a220b0db5bba5b17e2ebe5a64e4a8efa9eeeb67ec5f`

Release 签名：

```properties
STORE_FILE=release.keystore
STORE_PASSWORD=...
KEY_ALIAS=ptext
KEY_PASSWORD=...
```

保存为根目录 `release.properties`，放置 `release.keystore`，运行 `build.bat`。缺失时使用 debug keystore。两者均不入库。

## 架构

| 文件 | 职责 |
|---|---|
| `main.c` | 事件循环、输入状态机、JNI |
| `ui_lists.c` | 文件列表、书签列表、删除确认 |
| `ui_reading.c` | 阅读页页眉、页脚、正文绘制 |
| `ui_settings.c` | 字号、缩进、行距、主题 |
| `ui_util.c` | dp 缩放、UTF-8 转换、前缀截断 |
| `clamp.h` | 整数夹紧 |
| `reader.c` | mmap、分页、历史、书签、设置、检查点 |
| `encoding.c` | UTF-8、GBK、GB18030 |
| `persist.c` | 临时文件 + fsync + rename |
| `render.c` | 系统字体、RGB565、4-bit alpha 字形缓存 |
| `nolog.c` | `__android_log_print` 空实现 |
| `MainActivity.java` | NativeActivity 壳、SAF 导入、常亮 |

原生依赖仅 `libandroid.so`、`libc.so`。

```mermaid
flowchart TD
  GLUE["android_native_app_glue.c"] --> MAIN["main.c"]
  MAIN --> UIL["ui_lists.c"]
  MAIN --> UIR["ui_reading.c"]
  MAIN --> UIS["ui_settings.c"]
  MAIN --> UTIL["ui_util.c"]
  MAIN --> READER["reader.c"]
  MAIN --> RENDER["render.c"]
  UIL --> UTIL
  UIL --> READER
  UIL --> RENDER
  UIR --> READER
  UIR --> RENDER
  UIS --> READER
  UIS --> RENDER
  READER --> ENC["encoding.c"]
  READER --> PERSIST["persist.c"]
  READER --> RENDER
  RENDER --> STB["stb_truetype.h"]
  MAIN -. JNI .-> JAVA["MainActivity.java"]
  JAVA -. sRescan .-> MAIN
```

导入：`MainActivity.onActivityResult` → `pTEXT-import` 线程 → SAF 复制 + fsync + rename → `sRescan=1` → `rd_scan_dir`。

阅读：`rd_open_file` mmap → 编码探测 → `layout_line` → `paginate_full` / `paginate_step`。

热路径：输入 → `rd_next_page` / `rd_prev_page` → `layout_line` → 解码 / 字形宽度；绘制 → `r_draw_text*` → 字形缓存。

## 数据

```text
files/<name>.txt        正文
files/<name>.txt.name   原始显示名
.prog/<name>.txt.bin    进度
.bm/<name>.txt.bin      书签
.prog/settings.bin      设置
```

`rd_scan_dir` 只读书签头和文件大小；完整 checksum 在打开书签列表时校验。书签兼容旧裸 `Bookmark` 文件。

## 操作

- 阅读页九宫格：1/2/4/7 上一页，3/6/8/9 下一页。
- 5 短按添加/移除当前页书签；页脚中央显示 `*`。
- 按住 5 三秒打开设置；面板外点击关闭。
- 文件列表：滚动、`[ + ]` 导入、长按删除、`[ * ]` 书签入口。
- 书签列表：显示首行文字，点击跳转。
- 前台常亮；5 分钟无触摸后释放常亮。

## 验证

Visual Studio x64 开发者提示符：

```bat
tests\run_tests.bat
```

Linux 或 WSL：

```sh
sh tests/run_reader_tests.sh
```

- 编码：1,065,822 cases PASS。
- reader/persist：WSL PASS；ASan/UBSan PASS。
- 设备回归：启动、前后台、中文、书签、导入、图标、5 分钟释放、翻页重置计时、列表 UI 通过。
- 未验证：导入进度逐帧。

## 性能

口径：PKR110，Android 17，arm64-v8a；release APK；`clock_gettime(CLOCK_MONOTONIC)`；`p50` / `p95` 使用 nearest-rank；输入处理与帧绘制分开；帧 = `r_begin` → `r_end`；不含 SurfaceFlinger 提交等待。性能包临时启用 `PTEXT_BENCH` 与 liblog，正式包无日志代码。

采样：冷启动 10 次、打开文件 10 次、翻页 30 次、书签跳转 10 次。

| 指标 | 读数 | 口径 |
|---|---|---|
| Activity 冷启动 | p50 73 / p95 80 / max 80 ms | `am start -W`，n=10 |
| native 入口 → 首帧 | p50 17.4 / p95 19.1 / max 19.1 ms | native 计时，n=10 |
| 首帧列表绘制 | p50 2.55 / p95 5.08 / max 5.08 ms | 首帧绘制，n=10 |
| 打开文件输入 | p50 1.82 / p95 2.57 / max 2.57 ms | 输入回调，n=10 |
| 打开文件首帧 | p50 6.97 / p95 11.13 / max 11.13 ms | 帧绘制，n=10 |
| 翻页输入 | p50 10 / p95 278 / max 487 µs | 触摸回调，n=120 |
| 阅读页帧 | p50 6.64 / p95 13.32 / max 16.02 ms | 帧绘制，n=60 |
| 书签跳转输入 | p50 422 / p95 1681 / max 1681 µs | 输入回调，n=10 |
| 书签跳转首帧 | p50 7.74 / p95 12.37 / max 12.37 ms | 帧绘制，n=10 |
| APK | 115,503 B | 当前 release |

字形混合优化前后：

| 指标 | 优化前 | 优化后 |
|---|---:|---:|
| 阅读页帧 p50 | 8.58 ms | 6.64 ms |
| 阅读页帧 p95 | 13.93 ms | 13.32 ms |
| 阅读页帧 max | 19.17 ms | 16.02 ms |
| APK | 107,311 B | 115,503 B |

中位帧降低约 22.6%，p95 降低约 4.4%，max 降低约 16.4%。

优化项：预计算 16 级 alpha 混合表、共享字形绘制、删除重复页脚清屏。

## 范围

- 仅 Android 17 / arm64-v8a。
- 系统字体，不打包字体。
- 100 文件、50 书签、64 页历史、80 行/页、200 字符/行。
- 不提供搜索、目录、章节识别、Markdown。

## 许可

代码 MIT，见 [LICENSE](LICENSE)。

`stb_truetype` v1.28，Sean Barrett / RAD Game Tools，Public Domain 或 MIT；构建时下载，不随仓库分发。
