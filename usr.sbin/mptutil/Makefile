# $FreeBSD$

PROG=	mptutil
SRCS=	mptutil.c mpt_cam.c mpt_cmd.c mpt_config.c mpt_drive.c mpt_evt.c \
	mpt_show.c mpt_volume.c
#	mpt_flash.c
MAN=	mptutil.8

WARNS?= 3

LIBADD=	cam util

# Here be dragons
.ifdef DEBUG
CFLAGS+= -DDEBUG
.endif

.include <bsd.prog.mk>
