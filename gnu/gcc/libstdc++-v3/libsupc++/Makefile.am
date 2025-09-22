## Makefile for the GNU C++ Support library.
##
## Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005
## Free Software Foundation, Inc.
##
## Process this file with automake to produce Makefile.in.
##
## This file is part of GCC.
##
## GCC is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2, or (at your option)
## any later version.
##
## GCC is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with GCC; see the file COPYING.  If not, write to
## the Free Software Foundation, 51 Franklin Street, Fifth Floor,
## Boston, MA 02110-1301, USA.

include $(top_srcdir)/fragment.am

# Need this library to both be part of libstdc++.a, and installed
# separately too.
# 1) separate libsupc++.la
toolexeclib_LTLIBRARIES = libsupc++.la
# 2) integrated libsupc++convenience.la that is to be a part of libstdc++.a
noinst_LTLIBRARIES = libsupc++convenience.la


headers = \
	exception new typeinfo cxxabi.h exception_defines.h

if GLIBCXX_HOSTED
  c_sources = \
	cp-demangle.c 
endif

sources = \
	del_op.cc \
	del_opnt.cc \
	del_opv.cc \
	del_opvnt.cc \
	eh_alloc.cc \
	eh_arm.cc \
	eh_aux_runtime.cc \
	eh_call.cc \
	eh_catch.cc \
	eh_exception.cc \
	eh_globals.cc \
	eh_personality.cc \
	eh_term_handler.cc \
	eh_terminate.cc \
	eh_throw.cc \
	eh_type.cc \
	eh_unex_handler.cc \
	guard.cc \
	new_handler.cc \
	new_op.cc \
	new_opnt.cc \
	new_opv.cc \
	new_opvnt.cc \
	pure.cc \
	tinfo.cc \
	tinfo2.cc \
	vec.cc \
	vterminate.cc

libsupc___la_SOURCES = $(sources) $(c_sources)
libsupc__convenience_la_SOURCES = $(sources) $(c_sources)

glibcxxinstalldir = $(gxx_include_dir)
glibcxxinstall_HEADERS = $(headers)

# AM_CXXFLAGS needs to be in each subdirectory so that it can be
# modified in a per-library or per-sub-library way.  Need to manually
# set this option because CONFIG_CXXFLAGS has to be after
# OPTIMIZE_CXXFLAGS on the compile line so that -O2 can be overridden
# as the occasion call for it.
AM_CXXFLAGS = \
	-fno-implicit-templates \
	$(LIBSUPCXX_PICFLAGS) \
	$(WARN_CXXFLAGS) \
	$(OPTIMIZE_CXXFLAGS) \
	$(CONFIG_CXXFLAGS)

AM_MAKEFLAGS = \
	"gxx_include_dir=$(gxx_include_dir)"


# Use special rules for pulling things out of libiberty.  These
# objects should be compiled with the "C" compiler, not the C++
# compiler, and also should not use the C++ includes.
C_INCLUDES = -I.. -I$(toplevel_srcdir)/libiberty -I$(toplevel_srcdir)/include
C_COMPILE = \
	$(CC) $(DEFS) $(C_INCLUDES) \
	$(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS)

# LTCOMPILE is copied from LTCXXCOMPILE below.
LTCOMPILE = $(LIBTOOL) --tag CC --tag disable-shared --mode=compile $(CC) \
	    $(DEFS) $(C_INCLUDES) $(LIBSUPCXX_PICFLAGS) \
            $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS)

cp-demangle.c:
	rm -f $@
	$(LN_S) $(toplevel_srcdir)/libiberty/cp-demangle.c $@
cp-demangle.lo: cp-demangle.c
	$(LTCOMPILE) -DIN_GLIBCPP_V3 -Wno-error -c $<
cp-demangle.o: cp-demangle.c
	$(C_COMPILE) -DIN_GLIBCPP_V3 -Wno-error -c $<


# libstdc++ libtool notes

# 1) Need to explicitly set LTCXXCOMPILE so that AM_CXXFLAGS is
# last. (That way, things like -O2 passed down from the toplevel can
# be overridden by --enable-debug.)

# 2) In general, libtool expects an argument such as `--tag=CXX' when
# using the C++ compiler, because that will enable the settings
# detected when C++ support was being configured.  However, when no
# such flag is given in the command line, libtool attempts to figure
# it out by matching the compiler name in each configuration section
# against a prefix of the command line.  The problem is that, if the
# compiler name and its initial flags stored in the libtool
# configuration file don't match those in the command line, libtool
# can't decide which configuration to use, and it gives up.  The
# correct solution is to add `--tag CXX' to LTCXXCOMPILE and maybe
# CXXLINK, just after $(LIBTOOL), so that libtool doesn't have to
# attempt to infer which configuration to use.
#
# We have to put --tag disable-shared after --tag CXX lest things
# CXX undo the affect of disable-shared.
LTCXXCOMPILE = $(LIBTOOL) --tag CXX --tag disable-shared \
	       --mode=compile $(CXX) $(TOPLEVEL_INCLUDES) \
	       $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) 

LTLDFLAGS = $(shell $(SHELL) $(top_srcdir)/../libtool-ldflags $(LDFLAGS))

# 3) We'd have a problem when building the shared libstdc++ object if
# the rules automake generates would be used.  We cannot allow g++ to
# be used since this would add -lstdc++ to the link line which of
# course is problematic at this point.  So, we get the top-level
# directory to configure libstdc++-v3 to use gcc as the C++
# compilation driver.
CXXLINK = $(LIBTOOL) --tag CXX --tag disable-shared \
	  --mode=link $(CXX) \
	  $(OPT_LDFLAGS) $(SECTION_LDFLAGS) $(AM_CXXFLAGS) $(LTLDFLAGS) -o $@

# We have to have rules modified from the default to counteract SUN make
# prepending each of $(glibcxxinstall_HEADERS) with VPATH below.
install-glibcxxinstallHEADERS: $(glibcxxinstall_HEADERS)
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)$(glibcxxinstalldir)
	@list='$(glibcxxinstall_HEADERS)'; for p in $$list; do \
	  q=`echo $$p | sed -e 's,.*/,,'`; \
	  if test -f "$$p"; then d= ; else d="$(srcdir)/"; fi; \
	  echo " $(INSTALL_DATA) $$d$$p $(DESTDIR)$(glibcxxinstalldir)/$$q"; \
	  $(INSTALL_DATA) $$d$$p $(DESTDIR)$(glibcxxinstalldir)/$$q; \
	done

uninstall-glibcxxinstallHEADERS:
	@$(NORMAL_UNINSTALL)
	list='$(glibcxxinstall_HEADERS)'; for p in $$list; do \
	  q=`echo $$p | sed -e 's,.*/,,'`; \
	  rm -f $(DESTDIR)$(glibcxxinstalldir)/$$q; \
	done
