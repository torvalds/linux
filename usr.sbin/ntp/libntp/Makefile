# $FreeBSD$

.PATH: ${SRCTOP}/contrib/ntp/libntp \
	${SRCTOP}/contrib/ntp/lib/isc \
	${SRCTOP}/contrib/ntp/lib/isc/nls \
	${SRCTOP}/contrib/ntp/lib/isc/pthreads \
	${SRCTOP}/contrib/ntp/lib/isc/unix \

LIB= ntp
INTERNALLIB=

NTP_SRCS= systime.c	a_md5encrypt.c	adjtime.c	atoint.c \
	atolfp.c	atouint.c	audio.c	authkeys.c \
	authreadkeys.c	authusekey.c	bsd_strerror.c	buftvtots.c \
	caljulian.c	caltontp.c	calyearstart.c	clocktime.c \
	clocktypes.c	decodenetnum.c	dofptoa.c	dolfptoa.c \
	emalloc.c	findconfig.c	getopt.c	hextoint.c \
	hextolfp.c	humandate.c	icom.c		iosignal.c \
	is_ip_address.c \
	lib_strbuf.c	\
	libssl_compat.c \
	machines.c	mktime.c	modetoa.c \
	mstolfp.c	msyslog.c	netof.c		ntp_calendar.c \
	ntp_crypto_rnd.c		ntp_intres.c	ntp_libopts.c \
	ntp_lineedit.c	ntp_random.c	ntp_rfc2553.c	ntp_worker.c \
	numtoa.c	numtohost.c	octtoint.c	prettydate.c \
	recvbuff.c	refidsmear.c	\
	refnumtoa.c	snprintf.c	socket.c \
	socktoa.c	socktohost.c	ssl_init.c	statestr.c \
	strdup.c	strl_obsd.c	syssignal.c	timetoa.c \
	timevalops.c	uglydate.c	vint64ops.c	work_fork.c \
	work_thread.c	xsbprintf.c	ymd2yd.c

ISC_PTHREADS_SRCS= condition.c \
	thread.c \
	mutex.c

ISC_UNIX_SRCS= dir.c \
	errno2result.c \
	file.c \
	interfaceiter.c \
	net.c \
	stdio.c \
	stdtime.c \
	strerror.c \
	time.c \
	tsmemcmp.c

ISC_NLS_SRCS= msgcat.c

ISC_SRCS= assertions.c \
	buffer.c \
	backtrace-emptytbl.c \
	backtrace.c \
	error.c \
	event.c \
	inet_ntop.c \
	inet_pton.c \
	lib.c \
	log.c \
	md5.c \
	netaddr.c \
	netscope.c \
	ondestroy.c \
	random.c \
	result.c \
	task.c \
	sha1.c \
	sockaddr.c \
	${ISC_NLS_SRCS} \
	${ISC_PTHREADS_SRCS} \
	${ISC_UNIX_SRCS}

SRCS=	${NTP_SRCS} ${ISC_SRCS} version.c

CFLAGS+= -I${SRCTOP}/contrib/ntp/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/unix/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/pthreads/include \
	-I${SRCTOP}/contrib/ntp/sntp/libopts \
	-I${SRCTOP}/lib/libc/${MACHINE_ARCH} \
	-I${SRCTOP}/lib/libedit/edit \
	-I${.CURDIR:H} \
	-I${.CURDIR}/

CFLAGS+= -DHAVE_BSD_NICE -DHAVE_STDINT_H

CLEANFILES+= .version version.c

version.c:
	sh -e ${.CURDIR:H}/scripts/mkver ntpd

.include <bsd.lib.mk>
