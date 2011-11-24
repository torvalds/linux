lib_LIBRARIES = libnrx.a

AM_CFLAGS = -Wall -Werror

SUBDIRS = . autotest
DIST_SUBDIRS = autotest

libnrx_a_SOURCES =				\
	counters.c				\
	cvt.c					\
	event.c					\
	ibss.c					\
	ifname.c				\
	init.c					\
	mac_mib_defs.h				\
	mib.c					\
	nanoioctl.h				\
	nrx_lib.h				\
	nrx_priv.h				\
	nrx_proto.h				\
	param.c					\
	powersave.c				\
	printbuf.c				\
	priomap.c				\
	radio.c					\
	roam.c					\
	scan.c					\
	wext.c

libnrx_a_LIBADD  = $(LIBOBJS)

include_HEADERS = nrx_lib.h nrx_proto.h

# By default, do not build test_abar because it requires -pthread option
# which is not supported by many GNU compilers for embedded platforms 
noinst_PROGRAMS = test test_trig mibtable show_scan btcoex_ratio #test_abar

test_abar_CFLAGS = -pthread

show_scan_SOURCES = show_scan.c scan_support.c

btcoex_ratio_SOURCES = btcoex_ratio.c

LDADD = libnrx.a

$(libnrx_a_OBJECTS): $(srcdir)/nrx_proto.h $(srcdir)/nrx_priv.h
$(srcdir)/nrx_proto.h $(srcdir)/nrx_priv.h: $(filter %.c, $(libnrx_a_SOURCES))
	chmod +x autotest/replace_nanokey
	cd $(srcdir) && perl make-proto.pl -o nrx_proto.h -p nrx_priv.h $(filter %.c, $(libnrx_a_SOURCES))

HOSTDRIVER=$(top_srcdir)/../..

$(libnrx_a_OBJECTS): nanoioctl.h mac_mib_defs.h

nanoioctl.h: $(HOSTDRIVER)/kernel/ic/nanoioctl.h
	cp -p $(HOSTDRIVER)/kernel/ic/nanoioctl.h nanoioctl.h

mac_mib_defs.h: $(HOSTDRIVER)/WiFiEngine/wifi_drv/inc/mib_defs.h
	cp -p $(HOSTDRIVER)/WiFiEngine/wifi_drv/inc/mib_defs.h mac_mib_defs.h

DISTCLEANFILES = nanoioctl.h mac_mib_defs.h
