#	$OpenBSD: Makefile,v 1.67 2020/12/04 11:36:13 mvs Exp $
#
#  Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994
# 	The Regents of the University of California.  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that: (1) source code distributions
#  retain the above copyright notice and this paragraph in its entirety, (2)
#  distributions including binary code include the above copyright notice and
#  this paragraph in its entirety in the documentation or other materials
#  provided with the distribution, and (3) all advertising materials mentioning
#  features or use of this software display the following acknowledgement:
#  ``This product includes software developed by the University of California,
#  Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
#  the University nor the names of its contributors may be used to endorse
#  or promote products derived from this software without specific prior
#  written permission.
#  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
#  WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
#  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
#

PROG=	tcpdump
MAN=	tcpdump.8

CFLAGS+=-Wall -I${.CURDIR}/../../sbin/pfctl -I${.CURDIR}/../hostapd

# for pcap-int.h
CFLAGS+=-I${.CURDIR}/../../lib/libpcap

CFLAGS+=-DCSLIP -DPPP -DHAVE_FDDI -DETHER_SERVICE -DHAVE_ETHER_NTOHOST -DINET6

LDADD+=	-lpcap -lcrypto
DPADD+=	${LIBPCAP} ${LIBCRYPTO}

SRCS=	tcpdump.c addrtoname.c privsep.c privsep_fdpass.c privsep_pcap.c \
	print-ether.c print-ip.c print-arp.c print-tcp.c print-udp.c \
	print-atalk.c print-domain.c print-tftp.c print-bootp.c print-nfs.c \
	print-icmp.c print-sl.c print-ppp.c print-rip.c print-timed.c \
	print-snmp.c print-ntp.c print-null.c print-ospf.c print-gtp.c \
	print-fddi.c print-llc.c print-sunrpc.c print-hsrp.c print-vqp.c \
	print-vrrp.c print-wb.c print-decnet.c print-isoclns.c print-ipx.c \
	print-atm.c print-dvmrp.c print-krb.c print-pim.c print-netbios.c \
	util.c bpf_dump.c parsenfsfh.c version.c print-igrp.c \
	print-gre.c print-nsh.c print-radius.c print-enc.c print-cnfp.c \
	print-ipsec.c print-ike.c print-raw.c print-l2tp.c print-mobile.c \
	print-ip6.c print-ip6opts.c print-icmp6.c print-dhcp6.c print-frag6.c \
	print-bgp.c print-ospf6.c print-ripng.c print-rt6.c print-stp.c \
	print-etherip.c print-lwres.c print-lldp.c print-cdp.c print-pflog.c \
	print-pfsync.c pf_print_state.c print-ofp.c ofp_map.c \
	print-udpencap.c print-carp.c print-nhrp.c print-wg.c \
	print-802_11.c print-iapp.c print-mpls.c print-slow.c print-usbpcap.c \
	gmt2local.c savestr.c setsignal.c in_cksum.c

# TCP OS Fingerprinting
.PATH: ${.CURDIR}/../../sys/net
.PATH: ${.CURDIR}/../../sbin/pfctl
SRCS+=	pf_osfp.c pfctl_osfp.c
CFLAGS+=-I${.CURDIR}/../../sbin/pfctl
CFLAGS+=-DFAKE_PF_KERNEL

#SRCS+=	smbutil.c print-smb.c

AWKS =	atime.awk packetdat.awk send-ack.awk stime.awk

.include <bsd.prog.mk>
