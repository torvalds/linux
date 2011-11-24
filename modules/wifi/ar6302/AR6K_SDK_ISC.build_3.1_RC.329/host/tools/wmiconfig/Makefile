#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2005-2008 Atheros Corporation.  All rights reserved.
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
#
#------------------------------------------------------------------------------
#==============================================================================
# Author(s): ="Atheros"
#==============================================================================
CC :=$(ATH_CROSS_COMPILE_TYPE)gcc
ifeq ($(ATH_BUILD_PAL_APP),yes)
AR :=$(ATH_CROSS_COMPILE_TYPE)ar
RM :=$(ATH_CROSS_COMPILE_TYPE)rm

USERF_FLAGS=-DATH_INCLUDE_PAL
KERNEL_SRC :=$(ATH_LINUXPATH)
PAL_SRC=../pal_host_intf/pal_api.c ../pal_host_intf/pal_intf.c ../pal_host_intf/pal_util.c ../palEventRecv/hciParser.c
PAL_OBJ=pal_api.o pal_intf.o pal_util.o hciParser.o
PAL_INTF_LIB=libpal_intf.a
INCLUDE_THREAD=-lpthread
else
USERF_FLAGS=
PAL_OBJ=
PAL_INTF_LIB=
INCLUDE_THREAD=
endif
APP=wmiconfig

all:
ifeq ($(ATH_BUILD_PAL_APP),yes)
	$(CC) -Wall $(USERF_FLAGS) -c -I./ -I$(KERNEL_SRC)/include -I../../include -I../../../include -I../../wlan/include -I../../os/linux/include  $(PAL_SRC)
	$(AR) -crs $(PAL_INTF_LIB) $(PAL_OBJ)
endif
	$(CC) -Wall $(USERF_FLAGS) -DUSER_KEYS -g -I$(KERNEL_SRC)/include -I../../include -I../../../include -I../../wlan/include -I../../os/linux/include wmiconfig.c $(PAL_INTF_LIB)  -o $(APP) $(INCLUDE_THREAD)

clean:
ifeq ($(ATH_BUILD_PAL_APP),yes)
	$(RM) $(PAL_OBJ) $(PAL_INTF_LIB)
endif
	$(RM) $(APP)
