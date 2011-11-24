# EMSDIO master make file
# Copyright 2004-2006 Atheros Communications, Inc.
#
# Minimal build invocation:
#
#   make ALL_BUILD=1 CT_BUILD_TYPE=<build type> CT_OS_TYPE=<Operating System> default | clean
#
# Notes:
#     1. This makefile must be invoked from the host/ directory
#     2. The <build type> must match an entry in localmake.$(CT_OS_TYPE).inc.
#     3. The localmake.$(CT_OS_TYPE).inc can be overridden using an include file outside the build tree.
#        This file (name and path) can be set via the CT_MAKE_INCLUDE_OVERRIDE variable.
#        ** If CT_MAKE_INCLUDE_OVERRIDE is used, you can define all build variables in that file 
#        instead of using command line arguments **.  This feature is provided for developers
#        that may want to customize the build using a single include file.
#
#        For example :
#             
#            " make CT_MAKE_INCLUDE_OVERRIDE=$HOME/mymake.inc "
#        
#             could be used, as long as "mymake.inc" defines all the required variables (see below)
  
#  Required Variables:  
#      
#      CT_BUILD_TYPE  - build type defined in localmake.linux.inc, this variable is also used 
#                       to build a path to hold compiled binaries.
#      CT_OS_TYPE - operating system type (linux, etc...)
#	   CT_OS_SUB_TYPE - on linux, this must be "linux_2_4" for 2.4 kernels or left blank for 2.6 kernels.
#      CT_LINUXPATH - linux kernel source path
#      CT_CROSS_COMPILE_TYPE - optional cross compiler path 
#      CT_ARCH_CPU_TYPE  - CPU architecture type
#
#
# Override variables:
#
#    CT_MAKE_INCLUDE_OVERRIDE - full path to include file which overrides the default
#                               this file can contain other overrides specific to a developer's
#                               workspace environment.
#    CT_BUILD_OUTPUT_OVERRIDE - output path override for compiled binaries.
#

#include local variables

ifdef CT_MAKE_INCLUDE_OVERRIDE
-include $(CT_MAKE_INCLUDE_OVERRIDE)
else
-include localmake.$(CT_OS_TYPE).inc
-include localmake.$(CT_OS_TYPE).private.inc
endif

export CT_OS_TYPE
export CT_OS_SUB_TYPE
export CT_OS_TOP_LEVEL_RULE 
export CT_PASS_CFLAGS
export CT_SRC_BASE
export CT_BUILD_SUB_PROJ

# this makefile can only be invoked from the /EMSDIO/src base
CT_SRC_BASE :=$(shell pwd)
 
# export flags for which HCDs to build. Set the hcd driver name in hcd/ in your localmake.*.inc file.
export CT_HC_DRIVERS

# For Linux
ifeq ($(CT_OS_TYPE),linux) 

#make a copy for linux 2.4
EXTRA_CFLAGS += -DLINUX -I$(CT_SRC_BASE)/include 
ifneq ($(CT_RELEASE),1)
EXTRA_CFLAGS += -DDEBUG
endif  
export EXTRA_CFLAGS 

CT_SRC_OUTPUT :=$(CT_SRC_BASE)/../output

ifdef CT_BUILD_OUTPUT_OVERRIDE
_CT_COMPILED_OBJECTS_PATH :=$(CT_BUILD_OUTPUT_OVERRIDE)
_MAKE_OUTPUT_DIR :=
_CLEAN_OUTPUT_DIR :=
else
_CT_COMPILED_OBJECTS_PATH := $(CT_SRC_OUTPUT)/$(CT_BUILD_TYPE)
_MAKE_OUTPUT_DIR := mkdir --parents $(_CT_COMPILED_OBJECTS_PATH)
_CLEAN_OUTPUT_DIR := rm -R -f $(CT_SRC_OUTPUT)
endif

ifeq ($(CT_OS_SUB_TYPE),linux_2_4)

CT_PASS_CFLAGS := $(EXTRA_CFLAGS)
_CT_MOD_EXTENSION :=o
subdir-m += busdriver/ lib/ hcd/ 

# add in rules to make modules
CT_OS_TOP_LEVEL_RULE :=$(CT_LINUXPATH)/Rules.make
include $(CT_OS_TOP_LEVEL_RULE)
else
#2.6+
_CT_MOD_EXTENSION :=ko
obj-m += busdriver/ lib/ hcd/
endif

ifdef CT_BUILD_SUB_PROJ
_CT_SUBDIRS=$(CT_BUILD_SUB_PROJ)
else
_CT_SUBDIRS=$(CT_SRC_BASE)
endif

ifdef CT_CROSS_COMPILE_TYPE 
CT_MAKE_COMMAND_LINE=$(CT_OUTPUT_FLAGS) -C $(CT_LINUXPATH) SUBDIRS=$(_CT_SUBDIRS) ARCH=$(CT_ARCH_CPU_TYPE) CROSS_COMPILE=$(CT_CROSS_COMPILE_TYPE)
else   
CT_MAKE_COMMAND_LINE=$(CT_OUTPUT_FLAGS) -C $(CT_LINUXPATH) SUBDIRS=$(_CT_SUBDIRS)
endif

makeoutputdirs:
	$(_MAKE_OUTPUT_DIR)
    
default: makeoutputdirs
	$(MAKE) $(CT_MAKE_COMMAND_LINE) modules
	$(CT_SRC_BASE)/../scripts/getobjects.scr $(CT_SRC_BASE) $(_CT_COMPILED_OBJECTS_PATH) $(_CT_MOD_EXTENSION)
       
ifeq ($(CT_OS_SUB_TYPE),linux_2_4)
# on 2.4 we can't invoke the linux clean with SUBDIRS, it will just clean out the kernel
clean:
	find $(_CT_SUBDIRS) \( -name '*.[oas]' -o -name core -o -name '.*.flags' -o -name '.ko' -o -name '.*.cmd' \) -type f -print \
		| grep -v lxdialog/ | xargs rm -f
	$(_CLEAN_OUTPUT_DIR)
else 
clean:
	$(MAKE) $(CT_MAKE_COMMAND_LINE) clean
	find $(_CT_SUBDIRS) \( -name '*.[oas]' -o -name core -o -name '.*.flags' \) -type f -print \
		| grep -v lxdialog/ | xargs rm -f
	$(_CLEAN_OUTPUT_DIR)
endif
endif 


