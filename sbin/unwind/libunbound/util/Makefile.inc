#	$OpenBSD: Makefile.inc,v 1.6 2023/09/05 15:44:02 florian Exp $

.PATH: ${.CURDIR}/libunbound/util

SRCS+=	alloc.c as112.c config_file.c configlexer.c configparser.y edns.c \
	fptr_wlist.c locks.c mini_event.c module.c net_help.c netevent.c \
	proxy_protocol.c random.c rbtree.c regional.c rfc_1982.c rtt.c siphash.c \
	tcp_conn_limit.c timehist.c timeval_func.c tube.c ub_event_pluggable.c \
	util_log.c winsock_event.c

util_log.c:
	ln -s ${.CURDIR}/libunbound/util/log.c $@

CLEANFILES+=	util_log.c
