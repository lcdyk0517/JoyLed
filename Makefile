# LED Controllers Makefile
# 
# 编译方式:
#   make                                    # 本地编译
#   make CC=/path/to/aarch64-linux-gnu-gcc SYSROOT=/path/to/sysroot  # 交叉编译
#
# 示例 (使用 Linaro 工具链):
#   make CC=/opt/toolchains/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-gcc \
#        SYSROOT=/home/lcdyk/cross-build/sysroot

# 编译器配置
CC ?= gcc
STRIP = $(dir $(CC))$(notdir $(patsubst %gcc,%strip,$(CC)))

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