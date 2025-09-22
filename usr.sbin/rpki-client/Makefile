#	$OpenBSD: Makefile,v 1.37 2025/08/23 09:13:14 job Exp $

PROG=	rpki-client

SRCS+=	as.c
SRCS+=	aspa.c
SRCS+=	ccr.c
SRCS+=	cert.c
SRCS+=	cms.c
SRCS+=	constraints.c
SRCS+=	crl.c
SRCS+=	encoding.c
SRCS+=	filemode.c
SRCS+=	gbr.c
SRCS+=	geofeed.c
SRCS+=	http.c
SRCS+=	io.c
SRCS+=	ip.c
SRCS+=	json.c
SRCS+=	main.c
SRCS+=	mft.c
SRCS+=	mkdir.c
SRCS+=	ometric.c
SRCS+=	output-bgpd.c
SRCS+=	output-bird.c
SRCS+=	output-csv.c
SRCS+=	output-json.c
SRCS+=	output-ometric.c
SRCS+=	output.c
SRCS+=	parser.c
SRCS+=	print.c
SRCS+=	repo.c
SRCS+=	rfc3779.c
SRCS+=	roa.c
SRCS+=	rrdp.c
SRCS+=	rrdp_delta.c
SRCS+=	rrdp_notification.c
SRCS+=	rrdp_snapshot.c
SRCS+=	rrdp_util.c
SRCS+=	rsc.c
SRCS+=	rsync.c
SRCS+=	spl.c
SRCS+=	tak.c
SRCS+=	tal.c
SRCS+=	validate.c
SRCS+=	x509.c

MAN=	rpki-client.8

LDADD+= -lexpat -ltls -lssl -lcrypto -lutil -lz
DPADD+= ${LIBEXPAT} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO} ${LIBUTIL} ${LIBZ}

LDADD+= -lpthread
DPADD+= ${LIBPTHREAD}

CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith
#CFLAGS+= -Wcast-qual
CFLAGS+= -Wsign-compare

.include <bsd.prog.mk>
