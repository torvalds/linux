#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2005-2007 Atheros Corporation.  All rights reserved.
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
CC := $(ATH_CROSS_COMPILE_TYPE)gcc
KERNEL_SRC :=$(ATH_LINUXPATH)

ifndef CFLAGS
#CFLAGS = -MMD -O2 -Wall -g
CFLAGS = -Wall
endif

CFLAGS += -DCONFIG_CTRL_IFACE_UNIX -DCONFIG_CTRL_IFACE -DWAPI_ENABLE

all:
	$(CC) $(CFLAGS) -I./ -I$(KERNEL_SRC)/include -I../../include -I../../../include -I../../wlan/include \
					-I../../os/linux/include -I../../3rdparty/supplicant/opensrc_0_6_9/src/common \
					-I../../os/linux/include -I../../3rdparty/supplicant/opensrc_0_6_9/src/utils wac.c -o wac

#wpa_ctrl.o: os_internal.o
#	$(CC) $(CFLAGS) -I./ -I$(KERNEL_SRC)/include -I../../include -I../../../include -I../../wlan/include \
#					-I../../os/linux/include -I../../3rdparty/supplicant/opensrc_0_6_9/src/utils \
#					-c ../../3rdparty/supplicant/opensrc_0_6_9/src/common/wpa_ctrl.c
#
#os_internal.o:
#	$(CC) $(CFLAGS) -I./ -I$(KERNEL_SRC)/include -I../../include -I../../../include -I../../wlan/include \
#					-I../../os/linux/include -I../../3rdparty/supplicant/opensrc_0_6_9/src/utils \
#					-c ../../3rdparty/supplicant/opensrc_0_6_9/src/utils/os_internal.c
