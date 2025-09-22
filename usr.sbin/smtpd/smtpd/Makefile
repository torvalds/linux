#	$OpenBSD: Makefile,v 1.115 2024/05/07 09:10:33 op Exp $

.PATH:		${.CURDIR}/..

PROG=		smtpd

SRCS=	aliases.c
SRCS+=	bounce.c
SRCS+=	ca.c
SRCS+=	compress_backend.c
SRCS+=	config.c
SRCS+=	control.c
SRCS+=	crypto.c
SRCS+=	dict.c
SRCS+=	dns.c
SRCS+=	unpack_dns.c
SRCS+=	envelope.c
SRCS+=	esc.c
SRCS+=	expand.c
SRCS+=	forward.c
SRCS+=	iobuf.c
SRCS+=	ioev.c
SRCS+=	limit.c
SRCS+=	lka.c
SRCS+=	lka_filter.c
SRCS+=	lka_session.c
SRCS+=	log.c
SRCS+=	mailaddr.c
SRCS+=	mda.c
SRCS+=	mda_mbox.c
SRCS+=	mda_unpriv.c
SRCS+=	mda_variables.c
SRCS+=	mproc.c
SRCS+=	mta.c
SRCS+=	mta_session.c
SRCS+=	parse.y
SRCS+=	dispatcher.c
SRCS+=	proxy.c
SRCS+=	queue.c
SRCS+=	queue_backend.c
SRCS+=	report_smtp.c
SRCS+=	resolver.c
SRCS+=	rfc5322.c
SRCS+=	ruleset.c
SRCS+=	runq.c
SRCS+=	scheduler.c
SRCS+=	scheduler_backend.c
SRCS+=	smtp.c
SRCS+=	smtp_session.c
SRCS+=	smtpd.c
SRCS+=	srs.c
SRCS+=	ssl.c
SRCS+=	stat_backend.c
SRCS+=	table.c
SRCS+=	to.c
SRCS+=	tree.c
SRCS+=	util.c
SRCS+=	waitq.c

# backends
SRCS+=		compress_gzip.c

SRCS+=		table_db.c
SRCS+=		table_getpwnam.c
SRCS+=		table_proc.c
SRCS+=		table_static.c

SRCS+=		queue_fs.c
SRCS+=		queue_null.c
SRCS+=		queue_proc.c
SRCS+=		queue_ram.c

SRCS+=		scheduler_ramqueue.c
SRCS+=		scheduler_null.c
SRCS+=		scheduler_proc.c

SRCS+=		stat_ramstat.c

MAN=		sendmail.8 smtpd.8 smtpd-filters.7 smtpd-tables.7
MAN+=		smtpd.conf.5 table.5

BINDIR=		/usr/sbin

LDADD+=		-levent -lutil -ltls -lssl -lcrypto -lz
DPADD+=		${LIBEVENT} ${LIBUTIL} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO} ${LIBZ}

CFLAGS+=	-fstack-protector-all
CFLAGS+=	-I${.CURDIR}/..
CFLAGS+=	-Wall -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare
CFLAGS+=	-Werror-implicit-function-declaration
#CFLAGS+=	-Werror # during development phase (breaks some archs)
CFLAGS+=	-DIO_TLS
CFLAGS+=	-DQUEUE_PROFILING
YFLAGS=

.include <bsd.prog.mk>
