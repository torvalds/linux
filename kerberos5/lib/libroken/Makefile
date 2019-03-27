# $FreeBSD$

LIB=	roken
LIBADD=	crypt
VERSION_MAP=	${KRB5DIR}/lib/roken/version-script.map
INCS=	roken.h \
	roken-common.h \
	base64.h \
	getarg.h \
	hex.h \
	parse_bytes.h \
	parse_time.h \
	parse_units.h \
	resolve.h \
	rtbl.h \
	xdbm.h

SRCS=	base64.c \
	copyhostent.c \
	ecalloc.c \
	emalloc.c \
	erealloc.c \
	estrdup.c \
	strlwr.c \
	strsep_copy.c \
	strupr.c \
	bswap.c \
	cloexec.c \
	concat.c \
	ct.c \
	doxygen.c \
	dumpdata.c \
	environment.c \
	eread.c \
	esetenv.c \
	ewrite.c \
	get_default_username.c \
	get_window_size.c \
	getaddrinfo_hostspec.c \
	getarg.c \
	getnameinfo_verified.c \
	getprogname.c \
	h_errno.c \
	hex.c \
	hostent_find_fqdn.c \
	issuid.c \
	k_getpwnam.c \
	k_getpwuid.c \
	mini_inetd.c \
	net_read.c \
	net_write.c \
	parse_bytes.c \
	parse_time.c \
	parse_units.c \
	rand.c \
	realloc.c \
	resolve.c \
	roken.h \
	roken_gethostby.c \
	rtbl.c \
	setprogname.c \
	signal.c \
	simple_exec.c \
	snprintf.c \
	socket.c \
	strcollect.c \
	strerror_r.c \
	strpool.c \
	timeval.c \
	tm2time.c \
	unvis.c \
	verify.c \
	vis.c \
	warnerr.c \
	write_pid.c \
	xfree.c

CFLAGS+=-I${KRB5DIR}/lib/roken -I.

CLEANFILES= roken.h

roken.h:
	${MAKE_ROKEN} > ${.TARGET}

.include <bsd.lib.mk>

.PATH: ${KRB5DIR}/lib/roken
