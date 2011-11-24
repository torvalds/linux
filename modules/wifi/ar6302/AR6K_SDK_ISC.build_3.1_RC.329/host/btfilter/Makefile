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

CC=		$(ATH_CROSSS_COMPILE_TYPE)gcc
LD=		$(ATH_CROSSS_COMPILE_TYPE)ld
AR=		$(ATH_CROSSS_COMPILE_TYPE)ar
SOURCES=	btfilter_core.c \
		btfilter_action.c 
INCLUDES=	-I../include \
		-I../os/linux/include \
		-I../../include
CFLAGS=		-Wall
OBJECTS=	$(SOURCES:.c=.o)
LIBS=
LDFLAGS=	rcs
TARGETLIB=	btfilt

all: $(OBJECTS)
	$(AR) $(LDFLAGS) lib$(TARGETLIB).a $(OBJECTS)

.c.o:
	$(CC) -c $(CFLAGS) $(INCLUDES) $< -o $@

clean:
	rm -f lib$(TARGETLIB).a $(OBJECTS)
