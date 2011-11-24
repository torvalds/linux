#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2005-2007 Atheros Corporation.  All rights reserved.
# 
# The software source and binaries included in this development package are
# licensed, not sold. You, or your company, received the package under one
# or more license agreements. The rights granted to you are specifically
# listed in these license agreement(s). All other rights remain with Atheros
# Communications, Inc., its subsidiaries, or the respective owner including
# those listed on the included copyright notices.  Distribution of any
# portion of this package must be in strict compliance with the license
# agreement(s) terms.
# </copyright>
# 
# <summary>
# 	Wifi driver for AR6002
# </summary>
#
#------------------------------------------------------------------------------
#==============================================================================
# Author(s): ="Atheros"
#==============================================================================
CC := $(ATH_CROSS_COMPILE_TYPE)gcc
AR := $(ATH_CROSS_COMPILE_TYPE)ar
RM := $(ATH_CROSS_COMPILE_TYPE)rm

KERNEL_SRC :=$(ATH_LINUXPATH)
PAL_APP=palLogEvents

all:
	$(CC) -Wall -I./ -I../pal_host_intf/  -I$(KERNEL_SRC)/include -I../../include -I../../../include -I../../wlan/include -I../../os/linux/include hciParser.c main.c -o $(PAL_APP) -lpthread

clean:
	$(RM) $(PAL_OBJ) $(PAL_INTF_LIB) $(PAL_APP)
