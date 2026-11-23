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
| `test_glyph_blit.c` | 4-bit 字形裁剪、空位图、非法参数、随机差分 |
| `test_glyph_blit8.c` | 8-bit 字形正常/背景/裁剪/随机/空参数/空位图/非法 stride |
| `test_persist.c` | 临时文件 + fsync + rename 正常路径 |
| `test_persist_dir.c` | rename 后父目录 fsync；目录同步失败仍保留已提交文件 |
| `test_persist_fault.c` | 持久化写入故障注入 |
| `test_ui_settings.c` | 设置面板几何、命中、边界、文本适配 |
| `test_ui_lists.c` | 文件/书签列表滚动、命中、导入区、空列表、小窗口 |
| `test_ui_reading.c` | 阅读页页眉/页脚/正文/书签星号/空内容；单线程与 `PTEXT_MT` |
| `test_utf8_gbk.c` | UTF-8、GBK、GB18030、截断和非法序列 |
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
| reader/bookmarks | 5,000 |
| index parser | 100,000 |
| index file（真实 `.idx` 加载） | 5,000 |

## 覆盖说明

- `test_index.c` 覆盖索引解析器的全部分支。
- 行覆盖率、MC/DC 以 `run_mcdc.sh` 的实际输出为准；不再在本文维护手写百分比表。
- 测试是 host 测试，不覆盖 Android 生命周期、JNI、`ANativeWindow` 或 SurfaceFlinger。
- `render_host.c` 是确定性渲染桩。
- `android_lifecycle.sh` 专门覆盖 host 测试无法覆盖的 Activity 退出/重入路径。

## 待补：reader 剩余分支

当前 `reader.c` 为约 99.1% 分支覆盖、97.1% MC/DC，剩余约 7 个分支。`run_mcdc.sh` 仍会在 reader 门禁失败。

已 100% 的文件：

```text
glyph_blit.h
glyph_blit8.h
ui_reading.c
utf8.h
```

剩余分支位置：

| 行 | 场景 |
|---|---|
| 72, 74, 77 | legacy 书签 `fseek`/`ftell`/超大计数 |
| 119 | `bookmark_count` 空路径 |
| 179, 182 | sidecar 目录 `snprintf`/`opendir` 失败 |
| 189, 197, 202 | sidecar 名称后缀、目标路径长度、路径拼接 |
| 210, 224, 229 | name sidecar 目录失败、路径长度、unlink |
| 248 | `name_sidecar_path` 失败 |
| 435, 515, 528 | `sidecar_path` 失败 |
| 548 | `open_page_no` 下界 |
| 598 | `page_max_lines` 下界 |
| 676, 679 | `idx_save` 提前返回 |
| 761 | `idx_parse` 的 `prefix_count == NULL` |
| 767, 768 | `idx_lookup` 的前置条件 |
| 771, 773, 774 | 索引路径、`fopen`、`fseek` 失败 |
| 779, 784 | 索引大小、`fread` 失败 |
| 964 | `last_break_len > 0` |
| 1240 | `save_prog` 路径失败 |
| 1304 | 跳转 checkpoint 边界 |
| 1309 | `page_no` 下界 |
| 1357 | 多 checkpoint 反向分页 |
| 1404, 1411 | settings 路径失败 |
| 1506 | `bookmark_preview` 空 data/decode |
| 1525 | UTF-8 编码长度为 0 |

补测时优先扩展 `test_reader_io_fault.c` 的故障序列，再补超长路径和内部 checkpoint 状态测试。


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
