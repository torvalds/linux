#	$OpenBSD: Makefile,v 1.37 2024/06/30 18:17:09 deraadt Exp $

MAN=	pxeboot.8

.if ${MACHINE} == "i386"
S=	${.CURDIR}/../../../..
SADIR=	${.CURDIR}/..

PROG=	pxeboot
SRCS=	srt0.S conf.c devopen.c open.c
LD?=	ld
SIZE?=	size
LDFLAGS+=-nostdlib -Bstatic -Ttext $(LINKADDR) -N -x -nopie -znorelro
INSTALL_STRIP=
BINMODE=644

CLEANFILES+=	crt0.o ${PROG}.whole

.PATH:	${SADIR}/libsa
SRCS+=	debug_i386.S gidt.S pslid.S mdrandom.c
SRCS+=	cmd_i386.c exec_i386.c gateA20.c machdep.c
SRCS+=	apmprobe.c bioscons.c biosdev.c debug.c diskprobe.c memprobe.c \
	pciprobe.c ps2probe.c time.c
SRCS+=	pxe_call.S pxe.c pxe_net.c pxe_udp.c
SRCS+=	softraid_i386.c

.PATH:	${S}/stand/boot
SRCS+=	boot.c cmd.c vars.c bootarg.c

.PATH:	${S}/lib/libsa
SRCS+=	alloc.c ctime.c exit.c getchar.c globals.c hexdump.c \
	memcmp.c memmove.c memcpy.c memset.c \
	printf.c putchar.c snprintf.c strcmp.c strerror.c strlen.c strncmp.c \
	strncpy.c strtol.c strtoll.c
SRCS+=	close.c closeall.c cons.c cread.c dev.c disklabel.c dkcksum.c fchmod.c \
	fstat.c lseek.c read.c readdir.c stat.c
SRCS+=	elf32.c elf64.c loadfile.c arc4.c
SRCS+=	nfs.c ufs.c ufs2.c tftp.c
SRCS+=	bootp.c ether.c net.c netif.c rpc.c
SRCS+=	aes_xts.c bcrypt_pbkdf.c blowfish.c explicit_bzero.c hmac_sha1.c \
	pkcs5_pbkdf2.c rijndael.c sha1.c sha2.c softraid.c

.PATH:	${S}/lib/libkern/arch/i386
SRCS+=	ashldi3.c ashrdi3.c divdi3.c lshrdi3.c moddi3.c qdivrem.c udivdi3.c umoddi3.c

.PATH:	${S}/lib/libkern
SRCS+=	strlcpy.c

.PATH:	${S}/lib/libz
SRCS+=	adler32.c crc32.c inflate.c inftrees.c

srt0.o gidt.o pxe_call.o: assym.h

${PROG}: $(OBJS)
	$(LD) $(LDFLAGS) -o ${PROG} $(OBJS)
	@$(SIZE) ${PROG}
	cp ${PROG} ${PROG}.whole
	@if [ -x ${.OBJDIR}/${PROG} ]; then \
		objcopy -O binary ${PROG} ${.OBJDIR}/.tmp;\
		mv -f ${.OBJDIR}/.tmp ${.OBJDIR}/${PROG}; \
		ls -l ${.OBJDIR}/${PROG}; \
	fi

.else
NOPROG=
.endif

.include <bsd.prog.mk>

CPPFLAGS+=-DSOFTRAID
CPPFLAGS+=-DBOOTMAGIC=$(BOOTMAGIC) ${DEBUGFLAGS} -DLINKADDR=${LINKADDR}
CPPFLAGS+=-DSLOW -DSMALL -DNO_GZIP -DDYNAMIC_CRC_TABLE -DBUILDFIXED
CPPFLAGS+=-DHEAP_LIMIT=${HEAP_LIMIT}
CPPFLAGS+=-I${S}/stand/boot
CFLAGS+=$(SACFLAGS) -D__INTERNAL_LIBSA_CREAD -fno-pie
AFLAGS+=${NO_INTEGR_AS}
#AFLAGS+=-Wa,-R
# AFLAGS+=-Wa,-a
AFLAGS+=-fno-pie
