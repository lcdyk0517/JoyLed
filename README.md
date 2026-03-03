# LED Controllers

ARM64 平台 LED 控制器，支持 WS2812 LED 灯带和 MCU 串口控制。

本项目为逆向工程反编译重构，用于在更低 glibc 版本的系统上运行。

## 适用设备

| 程序 | 适用设备 |
|------|----------|
| mcu_led | XiFan XF40H, XiFan XF35H, AISLPC R36T, AISLPC R36TMAX, AISLPC K36S |
| ws2812 | XiFan DC40V, XiFan DC35V, XiFan XF40V, XiFan R36MAX2, XiFan XF28 |

## 编译

```bash
# 交叉编译
make CROSS_COMPILE=aarch64-linux-gnu- SYSROOT=/path/to/sysroot

# 本地编译
make
```

## 安装

```bash
make install
# 或指定安装路径
make install PREFIX=/usr
```

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