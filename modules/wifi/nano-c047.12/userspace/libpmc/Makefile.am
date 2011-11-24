lib_LIBRARIES = libpmc.a

AM_CFLAGS = -Wall -Werror

SUBDIRS = . autotest
DIST_SUBDIRS = autotest

libpmc_a_SOURCES =				\
	pmc_proto.h				\
	ia.c

libpmc_a_LIBADD  = $(LIBOBJS)

include_HEADERS = pmc_proto.h

LDADD = libpmc.a

$(libpmc_a_OBJECTS): $(srcdir)/pmc_proto.h $(srcdir)/../libnrx/nrx_lib.h $(srcdir)/../libnrx/nrx_proto.h
$(srcdir)/pmc_proto.h: $(filter %.c, $(libpmc_a_SOURCES))
	cd $(srcdir) && perl ../libnrx/make-proto.pl -o pmc_proto.h $(filter %.c, $(libpmc_a_SOURCES))

HOSTDRIVER=$(top_srcdir)/../../..

