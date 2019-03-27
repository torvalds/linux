# $FreeBSD$

.include <bsd.init.mk>

LIB=		ofw

SRCS=	devicename.c ofw_console.c ofw_copy.c ofw_disk.c \
	ofw_memory.c ofw_module.c ofw_net.c ofw_reboot.c \
	ofw_time.c openfirm.c
.PATH:	${ZFSSRC}
SRCS+=  devicename_stubs.c
CFLAGS+=	-I${ZFSSRC}

# Pick up the bootstrap header for some interface items
CFLAGS+=	-I${LDRSRC}

.ifdef(BOOT_DISK_DEBUG)
# Make the disk code more talkative
CFLAGS+= -DDISK_DEBUG
.endif

.include <bsd.lib.mk>
