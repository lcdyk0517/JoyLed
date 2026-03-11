# LED Controllers

ARM64 平台 LED 控制器，支持 WS2812 LED 灯带、MCU 串口控制和 R36Ultra 单线脉冲协议。

本项目为逆向工程反编译重构，用于在更低 glibc 版本的系统上运行。

## 适用设备

| 程序 | 适用设备 |
|------|----------|
| mcu_led | XiFan XF40H, XiFan XF35H, AISLPC R36T, AISLPC R36TMAX, AISLPC K36S |
| ws2812 | XiFan DC40V, XiFan DC35V, XiFan XF40V, XiFan R36MAX2, XiFan XF28 |
| leds-r36ultra | R36Ultra (内核驱动，单线脉冲协议) |

## 编译

```bash
# 交叉编译用户态程序
make CROSS_COMPILE=aarch64-linux-gnu- SYSROOT=/path/to/sysroot

# 本地编译用户态程序
make

# 编译内核模块
make -C /path/to/kernel/src M=$(pwd) modules
```

## 安装

```bash
# 用户态程序
make install
# 或指定安装路径
make install PREFIX=/usr

# 内核模块
insmod leds-r36ultra.ko
```

---

## leds-r36ultra - R36Ultra RGB LED 内核驱动

通过单线脉冲协议控制 RGB LED 的内核驱动，逆向自 R36Ultra 游戏机 5.10 内核。

### 硬件连接

| GPIO | 说明 |
|------|------|
| pulse-gpios | 脉冲输出，用于发送控制信号 |
| irq-gpios | 中断输入，用于接收 LED 控制器信号 (可选) |

### DTS 配置

```dts
joyleds {
    compatible = "r36ultra,led";
    pulse-gpios = <&gpio0 RK_PB3 GPIO_ACTIVE_HIGH>;
    irq-gpios = <&gpio0 RK_PB4 GPIO_ACTIVE_HIGH>;
    status = "okay";
};
```

### sysfs 接口

```bash
# 通过亮度值控制 (0-255 映射到模式)
echo 128 > /sys/class/leds/joyled/brightness

# 直接发送脉冲数
echo 1 > /sys/class/leds/joyled/pulse   # 红色
echo 9 > /sys/class/leds/joyled/pulse   # 彩虹

# GPIO 测试 (验证硬件连接)
echo 1 > /sys/class/leds/joyled/test    # 拉高
echo 0 > /sys/class/leds/joyled/test    # 拉低
```

### LED 模式

| 脉冲数 | 颜色/效果 |
|--------|----------|
| 1 | 红色 |
| 2 | 黄色 |
| 3 | 绿色 |
| 4 | 青色 |
| 5 | 蓝色 |
| 6 | 紫色 |
| 7 | 白色 |
| 8 | 心跳效果 |
| 9 | 彩虹循环 |
| 0/10+ | 关闭 |

### 亮度映射

| 亮度值 | 模式 |
|--------|------|
| 0 | 关闭 |
| 1-28 | 红色 |
| 29-56 | 黄色 |
| 57-84 | 绿色 |
| 85-112 | 青色 |
| 113-140 | 蓝色 |
| 141-168 | 紫色 |
| 169-196 | 白色 |
| 197-224 | 心跳 |
| 225-255 | 彩虹 |

---

## 逆向工程背景

### R36Ultra LED 驱动逆向

通过 Ghidra 反编译 R36Ultra 的 KERNEL (ARM64 Linux 5.10) 镜像，定位到 `udt,ledmodules` 驱动代码区域 (0x7a7000 - 0x7aa000)，分析出单线脉冲协议：

**协议时序：**
```
        ___     _   _   _   _       _
GPIO ___|   |___| |_| |_| |_| |_____| |___
       |<-7T->|<T>|<--- 2T x N --->|<-G->|

T = 1000us (脉冲延时)
G = 5000us (结束延时)
N = 脉冲数量 (决定模式)
```

**关键发现：**
1. 起始信号：拉高 7 个延时周期
2. 数据脉冲：每个脉冲 = 高→延时→低→延时
3. 延时参数：通过 `__const_udelay()` 参数计算得出
4. 初始化序列：先复位 irq GPIO，再发送 11 脉冲

---

## ws2812 - WS2812 LED 控制器

通过 SPI 接口控制 WS2812 LED 灯带。

### 用法

```bash
ws2812 <mode> [brightness]
```

### 参数

| 参数 | 说明 |
|------|------|
| mode | LED 模式 |
| brightness | 亮度级别: `LOW`, `MEDIUM`, `HIGH` (默认 HIGH) |

### 模式列表

**常亮颜色：**
- `Red` - 红色
- `Green` - 绿色
- `Blue` - 蓝色

**双色交替：**
- `Red_Green` - 红绿交替
- `Green_Blue` - 绿蓝交替
- `Blue_Red` - 蓝红交替

**三色循环：**
- `Red_Green_Blue` - 红绿蓝循环

**呼吸效果：**
- `Breathing` - 多色循环呼吸
- `Breathing_Red` - 红色呼吸
- `Breathing_Green` - 绿色呼吸
- `Breathing_Blue` - 蓝色呼吸
- `Breathing_Blue_Red` - 蓝红渐变呼吸
- `Breathing_Green_Blue` - 绿蓝渐变呼吸
- `Breathing_Red_Green` - 红绿渐变呼吸
- `Breathing_Red_Green_Blue` - RGB 渐变呼吸

**动态效果：**
- `Scrolling` - 滚动彩虹

**关闭：**
- `OFF` - 关闭所有 LED

### 示例

```bash
ws2812 Red                  # 红色常亮，高亮度
ws2812 Blue LOW             # 蓝色常亮，低亮度
ws2812 Breathing MEDIUM     # 多色呼吸，中亮度
ws2812 OFF                  # 关闭
```

---

## mcu_led - MCU LED 控制器

通过串口与 MCU 通信控制 LED。

### 用法

```bash
mcu_led <mode>
```

### 模式列表

**常亮颜色：**
| 模式 | 说明 |
|------|------|
| `Red` | 红色 |
| `Green` | 绿色 |
| `Blue` | 蓝色 |
| `White` | 白色 |
| `Orange` | 橙色 |
| `Purple` | 紫色 |
| `Cyan` | 青色 |

**呼吸效果：**
| 模式 | 说明 |
|------|------|
| `Breathing` | 多色循环呼吸 |
| `Breathing_Red` | 红色呼吸 |
| `Breathing_Green` | 绿色呼吸 |
| `Breathing_Blue` | 蓝色呼吸 |
| `Breathing_White` | 白色呼吸 |
| `Breathing_Orange` | 橙色呼吸 |
| `Breathing_Purple` | 紫色呼吸 |
| `Breathing_Cyan` | 青色呼吸 |

**动态效果：**
| 模式 | 说明 |
|------|------|
| `Flow` | 流动效果 |

**关闭：**
| 模式 | 说明 |
|------|------|
| `OFF` | 关闭所有 LED |

### 示例

```bash
mcu_led Red           # 红色常亮
mcu_led Breathing     # 多色呼吸
mcu_led Flow          # 流动效果
mcu_led OFF           # 关闭
```

---

## 技术参数

### leds-r36ultra

- **接口**: GPIO 单线脉冲协议
- **脉冲延时**: 1000us
- **结束延时**: 5000us
- **兼容字符串**: `r36ultra,led`

### ws2812

- **接口**: SPI (`/dev/spidev1.0`)
- **速度**: 8MHz
- **LED 数量**: 8
- **颜色顺序**: GRB

### mcu_led

- **接口**: UART (`/dev/ttyS2`)
- **波特率**: 9600
- **数据位**: 8
- **校验**: 无
- **停止位**: 1

---

## License

MIT
