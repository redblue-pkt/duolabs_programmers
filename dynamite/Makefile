CFLAGS_dynamite.o := -Wno-unused-variable
obj-m := dynamite.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	@cp -Rf dynamite/dynamite.ko /lib/modules/$(shell uname -r)/kernel/drivers/usb/misc
	depmod -a
