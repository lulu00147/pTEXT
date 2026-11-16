# pTEXT

Android 17 TXT 阅读器。C11 + Java 11，NativeActivity，arm64-v8a。

## 功能

- UTF-8、UTF-8 BOM、GBK、GB18030
- 系统字体，4-bit alpha，RGB565
- mmap、页式排版、逐帧预排版
- 文件列表：滚动、`[ + ]` 导入、长按删除、`[ * ]` 书签入口
- 阅读页：3×3 九宫格；1/2/4/7 上一页，3/6/8/9 下一页
- 5 短按书签，页脚显示 `*`；按住 3 秒设置
- 设置四行：字号、缩进、行距、主题
- 书签列表：滚动、点击跳转
- 进度、书签、设置原子写入

## 结构

```text
main.c            事件循环、输入、JNI
ui_lists.c        文件列表、书签列表、删除确认
ui_reading.c      阅读页绘制
ui_settings.c     设置面板
ui_util.c         dp 缩放、UTF-8 转换
reader.c          mmap、分页、书签、设置
persist.c         原子写入
encoding.c        UTF-8/GBK/GB18030
render.c          字体光栅、RGB565、字形缓存
MainActivity.java NativeActivity 壳、SAF 导入
```

## 构建

需要 Windows 和仓库根 `tools/`：NDK r30、CMake 4.5.0、Build Tools 37.0.0、JDK 25、Platform 37.2、Ninja 1.13.2。

```bat
build.bat
```

产物：`pTEXT.apk`；minSdk 37，targetSdk 37，compileSdk 37.2；versionCode 1，versionName 0.1。

release 签名：

1. 本地生成 `release.keystore`。
2. 根目录创建 `release.properties`：

```properties
STORE_FILE=release.keystore
STORE_PASSWORD=...
KEY_ALIAS=ptext
KEY_PASSWORD=...
```

3. 运行 `build.bat`。缺少上述文件时使用 debug keystore。

更换签名密钥后必须卸载旧包。keystore 与 properties 不入库。

## 验证

```bat
tests\run_tests.bat
```

```sh
sh tests/run_reader_tests.sh
```

- 编码测试：1,065,822 cases PASS。
- reader/persist：PASS。
- release 0.1 设备验证：UTF-8/GBK、11MB、空文件、书签列表/跳转、设置持久化、强杀恢复、旋转。

## 限制

- 仅 Android 17 / arm64-v8a。
- 无总页数；任意跳转页码可能显示 `--`。
- 文件列表上限 100 本。
- 不提供搜索、目录、章节识别。

## 许可

MIT，见 [LICENSE](LICENSE)。
