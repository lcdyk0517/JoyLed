# LED Controllers Makefile
# 
# 交叉编译示例:
#   make CROSS_COMPILE=aarch64-linux-gnu-
#   make CROSS_COMPILE=aarch64-linux-gnu- SYSROOT=/path/to/sysroot
#
# 本地编译:
#   make

# 编译器配置
CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

# sysroot (交叉编译时可选)
SYSROOT ?=
ifneq ($(SYSROOT),)
CFLAGS = --sysroot=$(SYSROOT)
endif

# 编译选项
CFLAGS += -Wall -O2

# 目标文件
TARGETS = ws2812 mcu_led

# 安装路径
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

.PHONY: all clean install uninstall

all: $(TARGETS)

ws2812: ws2812.c
	$(CC) $(CFLAGS) -o $@ $< -lm
	$(STRIP) $@

mcu_led: mcu_led.c
	$(CC) $(CFLAGS) -o $@ $<
	$(STRIP) $@

clean:
	rm -f $(TARGETS)

install: $(TARGETS)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 ws2812 $(DESTDIR)$(BINDIR)/ws2812
	install -m 755 mcu_led $(DESTDIR)$(BINDIR)/mcu_led

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ws2812
	rm -f $(DESTDIR)$(BINDIR)/mcu_led
