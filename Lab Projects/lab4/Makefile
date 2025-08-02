KDIR = /home/morris/linux
CC = aarch64-linux-gnu-gcc
obj-m := mydev.o

all:
	make -C $(KDIR) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$(shell pwd) modules
	$(CC) -o writer writer.c
	$(CC) -o reader reader.c

clean:
	make -C $(KDIR) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$(shell pwd) clean
	rm -rf writer reader
