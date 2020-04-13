all:
	$(MAKE) -C dynamite PWD=$(shell pwd)/dynamite
	$(MAKE) -C dynamite PWD=$(shell pwd)/ezusb
	$(MAKE) -C dynamite_control
	$(MAKE) -C ihex2fw

clean:
	$(MAKE) -C dynamite clean PWD=$(shell pwd)/dynamite
	$(MAKE) -C dynamite clean PWD=$(shell pwd)/ezusb
	$(MAKE) -C dynamite_control clean
	$(MAKE) -C ihex2fw clean

install:
	$(MAKE) -C dynamite install
	$(MAKE) -C ezusb install
	$(MAKE) -C dynamite_control install
	$(MAKE) -C ihex2fw install
