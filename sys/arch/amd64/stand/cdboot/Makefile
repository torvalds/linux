#	$OpenBSD: Makefile,v 1.45 2024/06/30 18:17:09 deraadt Exp $

MAN=	cdboot.8

.if ${MACHINE} == "amd64"
S=	${.CURDIR}/../../../..
SADIR=	${.CURDIR}/..

PROG=	cdboot
SRCS=	srt0.S boot.c conf.c
LD?=	ld
SIZE?=	size
AFLAGS+=-Dendbr64=
LDFLAGS+=-nostdlib -Bstatic -Ttext $(LINKADDR) -N -x -nopie -znorelro
LDFLAGS+=-melf_i386 -L/usr/libdata
INSTALL_STRIP=
BINMODE=644

.PATH:	${SADIR}/libsa
SRCS+=	machdep.c dev_i386.c exec_i386.c cmd_i386.c
SRCS+=	gidt.S mdrandom.c biosdev.c bioscons.c gateA20.c \
	memprobe.c diskprobe.c time.c run_amd64.S
SRCS+=	softraid_amd64.c

.PATH:	${S}/stand/boot
SRCS+=	cmd.c vars.c bootarg.c

.PATH:	${S}/lib/libsa
SRCS+=	alloc.c exit.c getchar.c hexdump.c putchar.c strcmp.c strlen.c \
	strncmp.c memcmp.c memcpy.c memmove.c memset.c printf.c snprintf.c \
	strerror.c strncpy.c strtol.c strtoll.c ctime.c strlcpy.c
SRCS+=	close.c closeall.c dev.c disklabel.c dkcksum.c fchmod.c fstat.c \
	lseek.c open.c read.c stat.c cread.c readdir.c cons.c \
	loadfile.c arc4.c elf32.c elf64.c
SRCS+=	ufs.c ufs2.c cd9660.c
SRCS+=	aes_xts.c bcrypt_pbkdf.c blowfish.c explicit_bzero.c hmac_sha1.c \
	pkcs5_pbkdf2.c rijndael.c sha1.c sha2.c softraid.c

.PATH:	${S}/lib/libkern/arch/i386 ${S}/lib/libkern
SRCS+=	ashldi3.c ashrdi3.c divdi3.c lshrdi3.c moddi3.c qdivrem.c

.PATH:	${S}/lib/libz
SRCS+=	adler32.c crc32.c inflate.c inftrees.c

srt0.o gidt.o: assym.h

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
CPPFLAGS+=-DBOOTMAGIC=$(BOOTMAGIC) ${DEBUGFLAGS}
CPPFLAGS+=-DLINKADDR=${LINKADDR}
CPPFLAGS+=-DSLOW -DSMALL -DNO_GZIP -DDYNAMIC_CRC_TABLE -DBUILDFIXED
CPPFLAGS+=-I${S}/stand/boot
CFLAGS+=-m32
CFLAGS+=${SACFLAGS} -D__INTERNAL_LIBSA_CREAD
CFLAGS+=-DOSREV=\"${OSREV}\" -DMACHINE=\"${MACHINE}\"
CFLAGS+=-DKERNEL=\"/${OSREV}/${MACHINE}/bsd.rd\"
CFLAGS+=-fno-pie
AFLAGS+=${NO_INTEGR_AS}
AFLAGS+=-m32
AFLAGS+=-fno-pie
