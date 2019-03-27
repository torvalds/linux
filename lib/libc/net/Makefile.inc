#	from @(#)Makefile.inc	8.2 (Berkeley) 9/5/93
# $FreeBSD$

# machine-independent net sources
.PATH: ${LIBC_SRCTOP}/net

CONFS+=	net/hosts net/hosts.equiv net/networks net/nsswitch.conf net/protocols
SRCS+=	base64.c ether_addr.c eui64.c \
	gai_strerror.c getaddrinfo.c \
	gethostbydns.c gethostbyht.c gethostbynis.c gethostnamadr.c \
	getifaddrs.c getifmaddrs.c getnameinfo.c \
	getnetbydns.c getnetbyht.c getnetbynis.c getnetnamadr.c \
	getproto.c getprotoent.c getprotoname.c getservent.c \
	if_indextoname.c if_nameindex.c if_nametoindex.c \
	ip6opt.c linkaddr.c map_v4v6.c name6.c ntoh.c \
	nsdispatch.c nslexer.l nsparser.y nss_compat.c \
	rcmd.c rcmdsh.c recv.c rthdr.c sctp_sys_calls.c send.c \
        sockatmark.c sourcefilter.c vars.c

.if ${MK_NS_CACHING} != "no"
SRCS+=	nscache.c nscachedcli.c
.endif

SYM_MAPS+=${LIBC_SRCTOP}/net/Symbol.map

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DINET6
.endif

CFLAGS+=-I${.OBJDIR}

# name6.c refers res_private.h
CFLAGS+=-I${LIBC_SRCTOP}/resolv

YFLAGS+=-p_nsyy
LFLAGS+=-P_nsyy

CFLAGS.nslexer.c= -DYY_BUF_SIZE=1024

MAN+=	byteorder.3 ethers.3 eui64.3 \
	getaddrinfo.3 gai_strerror.3 gethostbyname.3 \
	getifaddrs.3 getifmaddrs.3 getipnodebyname.3 \
	getnameinfo.3 getnetent.3 getprotoent.3 getservent.3 \
	if_indextoname.3 \
	inet.3 inet_net.3 \
	inet6_opt_init.3 inet6_option_space.3 inet6_rth_space.3 \
	inet6_rthdr_space.3 linkaddr.3 \
	nsdispatch.3 rcmd.3 rcmdsh.3 resolver.3 sockatmark.3 \
	sourcefilter.3 \
        sctp_bindx.3 sctp_connectx.3 sctp_freepaddrs.3 \
        sctp_getaddrlen.3 sctp_getassocid.3 sctp_getpaddrs.3 \
        sctp_opt_info.3 sctp_recvmsg.3 sctp_send.3 sctp_sendmsg.3 \

MLINKS+=byteorder.3 htonl.3 byteorder.3 htons.3 byteorder.3 ntohl.3 \
	byteorder.3 ntohs.3
MLINKS+=ethers.3 ether_aton.3 ethers.3 ether_hostton.3 ethers.3 ether_line.3 \
	ethers.3 ether_ntoa.3 ethers.3 ether_ntohost.3
MLINKS+=eui64.3 eui64_aton.3 eui64.3 eui64_hostton.3 \
	eui64.3 eui64_ntoa.3 eui64.3 eui64_ntohost.3
MLINKS+=getaddrinfo.3 freeaddrinfo.3 
MLINKS+=gethostbyname.3 endhostent.3 gethostbyname.3 gethostbyaddr.3 \
	gethostbyname.3 gethostbyname2.3 gethostbyname.3 gethostent.3 \
	gethostbyname.3 herror.3 gethostbyname.3 hstrerror.3 \
	gethostbyname.3 sethostent.3
MLINKS+=getifaddrs.3 freeifaddrs.3
MLINKS+=getifmaddrs.3 freeifmaddrs.3
MLINKS+=getipnodebyname.3 getipnodebyaddr.3 getipnodebyname.3 freehostent.3
MLINKS+=getnetent.3 endnetent.3 getnetent.3 getnetbyaddr.3 \
	getnetent.3 getnetbyname.3 getnetent.3 setnetent.3
MLINKS+=getprotoent.3 endprotoent.3 getprotoent.3 getprotobyname.3 \
	getprotoent.3 getprotobynumber.3 getprotoent.3 setprotoent.3
MLINKS+=getservent.3 endservent.3 getservent.3 getservbyname.3 \
	getservent.3 getservbyport.3 getservent.3 setservent.3
MLINKS+=if_indextoname.3 if_nametoindex.3 if_indextoname.3 if_nameindex.3 \
	if_indextoname.3 if_freenameindex.3
MLINKS+=inet.3 addr.3 inet.3 inet_addr.3 inet.3 inet_aton.3 \
	inet.3 inet_lnaof.3 inet.3 inet_makeaddr.3 inet.3 inet_netof.3 \
	inet.3 inet_network.3 inet.3 inet_ntoa.3 inet.3 inet_ntoa_r.3\
	inet.3 inet_ntop.3 inet.3 inet_pton.3 \
	inet.3 network.3 inet.3 ntoa.3 
MLINKS+= sctp_send.3 sctp_sendx.3
MLINKS+= sctp_sendmsg.3 sctp_sendmsgx.3
MLINKS+= sctp_freepaddrs.3 sctp_freeladdrs.3
MLINKS+= sctp_getpaddrs.3 sctp_getladdrs.3
MLINKS+=inet_net.3 inet_net_ntop.3 inet_net.3 inet_net_pton.3
MLINKS+=inet6_opt_init.3 inet6_opt_append.3 \
	inet6_opt_init.3 inet6_opt_find.3 \
	inet6_opt_init.3 inet6_opt_finish.3 \
	inet6_opt_init.3 inet6_opt_get_val.3 \
	inet6_opt_init.3 inet6_opt_next.3 \
	inet6_opt_init.3 inet6_opt_set_val.3 \
	inet6_option_space.3 inet6_option_alloc.3 \
	inet6_option_space.3 inet6_option_append.3 \
	inet6_option_space.3 inet6_option_find.3 \
	inet6_option_space.3 inet6_option_init.3 \
	inet6_option_space.3 inet6_option_next.3 \
	inet6_rth_space.3 inet6_rth_add.3 \
	inet6_rth_space.3 inet6_rth_getaddr.3 \
	inet6_rth_space.3 inet6_rth_init.3 \
	inet6_rth_space.3 inet6_rth_reverse.3 \
	inet6_rth_space.3 inet6_rth_segments.3 \
	inet6_rthdr_space.3 inet6_rthdr_add.3 \
	inet6_rthdr_space.3 inet6_rthdr_getaddr.3 \
	inet6_rthdr_space.3 inet6_rthdr_getflags.3 \
	inet6_rthdr_space.3 inet6_rthdr_init.3 \
	inet6_rthdr_space.3 inet6_rthdr_lasthop.3 \
	inet6_rthdr_space.3 inet6_rthdr_reverse.3 \
	inet6_rthdr_space.3 inet6_rthdr_segments.3
MLINKS+=linkaddr.3 link_addr.3 linkaddr.3 link_ntoa.3 
MLINKS+=rcmd.3 iruserok.3 rcmd.3 iruserok_sa.3 \
	rcmd.3 rcmd_af.3 \
	rcmd.3 rresvport.3 rcmd.3 rresvport_af.3 \
	rcmd.3 ruserok.3
MLINKS+=resolver.3 dn_comp.3 resolver.3 dn_expand.3 resolver.3 res_init.3 \
	resolver.3 res_mkquery.3 resolver.3 res_query.3 \
	resolver.3 res_search.3 resolver.3 res_send.3 resolver.3 dn_skipname.3 \
	resolver.3 ns_get16.3 resolver.3 ns_get32.3 \
	resolver.3 ns_put16.3 resolver.3 ns_put32.3
MLINKS+=sourcefilter.3 setipv4sourcefilter.3 sourcefilter.3 getipv4sourcefilter.3 \
	sourcefilter.3 setsourcefilter.3 sourcefilter.3 getsourcefilter.3

.if ${MK_HESIOD} != "no"
SRCS+=	hesiod.c 
MAN+=	hesiod.3
.endif

.if ${MK_NIS} == "no"
afterinstallconfig:
	sed -i "" -e 's/.*_compat:/# &/' -e 's/compat$$/files/' \
		${DESTDIR}/etc/nsswitch.conf
.endif
