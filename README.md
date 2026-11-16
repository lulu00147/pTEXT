# pTEXT

Android 原生 TXT 阅读器。C11 + Java 11，NativeActivity。

## 功能

- UTF-8、UTF-8 BOM；GB2312 常用子集
- 系统字体渲染，优先中文字体，不打包字体
- mmap 文件读取，页式排版，逐帧预排版
- 无标题栏、14dp 窄边距阅读页；底部显示当前页码与百分比
- 字号、缩进、行距、夜间/日间主题
- 页码仅当前页；总页数不计算，跳转或重排后可能显示 `--`
- 阅读页长按添加/移除书签；设置页查看、跳转
- 阅读进度在暂停、返回或终止时保存
- SAF 导入 txt

## 结构

```text
MainActivity.java      NativeActivity 壳、SAF 导入
main.c                 事件循环、UI 状态机、JNI
reader.c               mmap、编码、分页、书签、设置
render.c               系统字体、stb_truetype 光栅、RGB565、字形缓存
```

## 构建

需要 Windows 和仓库根目录的 `tools/`（NDK r30、CMake 4.5.0、Build Tools 37.0.0、JDK 25、Platform 37.2、Ninja 1.13.2；约 2.8 GB，不纳入 git）。

```bat
build.bat
```

产物 `pTEXT.apk`：arm64-v8a，minSdk 29，targetSdk 34，compileSdk 37.2。首次构建生成 debug keystore，已被 `.gitignore` 排除。

## 限制

- 无自动化测试；无 CI。
- GBK 仅覆盖 GB2312 一级、标点和全角字符，扩展字符显示 `?`；编码按前 4 KiB 探测，长 ASCII 前缀的 GBK 文件可能误判。
- 未声明 versionCode/versionName；仅 debug 签名。
- 系统字体覆盖与度量随设备变化；设备无 CJK 字体时中文不显示。
- 进度在暂停、返回或终止时保存，不每次翻页；写入非原子，崩溃可能丢失最后一次更新。
- 从保存进度打开文件时，首次上一页使用启发式扫描；大文件可能回退到文件头。

## 许可

代码按 MIT 发布（LICENSE 文件待补）。
