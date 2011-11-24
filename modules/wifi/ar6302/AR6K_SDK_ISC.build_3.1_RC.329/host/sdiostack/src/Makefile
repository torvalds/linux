# Copyright 2004-2006 Atheros Communications Inc.
# 
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
# Portions of this code were developed with information supplied from the 
# SD Card Association Simplified Specifications. The following conditions and disclaimers may apply:
#
#  The following conditions apply to the release of the SD simplified specification (“Simplified
#  Specification”) by the SD Card Association. The Simplified Specification is a subset of the complete 
#  SD Specification which is owned by the SD Card Association. This Simplified Specification is provided 
#  on a non-confidential basis subject to the disclaimers below. Any implementation of the Simplified 
#  Specification may require a license from the SD Card Association or other third parties.
#  Disclaimers:
#  The information contained in the Simplified Specification is presented only as a standard 
#  specification for SD Cards and SD Host/Ancillary products and is provided "AS-IS" without any 
#  representations or warranties of any kind. No responsibility is assumed by the SD Card Association for 
#  any damages, any infringements of patents or other right of the SD Card Association or any third 
#  parties, which may result from its use. No license is granted by implication, estoppel or otherwise 
#  under any patent or other rights of the SD Card Association or any third party. Nothing herein shall 
#  be construed as an obligation by the SD Card Association to disclose or distribute any technical 
#  information, know-how or other confidential information to any third party.
#
#
# The initial developers of the original code are Seung Yi and Paul Lever
#
# sdio@atheros.com
#
#
#
# EMSDIO master make file
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
#      One of the following should be used to build the tree:
#      ALL_BUILD=1 - build all source code
#      HDK_BUILD=1 - build only HCD and sample function drivers
#      PDK_BUILD=1 - build only the sample function drivers
#      BUS_BUILD=1 - build bus driver, library and HCDs for use with built-in SDIO cards.
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
export PDK_BUILD
export HDK_BUILD
export ALL_BUILD
export ATHRAW_FD_BUILD
export BUS_BUILD

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
ifeq ($(ALL_BUILD),1)
subdir-m += busdriver/ lib/ hcd/ function/   
else 
ifeq ($(BUS_BUILD),1)
subdir-m += busdriver/ lib/ hcd/
else
ifeq ($(PDK_BUILD),1)
subdir-m += function/
else
ifeq ($(HDK_BUILD),1)
subdir-m += hcd/ function/ 
endif
endif
endif
endif

# add in rules to make modules
CT_OS_TOP_LEVEL_RULE :=$(CT_LINUXPATH)/Rules.make
include $(CT_OS_TOP_LEVEL_RULE)
else
#2.6+
_CT_MOD_EXTENSION :=ko
ifeq ($(ALL_BUILD),1)
obj-m += busdriver/ lib/ hcd/ function/  
else 
ifeq ($(BUS_BUILD),1)
obj-m += busdriver/ lib/ hcd/
else 
ifeq ($(PDK_BUILD),1)
obj-m += function/
else
ifeq ($(HDK_BUILD),1)
obj-m += hcd/ function/ 
endif
endif
endif
endif

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
	$(MAKE) $(CT_MAKE_COMMAND_LINE) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules
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

# For QNX
ifeq ($(CT_OS_TYPE),qnx)  
LIST=VARIANT
EARLY_DIRS=lib
##ifndef QRECURSE
QRECURSE=./recurse.mk
##ifdef QCONFIG
###QRDIR=$(dir $(QCONFIG))
##endif
##endif
include $(QRDIR)$(QRECURSE)

endif
 


