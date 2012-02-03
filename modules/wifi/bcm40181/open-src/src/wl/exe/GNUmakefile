#
# GNUmakefile for wl/exe
#
# Copyright (c) 2000, Broadcom Corp.
# $Id: GNUmakefile,v 1.70.2.3 2010/12/14 05:41:23 Exp $

ifndef	SRCBASE
	SRCBASE = ../..
endif

UNAME = $(shell uname)

ifeq ($(findstring CYGWIN,$(UNAME)),CYGWIN)

#-----------------------------------------------------------------
# Windows build, don't include Makerules due to all: conflict

include ../../GNUmakefile.inc

else # UNAME

#-----------------------------------------------------------------
# Linux build
#

# This should be one of values recognized in src/Makerules
# 2) not windows, need to include first to pick up TARGETENV dependent vars
include $(SRCBASE)/Makerules



#ifdef WLCNT
  CFLAGS += -DWLCNT
#endif

#ifdef WIFI_ACT_FRAME
  CFLAGS += -DWIFI_ACT_FRAME
#endif

#ifdef WLEXTLOG
  CFLAGS += -DWLEXTLOG
#endif

ifeq ($(OLYMPIC_RWL),1)
    CFLAGS += -DOLYMPIC_RWL
endif
ifneq ($(findstring $(TARGETENV), "linux linuxmips linux26mips linuxmips_be linuxarm linuxarm_le android"),)
 

#ifdef WLP2P
  CFLAGS += -DWLP2P
#endif

# extra warnings
CFLAGS += -Wextra
CFLAGS += -DWLPFN -DWLPFN_AUTO_CONNECT -DLINUX 
CFLAGS += -DWLC_HIGH
CFLAGS += -I$(SRCBASE)/wl/sys -I$(SRCBASE)/wl/phy -I ../../include

# Discard any "MMX" or other qualifications on x86 so that
# any TARGETARCH containing x86 is just "x86"
ifeq ($(findstring x86,$(TARGETARCH)),x86)
        TARGETARCH = x86
endif

ASD        ?= 0 
ifneq ($(ASD),1)
  override ASD:=
endif
ifneq ($(ASD),1)
  NOASDDIR    = _noasd
endif

RWL        ?= 1 
ifeq ($(RWL),0)
  override RWL:=
endif

RWL_DONGLE        ?= 1 
RWL_SOCKET        ?= 1 
RWL_WIFI          ?= 1
RWL_SERIAL        ?= 1

ifneq ($(RWL_DONGLE),0)
RWL_SERVERS += $(SERVER_DONGLE)
endif
ifneq ($(RWL_SOCKET),0)
RWL_SERVERS += $(SERVER_SOCKET)
endif
ifneq ($(RWL_WIFI),0)
RWL_SERVERS += $(SERVER_WIFI)
endif
ifneq ($(RWL_SERIAL),0)
RWL_SERVERS += $(SERVER_SERIAL)
endif
# $(TARGETARCH) is set based on TARGETENV in src/Makerules.* files
WL_OBJS      := wlu.o wlu_linux.o bcmutils.o bcmwifi.o wlu_cmd.o wlu_iov.o wlu_client_shared.o wlu_pipe_linux.o wlu_pipe.o miniopt.o bcm_app_utils.o
SERVER_OBJS  := wlu_server_linux.o wlu_pipe_linux.o wlu_pipe.o wlu_server_shared.o shellproc_linux.o
WLM_OBJS     := $(WL_OBJS) wlm.o
WLHIGHSTUB_OBJS := wlhighstub.o bcm_rpc_char.o bcm_rpc.o linux_rpc_osl.o
WLHIGHSTUB_OBJS += wlc_low_stubs.o bcm_xdr.o

# Prefix obj/<type>/TARGETARCH to produced .obj files
WL_OBJS      := $(WL_OBJS:%.o=obj/wl$(NOASDDIR)/$(TARGETARCH)/%.o)
DONGLE_OBJS  := $(SERVER_OBJS:%.o=obj/dongle$(NOASDDIR)/$(TARGETARCH)/%.o)
SOCKET_OBJS  := $(SERVER_OBJS:%.o=obj/socket$(NOASDDIR)/$(TARGETARCH)/%.o)
WIFI_OBJS    := $(SERVER_OBJS:%.o=obj/wifi$(NOASDDIR)/$(TARGETARCH)/%.o)
SERIAL_OBJS  := $(SERVER_OBJS:%.o=obj/serial$(NOASDDIR)/$(TARGETARCH)/%.o)
WLM_OBJS     := $(WLM_OBJS:%.o=obj/wlm/$(TARGETARCH)/%.o)
WLHIGHSTUB_OBJS  := $(WLHIGHSTUB_OBJS:%.o=obj/wlhighstub/$(TARGETARCH)/%.o)

# TODO: Move final built objects to respective TARGETARCH dirs as well
# Final exe names
ARCH_SFX     := $(if $(findstring x86,$(TARGETARCH)),,$(TARGETARCH))
WL_EXE       := wl$(ARCH_SFX)
SERVER_SOCKET = socket$(NOASDDIR)/$(TARGETARCH)/wl_server_socket$(ARCH_SFX)
SERVER_DONGLE = dongle$(NOASDDIR)/$(TARGETARCH)/wl_server_dongle$(ARCH_SFX)
SERVER_WIFI   = wifi$(NOASDDIR)/$(TARGETARCH)/wl_server_wifi$(ARCH_SFX)
SERVER_SERIAL = wifi$(NOASDDIR)/$(TARGETARCH)/wl_server_serial$(ARCH_SFX)
WLM_SO       := wlm/$(TARGETARCH)/wlm$(ARCH_SFX).so
WLHIGHSTUB   := obj/wlhighstub/$(TARGETARCH)/wlhighstub$(ARCH_SFX)

# ASD specific flags
ifeq ($(ASD),1)
  ASD_CFLAGS := -DRWLASD
  ASD_CFLAGS += -I../../tools/Wifi_ASD_test/inc
ifneq ($(filter mips_be arm_android,$(TARGETARCH)),)
  ASD_LDFLAGS:= -L../../tools/Wifi_ASD_test/lib/$(TARGETARCH) -lwfa
else
  ASD_LDFLAGS:= -L../../tools/Wifi_ASD_test/lib/$(TARGETARCH) -lwfa -lpthread
endif
endif # ASD

vpath %.c $(SRCBASE)/shared $(SRCBASE)/wl/sys $(SRCBASE)/wl/phy

all:  build_deps

# Build servers for all but mips_be
ifeq ($(filter mips_be,$(TARGETARCH)),)
all:  $(WL_EXE) servers
else
all: $(WL_EXE)
endif

build_deps:
ifeq ($(ASD),1)
	$(MAKE) -C ../../tools/Wifi_ASD_test
endif # ASD

wlm: $(WLM_SO)

servers: $(RWL_SERVERS)

# Compilation targets
obj/wl$(NOASDDIR)/$(TARGETARCH)/%.o: %.c
	@mkdir -pv $(@D)
ifneq ($(RWL),)
	$(CC) -c $(CFLAGS) $(ASD_CFLAGS) -DRWL_SOCKET -DRWL_DONGLE -DRWL_WIFI -DRWL_SERIAL -o $@ $^
else
	$(CC) -c $(CFLAGS) $(ASD_CFLAGS) -o $@ $^
endif

obj/dongle$(NOASDDIR)/$(TARGETARCH)/%.o: %.c
	@mkdir -pv $(@D)
	$(CC) -c $(CFLAGS) $(ASD_CFLAGS) -DRWL_DONGLE -o $@ $^

obj/socket$(NOASDDIR)/$(TARGETARCH)/%.o: %.c
	@mkdir -pv $(@D)
	$(CC) -c $(CFLAGS) $(ASD_CFLAGS) -DRWL_SOCKET -o $@ $^

obj/wifi$(NOASDDIR)/$(TARGETARCH)/%.o: %.c
	@mkdir -pv $(@D)
	$(CC) -c $(CFLAGS) $(ASD_CFLAGS) -DRWL_WIFI -o $@ $^

obj/serial$(NOASDDIR)/$(TARGETARCH)/%.o: %.c
	@mkdir -pv $(@D)
	$(CC) -c $(CFLAGS) $(ASD_CFLAGS) -DRWL_SERIAL -o $@ $^

obj/wlm/$(TARGETARCH)/%.o: %.c
	@mkdir -pv $(@D)
	$(CC) -c $(CFLAGS) $(ASD_CFLAGS) -DRWL_DONGLE -DRWL_SOCKET -DRWL_WIFI -DRWL_SERIAL -DWLMSO -fPIC -o $@ $^

obj/wlhighstub/$(TARGETARCH)/%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $^

$(WLHIGHSTUB): $(WLHIGHSTUB_OBJS)
	@mkdir -pv $(@D)

# Final link targets
$(WL_EXE): $(WL_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ 

$(SERVER_DONGLE): $(DONGLE_OBJS)
	@mkdir -pv $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(ASD_LDFLAGS) 

$(SERVER_SOCKET): $(SOCKET_OBJS)
	@mkdir -pv $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(ASD_LDFLAGS)

$(SERVER_WIFI): $(WIFI_OBJS)
	@mkdir -pv $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(ASD_LDFLAGS)

$(SERVER_SERIAL): $(SERIAL_OBJS)
	@mkdir -pv $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(ASD_LDFLAGS)

$(WLM_SO): $(WLM_OBJS)
	@mkdir -pv $(@D)
	$(CC) $(LDFLAGS) -shared -o $@ $^


wldefs: wlmain.o
	$(CC) $(LDFLAGS) -o $@ $^

.PHONY: all clean wlm

clean:
	 rm -fv $(WL_EXE) $(WL_OBJS) $(SERVER_SOCKET) $(SERVER_DONGLE) $(SERVER_WIFI) $(SERVER_SERIAL) $(SOCKET_OBJS) $(SERIAL_OBJS) $(WIFI_OBJS) $(WLM_SO) $(WLM_OBJS) $(SERVER_SERIAL)

endif # TARGETENV linux

#-----------------------------------------------------------------
# MacOS build
#

ifeq ($(TARGETENV), macos)

include $(SRCBASE)/Makerules.env

#
# MacOS rules for top level MAKE call
#
ifeq ($(SUB_MAKE), )

PROJTGT = wl
BUILD = xcodebuild
PROJECT = wl.xcodeproj

all: wl

clean:
	rm -rf macos build

wl:
	@if [ ! -d macos ]; then mkdir macos; fi
	$(MAKE) -w -C macos -f ../GNUmakefile SRCBASE=../$(SRCBASE) SUB_MAKE=macos wl

wl_server_socket:
	@if [ ! -d macos ]; then mkdir macos; fi
	$(MAKE) -w -C macos -f ../GNUmakefile SRCBASE=../$(SRCBASE) SUB_MAKE=macos wl_server_socket

wl_server_wifi:
	@if [ ! -d macos ]; then mkdir macos; fi
	$(MAKE) -w -C macos -f ../GNUmakefile SRCBASE=../$(SRCBASE) SUB_MAKE=macos wl_server_wifi

#
# dongle wl is located ./build/Debug/wl
#
wl_dongle:
	$(BUILD) -project $(PROJECT) -target $(PROJTGT) -configuration Debug build

.PHONY: all clean wl wl_server_socket wl_server_wifi

endif # SUB_MAKE ""

#
# MacOS rules recursive MAKE call in ./macos directory
#
ifeq ($(SUB_MAKE),macos)

wl: ppc/wl i386/wl
	lipo -create ppc/wl i386/wl -output $@

wl_server_socket: ppc/wl_server_socket i386/wl_server_socket
	lipo -create ppc/wl_server_socket i386/wl_server_socket -output $@

wl_server_wifi: ppc/wl_server_wifi i386/wl_server_wifi
	lipo -create ppc/wl_server_wifi i386/wl_server_wifi -output $@

ppc/wl:
	@if [ ! -d ppc ]; then mkdir ppc; fi
	$(MAKE) -w -C ppc -f ../../GNUmakefile SRCBASE=../$(SRCBASE) SUB_MAKE=ppc TARGETARCH=PPC wl

i386/wl:
	@if [ ! -d i386 ]; then mkdir i386; fi
	$(MAKE) -w -C i386 -f ../../GNUmakefile SRCBASE=../$(SRCBASE) SUB_MAKE=i386 TARGETARCH=x86 wl

ppc/wl_server_socket:
	@if [ ! -d ppc ]; then mkdir ppc; fi
	$(MAKE) -w -C ppc -f ../../GNUmakefile SRCBASE=../$(SRCBASE) SUB_MAKE=ppc TARGETARCH=PPC wl_server_socket

i386/wl_server_socket:
	@if [ ! -d i386 ]; then mkdir i386; fi
	$(MAKE) -w -C i386 -f ../../GNUmakefile SRCBASE=../$(SRCBASE) SUB_MAKE=i386 TARGETARCH=x86 wl_server_socket

ppc/wl_server_wifi:
	@if [ ! -d ppc ]; then mkdir ppc; fi
	$(MAKE) -w -C ppc -f ../../GNUmakefile SRCBASE=../$(SRCBASE) SUB_MAKE=ppc TARGETARCH=PPC wl_server_wifi

i386/wl_server_wifi:
	@if [ ! -d i386 ]; then mkdir i386; fi
	$(MAKE) -w -C i386 -f ../../GNUmakefile SRCBASE=../$(SRCBASE) SUB_MAKE=i386 TARGETARCH=x86 wl_server_wifi

.PHONY: all wl wl_server_socket wl_server_wifi ppc/wl ppc/wl_server_socket ppc/wl_server_wifi i386/wl i386/wl_server_socket i386/wl_server_wifi

endif # SUB_MAKE macos


#
# MacOS rules for recursive MAKE call in ./macos/ppc or ./macos/i386 directory
#
ifneq ($(findstring $(SUB_MAKE), "ppc i386"), )

WL_OBJS := wlu.o wlu_macos.o bcmutils.o bcmwifi.o wlu_cmd.o wlu_iov.o miniopt.o bcm_app_utils.o
SERVER_OBJS := wlu_server_macos.o  wlu_pipe_linux.o wlu_pipe.o wlu_server_shared.o shellproc_linux.o
SERVER_SOCKET := wl_server_socket
SERVER_WIFI := wl_server_wifi

SOCKET_OBJS := $(SERVER_OBJS:%.o=socket/%.o)
WIFI_OBJS := $(SERVER_OBJS:%.o=wifi/%.o)

vpath %.c $(SRCBASE)/shared
vpath %.c ../..

MACOS_VER ?:= $(shell sw_vers -productVersion)

ifneq (,$(findstring 10.6,$(MACOS_VER)))
	GCDEFS  := $(GCDEFS) -DRDR_5905993 -DWLP2P -DWLMCHAN
endif

wl: $(WL_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(SERVER_SOCKET): $(SOCKET_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

socket/%.o: %.c
	@mkdir -p socket
	$(CC) -c $(CFLAGS) -DRWL_SOCKET -DREMOTE_WL -o $@ $^

$(SERVER_WIFI): $(WIFI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

wifi/%.o: %.c
	@mkdir -p wifi
	$(CC) -c $(CFLAGS) -DRWL_WIFI -DREMOTE_WL -o $@ $^

.PHONY: all

endif # SUB_MAKE pps or i386

endif # TARGETENV macos

endif # UNAME
