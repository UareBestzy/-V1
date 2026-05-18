ifneq ($(KERNELRELEASE),)

obj-m += sg90driver.o
obj-m += motordriver.o
obj-m += motordriver_TB6612.o
obj-m += fandriver.o
obj-m += sr501driver.o
obj-m += dht11driver.o
obj-m += rd03driver.o

else

CROSS_COMPILE ?= arm-buildroot-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc
APP_CFLAGS ?= -Wall -Wextra -O2
KERN_DIR ?= /home/book/100ask_imx6ull-sdk/Linux-4.9.88

TARGETS := sg90_test motor_test motor_test_TB6612 fan_test sr501_test dht11_test rd03_test rd03_monitor

.PHONY: all modules tests clean

all: modules tests

modules:
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) modules

tests: $(TARGETS)

sg90_test: sg90_test.c
	$(CC) $(APP_CFLAGS) -o $@ $<

motor_test: motor_test.c
	$(CC) $(APP_CFLAGS) -o $@ $<

motor_test_TB6612: motor_test.c
	$(CC) $(APP_CFLAGS) -DDEFAULT_MOTOR_DEVICE='"/dev/motor_tb6612"' -o $@ $<

fan_test: fan_test.c
	$(CC) $(APP_CFLAGS) -o $@ $<

sr501_test: sr501_test.c
	$(CC) $(APP_CFLAGS) -o $@ $<

dht11_test: dht11_test.c
	$(CC) $(APP_CFLAGS) -o $@ $<

rd03_test: rd03_test.c
	$(CC) $(APP_CFLAGS) -o $@ $<

rd03_monitor: rd03_monitor.c
	$(CC) $(APP_CFLAGS) -o $@ $<

clean:
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f $(TARGETS)

endif
