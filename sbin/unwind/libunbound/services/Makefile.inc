#	$OpenBSD: Makefile.inc,v 1.2 2020/03/23 08:37:27 florian Exp $

.PATH: ${.CURDIR}/libunbound/services

SRCS+=	authzone.c listen_dnsport.c localzone.c mesh.c modstack.c \
	outbound_list.c outside_network.c rpz.c view.c
