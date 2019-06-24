# SPDX-License-Identifier: GPL-2.0-only
# tools/power/acpi/tools/acpidump/Makefile - ACPI tool Makefile
#
# Copyright (c) 2015, Intel Corporation
#   Author: Lv Zheng <lv.zheng@intel.com>
#

include ../../Makefile.config

TOOL = acpidump
EXTRA_INSTALL = install-man
EXTRA_UNINSTALL = uninstall-man

vpath %.c \
	../../../../../drivers/acpi/acpica\
	./\
	../../common\
	../../os_specific/service_layers
CFLAGS += -DACPI_DUMP_APP -I.
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
	uthex.o\
	utmath.o\
	utnonansi.o\
	utprint.o\
	utstring.o\
	utstrsuppt.o\
	utstrtoul64.o\
	utxferror.o\
	oslinuxtbl.o\
	cmfsize.o\
	getopt.o

include ../../Makefile.rules

install-man: $(srctree)/man/acpidump.8
	$(ECHO) "  INST    " acpidump.8
	$(QUIET) $(INSTALL_DATA) -D $< $(DESTDIR)$(mandir)/man8/acpidump.8
uninstall-man:
	$(ECHO) "  UNINST  " acpidump.8
	$(QUIET) rm -f $(DESTDIR)$(mandir)/man8/acpidump.8
