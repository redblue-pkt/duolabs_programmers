all:
	$(MAKE) -C cas PWD=$(shell pwd)/cas
	$(MAKE) -C dynamite PWD=$(shell pwd)/dynamite
	$(MAKE) -C dynamite PWD=$(shell pwd)/ezusb
	$(MAKE) -C dynamite_control
	$(MAKE) -C ihex2fw
	$(MAKE) -C firmware

clean:
	$(MAKE) -C cas clean PWD=$(shell pwd)/cas
	$(MAKE) -C dynamite clean PWD=$(shell pwd)/dynamite
	$(MAKE) -C dynamite clean PWD=$(shell pwd)/ezusb
	$(MAKE) -C dynamite_control clean
	$(MAKE) -C ihex2fw clean

install:
	$(MAKE) -C cas install
	$(MAKE) -C dynamite install
	$(MAKE) -C ezusb install
	$(MAKE) -C dynamite_control install
	$(MAKE) -C firmware install
