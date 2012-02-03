#
# GNUmakefile for dhd/exe
#
# Copyright Open Broadcom Corp.
# $Id: GNUmakefile,v 1.25.4.3.4.1 2011/02/08 01:26:52 Exp $

ifndef	SRCBASE
	SRCBASE = ../..
endif

UNAME = $(shell uname)

#-----------------------------------------------------------------
# Windows build
# 1) windows, don't include Makerules due to all: conflict

ifeq ($(findstring CYGWIN,$(UNAME)),CYGWIN)

include ../../GNUmakefile.inc

else # UNAME

# 2) not windows, need to include first to pick up TARGETENV dependent vars
include $(SRCBASE)/Makerules



#ifdef BCMWPA2
CFLAGS += -DBCMWPA2
#endif
# Always include pktgen commands in the application
CFLAGS += -DSDTEST

#ifdef WLCNT
CFLAGS += -DWLCNT
#endif


CFLAGS += -DWLBTAMP

CFLAGS += -DDONGLEOVERLAYS

#-----------------------------------------------------------------
# Linux build
#
# This should be one of values recognized in src/Makerules

ifneq ($(findstring $(TARGETENV), "linux linuxmips linuxmips_be linux26mips linuxarm linuxarm_le android"),)
 
# $(TARGETARCH) is set based on TARGETENV in src/Makerules.* files
DHD_OBJS := dhdu.o dhdu_linux.o bcmutils.o bcmwifi.o wlu_client_shared.o wlu_pipe_linux.o wlu_pipe.o miniopt.o bcm_app_utils.o
DHD_EXE  := dhd
DHD_OBJS := $(DHD_OBJS:%.o=%.o)
ifneq ($(findstring x86,$(TARGETARCH)),x86)
  DHD_EXE  := $(DHD_EXE)$(TARGETARCH)
  DHD_OBJS := $(DHD_OBJS:%.o=$(TARGETARCH)/%.o)
endif

CFLAGS += -DWLPFN -DLINUX -g -I$(SRCBASE)/wl/exe

vpath %.c $(SRCBASE)/shared
vpath %.c $(SRCBASE)/wl/exe

all: $(DHD_EXE)

clean:
	 rm -fr $(DHD_EXE) $(DHD_OBJS) $(TARGETARCH) 

$(DHD_EXE): $(DHD_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) -c $(CFLAGS) -DRWL_SOCKET -DRWL_WIFI -DRWL_DONGLE -o $@ $^


ifneq ($(findstring x86, $(TARGETARCH)),x86)
$(TARGETARCH)/%.o: %.c
	@mkdir -p $(TARGETARCH)
	$(CC) -c $(CFLAGS) -I ../../include -DRWL_SOCKET -DRWL_WIFI -DRWL_DONGLE -o $@ $^
endif

.PHONY: all clean

endif # TARGETENV linux

endif # UNAME
