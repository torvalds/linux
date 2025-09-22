## Makefile for the toplevel directory of the mudflap library.
##
## Copyright (C) 2002, 2003, 2004
## Free Software Foundation, Inc.
##

AUTOMAKE_OPTIONS = 1.8 foreign
ACLOCAL_AMFLAGS = -I .. -I ../config
MAINT_CHARSET = latin1
SUBDIRS = testsuite

# May be used by various substitution variables.
gcc_version := $(shell cat $(top_srcdir)/../gcc/BASE-VER)

SECTION_FLAGS = @SECTION_FLAGS@
AM_CFLAGS = -Wall $(SECTION_FLAGS)

if LIBMUDFLAPTH
libmudflapth = libmudflapth.la
else
libmudflapth =
endif

toolexeclib_LTLIBRARIES = libmudflap.la $(libmudflapth)
target_noncanonical = @target_noncanonical@
libsubincludedir = $(libdir)/gcc/$(target_noncanonical)/$(gcc_version)/include
nobase_libsubinclude_HEADERS = mf-runtime.h


libmudflap_la_SOURCES = \
	mf-runtime.c \
	mf-heuristics.c \
	mf-hooks1.c \
	mf-hooks2.c
libmudflap_la_LIBADD = 
libmudflap_la_DEPENDENCIES = $(libmudflap_la_LIBADD)
libmudflap_la_LDFLAGS = -version-info `grep -v '^\#' $(srcdir)/libtool-version`

clean-local:
	rm -f pth/*.o pth/*.lo

pth/mf-runtime.lo: mf-runtime.c mf-runtime.h mf-impl.h
	$(LTCOMPILE) -DLIBMUDFLAPTH -c $(srcdir)/mf-runtime.c -o $@
pth/mf-heuristics.lo: mf-heuristics.c mf-runtime.h mf-impl.h
	$(LTCOMPILE) -DLIBMUDFLAPTH -c $(srcdir)/mf-heuristics.c -o $@
pth/mf-hooks1.lo: mf-hooks1.c mf-runtime.h mf-impl.h
	$(LTCOMPILE) -DLIBMUDFLAPTH -c $(srcdir)/mf-hooks1.c -o $@
pth/mf-hooks2.lo: mf-hooks2.c mf-runtime.h mf-impl.h
	$(LTCOMPILE) -DLIBMUDFLAPTH -c $(srcdir)/mf-hooks2.c -o $@
pth/mf-hooks3.lo: mf-hooks3.c mf-runtime.h mf-impl.h
	$(LTCOMPILE) -DLIBMUDFLAPTH -c $(srcdir)/mf-hooks3.c -o $@


libmudflapth_la_SOURCES =
libmudflapth_la_LIBADD = \
	pth/mf-runtime.lo \
	pth/mf-heuristics.lo \
	pth/mf-hooks1.lo \
	pth/mf-hooks2.lo \
	pth/mf-hooks3.lo
libmudflapth_la_DEPENDENCIES = $(libmudflapth_la_LIBADD)
libmudflapth_la_LDFLAGS = -version-info `grep -v '^\#' $(srcdir)/libtool-version`


# XXX hack alert
# From libffi/Makefile.am

# Work around what appears to be a GNU make bug handling MAKEFLAGS
# values defined in terms of make variables, as is the case for CC and
# friends when we are called from the top level Makefile.
AM_MAKEFLAGS = \
	"AR_FLAGS=$(AR_FLAGS)" \
	"CC_FOR_BUILD=$(CC_FOR_BUILD)" \
	"CFLAGS=$(CFLAGS)" \
	"CXXFLAGS=$(CXXFLAGS)" \
	"CFLAGS_FOR_BUILD=$(CFLAGS_FOR_BUILD)" \
	"CFLAGS_FOR_TARGET=$(CFLAGS_FOR_TARGET)" \
	"INSTALL=$(INSTALL)" \
	"INSTALL_DATA=$(INSTALL_DATA)" \
	"INSTALL_PROGRAM=$(INSTALL_PROGRAM)" \
	"INSTALL_SCRIPT=$(INSTALL_SCRIPT)" \
	"JC1FLAGS=$(JC1FLAGS)" \
	"LDFLAGS=$(LDFLAGS)" \
	"LIBCFLAGS=$(LIBCFLAGS)" \
	"LIBCFLAGS_FOR_TARGET=$(LIBCFLAGS_FOR_TARGET)" \
	"MAKE=$(MAKE)" \
	"MAKEINFO=$(MAKEINFO) $(MAKEINFOFLAGS)" \
	"PICFLAG=$(PICFLAG)" \
	"PICFLAG_FOR_TARGET=$(PICFLAG_FOR_TARGET)" \
	"SHELL=$(SHELL)" \
	"RUNTESTFLAGS=$(RUNTESTFLAGS)" \
	"exec_prefix=$(exec_prefix)" \
	"infodir=$(infodir)" \
	"libdir=$(libdir)" \
	"prefix=$(prefix)" \
	"includedir=$(includedir)" \
	"AR=$(AR)" \
	"AS=$(AS)" \
	"CC=$(CC)" \
	"CXX=$(CXX)" \
	"LD=$(LD)" \
	"LIBCFLAGS=$(LIBCFLAGS)" \
	"NM=$(NM)" \
	"PICFLAG=$(PICFLAG)" \
	"RANLIB=$(RANLIB)" \
	"DESTDIR=$(DESTDIR)"

MAKEOVERRIDES=

.PHONY: install-html

install-html:

## ################################################################

