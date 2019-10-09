# SPDX-License-Identifier: GPL-2.0-only
KDIR ?= ../../../

default:
	$(MAKE) -C $(KDIR) M=$$PWD

install: default
	$(MAKE) -C $(KDIR) M=$$PWD modules_install
