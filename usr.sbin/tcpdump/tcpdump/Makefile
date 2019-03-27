# $FreeBSD$

.include <src.opts.mk>

TCPDUMP_DISTDIR?= ${SRCTOP}/contrib/tcpdump
.PATH: ${TCPDUMP_DISTDIR}

PROG=	tcpdump

SRCS=	addrtoname.c \
	addrtostr.c \
	af.c \
	ascii_strcasecmp.c \
	checksum.c \
	cpack.c \
	gmpls.c \
	gmt2local.c \
	in_cksum.c \
	ipproto.c \
	l2vpn.c \
	machdep.c \
	netdissect.c \
	nlpid.c \
	oui.c \
	parsenfsfh.c \
	print.c \
	print-802_11.c \
	print-802_15_4.c \
	print-ah.c \
	print-ahcp.c \
	print-aodv.c \
	print-aoe.c \
	print-ap1394.c \
	print-arcnet.c \
	print-arp.c \
	print-ascii.c \
	print-atalk.c \
	print-atm.c \
	print-babel.c \
	print-beep.c \
	print-bfd.c \
	print-bgp.c \
	print-bootp.c \
	print-bt.c \
	print-calm-fast.c \
	print-carp.c \
	print-cdp.c \
	print-cfm.c \
	print-chdlc.c \
	print-cip.c \
	print-cnfp.c \
	print-dccp.c \
	print-decnet.c \
	print-dhcp6.c \
	print-domain.c \
	print-dtp.c \
	print-dvmrp.c \
	print-eap.c \
	print-egp.c \
	print-eigrp.c \
	print-enc.c \
	print-esp.c \
	print-ether.c \
	print-fddi.c \
	print-forces.c \
	print-fr.c \
	print-frag6.c \
	print-ftp.c \
	print-geneve.c \
	print-geonet.c \
	print-gre.c \
	print-hncp.c \
	print-hsrp.c \
	print-http.c \
	print-icmp.c \
	print-icmp6.c \
	print-igmp.c \
	print-igrp.c \
	print-ip.c \
	print-ip6.c \
	print-ip6opts.c \
	print-ipcomp.c \
	print-ipfc.c \
	print-ipnet.c \
	print-ipx.c \
	print-isakmp.c \
	print-isoclns.c \
	print-juniper.c \
	print-krb.c \
	print-l2tp.c \
	print-lane.c \
	print-ldp.c \
	print-lisp.c \
	print-llc.c \
	print-lldp.c \
	print-lmp.c \
	print-loopback.c \
	print-lspping.c \
	print-lwapp.c \
	print-lwres.c \
	print-m3ua.c \
	print-medsa.c \
	print-mobile.c \
	print-mobility.c \
	print-mpcp.c \
	print-mpls.c \
	print-mptcp.c \
	print-msdp.c \
	print-msnlb.c \
	print-nfs.c \
	print-nsh.c \
	print-ntp.c \
	print-null.c \
	print-olsr.c \
	print-openflow.c \
	print-openflow-1.0.c \
	print-ospf.c \
	print-ospf6.c \
	print-otv.c \
	print-pgm.c \
	print-pim.c \
	print-pktap.c \
	print-ppi.c \
	print-ppp.c \
	print-pppoe.c \
	print-pptp.c \
	print-radius.c \
	print-raw.c \
	print-resp.c \
	print-rip.c \
	print-ripng.c \
	print-rpki-rtr.c \
	print-rrcp.c \
	print-rsvp.c \
	print-rt6.c \
	print-rtsp.c \
	print-rx.c \
	print-sctp.c \
	print-sflow.c \
	print-sip.c \
	print-sl.c \
	print-sll.c \
	print-slow.c \
	print-smb.c \
	print-smtp.c \
	print-snmp.c \
	print-stp.c \
	print-sunatm.c \
	print-sunrpc.c \
	print-symantec.c \
	print-syslog.c \
	print-tcp.c \
	print-telnet.c \
	print-tftp.c \
	print-timed.c \
	print-tipc.c \
	print-token.c \
	print-udld.c \
	print-udp.c \
	print-vjc.c \
	print-vqp.c \
	print-vrrp.c \
	print-vtp.c \
	print-vxlan.c \
	print-vxlan-gpe.c \
	print-wb.c \
	print-zephyr.c \
	print-zeromq.c \
	setsignal.c \
	signature.c \
	smbutil.c \
	strtoaddr.c \
	tcpdump.c \
	util-print.c \
	version.c
CLEANFILES+=	version.c ${MAN}

CFLAGS+= -I${.CURDIR} -I${TCPDUMP_DISTDIR}
CFLAGS+= -DHAVE_CONFIG_H
CFLAGS+= -D_U_="__attribute__((unused))"

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=	-DINET6 -DHAVE_OS_IPV6_SUPPORT
.endif
.if ${MACHINE_CPUARCH} != "i386"
CFLAGS+=	-DLBL_ALIGN
.endif

LIBADD=	l pcap
.if ${MK_CASPER} != "no"
LIBADD+=	casper
LIBADD+=	cap_dns
CFLAGS+=-DHAVE_CASPER
.endif
.if ${MK_OPENSSL} != "no"
LIBADD+=	crypto
CFLAGS+= -I${SYSROOT:U${DESTDIR}}/usr/include/openssl
CFLAGS+= -DHAVE_LIBCRYPTO -DHAVE_OPENSSL_EVP_H
.endif

.if ${MK_PF} != "no"
SRCS+=	print-pflog.c \
	print-pfsync.c
CFLAGS+= -DHAVE_NET_PFVAR_H -DHAVE_NET_IF_PFLOG_H
.endif

version.c: ${TCPDUMP_DISTDIR}/VERSION
	rm -f version.c ; \
	    sed 's/.*/char version[] = "&";/' ${TCPDUMP_DISTDIR}/VERSION \
		> version.c

.include <bsd.prog.mk>

.for mp in ${MAN}
${mp}: ${mp}.in
	sed -e 's/@MAN_MISC_INFO@/7/g' -e 's/@MAN_FILE_FORMATS@/5/g' \
		${.ALLSRC} > ${.TARGET}
.endfor
