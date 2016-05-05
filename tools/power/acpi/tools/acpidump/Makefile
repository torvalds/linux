# tools/power/acpi/tools/acpidump/Makefile - ACPI tool Makefile
#
# Copyright (c) 2015, Intel Corporation
#   Author: Lv Zheng <lv.zheng@intel.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.

include ../../Makefile.config

TOOL = acpidump
EXTRA_INSTALL = install-man
EXTRA_UNINSTALL = uninstall-man

vpath %.c \
	../../../../../drivers/acpi/acpica\
	./\
	../../common\
	../../os_specific/service_layers
CFLAGS += -DACPI_DUMP_APP -I.\
	-I../../../../../drivers/acpi/acpica\
	-I../../../../../include
TOOL_OBJS = \
	apdump.o\
	apfiles.o\
	apmain.o\
	osunixdir.o\
	osunixmap.o\
	osunixxf.o\
	tbprint.o\
	tbxfroot.o\
	utascii.o\
	utbuffer.o\
	utdebug.o\
	utexcep.o\
	utglobal.o\
	utmath.o\
	utnonansi.o\
	utprint.o\
	utstring.o\
	utxferror.o\
	oslibcfs.o\
	oslinuxtbl.o\
	cmfsize.o\
	getopt.o

include ../../Makefile.rules

install-man: ../../man/acpidump.8
	$(INSTALL_DATA) -D $< $(DESTDIR)${mandir}/man8/acpidump.8
uninstall-man:
	- rm -f $(DESTDIR)${mandir}/man8/acpidump.8
