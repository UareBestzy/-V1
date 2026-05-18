CROSS_COMPILE ?= /home/book/100ask_imx6ull-sdk/Buildroot_2020.02.x/output/host/bin/arm-buildroot-linux-gnueabihf-
SYSROOT ?= /home/book/100ask_imx6ull-sdk/Buildroot_2020.02.x/output/host/arm-buildroot-linux-gnueabihf/sysroot

CC := $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -O2
CPPFLAGS += --sysroot=$(SYSROOT)
LDFLAGS += --sysroot=$(SYSROOT)
LDLIBS += -lssl -lcrypto

TARGET := huawei_mqtt_client

.PHONY: all clean

all: $(TARGET)

$(TARGET): huawei_mqtt_client.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET) tencent_mqtt_client
