# drm_splash

一个极简的 DRM 显示工具：抢占 DRM Master，把一张 PNG 全屏显示若干秒，然后恢复原来的显示状态。

适用于需要在 Linux 控制台或图形会话之上临时显示一张图片的场景，例如启动画面、提示图、调试用全屏截图展示等。

## 功能

- 直接通过 libdrm 操作 DRM，抢占 DRM Master
- 使用 libpng 解码 PNG，并自动缩放到当前屏幕分辨率
- 显示指定秒数后，恢复原始 CRTC 配置并释放 Master
- 支持通过信号（SIGINT (Ctrl+C) / SIGTERM）提前结束显示并恢复
- 支持通过环境变量自定义显示时长
- 不指定 DRM 设备路径时自动检测

## 依赖

- libdrm
- libpng
- Meson（构建时）
- Ninja（构建时）
- 一个可用的 DRM 设备（通常为 `/dev/dri/card0`）

在 Arch Linux 上：

```bash
sudo pacman -S libdrm libpng meson ninja
```

## 编译

```bash
meson setup build
meson compile -C build
```

## 使用

```bash
sudo ./drm_splash <image.png> [card_path]
```

- `<image.png>`：要显示的 PNG 文件路径
- `[card_path]`：可选，DRM 设备路径。不指定时自动检测（遍历 `/dev/dri/card*`，选择第一个可用的）

示例：

```bash
sudo ./drm_splash splash.png
sudo ./drm_splash splash.png /dev/dri/card1
```

### 自定义显示时长

默认显示 5 秒。可以通过环境变量 `DRM_SPLASH_RESTORE_TIME` 指定秒数：

```bash
sudo env DRM_SPLASH_RESTORE_TIME=10 ./drm_splash splash.png
```

> 注意：`sudo` 默认会重置环境变量，因此需要配合 `env` 使用。

### 提前结束

显示期间按 `Ctrl+C` 会触发 `SIGINT`，程序会立即恢复原始显示状态并退出。

## 工作原理

1. 检查 root 权限
2. 切换到空闲 VT
3. 打开 DRM 设备并获取 DRM Master
4. 保存当前 CRTC 的原始配置（framebuffer、mode、位置）
5. 用 libpng 解码 PNG，并缩放到屏幕当前分辨率
6. 创建 dumb buffer，将像素数据写入，注册为 framebuffer
7. 调用 `drmModeSetCrtc` 显示图片
8. 等待指定时长（可被信号中断）
9. 恢复原始 CRTC 配置，释放 Master，切回原 VT

## 限制

- 仅处理第一个已连接的显示输出，多屏环境下其他屏幕不受影响
- 使用 CPU 缩放（最近邻），大图缩放时可能有锯齿
- dumb buffer 不支持 GPU 硬件加速或图层合成
- 恢复依赖原始 CRTC 配置仍然有效；如果原图形会话已退出，恢复可能失败

## 许可

MIT License，详见 [LICENSE](LICENSE) 文件。
