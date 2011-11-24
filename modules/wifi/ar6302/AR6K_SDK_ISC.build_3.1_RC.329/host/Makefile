#------------------------------------------------------------------------------
# Copyright (c) 2010 Atheros Communications Inc.
# All rights reserved.
#
# $ATH_LICENSE_HOSTSDK0_C$
#
# Author(s): ="Atheros"
#------------------------------------------------------------------------------

# Include local variables
ifdef ATH_MAKE_INCLUDE_OVERRIDE
_LOCALMAKE_INCLUDE = $(ATH_MAKE_INCLUDE_OVERRIDE)
else
_LOCALMAKE_INCLUDE = localmake.linux.inc
endif

-include $(_LOCALMAKE_INCLUDE)

export  ATH_SRC_BASE
export  ATH_SDIO_STACK_BASE
export  ATH_HC_DRIVERS
export  ATH_HTC_RAW_INT_ENV
export  ATH_ANDROID_ENV
export  ATH_SOFTMAC_FILE_USED
export  ATH_CFG80211_ENV
export  ATH_DEBUG_DRIVER
export	ATH_BUS_SUBTYPE
export	ATH_CFG80211_ENV

MAKE :=make
ATH_SRC_BASE :=$(shell pwd)
ATH_HIF_TYPE :=`echo $(ATH_BUS_TYPE) | tr [:upper:] [:lower:]`
KMOD_EXTENSION :=ko

# Environment variable for cfg80211 support
ATH_CFG80211_ENV ?=no
ATH_BUILD_PAL_APP ?=yes

# control variables for conditionally compiling some tools
ATH_BUILD_BTFILTER ?=no
ATH_BUILD_3RDPARTY ?=no

export ATH_BUIL_PAL_APP

#all: modules tools tests lib 3rdparty
all: modules

modules:
	echo $(ATH_BUS_TYPE)
	echo $(ATH_HIF_TYPE)
	$(MAKE) -C hif/$(ATH_HIF_TYPE)
	-rm -f $(ATH_SRC_BASE)/os/linux/Module.symvers
	-cp -f $(COMPILED_IMAGE_OBJECTS_PATH)/Module.symvers $(ATH_SRC_BASE)/os/linux
	$(MAKE)  $(_CROSS_COMPILE_LINE) -C $(ATH_LINUXPATH) ATH_HIF_TYPE=$(ATH_HIF_TYPE) SUBDIRS=$(ATH_SRC_BASE)/os/linux modules

tools: FORCE
	$(MAKE) -C tools/bmiloader/
	$(MAKE) -C tools/eeprom/
	$(MAKE) -C tools/pal_host_intf/
	$(MAKE) -C tools/palEventRecv/
	$(MAKE) -C tools/wmiconfig/
	$(MAKE) -C tools/recEvent/
ifeq ($(ATH_BUILD_BTFILTER),yes) 	
	$(MAKE) -C btfilter/ -f Makefile
	$(MAKE) -C tools/athbtfilter/bluez/
endif
	$(MAKE) -C tools/drvdebugctrl
	$(MAKE) -C tools/dbgParser

lib: FORCE
	$(MAKE) -C lib/wac/

tests: FORCE
	$(MAKE) -C tests/mboxping/

3rdparty: FORCE
ifeq ($(ATH_BUILD_3RDPARTY),yes)
	$(MAKE) -C 3rdparty/supplicant/opensrc_0_6_10/wpa_supplicant/
	$(MAKE) -C 3rdparty/hostapd/hostapd-0.6.8/hostapd
	$(MAKE) -C 3rdparty/supplicant/p2p/wpa_supplicant/
endif

install: FORCE
	$(STRIP) -g -S -d $(ATH_SRC_BASE)/os/linux/ar6000.$(KMOD_EXTENSION)
	cp -f $(ATH_SRC_BASE)/os/linux/ar6000.$(KMOD_EXTENSION) $(COMPILED_IMAGE_OBJECTS_PATH)/ar6302.$(KMOD_EXTENSION)
	cp -f $(ATH_SRC_BASE)/../../ath6k $(COMPILED_IMAGE_OBJECTS_PATH) -rf
#	if test -e $(ATH_SRC_BASE)/os/linux/bt_hci_sdio.$(KMOD_EXTENSION); \
#	then	\
#		cp -f $(ATH_SRC_BASE)/os/linux/bt_hci_sdio.$(KMOD_EXTENSION) $(COMPILED_IMAGE_OBJECTS_PATH);	\
#	fi
#	cp -f tools/bmiloader/bmiloader $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f tools/eeprom/eeprom $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f tools/wmiconfig/wmiconfig $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f tools/recEvent/recEvent $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f tools/pal_host_intf/pal_app $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f tools/palEventRecv/palLogEvents $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f tools/pal_host_intf/libpal_intf.a $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f tools/recEvent/restore.sh $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f tools/dbgParser/dbgParser $(COMPILED_IMAGE_OBJECTS_PATH)
#
#ifeq ($(ATH_BUILD_3RDPARTY),yes)
#	cp -f 3rdparty/supplicant/opensrc_0_6_10/wpa_supplicant/wpa_supplicant  $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f 3rdparty/supplicant/opensrc_0_6_10/wpa_supplicant/wpa_cli  $(COMPILED_IMAGE_OBJECTS_PATH)
#	$(STRIP) 3rdparty/hostapd/hostapd-0.6.8/hostapd/hostapd
#	$(STRIP) 3rdparty/hostapd/hostapd-0.6.8/hostapd/hostapd_cli
#	$(STRIP) 3rdparty/hostapd/hostapd-0.6.8/hostapd/hostapd_wps
#	cp -f 3rdparty/hostapd/hostapd-0.6.8/hostapd/hostapd $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f 3rdparty/hostapd/hostapd-0.6.8/hostapd/hostapd_cli $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f 3rdparty/hostapd/hostapd-0.6.8/hostapd/hostapd_wps $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f 3rdparty/hostapd/hostapd-0.6.8/hostapd/config/ar6k* $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f  3rdparty/supplicant/p2p/wpa_supplicant/p2p $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f 3rdparty/supplicant/p2p/wpa_supplicant/p2p_cli $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f 3rdparty/supplicant/p2p/wpa_supplicant/p2p.conf $(COMPILED_IMAGE_OBJECTS_PATH)
#endif
#	if [ -e btfilter/libbtfilt.a ]; \
#		then \
#		cp -f btfilter/libbtfilt.a $(COMPILED_IMAGE_OBJECTS_PATH); \
#	fi
#	if [ -e tools/athbtfilter/bluez/abtfilt ]; \
#		then \
#		cp -f tools/athbtfilter/bluez/abtfilt $(COMPILED_IMAGE_OBJECTS_PATH); \
#	fi
#	cp -f tools/drvdebugctrl/drvdebugctrl $(COMPILED_IMAGE_OBJECTS_PATH);
#
clean:
	rm -f os/linux/ar6000.mod.c os/linux/*.o os/linux/*.ko \
	wmi/*.o htc/AR6000/src/*.o htc/*.o \
	bmi/src/*.o wlan/src/*.o driver/* apps/wpa_supplicant/*.o \
	tools/bmiloader/bmiloader tests/mboxping/mboxping \
	tools/wmiconfig/wmiconfig tools/eeprom/eeprom \
	tools/recEvent/recEvent tools/dbgParser/dbgParser \
	btfilter/*.o btfilter/*.a 3rdparty/hostapd/hostapd-0.5.10/*.d \
	tools/athbtfilter/bluez/abtfilt	tools/drvdebugctrl/drvdebugctrl \
	tools/palEventRecv/palLogEvents tools/pal_host_intf/libpal_intf.a \
	lib/wac/*.o lib/wac/*.so lib/wac/*.a

	rm -f $(ATH_SRC_BASE)/os/linux/Module.symvers
	rm -f $(COMPILED_IMAGE_OBJECTS_PATH)/*.ko

# The kernel module build process leaves some intermediate files, this will clean up all these files
	find $(ATH_SRC_BASE) \( -not -path "*.output*" -a -name '*.[oas]' -o -name core -o -name '.*.flags' -o -name '.ko' -o -name '.*.cmd' -o -name "modules.order" -o -name "*.o.d" \) -type f -print \
		| grep -v lxdialog/ | xargs rm -f
	find $(ATH_SRC_BASE) -name ".tmp_versions" -type d -print | xargs rm -rf
	$(MAKE) -C hif/$(ATH_HIF_TYPE) clean
ifeq ($(ATH_BUILD_3RDPARTY),yes)
	$(MAKE) -C 3rdparty/hostapd/hostapd-0.6.8/hostapd/ clean
	$(MAKE) -C 3rdparty/supplicant/opensrc_0_6_10/wpa_supplicant/ clean
	$(MAKE) -C 3rdparty/supplicant/p2p/wpa_supplicant/ clean
endif

clobber:clean
	rm -rf .output

FORCE:
