# PTP 1588 clock support - User space test program
#
# Copyright (C) 2010 OMICRON electronics GmbH
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

CC        = $(CROSS_COMPILE)gcc
INC       = -I$(KBUILD_OUTPUT)/usr/include
CFLAGS    = -Wall $(INC)
LDLIBS    = -lrt
PROGS     = testptp

all: $(PROGS)

testptp: testptp.o

clean:
	rm -f testptp.o

distclean: clean
	rm -f $(PROGS)
