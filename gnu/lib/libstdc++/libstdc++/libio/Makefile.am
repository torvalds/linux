## Makefile for the libio subdirectory of the GNU C++ Standard library.
##
## Copyright (C) 1999, 2000, 2001 Free Software Foundation, Inc.
##
## This file is part of the libstdc++ version 3 distribution.
## Process this file with automake to produce Makefile.in.

## This file is part of the GNU ISO C++ Library.  This library is free
## software; you can redistribute it and/or modify it under the
## terms of the GNU General Public License as published by the
## Free Software Foundation; either version 2, or (at your option)
## any later version.

## This library is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.

## You should have received a copy of the GNU General Public License along
## with this library; see the file COPYING.  If not, write to the Free
## Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
## USA.

AUTOMAKE_OPTIONS = 1.3 cygnus

mkinstalldirs = $(SHELL) $(toplevel_srcdir)/mkinstalldirs

if GLIBCPP_BUILD_LIBIO
noinst_LTLIBRARIES = libio.la
else
noinst_LTLIBRARIES =
endif

# Use common includes from acinclude.m4/GLIBCPP_EXPORT_INCLUDES
GLIBCPP_INCLUDES = @GLIBCPP_INCLUDES@
TOPLEVEL_INCLUDES = @TOPLEVEL_INCLUDES@
LIBIO_INCLUDES = @LIBIO_INCLUDES@

INCLUDES = \
	-nostdinc++ \
	$(GLIBCPP_INCLUDES) $(LIBIO_INCLUDES) $(TOPLEVEL_INCLUDES)	

libio_headers = \
        libio.h libioP.h iolibio.h

if GLIBCPP_NEED_LIBIO
LIBIO_SRCS = \
	filedoalloc.c genops.c fileops.c stdfiles.c c_codecvt.c \
	iofclose.c iofopen.c 
else
LIBIO_SRCS =
endif

if GLIBCPP_NEED_WLIBIO
LIBIO_WSRCS = \
	wfiledoalloc.c wfileops.c wgenops.c iofwide.c
else
LIBIO_WSRCS =
endif


EXTRA_DIST = iostreamP.h

libio_la_SOURCES = $(LIBIO_SRCS) $(LIBIO_WSRCS)


AM_CFLAGS = -D_GNU_SOURCE 
