#	$FreeBSD$
#

.PATH:	${SRCDIR}/cd9660

CFLAGS+=-I${SRCTOP}/sys/fs/cd9660/

SRCS+=	cd9660_strings.c cd9660_debug.c cd9660_eltorito.c \
	cd9660_write.c cd9660_conversion.c iso9660_rrip.c cd9660_archimedes.c
