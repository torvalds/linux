#	@(#)Makefile	8.1 (Berkeley) 6/12/93
# $FreeBSD$

.include <src.opts.mk>

PROG=	netstat
SRCS=	if.c inet.c main.c mbuf.c mroute.c netisr.c nl_symbols.c route.c \
	unix.c mroute6.c ipsec.c bpf.c pfkey.c sctp.c \
	nl_defs.h

nl_symbols.c: nlist_symbols
	awk '\
	    BEGIN { \
		print "#include <sys/param.h>"; \
		print "#include <nlist.h>"; \
		print "struct nlist nl[] = {"; \
	    } \
	    !/^\#/ { printf("\t{ .n_name = \"%s\" },\n", $$2); } \
	    END { print "\t{ .n_name = NULL },\n};" } \
	    ' < ${.ALLSRC} > ${.TARGET} || rm -f ${.TARGET}
nl_defs.h: nlist_symbols
	awk '\
	    BEGIN { \
		print "#include <nlist.h>"; \
		print "extern struct nlist nl[];"; \
		i = 0; \
	    } \
	    !/^\#/ { printf("\#define\tN%s\t%s\n", toupper($$2), i++); }' \
	    < ${.ALLSRC} > ${.TARGET} || rm -f ${.TARGET}
CLEANFILES+=	nl_symbols.c nl_defs.h
CFLAGS+=	-I${.OBJDIR}

WARNS?=	3
CFLAGS+=-fno-strict-aliasing

CFLAGS+=-DIPSEC
CFLAGS+=-DSCTP

.if ${MK_INET_SUPPORT} != "no"
CFLAGS+=-DINET
.endif

.if ${MK_INET6_SUPPORT} != "no"
SRCS+=	inet6.c
CFLAGS+=-DINET6
.endif

.if ${MK_OFED} != "no"
CFLAGS+=-DSDP
.endif

.if ${MK_PF} != "no"
CFLAGS+=-DPF
.endif

BINGRP=	kmem
BINMODE=2555
LIBADD=	kvm memstat xo util

.if ${MK_NETGRAPH_SUPPORT} != "no"
SRCS+=	netgraph.c
LIBADD+=	netgraph
CFLAGS+=-DNETGRAPH
.endif

.include <bsd.prog.mk>
