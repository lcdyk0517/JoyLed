/*
 * MCU LED Controller via UART
 * Copyright (C) 2026 lcdyk0517@qq.com
 * 
 * 逆向工程反编译重构
 * 适用设备: XiFan XF40H, XiFan XF35H, AISLPC R36T, AISLPC R36TMAX, AISLPC K36S
 * 
 * 用法: mcu_led <mode>
 * 
 * 示例:
 *   mcu_led Red
 *   mcu_led Blue
 *   mcu_led OFF
 *   mcu_led Breath
 * 
 * 编译: make
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

/* 固定配置 */
#define UART_DEVICE     "/dev/ttyS2"
#define UART_BAUDRATE   B9600
#define SEND_COUNT      1

/* 模式映射结构 */
typedef struct {
    const char *name;
    uint8_t cmd;
} mode_entry_t;

/* 模式映射表 */
static const mode_entry_t mode_table[] = {
    /* 常亮颜色 */
    {"Red",         3},
    {"Green",       1},
    {"Blue",        2},
    {"White",       7},
    {"Orange",      5},
    {"Purple",      6},
    {"Cyan",        4},
    
    /* 呼吸效果 */
    {"Breathing_Red",      19},
    {"Breathing_Green",    17},
    {"Breathing_Blue",     18},
    {"Breathing_White",    23},
    {"Breathing_Orange",   21},
    {"Breathing_Purple",   22},
    {"Breathing_Cyan",     20},
    {"Breathing",          24},
    
    /* 动态效果 */
    {"Flow",        8},
    
    /* 关闭 */
    {"OFF",         0},
    
    {NULL, 0}
};

/* ==================== 串口函数 ==================== */

static int uart_open(void) {
    int fd = open(UART_DEVICE, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open uart failed");
        return -1;
    }
    
    struct termios options;
    if (tcgetattr(fd, &options) < 0) {
        close(fd);
        return -1;
    }
    
    cfsetospeed(&options, UART_BAUDRATE);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag |= CREAD;
    tcsetattr(fd, TCSANOW, &options);
    
    return fd;
}

/* ==================== 发送函数 ==================== */

static int mcu_send(int fd, uint8_t cmd) {
    ssize_t ret = write(fd, &cmd, 1);
    if (ret < 0) {
        puts("send cmd error");
        return -1;
    }
    return 0;
}

/* ==================== 主函数 ==================== */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <mode>\n", argv[0]);
        printf("Modes: OFF, Red, Green, Blue, White, Orange, Purple, Cyan,\n");
        printf("       Breathing, Breathing_Red, Breathing_Green, Breathing_Blue,\n");
        printf("       Breathing_White, Breathing_Orange, Breathing_Purple, Breathing_Cyan,\n");
        printf("       Flow.\n");
        return 1;
    }
    
    /* 查找模式 */
    uint8_t cmd = 0;
    int found = 0;
    for (const mode_entry_t *entry = mode_table; entry->name; entry++) {
        if (strcmp(argv[1], entry->name) == 0) {
            cmd = entry->cmd;
            found = 1;
            break;
        }
    }
    
    if (!found) {
        fprintf(stderr, "Invalid mode: %s\n", argv[1]);
        return 1;
    }
    
    /* 打开串口并发送 */
    int fd = uart_open();
    if (fd < 0) {
        return 1;
    }
    
    int ret = mcu_send(fd, cmd);
    close(fd);
    
    return ret < 0 ? 1 : 0;
}