# pTEXT

纯 C + Android NDK 实现的极简 TXT 阅读器。

## 架构

```
NativeActivity (Java 壳)
└── libptext.so (纯 C, 零堆分配)
    ├── main.c     事件循环 / UI 状态机 / JNI 桥接
    ├── reader.c   文件 mmap / 编码解码 (UTF-8 + GBK) / 分页排版 / 书签 / 进度
    └── render.c   stb_truetype 软光栅 → RGB565 渲染 / 字形缓存 / 日夜主题
```

## 功能

- UTF-8 和 GBK 编码自动检测与解码
- mmap 零拷贝文件读取
- 页式排版 + 预排版（vsync 逐帧增量）
- 字号 / 缩进 / 行距 / 日夜主题可调
- 书签管理，阅读进度自动保存
- SAF 文件导入（Android Storage Access Framework）
- 64 页翻页历史栈，精确前进/后退

## 构建

### 前提

- Windows + Android SDK（含 NDK 26.x、CMake 3.22+、Build Tools 34.x）
- JDK 17+
- 编辑 `build.bat` 中的 `SDK_ROOT`、`JAVA_HOME` 等路径

### 编译

```batch
build.bat
```

生成 `pTEXT.apk`（arm64-v8a, minSdk 29, targetSdk 34）。

## 字体

使用 OPPOSans 字体（`app/src/main/assets/fonts/OPPOSans.ttf`）。替换字体时保持文件名一致即可。

## 许可

MIT
