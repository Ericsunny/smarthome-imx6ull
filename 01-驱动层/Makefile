export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
obj-m += led.o beep.o ap3216c.o dht11.o hc_sr505.o hc_sr04.o sg90.o step28byj48.o
KDIR := /home/alientek/alpha/alientek-alpha/kernel-alientek
PWD ?= $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
