# $FreeBSD$

.include <src.opts.mk>

PACKAGE=sendmail
SENDMAIL_DIR=${SRCTOP}/contrib/sendmail
.PATH:	${SENDMAIL_DIR}/libsm

CFLAGS+=-I${SENDMAIL_DIR}/src -I${SENDMAIL_DIR}/include -I.
CFLAGS+=-DNEWDB -DNIS -DMAP_REGEX -DNOT_SENDMAIL
CFLAGS+=-DHAVE_NANOSLEEP

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DNETINET6
.endif

# User customizations to the sendmail build environment
CFLAGS+=${SENDMAIL_CFLAGS}

WARNS?=	2

LIB=	sm

SRCS+=	sm_os.h
SRCS+=	assert.c debug.c errstring.c exc.c heap.c match.c rpool.c \
	strdup.c strerror.c strl.c clrerr.c fclose.c feof.c ferror.c \
	fflush.c fget.c fpos.c findfp.c flags.c fopen.c fprintf.c \
	fpurge.c fput.c fread.c fscanf.c fseek.c fvwrite.c fwalk.c \
	fwrite.c get.c makebuf.c put.c refill.c rewind.c setvbuf.c \
	smstdio.c snprintf.c sscanf.c stdio.c strio.c ungetc.c \
	vasprintf.c vfprintf.c vfscanf.c vprintf.c vsnprintf.c \
	wbuf.c wsetup.c string.c stringf.c \
	xtrap.c strto.c test.c strcasecmp.c strrevcmp.c \
	signal.c clock.c config.c sem.c shm.c mbdb.c strexit.c cf.c ldap.c \
	niprop.c mpeix.c memstat.c util.c inet6_ntop.c
CLEANFILES+=sm_os.h

INTERNALLIB=

sm_os.h: ${SENDMAIL_DIR}/include/sm/os/sm_os_freebsd.h .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}

.include <bsd.lib.mk>
