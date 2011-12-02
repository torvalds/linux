#
# GNUmakefile for dhd/exe
# (Basically a copy of wl/exe linux portion.)
#
# Copyright (C) 1999-2010, Broadcom Corporation
# 
#      Unless you and Broadcom execute a separate written software license
# agreement governing use of this software, this software is licensed to you
# under the terms of the GNU General Public License version 2 (the "GPL"),
# available at http://www.broadcom.com/licenses/GPLv2.php, with the
# following added to such license:
# 
#      As a special exception, the copyright holders of this software give you
# permission to link this software with independent modules, and to copy and
# distribute the resulting executable under terms of your choice, provided that
# you also meet, for each linked independent module, the terms and conditions of
# the license of that module.  An independent module is a module which is not
# derived from this software.  The special exception does not apply to any
# modifications of the software.
# 
#      Notwithstanding the above, under no circumstances may you combine this
# software in any way with any other Broadcom software provided under a license
# other than the GPL, without Broadcom's express prior written consent.
#
# $Id: GNUmakefile,v 1.9.24.4.2.4 2009/03/04 03:53:29 Exp $

SRCBASE = ../..

UNAME = $(shell uname)

#-----------------------------------------------------------
# Windows build
# 1) windows, don't include Makerules due to all: conflict
ifeq ($(findstring CYGWIN,$(UNAME)),CYGWIN)

SRCFILE = 
include ../../GNUmakefile.inc

else # UNAME

# 2) not windows, need to include first to pick up TARGETENV dependent vars
include $(SRCBASE)/Makerules


# Always include pktgen commands in the application
CFLAGS += -DSDTEST
CFLAGS += -DBCMSPI

#-----------------------------------------------------------
# Linux build
#
ifneq ($(findstring $(TARGETENV), "linux linuxmips linuxmips_be linuxarm linuxarm_le linuxarm_omap android"),)

# $(TARGETARCH) is set based on TARGETENV in src/Makerules.* files
DHD_OBJS := dhdu.o dhdu_linux.o bcmutils.o miniopt.o
DHD_EXE  := dhd

ifneq ($(findstring x86,$(TARGETARCH)),x86)
  DHD_EXE  := $(DHD_EXE)$(TARGETARCH)
  DHD_OBJS := $(DHD_OBJS:%.o=$(TARGETARCH)/%.o)
endif

# extra warnings
CFLAGS += -Wextra $(CUSTOM_FLAGS)

ifeq ($(TARGETENV), linuxarm_omap)
CFLAGS += -DBGBRD
endif

vpath %.c $(SRCBASE)/shared

all: $(DHD_EXE)

$(DHD_EXE): $(DHD_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGETARCH)/%.o: %.c
	@mkdir -p $(TARGETARCH)
	$(CC) -c $(CFLAGS) -o $@ $^

clean:
	@rm -rf $(TARGETARCH)
	rm -f $(DHD_EXE) *.o
	rm -rf build

endif # linux

#-----------------------------------------------------------
# MacOS
#
ifeq ($(TARGETENV), macos)
DHD_EXE := dhd_macos
BUILD   := xcodebuild
PROJECT := dhd.xcodeproj
PROJTGT := dhd

all: $(DHD_EXE)

# Executable is located in build/Debug/dhd
dhd_macos:
	$(BUILD) -project $(PROJECT) -target $(PROJTGT) -configuration Debug build

clean:
	rm -f $(DHD_EXE) *.o
	rm -rf build

endif # macos


.PHONY: all clean

endif # UNAME
