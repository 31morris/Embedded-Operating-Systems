obj-m += lab3_driver.o
KDIR = /home/morris/linux
CC = /usr/bin/aarch64-linux-gnu-gcc

all:
	make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(KDIR) M=$(shell pwd) modules
	$(CC) -o lab3_writer lab3_writer.c

clean:
	make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(KDIR) M=$(shell pwd) clean
	rm -f lab3_writer
