# $FreeBSD$
PROG=	mfiutil

SRCS=	mfiutil.c mfi_bbu.c mfi_cmd.c mfi_config.c mfi_drive.c mfi_evt.c \
	mfi_flash.c mfi_patrol.c mfi_show.c mfi_volume.c mfi_foreign.c \
	mfi_properties.c
MAN8=	mfiutil.8

CFLAGS.gcc+= -fno-builtin-strftime

LIBADD=	util

# Here be dragons
.ifdef DEBUG
CFLAGS+= -DDEBUG
.endif

.include <bsd.prog.mk>
