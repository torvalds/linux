noinst_PROGRAMS = nrxpriv header2test

AM_CFLAGS = -Wall

EXTRA_DIST = testprogram.in.c replace_nanokey

nrxpriv_SOURCES = nrxpriv.c ../scan_support.c

# Variables needed for generating test api
export NRX_API_PROTOCOL_HEADERS=$(top_srcdir)/nrx_proto.h
export NRX_API_PRIVATE_HEADERS=$(top_srcdir)/nrx_priv.h
export NRX_API_TYPE_HEADERS=$(top_srcdir)/nrx_lib.h
export NRX_API_LIBRARY_FILE=$(top_builddir)/libnrx.a
export NRX_API_OUTPUT_FILE = nrxpriv


nrxpriv.c: $(srcdir)/testprogram.in.c $(top_builddir)/libnrx.a $(top_srcdir)/nrx_proto.h replace_nanokey header2test
	$(srcdir)/replace_nanokey $(srcdir)/testprogram.in.c > nrxpriv.c || exit 1
	if [ -f $(srcdir)/nrxpriv ]; then for i in `$(srcdir)/nrxpriv --help | grep nrx_ | cut -d \  -f 7` ; do \
	    ln -s -f nrxpriv $$i 2> /dev/null ; \
	done; fi

header2test: $(srcdir)/header2test.c
	gcc $(srcdir)/header2test.c -o header2test 

nrxpriv_LDADD = ../libnrx.a

CLEANFILES = nrxpriv.c

#clean-am:
#	for i in `if [ -f $(srcdir)/nrxpriv ]; then $(srcdir)/nrxpriv --help | grep nrx_; fi` ; do \
#	    rm -f $$i ; \
#       done

