# Tests

## Encoding fuzz/regression

Windows, from a Visual Studio x64 developer prompt:

```bat
run_tests.bat
```

Covers 65,536 GBK byte pairs, 1,000,000 random UTF-8 buffers, fuzz seed cases and UTF-8 roundtrips.

## Reader pagination

Linux or WSL with a C compiler:

```sh
sh run_reader_tests.sh
```

Covers empty files, long lines, multi-page iteration, indent, line gap, bookmark write/read and bookmark jump.

The tests use `render_host.c` as a deterministic renderer stub. They are not Android UI tests.
