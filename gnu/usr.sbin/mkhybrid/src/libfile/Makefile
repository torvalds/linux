# Makefile for file(1) cmd. 
# Copyright (c) Ian F. Darwin 86/09/01 - see LEGAL.NOTICE.
# @(#)$Id: Makefile,v 1.1 2000/10/10 20:40:36 beck Exp $
#
# This software is not subject to any license of the American Telephone
# and Telegraph Company or of the Regents of the University of California.
#
# Permission is granted to anyone to use this software for any purpose on
# any computer system, and to alter it and redistribute it freely, subject
# to the following restrictions:
#
# 1. The author is not responsible for the consequences of use of this
#    software, no matter how awful, even if they arise from flaws in it.
#
# 2. The origin of this software must not be misrepresented, either by
#    explicit claim or by omission.  Since few users ever read sources,
#    credits must appear in the documentation.
#
# 3. Altered versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.  Since few users
#    ever read sources, credits must appear in the documentation.
#
# 4. This notice may not be removed or altered.
#
# Makefile altered for mkhybrid James Pearson 19/5/98
#
VERSION	= 3.22
DEFS	=
#DEFS	= -DMAIN
#DEFS	= -DMAGIC='"$(MAGIC)"' -DBUILTIN_ELF # -Dvoid=int
CC	= cc
COPTS	= -g 		# newer compilers allow both; else drop -O
# For truly antique environments, use this for (dummy) include files:
#COPTS	= -O # -Ilocalinc
CFLAGS	= $(COPTS) $(DEFS)
LDFLAGS	= $(COPTS) # -Bstatic	# older gdb couldn't handle shared libs
RANLIB  = ranlib
TARGET  = libfile.a
ARFLAGS = rc

# There are no system-dependant configuration options (except maybe CFLAGS).
# Uncomment any of these that is missing from your "standard" library.

SRCS = file.c apprentice.c softmagic.c 

OBJS = file.o apprentice.o softmagic.o

all ::		$(TARGET)

$(TARGET): 	$(OBJS)
		ar $(ARFLAGS) $@ $(OBJS)
		$(RANLIB) $@

clean:
		rm -f *.o core $(TARGET)

$(OBJS):	file.h patchlevel.h
