#	$OpenBSD: Makefile,v 1.43 2024/06/30 18:17:09 deraadt Exp $

MAN=	pxeboot.8

.if ${MACHINE} == "amd64"
S=	${.CURDIR}/../../../..
SADIR=	${.CURDIR}/..

PROG=	pxeboot
SRCS=	srt0.S conf.c devopen.c open.c
LD?=	ld
SIZE?=	size
AFLAGS+=-Dendbr64=
LDFLAGS+=-nostdlib -Bstatic -Ttext $(LINKADDR) -N -x -nopie -znorelro
LDFLAGS+=-melf_i386 -L/usr/libdata
INSTALL_STRIP=
BINMODE=644

.PATH:	${SADIR}/libsa
# i386 stuff (so, it will possibly load in the same 64k)
SRCS+=	machdep.c exec_i386.c cmd_i386.c run_amd64.S
SRCS+=	gidt.S mdrandom.c biosdev.c bioscons.c gateA20.c \
	memprobe.c diskprobe.c time.c ## biosprobe.c
SRCS+=	pxe.c pxe_call.S pxe_net.c pxe_udp.c
SRCS+=	softraid_amd64.c

.PATH:	${S}/stand/boot
SRCS+=	boot.c cmd.c vars.c bootarg.c

.PATH:	${S}/lib/libsa
SRCS+=	alloc.c exit.c getchar.c getfile.c getln.c globals.c hexdump.c \
	strcmp.c strlen.c strncmp.c memcmp.c memcpy.c memmove.c memset.c \
	printf.c putchar.c \
	snprintf.c strerror.c strncpy.c strtol.c strtoll.c ctime.c strlcpy.c \
	strlcat.c
SRCS+=	aes_xts.c bcrypt_pbkdf.c blowfish.c explicit_bzero.c hmac_sha1.c \
	pkcs5_pbkdf2.c rijndael.c sha1.c sha2.c softraid.c

SRCS+=	close.c closeall.c dev.c disklabel.c dkcksum.c fchmod.c fstat.c \
	ioctl.c lseek.c read.c stat.c write.c cread.c readdir.c cons.c \
	loadfile.c arc4.c elf32.c elf64.c
SRCS+=	ether.c net.c netif.c rpc.c
SRCS+=	bootp.c bootparam.c
SRCS+=	ufs.c ufs2.c nfs.c tftp.c

.PATH:	${S}/lib/libkern/arch/amd64 ${S}/lib/libkern
SRCS+=	ashldi3.c ashrdi3.c divdi3.c lshrdi3.c moddi3.c qdivrem.c
SRCS+=	udivdi3.c umoddi3.c

.PATH:	${S}/lib/libz
SRCS+=	adler32.c crc32.c inflate.c inftrees.c

srt0.o gidt.o pxe_call.o: assym.h

${PROG}: $(OBJS)
	$(LD) $(LDFLAGS) -o ${PROG} $(OBJS)
	@$(SIZE) $(PROG)
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
CPPFLAGS+=-DHEAP_LIMIT=${HEAP_LIMIT} -I${S}/stand/boot #-DCOMPAT_UFS
CFLAGS+=-m32
CFLAGS+=$(SACFLAGS) -D__INTERNAL_LIBSA_CREAD -fno-pie
AFLAGS+=${NO_INTEGR_AS}
# AFLAGS+=-Wa,-a
AFLAGS+=-m32 # -Wa,-R
AFLAGS+=-fno-pie
