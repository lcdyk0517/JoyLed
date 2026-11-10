/*
 * WS2812 LED Controller
 * Copyright (C) 2026 lcdyk0517@qq.com
 * 
 * 逆向工程反编译重构
 * 适用设备: XiFan DC40V, XiFan DC35V, XiFan XF40V, XiFan R36MAX2, XiFan XF28
 * 
 * 用法: ws2812 [--device <spidev>] <mode> [brightness]
 * 
 * 编译: make
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <math.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <linux/input.h>

/* ==================== 配置参数 ==================== */

#define LED_COUNT           8
#define LED_BUFFER_SIZE     (LED_COUNT * 24)
#define SPI_SPEED           8000000
#define DEFAULT_SPI_DEVICE  "/dev/spidev1.0"
#define JOYSTICK_DEVICE     "/dev/input/event3"

/* 刷新间隔 (微秒) */
#define REFRESH_FAST        10000
#define REFRESH_NORMAL      50000
#define REFRESH_SLOW        200000

/* 亮度级别 */
#define BRIGHTNESS_LOW      0
#define BRIGHTNESS_MEDIUM   2
#define BRIGHTNESS_HIGH     3

/* 颜色常量 */
#define COLOR_RED           255, 0, 0
#define COLOR_GREEN         0, 255, 0
#define COLOR_BLUE          0, 0, 255
#define COLOR_YELLOW        255, 255, 0
#define COLOR_PURPLE        255, 0, 255
#define COLOR_CYAN          0, 255, 255
#define COLOR_WHITE         255, 255, 255

/* ==================== 全局变量 ==================== */

static int spi_fd = -1;
static volatile int quit = 0;
static uint8_t brightness_level = BRIGHTNESS_HIGH;

/* ==================== 结构体定义 ==================== */

typedef struct {
    uint8_t r, g, b;
} color_t;

typedef void (*effect_func_t)(uint8_t *buffer);

typedef struct {
    const char *name;
    effect_func_t func;
} mode_entry_t;

/* ==================== 信号处理 ==================== */

static void signal_handler(int sig) {
    (void)sig;
    quit = 1;
}

/* ==================== 摇杆接口函数 ==================== */

static int joystick_open(const char *device) {
    int fd = open(device, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open joystick device");
        return -1;
    }
    return fd;
}

static int joystick_read(int fd, int *x, int *y) {
    struct input_event ev;
    static int axis_x = 0, axis_y = 0;

    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_RX || ev.code == ABS_X) {
                axis_x = ev.value;
            } else if (ev.code == ABS_RY || ev.code == ABS_Y) {
                axis_y = ev.value;
            }
        }
    }

    *x = axis_x;
    *y = axis_y;
    return 0;
}

/* ==================== SPI 接口函数 ==================== */

static int spi_init(const char *device) {
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        perror("Failed to open SPI device");
        return -1;
    }
    
    uint8_t mode = 0;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("Failed to set SPI mode");
        close(fd);
        return -1;
    }
    
    uint8_t bits = 8;
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        perror("Failed to set SPI bits per word");
        close(fd);
        return -1;
    }
    
    uint32_t speed = SPI_SPEED;
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("Failed to set SPI speed");
        close(fd);
        return -1;
    }
    
    return fd;
}

static void spi_write(int fd, uint8_t *data, size_t len) {
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)data;
    tr.len = len;
    tr.delay_usecs = 50;
    tr.speed_hz = SPI_SPEED;
    tr.bits_per_word = 8;
    
    ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
}

/* ==================== WS2812 编码函数 ==================== */

/* 亮度查找表 */
static const uint8_t brightness_table[] = {
    [BRIGHTNESS_LOW]    = 13,   /* 5% */
    [BRIGHTNESS_MEDIUM] = 102,  /* 40% */
    [BRIGHTNESS_HIGH]   = 255   /* 100% */
};

static uint8_t apply_brightness(uint8_t value) {
    return (uint16_t)value * brightness_table[brightness_level] / 255;
}

/* 设置单个 LED 颜色 (GRB 顺序) */
static void ws2812_set_color(uint8_t *buffer, int led, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t *ptr = buffer + led * 24;
    
    r = apply_brightness(r);
    g = apply_brightness(g);
    b = apply_brightness(b);
    
    for (int i = 0; i < 8; i++) {
        ptr[i]      = (g & (1 << (7 - i))) ? 0xFC : 0xC0;
        ptr[i + 8]  = (r & (1 << (7 - i))) ? 0xFC : 0xC0;
        ptr[i + 16] = (b & (1 << (7 - i))) ? 0xFC : 0xC0;
    }
}

static void ws2812_set_all(uint8_t *buffer, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < LED_COUNT; i++) {
        ws2812_set_color(buffer, i, r, g, b);
    }
}

static void ws2812_off(uint8_t *buffer) {
    ws2812_set_all(buffer, 0, 0, 0);
    spi_write(spi_fd, buffer, LED_BUFFER_SIZE);
}

static void refresh(uint8_t *buffer) {
    spi_write(spi_fd, buffer, LED_BUFFER_SIZE);
}

/* ==================== 亮度解析 ==================== */

static int parse_brightness(const char *str) {
    static const struct { const char *name; uint8_t level; } brightness_map[] = {
        {"LOW", BRIGHTNESS_LOW}, {"MEDIUM", BRIGHTNESS_MEDIUM}, {"HIGH", BRIGHTNESS_HIGH}
    };
    for (int i = 0; i < 3; i++) {
        if (strcmp(str, brightness_map[i].name) == 0) {
            brightness_level = brightness_map[i].level;
            printf("%s brightness.\n", str);
            return 0;
        }
    }
    return -1;
}

/* ==================== LED 效果函数 ==================== */

/* 常亮效果 */
static void effect_constant(uint8_t *buffer, color_t color) {
    while (!quit) {
        ws2812_set_all(buffer, color.r, color.g, color.b);
        refresh(buffer);
        usleep(REFRESH_NORMAL);
    }
}

static void effect_red(uint8_t *buffer)     { effect_constant(buffer, (color_t){COLOR_RED}); }
static void effect_green(uint8_t *buffer)   { effect_constant(buffer, (color_t){COLOR_GREEN}); }
static void effect_blue(uint8_t *buffer)    { effect_constant(buffer, (color_t){COLOR_BLUE}); }

/* 交替效果 */
static void effect_alternate(uint8_t *buffer, color_t c1, color_t c2) {
    while (!quit) {
        for (int i = 0; i < LED_COUNT; i++) {
            ws2812_set_color(buffer, i, i % 2 == 0 ? c1.r : c2.r, 
                                      i % 2 == 0 ? c1.g : c2.g, 
                                      i % 2 == 0 ? c1.b : c2.b);
        }
        refresh(buffer);
        usleep(REFRESH_NORMAL);
    }
}

static void effect_red_green(uint8_t *buffer)  { effect_alternate(buffer, (color_t){COLOR_RED}, (color_t){COLOR_GREEN}); }
static void effect_green_blue(uint8_t *buffer) { effect_alternate(buffer, (color_t){COLOR_GREEN}, (color_t){COLOR_BLUE}); }
static void effect_blue_red(uint8_t *buffer)   { effect_alternate(buffer, (color_t){COLOR_BLUE}, (color_t){COLOR_RED}); }

/* 三色循环 */
static void effect_red_green_blue(uint8_t *buffer) {
    static const color_t colors[3] = {{COLOR_RED}, {COLOR_GREEN}, {COLOR_BLUE}};
    while (!quit) {
        for (int i = 0; i < LED_COUNT; i++) {
            ws2812_set_color(buffer, i, colors[i % 3].r, colors[i % 3].g, colors[i % 3].b);
        }
        refresh(buffer);
        usleep(REFRESH_NORMAL);
    }
}

/* 单色呼吸 */
static void effect_breath_solid(uint8_t *buffer, color_t color) {
    int brightness = 0, direction = 1;
    while (!quit) {
        ws2812_set_all(buffer, 
            (uint16_t)color.r * brightness / 255,
            (uint16_t)color.g * brightness / 255,
            (uint16_t)color.b * brightness / 255);
        refresh(buffer);
        usleep(REFRESH_FAST);
        
        brightness += direction;
        if (brightness >= 255) direction = -1;
        if (brightness <= 0) direction = 1;
    }
}

static void effect_breath_red(uint8_t *buffer)   { effect_breath_solid(buffer, (color_t){COLOR_RED}); }
static void effect_breath_green(uint8_t *buffer) { effect_breath_solid(buffer, (color_t){COLOR_GREEN}); }
static void effect_breath_blue(uint8_t *buffer)  { effect_breath_solid(buffer, (color_t){COLOR_BLUE}); }

/* 渐变呼吸 */
typedef void (*color_calc_t)(int b, color_t *c);

static void effect_breath_gradient(uint8_t *buffer, color_calc_t calc) {
    int brightness = 0, direction = 1;
    color_t color;
    while (!quit) {
        calc(brightness, &color);
        ws2812_set_all(buffer, color.r, color.g, color.b);
        refresh(buffer);
        usleep(REFRESH_FAST);
        
        brightness += direction;
        if (brightness >= 255) direction = -1;
        if (brightness <= 0) direction = 1;
    }
}

static void calc_blue_red(int b, color_t *c)     { c->r = b; c->g = 0; c->b = 255 - b; }
static void calc_green_blue(int b, color_t *c)   { c->r = 0; c->g = b; c->b = 255 - b; }
static void calc_red_green(int b, color_t *c)    { c->r = b; c->g = 255 - b; c->b = 0; }
static void calc_rgb_gradient(int b, color_t *c) { c->r = b; c->g = b; c->b = 255 - b; }

static void effect_breath_blue_red(uint8_t *buffer)    { effect_breath_gradient(buffer, calc_blue_red); }
static void effect_breath_green_blue(uint8_t *buffer)  { effect_breath_gradient(buffer, calc_green_blue); }
static void effect_breath_red_green(uint8_t *buffer)   { effect_breath_gradient(buffer, calc_red_green); }
static void effect_breath_rgb(uint8_t *buffer)         { effect_breath_gradient(buffer, calc_rgb_gradient); }

/* 多色循环呼吸 */
static void effect_breathing(uint8_t *buffer) {
    static const color_t colors[] = {
        {COLOR_RED}, {COLOR_GREEN}, {COLOR_BLUE},
        {COLOR_YELLOW}, {COLOR_PURPLE}, {COLOR_CYAN}, {COLOR_WHITE}
    };
    const int num_colors = sizeof(colors) / sizeof(colors[0]);
    int color_idx = 0, brightness = 0, direction = 1;
    
    while (!quit) {
        ws2812_set_all(buffer,
            (uint16_t)colors[color_idx].r * brightness / 255,
            (uint16_t)colors[color_idx].g * brightness / 255,
            (uint16_t)colors[color_idx].b * brightness / 255);
        refresh(buffer);
        usleep(REFRESH_FAST);
        
        brightness += direction * 2;
        if (brightness >= 255) { brightness = 255; direction = -1; }
        if (brightness <= 0) { brightness = 0; direction = 1; color_idx = (color_idx + 1) % num_colors; }
    }
}

/* 滚动彩虹 */
static void effect_scrolling(uint8_t *buffer) {
    static const color_t colors[] = {
        {COLOR_RED}, {255, 127, 0}, {COLOR_YELLOW},
        {COLOR_GREEN}, {COLOR_CYAN}, {COLOR_BLUE}, {COLOR_PURPLE}
    };
    const int num_colors = sizeof(colors) / sizeof(colors[0]);
    int color_idx = 0;
    
    while (!quit) {
        for (int i = 0; i < LED_COUNT; i++) {
            int idx = (color_idx + i) % num_colors;
            ws2812_set_color(buffer, i, colors[idx].r, colors[idx].g, colors[idx].b);
        }
        refresh(buffer);
        usleep(REFRESH_SLOW);
        color_idx = (color_idx + 1) % num_colors;
    }
}

/* 关闭 */
static void effect_off(uint8_t *buffer) {
    ws2812_off(buffer);
}

/* 摇杆追光效果 */

/* 摇杆追光效果：Aurora + Comet */
static void effect_joystick(uint8_t *buffer) {
    int js_fd = joystick_open(JOYSTICK_DEVICE);
    if (js_fd < 0)
        return;

    int x = 0, y = 0;
    int center_x = 0, center_y = 0;

    /* 动画参数 */
    const float DEADZONE = 0.18f;
    const float RESPONSE = 0.32f;
    const float MAIN_SIGMA = 0.65f;
    const float TRAIL_DECAY = 0.94f;
    const float TRAIL_GAIN = 0.65f;
    const float BREATH_SPEED = 0.055f;

    /* LED 状态 */
    float position = 0.0f;
    float previous_position = 0.0f;
    float velocity = 0.0f;

    float trail_level[LED_COUNT] = {0};
    float trail_hue[LED_COUNT] = {0};

    float breath_phase = 0.0f;
    float breath = 1.0f;

    int initialized = 0;

    /* 摇杆中心校准 */
    for (int i = 0; i < 100 && !quit; i++) {
        joystick_read(js_fd, &x, &y);
        center_x += x;
        center_y += y;
        usleep(2000);
    }

    center_x /= 100;
    center_y /= 100;

    /*
     * 兼容常见轴范围：
     * -32768 ~ 32767
     * 0 ~ 255
     * 0 ~ 1023
     */
    float scale_x = center_x > 0 ? (float)center_x : 32768.0f;
    float scale_y = center_y > 0 ? (float)center_y : 32768.0f;

    if (scale_x < 1.0f) scale_x = 32768.0f;
    if (scale_y < 1.0f) scale_y = 32768.0f;

    /*
     * HSV 转 RGB
     * H: 0 ~ 360
     * S: 0 ~ 1
     * V: 0 ~ 1
     */
    #define HSV_TO_RGB(H, R, G, B) do { \
        float _h = fmodf((H), 360.0f); \
        if (_h < 0.0f) _h += 360.0f; \
        float _x = 1.0f - fabsf(fmodf(_h / 60.0f, 2.0f) - 1.0f); \
        float _m = 0.0f; \
        float _r, _g, _b; \
        if (_h < 60.0f)       { _r = 1; _g = _x; _b = 0; } \
        else if (_h < 120.0f){ _r = _x; _g = 1; _b = 0; } \
        else if (_h < 180.0f){ _r = 0; _g = 1; _b = _x; } \
        else if (_h < 240.0f){ _r = 0; _g = _x; _b = 1; } \
        else if (_h < 300.0f){ _r = _x; _g = 0; _b = 1; } \
        else                 { _r = 1; _g = 0; _b = _x; } \
        (R) = (uint8_t)((_r + _m) * 255.0f); \
        (G) = (uint8_t)((_g + _m) * 255.0f); \
        (B) = (uint8_t)((_b + _m) * 255.0f); \
    } while (0)

    while (!quit) {
        joystick_read(js_fd, &x, &y);

        /* 归一化摇杆输入 */
        float nx = (x - center_x) / scale_x;
        float ny = (y - center_y) / scale_y;

        if (nx > 1.0f) nx = 1.0f;
        if (nx < -1.0f) nx = -1.0f;
        if (ny > 1.0f) ny = 1.0f;
        if (ny < -1.0f) ny = -1.0f;

        float magnitude = sqrtf(nx * nx + ny * ny);
        if (magnitude > 1.0f)
            magnitude = 1.0f;

        int active = magnitude > DEADZONE;

        float target = position;

        if (active) {
            /* X 向右为正，Y 向下为正 */
            float angle = atan2f(-ny, nx);

            /* LED 0 = 左下，LED 0~7 顺时针 */
            target = (angle + (float)M_PI * 0.75f)
                * LED_COUNT / (2.0f * (float)M_PI);

            target = fmodf(target + LED_COUNT, LED_COUNT);
            while (target < 0.0f)
                target += LED_COUNT;

            while (target >= LED_COUNT)
                target -= LED_COUNT;

            if (!initialized) {
                position = target;
                previous_position = target;
                initialized = 1;
            }
        }

        /* 环形最短路径插值 */
        float delta = target - position;

        while (delta > LED_COUNT / 2.0f)
            delta -= LED_COUNT;

        while (delta < -LED_COUNT / 2.0f)
            delta += LED_COUNT;

        if (active) {
            position += delta * RESPONSE;
        } else {
            /*
             * 回中后保持当前位置，
             * 不再追踪方向，光效自然消散。
             */
        }

        while (position < 0.0f)
            position += LED_COUNT;

        while (position >= LED_COUNT)
            position -= LED_COUNT;

        velocity = position - previous_position;

        if (velocity > LED_COUNT / 2.0f)
            velocity -= LED_COUNT;

        if (velocity < -LED_COUNT / 2.0f)
            velocity += LED_COUNT;

        previous_position = position;

        /* 速度影响拖尾长度 */
        float speed = fabsf(velocity);
        float tail_length = 0.5f + speed * 18.0f;

        if (tail_length > 3.0f)
            tail_length = 3.0f;

        /* 根据摇杆方向生成连续色相 */
        float hue = position * (360.0f / LED_COUNT);

        /* 摇杆越偏离中心，光效越明显 */
        float intensity = 1.0f;

        if (active) {
            float input_strength =
                (magnitude - DEADZONE) / (1.0f - DEADZONE);

            if (input_strength < 0.0f)
                input_strength = 0.0f;

            intensity = 0.65f + input_strength * 0.35f;
        }

        /* 柔和呼吸 */
        breath_phase += BREATH_SPEED;

        if (breath_phase >= 2.0f * (float)M_PI)
            breath_phase -= 2.0f * (float)M_PI;

        breath = 0.88f + 0.12f * sinf(breath_phase);

        /* 逐颗 LED 计算光效 */
        for (int i = 0; i < LED_COUNT; i++) {

            /* 环形距离 */
            float distance = fabsf((float)i - position);

            if (distance > LED_COUNT / 2.0f)
                distance = LED_COUNT - distance;

            /*
             * 高斯主光晕
             * 主光点向两侧平滑扩散。
             */
            float main_light = expf(
                -(distance * distance) /
                (2.0f * MAIN_SIGMA * MAIN_SIGMA)
            );

            /*
             * 彗星拖尾。
             * 根据移动方向判断 LED 是否位于后方。
             */
            float comet = 0.0f;

            if (active && speed > 0.001f) {
                float behind = 0.0f;

                if (velocity > 0.0f) {
                    behind = position - (float)i;
                } else {
                    behind = (float)i - position;
                }

                while (behind < 0.0f)
                    behind += LED_COUNT;

                if (behind > 0.0f && behind < LED_COUNT / 2.0f) {
                    comet = expf(-behind / tail_length)
                          * TRAIL_GAIN
                          * (speed * 12.0f > 1.0f
                             ? 1.0f : speed * 12.0f);
                }
            }

            /*
             * 记录经过的光点。
             * 每颗 LED 保留自己的历史亮度和颜色。
             */
            if (active && main_light > 0.08f) {
                float value = main_light * intensity * 0.65f;

                if (value > trail_level[i]) {
                    trail_level[i] = value;
                    trail_hue[i] = hue;
                }
            }

            /* 指数衰减 */
            trail_level[i] *= TRAIL_DECAY;

            if (trail_level[i] < 0.005f)
                trail_level[i] = 0.0f;

            /* 摇杆回中后禁止生成新的主光 */
            float main_value = active
                ? main_light * intensity * breath
                : 0.0f;

            /* 回中后仅保留已有拖尾 */
            float trail_value = trail_level[i];

            float total =
                main_value + comet * intensity + trail_value;

            if (total > 1.0f)
                total = 1.0f;

            /*
             * 主光与历史光尾分别进行颜色混合。
             */
            uint8_t mr, mg, mb;
            uint8_t tr, tg, tb;

            HSV_TO_RGB(hue, mr, mg, mb);
            HSV_TO_RGB(trail_hue[i], tr, tg, tb);

            float main_weight = active
                ? main_value + comet * intensity
                : 0.0f;
            float trail_weight = trail_value;

            float weight = main_weight + trail_weight;

            uint8_t r = 0, g = 0, b = 0;

            if (weight > 0.001f) {
                r = (uint8_t)(
                    (mr * main_weight + tr * trail_weight) / weight
                );

                g = (uint8_t)(
                    (mg * main_weight + tg * trail_weight) / weight
                );

                b = (uint8_t)(
                    (mb * main_weight + tb * trail_weight) / weight
                );
            }

            /*
             * 输出强度。
             * ws2812_set_color 内部会统一应用全局亮度。
             */
            r = (uint8_t)(r * total);
            g = (uint8_t)(g * total);
            b = (uint8_t)(b * total);

            ws2812_set_color(buffer, i, r, g, b);
        }

        refresh(buffer);
        usleep(REFRESH_FAST);
    }

    #undef HSV_TO_RGB

    close(js_fd);
}


/* ==================== 模式映射表 ==================== */

static const mode_entry_t mode_table[] = {
    {"OFF",                      effect_off},
    {"Joystick",                 effect_joystick},
    {"Scrolling",                effect_scrolling},
    {"Breathing",                effect_breathing},
    {"Breathing_Red",            effect_breath_red},
    {"Breathing_Green",          effect_breath_green},
    {"Breathing_Blue",           effect_breath_blue},
    {"Breathing_Blue_Red",       effect_breath_blue_red},
    {"Breathing_Green_Blue",     effect_breath_green_blue},
    {"Breathing_Red_Green",      effect_breath_red_green},
    {"Breathing_Red_Green_Blue", effect_breath_rgb},
    {"Red_Green_Blue",           effect_red_green_blue},
    {"Blue_Red",                 effect_blue_red},
    {"Blue",                     effect_blue},
    {"Green_Blue",               effect_green_blue},
    {"Green",                    effect_green},
    {"Red_Green",                effect_red_green},
    {"Red",                      effect_red},
    {NULL, NULL}
};

/* ==================== 主函数 ==================== */

int main(int argc, char *argv[]) {
    const char *spi_device = DEFAULT_SPI_DEVICE;
    int argi = 1;

    if (argc < 2) {
usage:
        printf("Usage: %s [--device <spidev>] <mode> [brightness]\n", argv[0]);
        printf("Modes: OFF, Joystick, Scrolling, Breathing, Breathing_Red, Breathing_Green, "
               "Breathing_Blue, Breathing_Blue_Red, Breathing_Green_Blue, "
               "Breathing_Red_Green, Breathing_Red_Green_Blue, Red_Green_Blue, "
               "Blue_Red, Blue, Green_Blue, Green, Red_Green, Red.\n");
        printf("Brightness: LOW, MEDIUM, HIGH (default: HIGH)\n");
        return 1;
    }
    
    if (strcmp(argv[argi], "--device") == 0) {
        if (argc <= argi + 1) {
            fprintf(stderr, "Missing spidev path after --device\n");
            return 1;
        }
        spi_device = argv[argi + 1];
        argi += 2;
    }

    if (argc <= argi)
        goto usage;

    if (argc > argi + 1 && parse_brightness(argv[argi + 1]) < 0) {
        fprintf(stderr, "Invalid brightness: %s\n", argv[argi + 1]);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    spi_fd = spi_init(spi_device);
    if (spi_fd < 0) return 1;
    
    uint8_t *buffer = calloc(LED_BUFFER_SIZE + 1, 1);
    if (!buffer) {
        perror("Failed to allocate buffer");
        close(spi_fd);
        return 1;
    }
    
    /* 查找并执行模式 */
    for (const mode_entry_t *entry = mode_table; entry->name; entry++) {
        if (strcmp(argv[argi], entry->name) == 0) {
            entry->func(buffer);
            break;
        }
    }
    
    ws2812_off(buffer);
    free(buffer);
    close(spi_fd);
    
    puts("leds exit..");
    return 0;
}
