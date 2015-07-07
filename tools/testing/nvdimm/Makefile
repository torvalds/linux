KDIR ?= ../../../

default:
	$(MAKE) -C $(KDIR) M=$$PWD

install: default
	$(MAKE) -C $(KDIR) M=$$PWD modules_install
