# Tests

## Encoding

Windows，Visual Studio x64 开发环境：

```bat
run_tests.bat
```

覆盖 65,536 组 GBK 双字节、1,000,000 组随机 UTF-8、fuzz 种子、UTF-8 往返，以及 UTF-8 截断/非法序列、GBK/GB18030 边界和编码失败分支。共 1,065,822 cases。

## Reader / Persist

Linux 或 WSL：

```sh
sh run_reader_tests.sh
```

覆盖空文件、长行、多页、缩进、行距、书签读写/扫描/跳转/首行预览、BOM、CRLF、GBK、名称 sidecar、legacy 书签、删除 sidecar、损坏 settings 迁移、预取翻页，并检查 50 次 open/close 后 fd 与 mmap 数量不增长。

新增 `test_ui_settings.c`，覆盖设置面板几何、行命中、按钮边界、绘制分支、`ui_fit_text` 和 UTF-8 转换。

新增 `test_ui_lists.c`，覆盖文件列表/书签列表的滚动、命中、导入区、空列表、错误提示和 200×160 小窗口分支。reader 测试还覆盖 1×1 窗口下的分页布局下限。

新增 `test_ui_reading.c`，覆盖阅读页页眉/页脚、正文、书签星号、空白正文、页码 0 和无文件路径；同时用 `PTEXT_MT` 编译覆盖实际三线程正文路径。

新增 `test_persist_fault.c`，用链接器 wrap 注入 `EINTR`、EIO、短写、`fsync`、`rename`、`close` 失败。

字形测试覆盖远端/近端裁剪、左侧完全离屏、空位图和非法参数，并加入 5,000 轮随机差分测试。

当前 host 可测模块行覆盖率（gcov）：

```text
reader.c       100.0%
encoding.c     100.0%
persist.c     100.0% (normal + fault injection)
glyph_blit.h  100.0%
ui_reading.c  100.0% (single-thread and MT)
ui_settings.c 100.0%
ui_lists.c    100.0%
ui_util.c     100.0%
```

`render_host.c` 是确定性渲染桩。测试不是 Android UI 测试。

## Sanitizer / Fuzz

Linux 或 WSL：

```sh
sh run_sanitizers.sh
```

启用 ASan、UBSan、LSan，运行 reader、glyph blit、persist、encoding、settings，以及 5,000 轮随机 reader/书签输入。不覆盖 Android 生命周期、JNI、ANativeWindow 或 SurfaceFlinger。

## MC/DC

Linux 或 WSL，需 clang 19：

```sh
sh run_mcdc.sh
```

使用 `-fcoverage-mcdc` 统计布尔决策，并执行阈值门禁：

```text
reader.c      = 100%
encoding.c    = 100%
persist.c     = 100%
UI/glyph      = 100%
```

```text
glyph_blit.h  100.0%
ui_lists.c    100.0%
ui_reading.c  100.0%
ui_settings.c 100.0%
ui_util.c     100.0%
persist.c      100.0%
encoding.c     100.0%
reader.c       100.0%
```

## ThreadSanitizer

Linux 或 WSL，需 clang：

```sh
sh run_tsan.sh
```

编译实际 `PTEXT_MT` 三线程 UI 路径，注入线程创建失败，并在 TSan 下运行。

## Mutation Smoke

Linux 或 WSL：

```sh
sh run_mutation_smoke.sh
```

对关键守卫做受控变异，测试必须失败：

- 删除字形左侧裁剪守卫
- 删除字形右侧裁剪守卫
- 恢复设置行边界 bug
- 修改 UTF-8 编码边界
- 修改 persist 零写处理

任意变异存活则脚本失败。当前 5/5 被测试杀死。

## Windows ASan

Visual Studio x64 Native Tools 命令提示符：

```bat
run_asan_vs.bat
```

使用 MSVC `/fsanitize=address` 运行 glyph blit 和 encoding。Windows ASan 不支持 LSan，且不覆盖 POSIX reader/persist 路径。

## libFuzzer

Linux 或 WSL，需 clang：

```sh
sh run_libfuzzer.sh
```

coverage-guided fuzz：encoding 50,000 次，reader/书签 5,000 次，启用 ASan/UBSan/LSan。
