#	$OpenBSD: Makefile,v 1.50 2024/06/30 18:17:09 deraadt Exp $

COPTS?=
MAN?=	boot.8

.if !empty(COPTS:M-DFDBOOT)
SOFTRAID?=no
.else
SOFTRAID?=yes
.endif

.if ${MACHINE} == "amd64"
S=	${.CURDIR}/../../../..
SADIR=	${.CURDIR}/..

PROG?=	boot
SRCS=	srt0.S conf.c
LD?=	ld
SIZE?=	size
AFLAGS+=-Dendbr64=
LDFLAGS+=-nostdlib -Bstatic -Ttext $(LINKADDR) -N -x -nopie -znorelro
LDFLAGS+=-melf_i386 -L/usr/libdata

.PATH:	${S}/stand/boot
SRCS+=	boot.c bootarg.c cmd.c vars.c

.PATH:	${SADIR}/libsa
SRCS+=	gidt.S mdrandom.c run_amd64.S
SRCS+=	cmd_i386.c dev_i386.c exec_i386.c gateA20.c machdep.c
SRCS+=	bioscons.c biosdev.c diskprobe.c memprobe.c time.c
.if ${SOFTRAID:L} == "yes"
SRCS+=	softraid_amd64.c
.endif

.PATH:	${S}/lib/libsa
SRCS+=	alloc.c ctime.c exit.c getchar.c hexdump.c \
	memcmp.c memcpy.c memmove.c memset.c printf.c \
	putchar.c snprintf.c strcmp.c strerror.c strlen.c strncmp.c strncpy.c \
	strtol.c strtoll.c
SRCS+=	close.c closeall.c cons.c cread.c dev.c disklabel.c dkcksum.c fchmod.c \
	fstat.c lseek.c open.c read.c readdir.c stat.c
SRCS+=	elf32.c elf64.c loadfile.c arc4.c
SRCS+=	ufs.c
.if empty(COPTS:M-DFDBOOT)
SRCS+=	ufs2.c
.endif
.if ${SOFTRAID:L} == "yes"
SRCS+=	aes_xts.c bcrypt_pbkdf.c blowfish.c explicit_bzero.c hmac_sha1.c \
	pkcs5_pbkdf2.c rijndael.c sha1.c sha2.c softraid.c
.endif

.PATH:	${S}/lib/libkern
SRCS+=	ashldi3.c ashrdi3.c divdi3.c lshrdi3.c moddi3.c qdivrem.c
SRCS+=	strlcpy.c

.PATH:	${S}/lib/libz
SRCS+=	adler32.c crc32.c inflate.c inftrees.c

boot.bin: boot
	objcopy -v -O binary ${PROG} boot.bin

CLEANFILES+= ${PROG}.new

srt0.o gidt.o: assym.h

${PROG}: $(OBJS)
	$(LD) $(LDFLAGS) -o ${PROG}.new $(OBJS)
	@perl ${SADIR}/boot/check-boot.pl ${PROG}.new
	@mv ${PROG}.new ${PROG}
	@$(SIZE) ${PROG}

.else
NOPROG=
.endif

.include <bsd.prog.mk>

.if ${SOFTRAID:L} == "yes"
CPPFLAGS+=-DSOFTRAID
.endif
CPPFLAGS+=-DBOOTMAGIC=$(BOOTMAGIC) ${DEBUGFLAGS} -DLINKADDR=${LINKADDR}
CPPFLAGS+=-DSLOW -DSMALL -DNO_GZIP -DDYNAMIC_CRC_TABLE -DBUILDFIXED
CPPFLAGS+=-DHIBERNATE
CPPFLAGS+=-DHEAP_LIMIT=${HEAP_LIMIT} -I${S}/stand/boot #-DCOMPAT_UFS
CFLAGS+=-m32 $(SACFLAGS) -D__INTERNAL_LIBSA_CREAD -fno-pie
AFLAGS+=${NO_INTEGR_AS}
AFLAGS+=-m32 # -Wa,-R
# AFLAGS+=-Wa,-a
AFLAGS+=-fno-pie
