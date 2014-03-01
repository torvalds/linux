# tools/power/acpi/Makefile - ACPI tool Makefile
#
# Copyright (c) 2013, Intel Corporation
#   Author: Lv Zheng <lv.zheng@intel.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.

OUTPUT=./
ifeq ("$(origin O)", "command line")
	OUTPUT := $(O)/
endif

ifneq ($(OUTPUT),)
# check that the output directory actually exists
OUTDIR := $(shell cd $(OUTPUT) && /bin/pwd)
$(if $(OUTDIR),, $(error output directory "$(OUTPUT)" does not exist))
endif

# --- CONFIGURATION BEGIN ---

# Set the following to `true' to make a unstripped, unoptimized
# binary. Leave this set to `false' for production use.
DEBUG ?=	true

# make the build silent. Set this to something else to make it noisy again.
V ?=		false

# Prefix to the directories we're installing to
DESTDIR ?=

# --- CONFIGURATION END ---

# Directory definitions. These are default and most probably
# do not need to be changed. Please note that DESTDIR is
# added in front of any of them

bindir ?=	/usr/bin
sbindir ?=	/usr/sbin
mandir ?=	/usr/man

# Toolchain: what tools do we use, and what options do they need:

INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL}
INSTALL_DATA  = ${INSTALL} -m 644
INSTALL_SCRIPT = ${INSTALL_PROGRAM}

# If you are running a cross compiler, you may want to set this
# to something more interesting, like "arm-linux-".  If you want
# to compile vs uClibc, that can be done here as well.
CROSS = #/usr/i386-linux-uclibc/usr/bin/i386-uclibc-
CC = $(CROSS)gcc
LD = $(CROSS)gcc
STRIP = $(CROSS)strip
HOSTCC = gcc

# check if compiler option is supported
cc-supports = ${shell if $(CC) ${1} -S -o /dev/null -x c /dev/null > /dev/null 2>&1; then echo "$(1)"; fi;}

# use '-Os' optimization if available, else use -O2
OPTIMIZATION := $(call cc-supports,-Os,-O2)

WARNINGS := -Wall
WARNINGS += $(call cc-supports,-Wstrict-prototypes)
WARNINGS += $(call cc-supports,-Wdeclaration-after-statement)

KERNEL_INCLUDE := ../../../include
CFLAGS += -D_LINUX -DDEFINE_ALTERNATE_TYPES -I$(KERNEL_INCLUDE)
CFLAGS += $(WARNINGS)

ifeq ($(strip $(V)),false)
	QUIET=@
	ECHO=@echo
else
	QUIET=
	ECHO=@\#
endif
export QUIET ECHO

# if DEBUG is enabled, then we do not strip or optimize
ifeq ($(strip $(DEBUG)),true)
	CFLAGS += -O1 -g -DDEBUG
	STRIPCMD = /bin/true -Since_we_are_debugging
else
	CFLAGS += $(OPTIMIZATION) -fomit-frame-pointer
	STRIPCMD = $(STRIP) -s --remove-section=.note --remove-section=.comment
endif

# if DEBUG is enabled, then we do not strip or optimize
ifeq ($(strip $(DEBUG)),true)
	CFLAGS += -O1 -g -DDEBUG
	STRIPCMD = /bin/true -Since_we_are_debugging
else
	CFLAGS += $(OPTIMIZATION) -fomit-frame-pointer
	STRIPCMD = $(STRIP) -s --remove-section=.note --remove-section=.comment
endif

# --- ACPIDUMP BEGIN ---

vpath %.c \
	tools/acpidump

DUMP_OBJS = \
	acpidump.o

DUMP_OBJS := $(addprefix $(OUTPUT)tools/acpidump/,$(DUMP_OBJS))

$(OUTPUT)acpidump: $(DUMP_OBJS)
	$(ECHO) "  LD      " $@
	$(QUIET) $(LD) $(CFLAGS) $(LDFLAGS) $(DUMP_OBJS) -L$(OUTPUT) -o $@
	$(QUIET) $(STRIPCMD) $@

$(OUTPUT)tools/acpidump/%.o: %.c
	$(ECHO) "  CC      " $@
	$(QUIET) $(CC) -c $(CFLAGS) -o $@ $<

# --- ACPIDUMP END ---

all: $(OUTPUT)acpidump
	echo $(OUTPUT)

clean:
	-find $(OUTPUT) \( -not -type d \) -and \( -name '*~' -o -name '*.[oas]' \) -type f -print \
	 | xargs rm -f
	-rm -f $(OUTPUT)acpidump

install-tools:
	$(INSTALL) -d $(DESTDIR)${bindir}
	$(INSTALL_PROGRAM) $(OUTPUT)acpidump $(DESTDIR)${sbindir}

install-man:
	$(INSTALL_DATA) -D man/acpidump.8 $(DESTDIR)${mandir}/man8/acpidump.8

install: all install-tools install-man

uninstall:
	- rm -f $(DESTDIR)${sbindir}/acpidump
	- rm -f $(DESTDIR)${mandir}/man8/acpidump.8

.PHONY: all utils install-tools install-man install uninstall clean
