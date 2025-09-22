#	$OpenBSD: Makefile.inc,v 1.61 2024/04/15 14:30:48 naddy Exp $

# net sources
.PATH: ${LIBCSRCDIR}/net

CFLAGS+=-DRESOLVSORT

SRCS+=	base64.c ethers.c freeaddrinfo.c \
	gai_strerror.c getaddrinfo.c gethostnamadr.c \
	getifaddrs.c getnameinfo.c getnetent.c \
	getnetnamadr.c getpeereid.c getproto.c getprotoent.c getprotoname.c \
	getservbyname.c getservbyport.c getservent.c getrrsetbyname.c \
	herror.c htonl.c htons.c \
	if_indextoname.c if_nameindex.c if_nametoindex.c inet_addr.c \
	inet_lnaof.c inet_makeaddr.c inet_neta.c inet_netof.c inet_network.c \
	inet_net_ntop.c inet_net_pton.c inet_ntoa.c inet_ntop.c inet_pton.c \
	linkaddr.c ntohl.c ntohs.c rcmd.c rcmdsh.c ruserok.c \
	rresvport.c recv.c res_comp.c res_data.c \
	res_debug.c res_debug_syms.c res_init.c res_mkquery.c res_query.c \
	res_random.c res_send.c \
	send.c sethostent.c sockatmark.c

# IPv6
SRCS+=	ip6opt.c rthdr.c vars6.c

MAN+=	htobe64.3 ether_aton.3 gai_strerror.3 getaddrinfo.3 gethostbyname.3 \
	getifaddrs.3 getnameinfo.3 getnetent.3 getpeereid.3 getprotoent.3 \
	getrrsetbyname.3 getservent.3 htonl.3 if_indextoname.3 \
	inet_addr.3 inet_lnaof.3 inet_net_ntop.3 inet_ntop.3 \
	inet6_opt_init.3 inet6_rth_space.3 link_ntoa.3 \
	rcmd.3 rcmdsh.3 res_init.3 sockatmark.3
