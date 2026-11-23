# Tests

测试按职责分文件，目录保持扁平；脚本从 `tests/` 运行。

## 入口

| 命令 | 用途 |
|---|---|
| `sh run_all.sh` | Linux/WSL 全量：单元、ASan/UBSan、libFuzzer |
| `sh run_reader_tests.sh` | Reader、索引、字形、UI、持久化 |
| `sh run_sanitizers.sh` | ASan/UBSan/LSan；含故障与崩溃测试 |
| `sh run_libfuzzer.sh` | 编码、reader、索引解析、索引文件 fuzz |
| `sh run_mcdc.sh` | MC/DC 门禁，需 clang 19 |
| `sh run_tsan.sh` | 三线程 UI 的 TSan |
| `sh run_mutation_smoke.sh` | 受控变异，测试必须杀死变异 |
| `sh android_lifecycle.sh` | 真机/模拟器：退出后重新进入必须回到 MainActivity |
| `run_tests.bat` | Windows x64 编码测试 |
| `run_asan_vs.bat` | Windows MSVC ASan |

## 单元测试

| 文件 | 覆盖 |
|---|---|
| `test_reader.c` | 空文件、长行、多页、缩进/行距、书签、BOM、CRLF、编码、名称 sidecar、删除、设置迁移、预取、索引生命周期 |
| `test_index.c` | 索引头/记录解析、校验和、单调性、截断、多余数据、1024/1025 记录、`INT64_MAX` 边界 |
| `test_index_fault.c` | `.idx.tmp` 的 open/write/短写/fsync/close/rename 失败；旧索引不被破坏 |
| `test_index_crash.c` | 临时写入后、fsync 后、rename 后杀进程；验证原子性 |
| `test_glyph_blit.c` | 4-bit 字形裁剪、空位图、非法参数、随机差分、1×1/1×2/2×1/2×2 有限域穷举 |
| `test_glyph_blit8.c` | 8-bit 字形正常/背景/裁剪/随机/空参数/空位图/非法 stride/有限域穷举 |
| `test_persist.c` | 临时文件 + fsync + rename 正常路径 |
| `test_persist_dir.c` | rename 后父目录 fsync；目录同步失败仍保留已提交文件 |
| `test_persist_fault.c` | 持久化写入故障注入 |
| `test_ui_settings.c` | 设置面板几何、命中、边界、文本适配 |
| `test_ui_lists.c` | 文件/书签列表滚动、命中、导入区、空列表、小窗口 |
| `test_ui_reading.c` | 阅读页页眉/页脚/正文/书签星号/空内容；单线程与 `PTEXT_MT` |
| `test_utf8_gbk.c` | UTF-8、GBK、GB18030、截断和非法序列 |
| `test_affinity.c` | 大小核模式夹紧和绑定入口 |
| `test_reader_io_fault.c` | reader 的 fopen/open/mmap/fread/fseek 等 I/O 故障 |

编码固定用例与随机用例合计 1,065,822 cases。

## 索引生命周期

`test_reader.c` 覆盖：

- 小文件不生成 `.idx`
- 删除 `.idx` 后自动重建
- 设置变化、文件大小变化使旧索引失效
- 删除书籍清理 `.prog` 和 `.idx`
- 索引可用时跳转，损坏时回退

`test_index_fault.c` 和 `test_index_crash.c` 额外覆盖写入故障与进程中断后的原子性。

## Fuzz

`run_libfuzzer.sh` 启用 ASan/UBSan/LSan：

| 目标 | 次数 |
|---|---:|
| encoding | 50,000 |
| glyph blit8 差分 | 20,000 |
| glyph blit 4-bit 差分 | 20,000 |
| reader/bookmarks | 5,000 |
| index parser | 100,000 |
| index file（真实 `.idx` 加载） | 5,000 |

## 覆盖说明

- `test_index.c` 覆盖索引解析器的全部分支。
- 行覆盖率、MC/DC 以 `run_mcdc.sh` 的实际输出为准；不再在本文维护手写百分比表。
- 测试是 host 测试，不覆盖 Android 生命周期、JNI、`ANativeWindow` 或 SurfaceFlinger。
- `render_host.c` 是确定性渲染桩。
- `android_lifecycle.sh` 专门覆盖 host 测试无法覆盖的 Activity 退出/重入路径。

## reader 覆盖

`reader.c` 已达到 100% 行、分支、函数和 MC/DC，`run_mcdc.sh` reader 门禁通过。

全部目标文件：

```text
glyph_blit.h
glyph_blit8.h
ui_reading.c
utf8.h
reader.c
```


## Windows

编码测试：

```bat
run_tests.bat
```

ASan：

```bat
run_asan_vs.bat
```

Windows ASan 不支持 LSan，也不覆盖 POSIX reader/persist 路径。
