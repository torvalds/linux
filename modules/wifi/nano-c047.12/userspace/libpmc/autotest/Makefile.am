noinst_PROGRAMS = pmcpriv header2test

AM_CFLAGS = -Wall

EXTRA_DIST = ../../libnrx/autotest/testprogram.in.c ../../libnrx/autotest/replace_nanokey

pmcpriv_SOURCES = pmcpriv.c

# Variables needed for generating test api
export NRX_API_PROTOCOL_HEADERS = $(top_srcdir)/pmc_proto.h
export NRX_API_PRIVATE_HEADERS = $(top_srcdir)/../libnrx/nrx_proto.h
export NRX_API_TYPE_HEADERS = $(top_builddir)/../libnrx/nrx_lib.h
export NRX_API_LIBRARY_FILE = $(top_builddir)/libpmc.a
export NRX_API_OUTPUT_FILE = pmcpriv

pmcpriv.c: $(top_srcdir)/../libnrx/autotest/testprogram.in.c $(top_builddir)/libpmc.a $(top_srcdir)/../libnrx/libnrx.a $(top_srcdir)/pmc_proto.h $(top_srcdir)/../libnrx/autotest/replace_nanokey header2test
	$(top_srcdir)/../libnrx/autotest/replace_nanokey $(top_srcdir)/../libnrx/autotest/testprogram.in.c > pmcpriv.c || exit 1
	if [ -f $(srcdir)/pmcpriv ]; then for i in `$(srcdir)/pmcpriv --help | grep nrx_ | cut -d \  -f 7` ; do \
	    ln -s -f pmcpriv $$i 2> /dev/null ; \
	done; fi

header2test: $(top_srcdir)/../libnrx/autotest/header2test.c
	gcc $(top_srcdir)/../libnrx/autotest/header2test.c -o header2test 

pmcpriv_LDADD = $(top_builddir)/libpmc.a $(top_builddir)/../libnrx/libnrx.a

#clean-am:
#	for i in `if [ -f $(srcdir)/pmcpriv ]; then $(srcdir)/pmcpriv --help | grep pmc_; fi` ; do \
#	    rm -f $$i ; \
#       done

