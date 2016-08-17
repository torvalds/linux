include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/lib

sbin_PROGRAMS = splat

splat_SOURCES = splat.c
splat_LDFLAGS = $(top_builddir)/lib/libcommon.la

EXTRA_DIST = splat.h
