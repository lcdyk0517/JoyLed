/*
 * WS2812 LED Controller
 * Copyright (C) 2026 lcdyk0517@qq.com
 * 
 * 逆向工程反编译重构
 * 适用设备: XiFan DC40V, XiFan DC35V, XiFan XF40V, XiFan R36MAX2, XiFan XF28
 * 
 * 用法: ws2812 <mode> [brightness]
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
#define SPI_DEVICE          "/dev/spidev1.0"
#define JOYSTICK_DEVICE     "/dev/input/event2"

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
            if (ev.code == ABS_X) {
                axis_x = ev.value;
            } else if (ev.code == ABS_Y) {
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
static void effect_joystick(uint8_t *buffer) {
    int js_fd = joystick_open(JOYSTICK_DEVICE);
    if (js_fd < 0) return;
    
    int x, y, center_x = 0, center_y = 0;
    static const color_t colors[8] = {
        {COLOR_RED}, {COLOR_YELLOW}, {COLOR_GREEN}, {COLOR_CYAN},
        {COLOR_BLUE}, {COLOR_PURPLE}, {255, 127, 0}, {COLOR_WHITE}
    };
    int current_led = -1;
    int trail[3] = {-1, -1, -1};
    int breath = 0, breath_dir = 1;
    uint8_t max_brightness = brightness_table[brightness_level];
    uint8_t min_brightness = max_brightness / 3;
    
    for (int i = 0; i < 50; i++) {
        joystick_read(js_fd, &x, &y);
        center_x += x;
        center_y += y;
        usleep(1000);
    }
    center_x /= 50;
    center_y /= 50;
    
    while (!quit) {
        joystick_read(js_fd, &x, &y);
        
        int dx = x - center_x;
        int dy = y - center_y;
        int distance = dx * dx + dy * dy;
        int deadzone = 300;
        
        if (distance > deadzone * deadzone) {
            double angle = atan2(-dx, dy);
            int led = (int)(angle * 4 / 3.14159 + 4.5) % 8;
            
            if (led != current_led) {
                trail[2] = trail[1];
                trail[1] = trail[0];
                trail[0] = current_led;
                current_led = led;
            }
        } else {
            current_led = -1;
            trail[0] = trail[1] = trail[2] = -1;
        }
        
        ws2812_set_all(buffer, 0, 0, 0);
        
        if (current_led >= 0) {
            breath += breath_dir * 8;
            if (breath >= max_brightness) { breath = max_brightness; breath_dir = -1; }
            if (breath <= min_brightness) { breath = min_brightness; breath_dir = 1; }
            
            uint8_t r = (uint16_t)colors[current_led].r * breath / 255;
            uint8_t g = (uint16_t)colors[current_led].g * breath / 255;
            uint8_t b = (uint16_t)colors[current_led].b * breath / 255;
            ws2812_set_color(buffer, current_led, r, g, b);
            
            for (int i = 0; i < 3; i++) {
                if (trail[i] >= 0 && trail[i] != current_led) {
                    uint8_t dim = (max_brightness - i * 70) * breath / max_brightness;
                    ws2812_set_color(buffer, trail[i], dim, dim, dim);
                }
            }
        } else {
            breath = min_brightness;
            breath_dir = 1;
        }
        
        refresh(buffer);
        usleep(REFRESH_FAST);
    }
    
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
    if (argc < 2) {
        printf("Usage: %s <mode> [brightness]\n", argv[0]);
        printf("Modes: OFF, Joystick, Scrolling, Breathing, Breathing_Red, Breathing_Green, "
               "Breathing_Blue, Breathing_Blue_Red, Breathing_Green_Blue, "
               "Breathing_Red_Green, Breathing_Red_Green_Blue, Red_Green_Blue, "
               "Blue_Red, Blue, Green_Blue, Green, Red_Green, Red.\n");
        printf("Brightness: LOW, MEDIUM, HIGH (default: HIGH)\n");
        return 1;
    }
    
    if (argc >= 3 && parse_brightness(argv[2]) < 0) {
        fprintf(stderr, "Invalid brightness: %s\n", argv[2]);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    spi_fd = spi_init(SPI_DEVICE);
    if (spi_fd < 0) return 1;
    
    uint8_t *buffer = calloc(LED_BUFFER_SIZE + 1, 1);
    if (!buffer) {
        perror("Failed to allocate buffer");
        close(spi_fd);
        return 1;
    }
    
    /* 查找并执行模式 */
    for (const mode_entry_t *entry = mode_table; entry->name; entry++) {
        if (strcmp(argv[1], entry->name) == 0) {
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
