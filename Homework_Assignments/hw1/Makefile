obj-m += led_driver.o
obj-m += seven_seg_driver.o

KDIR = /home/morris/linux
CC = /usr/bin/aarch64-linux-gnu-gcc
# CC = gcc

all:
	make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(KDIR) M=$(shell pwd) modules
	$(CC) -o hw1 hw1.c

clean:
	make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(KDIR) M=$(shell pwd) clean
	rm -f hw1