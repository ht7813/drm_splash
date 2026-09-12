// drm_splash.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <png.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include "helper.h"

#define DEFAULT_RESTORE_TIMEOUT 5

// ---------- 保存/恢复用的状态 ----------
struct saved_state {
    int fd;
    uint32_t crtc_id;
    drmModeCrtcPtr orig_crtc;   // 原始 CRTC 配置（含 framebuffer、mode）
    int vt_fd;
    int orig_vt;
    int has_vt;
};

// ---------- 工具函数 ----------
static void die(const char *msg) {
    fprintf(stderr, "错误: %s: %s\n", msg, strerror(errno));
    exit(1);
}

// 用 libpng 解码，输出统一为 32 位 XRGB8888（小端下 B,G,R,X）
static uint32_t *load_png_xrgb(const char *path, int *out_w, int *out_h) {
    FILE *fp = fopen(path, "rb");
    if (!fp) die("打开 PNG");

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) die("png_create_read_struct");
    png_infop info = png_create_info_struct(png);
    if (!info) die("png_create_info_struct");

    if (setjmp(png_jmpbuf(png))) {
        fprintf(stderr, "libpng 解码失败\n");
        exit(1);
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    int w = png_get_image_width(png, info);
    int h = png_get_image_height(png, info);
    int color = png_get_color_type(png, info);
    int depth = png_get_bit_depth(png, info);

    // 统一转换成 8 位 RGB
    if (depth == 16) png_set_strip_16(png);
    if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    // 统一补成 RGBA
    if (color == PNG_COLOR_TYPE_RGB ||
        color == PNG_COLOR_TYPE_GRAY ||
        color == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    size_t rowbytes = png_get_rowbytes(png, info);
    png_bytep *rows = malloc(h * sizeof(png_bytep));
    for (int y = 0; y < h; y++) rows[y] = malloc(rowbytes);
    png_read_image(png, rows);
    png_read_end(png, NULL);

    // 转为 XRGB8888
    uint32_t *pixels = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        png_bytep s = rows[y];
        for (int x = 0; x < w; x++) {
            uint8_t r = s[x*4+0], g = s[x*4+1], b = s[x*4+2];
            pixels[y*w + x] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
        }
        free(rows[y]);
    }
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    *out_w = w; *out_h = h;
    return pixels;
}

// 切换到指定 VT
static int switch_vt(int vt_num, int *out_orig_vt) {
    int fd = open("/dev/tty0", O_RDWR);
    if (fd < 0) return -1;

    struct vt_stat vts;
    if (ioctl(fd, VT_GETSTATE, &vts) < 0) { close(fd); return -1; }
    if (out_orig_vt) *out_orig_vt = vts.v_active;

    if (ioctl(fd, VT_ACTIVATE, vt_num) < 0) { close(fd); return -1; }
    if (ioctl(fd, VT_WAITACTIVE, vt_num) < 0) { close(fd); return -1; }
    return fd;  // 保持打开，退出时切回
}

void handler(int signal) {
    (void)signal;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <image.png> [卡片设备路径]\n", argv[0]);
        return 1;
    }
    const char *img_path = argv[1];
    const char *card_path = argc > 2 ? argv[2] : "auto";

    if (geteuid() != 0) {
        fprintf(stderr, "需要 root 权限\n");
        return 1;
    }

    struct saved_state st = {0};
    st.fd = -1;
    st.vt_fd = -1;

    signal(SIGINT, handler);
    signal(SIGTERM, handler);

    // ---------- 1. 切换 VT ----------
    // 找一个空闲 VT（简单起见用 tty6；也可用 VT_OPENQRY 自动找）
    st.vt_fd = switch_vt(6, &st.orig_vt);
    if (st.vt_fd < 0)
        fprintf(stderr, "警告: 无法切换 VT，继续尝试\n");

    // ---------- 2. 打开 DRM 设备并成为 Master ----------
    if (strcmp(card_path, "auto") == 0) {
        for (int i = 0; i < 8; i++)
        {
            char dev[64];
            snprintf(dev, sizeof(dev), "/dev/dri/card%d", i);
            st.fd = open(dev, O_RDWR | O_CLOEXEC);
            if (st.fd < 0) continue;
            if (!has_connected_display(st.fd))
            {
                close(st.fd);
                st.fd = -1;
                continue;
            }
            break;
        }
        if (st.fd < 0)
        {
            die("未找到可用的 DRM 设备（已尝试 /dev/dri/card0..card7），"
            "可先执行 ls /dev/dri 查看实际设备名");
        }
    }else {
        st.fd = open(card_path, O_RDWR | O_CLOEXEC);
    }
    if (st.fd < 0) die("打开 DRM 设备");

    if (drmSetMaster(st.fd) < 0)
        fprintf(stderr, "警告: drmSetMaster 失败（可能已被占用）\n");

    // ---------- 3. 保存原始 CRTC 状态 ----------
    drmModeResPtr res = drmModeGetResources(st.fd);
    if (!res) die("drmModeGetResources");

    // 选第一个已连接的 Connector 和它当前的 CRTC
    drmModeConnectorPtr conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnectorPtr c = drmModeGetConnector(st.fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
            break;
        }
        if (c) drmModeFreeConnector(c);
    }
    if (!conn) die("找不到已连接的显示输出");

    if (conn->encoder_id == 0) die("Connector 没有绑定 Encoder");
    drmModeEncoderPtr enc = drmModeGetEncoder(st.fd, conn->encoder_id);
    if (!enc) die("drmModeGetEncoder");

    st.crtc_id = enc->crtc_id;
    st.orig_crtc = drmModeGetCrtc(st.fd, st.crtc_id);  // 保存原始状态
    drmModeFreeEncoder(enc);

    // ---------- 4. 解码 PNG ----------
    int img_w, img_h;
    uint32_t *pixels = load_png_xrgb(img_path, &img_w, &img_h);
    printf("图片: %dx%d\n", img_w, img_h);
    drmModeModeInfo *mode = NULL;
    for (int i = 0; i < conn->count_modes; i++) {
        if (conn->modes[i].hdisplay == img_w &&
            conn->modes[i].vdisplay == img_h) {
            mode = &conn->modes[i];
        break;
            }
    }
    if (!mode) {
        // 没有精确匹配的 mode，用最接近的，或者放弃
        fprintf(stderr, "警告：找不到 %dx%d 的显示模式，将使用默认值并拉伸图片：%dx%d\n", img_w, img_h,
                conn->modes[0].hdisplay, conn->modes[0].vdisplay);
        mode = &conn->modes[0];
        // 目标：屏幕 mode 的尺寸
        int mode_w = mode->hdisplay;
        int mode_h = mode->vdisplay;

        // 计算等比缩放后的尺寸，保持宽高比，居中
        float scale = (float)mode_w / img_w;
        if ((float)mode_h / img_h < scale)
            scale = (float)mode_h / img_h;

        int scaled_w = (int)(img_w * scale);
        int scaled_h = (int)(img_h * scale);
        int off_x = (mode_w - scaled_w) / 2;
        int off_y = (mode_h - scaled_h) / 2;

        // 分配 mode 尺寸的像素缓冲，初始化为黑色
        uint32_t *canvas = calloc((size_t)mode_w * mode_h, 4);
        for (size_t i = 0; i < (size_t)mode_w * mode_h; i++) {
            canvas[i] = 0xFF000000;  // 黑

            // 最近邻缩放，写入 canvas 的居中区域
            for (int y = 0; y < scaled_h; y++) {
                int src_y = (int)(y / scale);
                if (src_y >= img_h) src_y = img_h - 1;
                for (int x = 0; x < scaled_w; x++) {
                    int src_x = (int)(x / scale);
                    if (src_x >= img_w) src_x = img_w - 1;
                    canvas[(size_t)(off_y + y) * mode_w + (off_x + x)] =
                    pixels[(size_t)src_y * img_w + src_x];
                }
            }
            free(pixels);
            pixels = canvas;
            img_w = mode_w;
            img_h = mode_h;
        }
    }

    // ---------- 5. 创建 dumb buffer ----------
    struct drm_mode_create_dumb create = {0};
    create.width = img_w;
    create.height = img_h;
    create.bpp = 32;
    if (drmIoctl(st.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0)
        die("CREATE_DUMB");

    uint32_t fb_id;
    if (drmModeAddFB(st.fd, img_w, img_h, 24, 32,
                     create.pitch, create.handle, &fb_id) < 0)
        die("drmModeAddFB");

    struct drm_mode_map_dumb map_req = {0};
    map_req.handle = create.handle;
    if (drmIoctl(st.fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) < 0)
        die("MAP_DUMB");

    uint8_t *mapped = mmap(NULL, create.size, PROT_READ | PROT_WRITE,
                           MAP_SHARED, st.fd, map_req.offset);
    if (mapped == MAP_FAILED) die("mmap dumb buffer");

    // 按 pitch 逐行拷贝
    for (int y = 0; y < img_h; y++) {
        memcpy(mapped + (size_t)y * create.pitch,
               pixels + (size_t)y * img_w,
               (size_t)img_w * 4);
    }
    free(pixels);

    // ---------- 6. 设置为当前 CRTC 的 framebuffer ----------
    // 如果图片尺寸和 mode 不同，这里仍然用 mode 的时序，图片会显示在左上角
    if (drmModeSetCrtc(st.fd, st.crtc_id, fb_id, 0, 0,
                       &conn->connector_id, 1, mode) < 0)
        die("drmModeSetCrtc");
    int restoreTime = DEFAULT_RESTORE_TIMEOUT;
    char* timeout = getenv("DRM_SPLASH_RESTORE_TIME");
    if (timeout) {
        restoreTime = atoi(timeout);
        if (restoreTime == 0) restoreTime = DEFAULT_RESTORE_TIMEOUT;
    }
    printf("已显示，%d 秒后恢复...\n", restoreTime);
    struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
    struct timespec rem;

    while (restoreTime > 0) {
        if (nanosleep(&ts, &rem) < 0) {
            if (errno == EINTR) {
                printf("收到信号，提前恢复\n");
                break;         // 被信号中断
            }
            // 其他错误
            break;
        }
        restoreTime--;
    }

    // ---------- 7. 恢复原始状态 ----------
    if (st.orig_crtc) {
        drmModeSetCrtc(st.fd, st.orig_crtc->crtc_id,
                       st.orig_crtc->buffer_id,
                       st.orig_crtc->x, st.orig_crtc->y,
                       &conn->connector_id, 1,
                       &st.orig_crtc->mode);
        drmModeFreeCrtc(st.orig_crtc);
    }

    // 释放 dumb buffer
    drmModeRmFB(st.fd, fb_id);
    struct drm_mode_destroy_dumb destroy = {0};
    destroy.handle = create.handle;
    drmIoctl(st.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    munmap(mapped, create.size);

    // 释放 Master，让原会话恢复控制
    drmDropMaster(st.fd);

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    close(st.fd);

    // 切回原 VT
    if (st.vt_fd >= 0) {
        ioctl(st.vt_fd, VT_ACTIVATE, st.orig_vt);
        ioctl(st.vt_fd, VT_WAITACTIVE, st.orig_vt);
        close(st.vt_fd);
    }

    printf("已恢复\n");
    return 0;
}
