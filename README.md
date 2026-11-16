# pTEXT

Android 17 TXT 阅读器。C11 + Java 11，NativeActivity，arm64-v8a。

## 功能

- UTF-8、UTF-8 BOM、GBK、GB18030
- 系统字体，4-bit alpha，RGB565
- mmap、页式排版、逐帧预排版
- 文件列表：滚动、`[ + ]` 导入、长按删除、`[ * ]` 书签入口
- 阅读页：3×3 九宫格；1/2/4/7 上一页，3/6/8/9 下一页
- 启动图标：深色背景、白色 p、碗内三条文字线
- 5 短按书签，页脚显示 `*`；按住 3 秒设置
- 书签列表显示首行文字，点击跳转
- 设置四行：字号、缩进、行距、主题
- 前台保持常亮；5 分钟无触摸后交还系统熄屏
- 进度、书签、设置原子写入

## 构建

需要 Windows 和仓库根 `tools/`：

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

3. 运行 `build.bat`。缺少文件时使用 debug keystore。

更换密钥后卸载旧包。keystore 与 properties 不入库。

## 验证

```bat
tests\run_tests.bat
```

```sh
sh tests/run_reader_tests.sh
```

- 编码测试：1,065,822 cases PASS。
- reader/persist：PASS。

## 许可

MIT，见 [LICENSE](LICENSE)。
