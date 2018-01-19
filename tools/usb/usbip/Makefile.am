# SPDX-License-Identifier: GPL-2.0
SUBDIRS := libsrc src
includedir = @includedir@/usbip
include_HEADERS := $(addprefix libsrc/, \
		     usbip_common.h vhci_driver.h usbip_host_driver.h)

dist_man_MANS := $(addprefix doc/, usbip.8 usbipd.8)
