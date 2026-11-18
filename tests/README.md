# Tests

## Encoding

Windows，Visual Studio x64 开发环境：

```bat
run_tests.bat
```

覆盖 65,536 组 GBK 双字节、1,000,000 组随机 UTF-8、fuzz 种子、UTF-8 往返。共 1,065,822 cases。

## Reader / Persist

Linux 或 WSL：

```sh
sh run_reader_tests.sh
```

覆盖空文件、长行、多页、缩进、行距、书签读写/扫描/跳转/首行预览、原子写入失败清理，并检查 50 次 open/close 后 fd 与 mmap 数量不增长。

`render_host.c` 是确定性渲染桩。测试不是 Android UI 测试。

## Sanitizer / Fuzz

Linux 或 WSL：

```sh
sh run_sanitizers.sh
```

启用 ASan、UBSan、LSan，运行 reader、glyph blit、persist、encoding，以及 5,000 轮随机 reader/书签输入。不覆盖 Android 生命周期、JNI、ANativeWindow 或 SurfaceFlinger。
