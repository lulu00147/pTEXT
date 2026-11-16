# pTEXT

Android 原生 TXT 阅读器。C11 + Java 11，NativeActivity。

## 功能

- UTF-8、UTF-8 BOM、GBK（GB2312 + GBK 扩展）
- 系统字体渲染，优先中文字体，不打包字体
- 4-bit alpha 字形抗锯齿，RGB565 背景混合
- mmap 文件读取，页式排版，逐帧预排版
- 无标题栏、全屏 edge-to-edge、5dp 正文边距；顶部书名/时间，底部页码/百分比
- 文件列表支持滚动、长文件名省略、长按 3 秒删除；右下角 `[ + ]` 导入
- 默认 24sp 正文、`#A9A9A9` 字色、按汉字宽度缩进、满行两端对齐并可压缩/放宽字间空隙
- 阅读页 3×3 九宫格：1/2/4/7 上一页，3/6/8/9 下一页，5 短按书签、按住 3 秒设置
- 书签状态显示在页脚中央；设置面板保留字号、缩进、行距、主题四行
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

产物 `pTEXT.apk`：arm64-v8a，仅 Android 17 / API 37，targetSdk 37，compileSdk 37.2，versionCode 1，versionName 0.1。

发布签名：

1. 本地生成 `release.keystore`（不入库）。
2. 在根目录创建 `release.properties`（不入库）：
   ```properties
   STORE_FILE=release.keystore
   STORE_PASSWORD=...
   KEY_ALIAS=ptext
   KEY_PASSWORD=...
   ```
3. 再次运行 `build.bat`，脚本自动使用 release keystore；缺失时回退 debug keystore。

更换签名密钥后，Android 不允许覆盖安装，需要卸载旧包再安装新包。

## 验证

- 主机 fuzz：4,065,536 组随机 UTF-8 输入、65,536 组 GBK 双字节组合，ASan 通过。
- 主机测试：`tests\run_tests.bat`，1,065,536 组用例通过。
- 设备冒烟：文件列表、打开阅读、下一页、短按5书签、长按5设置、面板外关闭、强杀后进度恢复、文件长按删除通过。
- 未测：实际 GBK 文件导入、≥10 MB 文件、空文件、旋转、设置持久化。

## 限制

- 无 CI。
- GBK 使用 126×191 Unicode 表，覆盖 GB2312 与 GBK 扩展；GB18030 四字节序列显示 `?`。
- 编码按前 4 KiB 探测，长 ASCII 前缀的 GBK 文件可能误判。
- 书签可以短按中心格 5 添加/移除并在页脚显示，但没有书签列表/跳转入口。
- release keystore 和密码只保存在本地，不入库。
- 系统字体覆盖与度量随设备变化；设备无 CJK 字体时中文不显示。
- 进度在暂停、返回或终止时保存，不每次翻页；写入非原子，崩溃可能丢失最后一次更新。
- 从保存进度打开文件时，首次上一页使用启发式扫描；大文件可能回退到文件头。

## 许可

MIT，见 [LICENSE](LICENSE)。
