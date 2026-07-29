obj-m += driver.o
# Link bmp280.c object file with driver.c
# driver-objs := driver.o bmp280.o  ← use this if separate bmp280.c

KDIR = /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(shell pwd) modules

clean:
	make -C $(KDIR) M=$(shell pwd) clean
