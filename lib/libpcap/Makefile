# Makefile for libpcap
# $FreeBSD$

SHLIBDIR?= /lib

.include <src.opts.mk>

PACKAGE=lib${LIB}
LIB=	pcap

SRCS=	bpf_dump.c \
	bpf_filter.c \
	bpf_image.c \
	etherent.c \
	fad-getad.c \
	fmtutils.c \
	gencode.c \
	grammar.y \
	nametoaddr.c \
	optimize.c \
	pcap-bpf.c \
	pcap-common.c \
	pcap-netmap.c \
	pcap.c \
	savefile.c \
	scanner.l \
	sf-pcap.c \
	sf-pcapng.c \
	tokdefs.h

# Old compatibility headers
INCS=	fmtutils.h \
	pcap-bpf.h \
	pcap-namedb.h \
	pcap-netmap.h \
	pcap.h

PCAPINCS= \
	pcap/bluetooth.h \
	pcap/bpf.h \
	pcap/can_socketcan.h \
	pcap/compiler-tests.h \
	pcap/dlt.h \
	pcap/funcattrs.h \
	pcap/ipnet.h \
	pcap/namedb.h \
	pcap/nflog.h \
	pcap/pcap-inttypes.h \
	pcap/pcap.h \
	pcap/sll.h \
	pcap/usb.h \
	pcap/vlan.h

PCAPINCSDIR=	${INCLUDEDIR}/pcap
INCSGROUPS=	INCS PCAPINCS

MAN=	pcap.3 \
	pcap_activate.3 \
	pcap_breakloop.3 \
	pcap_can_set_rfmon.3 \
	pcap_close.3 \
	pcap_compile.3 \
	pcap_create.3 \
	pcap_datalink.3 \
	pcap_datalink_name_to_val.3 \
	pcap_datalink_val_to_name.3 \
	pcap_dump.3 \
	pcap_dump_close.3 \
	pcap_dump_file.3 \
	pcap_dump_flush.3 \
	pcap_dump_ftell.3 \
	pcap_dump_open.3 \
	pcap_file.3 \
	pcap_fileno.3 \
	pcap_findalldevs.3 \
	pcap_freecode.3 \
	pcap_get_required_select_timeout.3 \
	pcap_get_selectable_fd.3 \
	pcap_get_tstamp_precision.3 \
	pcap_geterr.3 \
	pcap_inject.3 \
	pcap_is_swapped.3 \
	pcap_lib_version.3 \
	pcap_list_datalinks.3 \
	pcap_list_tstamp_types.3 \
	pcap_lookupdev.3 \
	pcap_lookupnet.3 \
	pcap_loop.3 \
	pcap_major_version.3 \
	pcap_next_ex.3 \
	pcap_offline_filter.3 \
	pcap_open_dead.3 \
	pcap_open_live.3 \
	pcap_open_offline.3 \
	pcap_set_buffer_size.3 \
	pcap_set_datalink.3 \
	pcap_set_immediate_mode.3 \
	pcap_set_promisc.3 \
	pcap_set_protocol.3 \
	pcap_set_rfmon.3 \
	pcap_set_snaplen.3 \
	pcap_set_timeout.3 \
	pcap_set_tstamp_precision.3 \
	pcap_set_tstamp_type.3 \
	pcap_setdirection.3 \
	pcap_setfilter.3 \
	pcap_setnonblock.3 \
	pcap_snapshot.3 \
	pcap_stats.3 \
	pcap_statustostr.3 \
	pcap_strerror.3 \
	pcap_tstamp_type_name_to_val.3 \
	pcap_tstamp_type_val_to_name.3

MLINKS= \
	pcap_datalink_val_to_name.3 pcap_datalink_val_to_description.3 \
	pcap_dump_open.3 pcap_dump_fopen.3 \
	pcap_findalldevs.3 pcap_freealldevs.3 \
	pcap_geterr.3 pcap_perror.3 \
	pcap_inject.3 pcap_sendpacket.3 \
	pcap_list_datalinks.3 pcap_free_datalinks.3 \
	pcap_list_tstamp_types.3 pcap_free_tstamp_types.3 \
	pcap_loop.3 pcap_dispatch.3 \
	pcap_major_version.3 pcap_minor_version.3 \
	pcap_next_ex.3 pcap_next.3 \
	pcap_open_offline.3 pcap_fopen_offline.3 \
	pcap_setnonblock.3 pcap_getnonblock.3

# Our man pages are a special copy from the distdir. See below.
CLEANFILES+=${MAN}
CLEANFILES+=tokdefs.h scanner.h 

YFLAGS+=-p pcap_
LFLAGS+=-Ppcap_ --header-file=${.OBJDIR}/scanner.h --nounput
CFLAGS+=-DHAVE_CONFIG_H -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=-D_U_="__attribute__((unused))"
CFLAGS+=-DHAVE_SNPRINTF -DHAVE_VSNPRINTF
CFLAGS+=-DBUILDING_PCAP
.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DINET6
.endif
.if ${MK_PF} != "no"
CFLAGS+=-DHAVE_NET_PFVAR_H
.endif

CFLAGS+= -DPCAP_SUPPORT_NETMAP

.if ${MK_OFED} != "no"
SRCS+= pcap-rdmasniff.c
LIBADD+= ibverbs
LIBADD+= mlx5
CFLAGS+= -DPCAP_SUPPORT_RDMASNIFF
.endif

WARNS?=	0

SHLIB_MAJOR=	8

#
# Magic to grab sources out of src/contrib
#
PCAP_DISTDIR?=${SRCTOP}/contrib/libpcap
CFLAGS+=-I${PCAP_DISTDIR}
.PATH:	${PCAP_DISTDIR}
.PATH:	${PCAP_DISTDIR}/bpf/net

tokdefs.h: grammar.h .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}

#
# Magic to convert the man pages to something non Solarish
#
.for _page in ${MAN}
${_page}:
	if [ -f ${PCAP_DISTDIR}/${_page:S/3$/3pcap/} ]; then		\
		F=${_page:S/3$/3pcap/}; 				\
	elif [ -f ${PCAP_DISTDIR}/${_page:S/3$/3pcap.in/} ]; then		\
		F=${_page:S/3$/3pcap.in/}; 				\
	elif [ -f ${PCAP_DISTDIR}/${_page:S/5$/manfile.in/} ]; then	\
		F=${_page:S/5$/manfile.in/};				\
	elif [ -f ${PCAP_DISTDIR}/${_page:S/5$/manfile/} ]; then	\
		F=${_page:S/5$/manfile/};				\
	else								\
		F=${_page:S/7$/manmisc.in/};				\
	fi;								\
	sed -e 's/3PCAP/3/g' ${PCAP_DISTDIR}/$$F > ${_page}
.endfor

.include <bsd.lib.mk>
