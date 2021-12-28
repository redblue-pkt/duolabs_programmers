all:
	$(MAKE) -C cas PWD=$(shell pwd)/cas
	$(MAKE) -C dynamite PWD=$(shell pwd)/dynamite
	$(MAKE) -C dynamite PWD=$(shell pwd)/ezusb
	$(MAKE) -C cas_control
	$(MAKE) -C dynamite_control
	$(MAKE) -C ihex2fw
	$(MAKE) -C firmware
	@git submodule sync && git submodule update --init && $(MAKE) -C oscam

clean:
	$(MAKE) -C cas clean PWD=$(shell pwd)/cas
	$(MAKE) -C dynamite clean PWD=$(shell pwd)/dynamite
	$(MAKE) -C dynamite clean PWD=$(shell pwd)/ezusb
	$(MAKE) -C cas_control clean
	$(MAKE) -C dynamite_control clean
	$(MAKE) -C ihex2fw clean
	$(MAKE) -C oscam clean

install:
	$(MAKE) -C cas install
	$(MAKE) -C dynamite install
	$(MAKE) -C ezusb install
	$(MAKE) -C cas_control install
	$(MAKE) -C dynamite_control install
	$(MAKE) -C firmware install
	@$(foreach file, $(wildcard oscam/Distribution/oscam-1.20_*-*-linux-gnu), cp -rf $(file) /usr/bin/oscam;)
