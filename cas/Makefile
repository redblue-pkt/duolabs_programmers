CFLAGS_cas.o := -Wno-unused-variable -Wno-unused-function -Wno-uninitialized -Wno-maybe-uninitialized
obj-m := cas.o ../ezusb/

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	@$(foreach file, $(wildcard cas.ko), cp -rf $(file) /lib/modules/$(shell uname -r)/kernel/drivers/usb/misc; depmod -a;)
