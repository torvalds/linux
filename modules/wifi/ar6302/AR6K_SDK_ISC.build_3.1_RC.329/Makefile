# 
# Minimal build invocation:
#   
#     make (all | clean | clobber)  
#
# Extended Make invocation:
#
#     make ATH_BUILD_TYPE=<build type> ATH_SDIO_STACK_BASE=<sdio stack install path> (all | clean | clobber)
#
# Notes:
#     1. This makefile must be invoked from the host/ directory
#     2. The <build type> must match an entry in localmake.linux.inc.
#     3. The localmake.linux.inc can be overridden using an include file outside the build tree.
#        This file (name and path) can be set via the ATH_MAKE_INCLUDE_OVERRIDE variable.
#        ** If ATH_MAKE_INCLUDE_OVERRIDE is used, you can define all build variables in that file 
#        instead of using command line arguments **.  This feature is provided for developers
#        that may want to customize the build using a single include file.
#
#        For example :
#             
#            " make ATH_MAKE_INCLUDE_OVERRIDE=$HOME/mymake.inc "
#        
#             could be used, as long as "mymake.inc" defines all the required variables (see below)
#
#  Required Variables:  
#      
#      ATH_OS_SUB_TYPE - on linux, this must be "linux_2_4" for 2.4 kernels or left blank for 2.6 kernels.
#      ATH_LINUXPATH - linux kernel source path
#      ATH_CROSS_COMPILE_TYPE - optional cross compiler path , leave blank for local gcc compilation
#      ATH_ARCH_CPU_TYPE  - CPU architecture type, leave blank for local gcc compilation
#      ATH_SDIO_STACK_BASE - SDIO Stack installation path to compile the SDIO HIF layer against an externally supplied
#                            SDIO stack source.
#
#  Override variables:
#
#    ATH_MAKE_INCLUDE_OVERRIDE - full path to include file which overrides the default (localmake.linux.inc)
#                                this file can contain other overrides specific to a developers
#                                workspace environment.
#    ATH_BUILD_OUTPUT_OVERRIDE - output path override for compiled executable and
#                                database image
#
#
# Include local variables
WORKAREA=$(shell pwd)

ifdef ATH_MAKE_INCLUDE_OVERRIDE
_LOCALMAKE_INCLUDE = $(ATH_MAKE_INCLUDE_OVERRIDE)
else
_LOCALMAKE_INCLUDE = $(WORKAREA)/host/localmake.linux.inc
endif

-include $(_LOCALMAKE_INCLUDE)

CC :=$(ATH_CROSS_COMPILE_TYPE)gcc
LD :=$(ATH_CROSS_COMPILE_TYPE)ld
STRIP :=$(ATH_CROSS_COMPILE_TYPE)strip
ATH_SRC_BASE :=$(WORKAREA)/host

# Set cross compile type (if any)
ifdef ATH_CROSS_COMPILE_TYPE
_CROSS_COMPILE_LINE := ARCH=$(ATH_ARCH_CPU_TYPE) CROSS_COMPILE=$(ATH_CROSS_COMPILE_TYPE)
endif

# Set up object output areas
ifdef ATH_BUILD_OUTPUT_OVERRIDE
_COMPILED_OBJECTS_PATH :=$(ATH_BUILD_OUTPUT_OVERRIDE)
COMPILED_BIN_OBJECTS_PATH :=$(ATH_BUILD_OUTPUT_OVERRIDE)
COMPILED_IMAGE_OBJECTS_PATH :=$(ATH_BUILD_OUTPUT_OVERRIDE)
_MAKE_IMAGE_OUTPUT_DIR :=
_MAKE_BIN_OUTPUT_DIR :=
_CLEAN_IMAGE_OUTPUT_DIR :=
_CLEAN_BIN_OUTPUT_DIR :=
else
_COMPILED_OBJECTS_PATH := $(ATH_SRC_BASE)/.output/$(ATH_BUILD_TYPE)-$(ATH_BUS_TYPE)
COMPILED_BIN_OBJECTS_PATH := $(ATH_SRC_BASE)/.output/bin
COMPILED_IMAGE_OBJECTS_PATH :=$(_COMPILED_OBJECTS_PATH)/$(COMPILED_OBJECTS_PREFIX)image
_MAKE_IMAGE_OUTPUT_DIR := mkdir --parents $(COMPILED_IMAGE_OBJECTS_PATH)
_MAKE_BIN_OUTPUT_DIR := mkdir --parents $(COMPILED_BIN_OBJECTS_PATH)
_CLEAN_IMAGE_OUTPUT_DIR := rm -R -f $(COMPILED_IMAGE_OBJECTS_PATH)
_CLEAN_BIN_OUTPUT_DIR := rm -R -f $(COMPILED_BIN_OBJECTS_PATH)
endif

# Create needed directories for ART packaging
ARTROOTDIR := $(PKGDIR_TLA)/host/tools/systemtools
ARTSCRIPTDIR := $(ARTROOTDIR)/tools/scripts
PKG_PARENT_DIR := $(COMPILED_IMAGE_OBJECTS_PATH)/art_rel
PKG_DIR := $(PKG_PARENT_DIR)/art/bin

# Export variable that are relevant for components beyond host driver
export  CC
export  LD
export  STRIP
export  ATH_BUILD_TYPE
export  ATH_OS_SUB_TYPE
export  ATH_LINUXPATH
export  ATH_CROSS_COMPILE_TYPE
export  ATH_ARCH_CPU_TYPE
export  ATH_BUS_TYPE
export  ATH_BUS_SUBTYPE
export  COMPILED_IMAGE_OBJECTS_PATH
export  COMPILED_BIN_OBJECTS_PATH

all: isc fw tla gpl

common:
ifndef ATH_BUILD_TYPE 
	@echo "Please edit the $(_LOCALMAKE_INCLUDE) file"
	exit 1
endif
	$(_MAKE_IMAGE_OUTPUT_DIR)
	$(_MAKE_BIN_OUTPUT_DIR)

isc: common build-i install-i

build-i:
	$(MAKE) -C $(WORKAREA)/host

install-i:
	$(MAKE) -C $(WORKAREA)/host install

tla: common build-t install-t

build-t:
ifdef PKGDIR_TLA
	$(MAKE) -C $(PKGDIR_TLA)/host
endif

install-t:
ifdef PKGDIR_TLA
	mkdir -p $(PKG_PARENT_DIR)
	mkdir -p $(PKG_PARENT_DIR)/art
	mkdir -p $(PKG_PARENT_DIR)/art/bin
	mkdir -p $(PKG_PARENT_DIR)/config

	cp -f $(PKGDIR_TLA)/host/tools/bdiff/bdiff $(COMPILED_IMAGE_OBJECTS_PATH)
	cp -f $(PKGDIR_TLA)/host/tools/mkdsetimg/mkdsetimg $(COMPILED_IMAGE_OBJECTS_PATH)
	cp -f $(PKGDIR_TLA)/host/tools/regDbGen/regulatory*.bin $(COMPILED_BIN_OBJECTS_PATH)
	cp -f $(PKGDIR_TLA)/host/tools/tcmd/athtestcmd $(COMPILED_IMAGE_OBJECTS_PATH)

	if [ -e $(ARTROOTDIR) ]; \
	then \
		if [ -e $(ARTROOTDIR)/tools/createini ]; \
		then \
			if [ -e $(ARTROOTDIR)/tools/createini/Linux ]; \
			then \
			cp -f $(ARTROOTDIR)/art/Linux/art.out $(COMPILED_IMAGE_OBJECTS_PATH); \
			cp -f $(ARTROOTDIR)/devlib/Linux/libdevlib.so $(COMPILED_IMAGE_OBJECTS_PATH); \
			fi \
		else \
			if [ -e $(ARTROOTDIR)/art/Linux_cust ]; \
			then \
			cp -f $(ARTROOTDIR)/art/Linux_cust/art.out $(COMPILED_IMAGE_OBJECTS_PATH); \
			cp -f $(ARTROOTDIR)/devlib/Linux_cust/libdevlib.so $(COMPILED_IMAGE_OBJECTS_PATH); \
			fi \
		fi \
	fi	

	if [ -e $(ARTROOTDIR) ];   \
	then \
		if [ -e $(ARTROOTDIR)/tools/createini_ar6003 ]; \
		then \
			if [ -e $(ARTROOTDIR)/tools/createini_ar6003/Linux ]; \
			then \
				cp -f $(ARTROOTDIR)/art_ar6003/Linux/art.out $(COMPILED_IMAGE_OBJECTS_PATH); \
				cp -f $(ARTROOTDIR)/devlib_ar6003/Linux/libdevlib.so $(COMPILED_IMAGE_OBJECTS_PATH); \
				cp -f $(ARTROOTDIR)/devlib_ar6003/Linux/libdevlib.so $(PKG_DIR); \
				cp -f $(ARTROOTDIR)/art_ar6003/Linux/art.out $(PKG_DIR); \
				cp -f $(ARTROOTDIR)/bringup/ar5k/test/art/*.* $(PKG_DIR); \
				cp -f $(ARTROOTDIR)/art_ar6003/*.bin $(PKG_DIR); \
				rm -f $(PKG_DIR)/artsetup.txt; \
				mv $(PKG_DIR)/artsetup_ar6003.txt $(PKG_DIR)/artsetup.txt; \
				rm -f $(PKG_DIR)/calsetup.txt; \
				mv $(PKG_DIR)/calsetup_ar6003.txt $(PKG_DIR)/calsetup.txt; \
				cp -f $(ARTROOTDIR)/bringup/ar5k/test/art/calTrgtPwr_ar6* $(PKG_DIR); \
				cp -f $(ARTROOTDIR)/bringup/ar5k/test/art/calTrgtPwr_ar500* $(PKG_DIR); \
				cp -f $(ARTSCRIPTDIR)/loadTgt.local.sh $(PKG_DIR)/loadTgt.sh; \
				cp -f $(ARTSCRIPTDIR)/unloadTgt.local.sh $(PKG_DIR)/unloadTgt.sh; \
				cp -f $(ARTSCRIPTDIR)/mercury_env_bash $(PKG_DIR); \
				cp -f $(ARTSCRIPTDIR)/mercury_env_csh $(PKG_DIR); \
				cp -f $(ARTSCRIPTDIR)/runART.local.sh $(PKG_DIR)/runART.sh; \
				cp -f $(ARTROOTDIR)/doc/ARTSupplementaryNotes.txt $(PKG_DIR); \
				cp -f $(ARTROOTDIR)/art_ar6003/macid.txt $(PKG_DIR); \
				cp -f $(ARTROOTDIR)/art_ar6003/btid.txt $(PKG_DIR); \
				cp -f $(ARTROOTDIR)/lib/libgpibenet.so $(PKG_DIR); \
				cp -f $(ARTROOTDIR)/devlib_ar6003/venus_tx_gain_2.tbl $(PKG_DIR); \
				cp -fp $(ARTROOTDIR)/bringup/ar5k/config/*.eep $(PKG_PARENT_DIR)/config; \
			fi \
		else \
			if [ -e $(ARTROOTDIR)/art_ar6003/Linux_cust ]; \
			then \
			cp -f $(ARTROOTDIR)/art_ar6003/Linux_cust/art.out $(COMPILED_IMAGE_OBJECTS_PATH); \
			cp -f $(ARTROOTDIR)/devlib_ar6003/Linux_cust/libdevlib.so $(COMPILED_IMAGE_OBJECTS_PATH); \
			fi \
		fi \
	fi
endif

gpl: common build-g install-g

build-g:

install-g:

fw: build-f install-f

build-f:

install-f:
	rm -rf $(WORKAREA)/target
	cp -R $(PKGDIR_FW)/target $(WORKAREA)/

clobber: clean
	rm -rf $(WORKAREA)/host/.output

clean:
	$(MAKE) -C $(WORKAREA)/host clean
	$(MAKE) -C $(PKGDIR_TLA)/host clean
