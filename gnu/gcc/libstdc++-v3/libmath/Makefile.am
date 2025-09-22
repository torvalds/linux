## Makefile for the math subdirectory of the GNU C++ Standard library.
##
## Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003
## Free Software Foundation, Inc.
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
## Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
## USA.

# May be used by various substitution variables.
gcc_version := $(shell cat $(top_srcdir)/../gcc/BASE-VER)

mkinstalldirs = $(SHELL) $(toplevel_srcdir)/mkinstalldirs

noinst_LTLIBRARIES = libmath.la

libmath_la_LIBADD = $(LIBMATHOBJS)

libmath_la_DEPENDENCIES = $(libmath_la_LIBADD)

libmath_la_SOURCES = stubs.c

AM_CPPFLAGS = $(CANADIAN_INCLUDES)

# Only compiling "C" sources in this directory.
LIBTOOL = @LIBTOOL@ --tag CC
