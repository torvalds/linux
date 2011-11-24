#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2008 Atheros Corporation.  All rights reserved.
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

#USE_GLIB=1
#USE_HCIUTILS=1

ifdef V210	
V210_DIR=/tftpboot/ubuntu-taiwan
endif

ifdef V210	
CC=		$(ATH_CROSS_COMPILE_TYPE)gcc
LD=		$(ATH_CROSS_COMPILE_TYPE)ld
else
CC=		$(ATH_CROSSS_COMPILE_TYPE)gcc
LD=		$(ATH_CROSSS_COMPILE_TYPE)ld
endif
SOURCES=abtfilt_main.c \
		abtfilt_wlan.c \
		abtfilt_core.c \
		abtfilt_utils.c \
		btfilter_action.c\
		btfilter_core.c
	
INCLUDES=	-I../../../include \
		-I../../../os/linux/include \
		-I../../../../include \
		-I../../../ \
		-I$(V210_DIR)/usr/include/dbus-1.0/ \
		-I$(V210_DIR)/usr/lib/dbus-1.0/include \
		-I$(V210_DIR)/usr/include/bluetooth \
			
CFLAGS=		-Wall -g -DABF_DEBUG
#LIBS= 		-ldbus-1 -lpthread -lbtfilt -lrt -lbluetooth
LIBS= 		-ldbus-1 -lpthread -lrt -lbluetooth

ifdef USE_HCIUTILS
SOURCES += abtfilt_bluez_hciutils.c
LIBS += -ldl
else
CFLAGS += -DCONFIG_NO_HCILIBS
endif

ifdef USE_GLIB	
SOURCES += abtfilt_bluez_dbus_glib.c

LIBS += -lgobject-2.0 -lglib-2.0 -ldbus-glib-1

INCLUDES += -I/usr/include/glib-2.0     \
			-I/usr/lib/glib-2.0/include \

OBJECTS=	$(SOURCES:.c=.o)			

else	
SOURCES += abtfilt_bluez_dbus.c

OBJECTS=	$(SOURCES:.c=.o)
endif



#LDFLAGS=	-L$(WORKAREA)/host/btfilter
#copy libbluetooth.so from /tftpboot/ubuntu-taiwan/usr/lib to /tftpboot/ubuntu-taiwan/lib [for V210]
ifdef V210	
LDFLAGS=	-L/tftpboot/ubuntu-taiwan/lib
endif
FILTERAPP=	abtfilt

all: $(OBJECTS)
	$(CC) -o $(FILTERAPP) $(OBJECTS) $(LDFLAGS) $(LIBS)

.c.o:
	$(CC) -c $(CFLAGS) $(INCLUDES) $< -o $@

clean:
	rm -f $(FILTERAPP) $(OBJECTS)
