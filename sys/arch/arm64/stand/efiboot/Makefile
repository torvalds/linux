#	$OpenBSD: Makefile,v 1.23 2024/06/30 18:17:09 deraadt Exp $

NOMAN=		#

.if ${MACHINE} == "arm64"

PROG=		BOOTAA64.EFI
OBJFMT=		binary
INSTALL_STRIP=
BINDIR=		/usr/mdec
SRCS=		start.S self_reloc.c efiboot.c conf.c exec.c efiacpi.c
SRCS+=		efidev.c efipxe.c efirng.c fdt.c smbios.c
SRCS+=		softraid_arm64.c

S=		${.CURDIR}/../../../..
EFIDIR=		${S}/stand/efi

OBJCOPY?=	objcopy
OBJDUMP?=	objdump

LDFLAGS+=-nostdlib -T ${.CURDIR}/ldscript.arm64 -Bsymbolic -shared
LDFLAGS+=--pack-dyn-relocs=none --no-execute-only

.PATH:	${S}/stand/boot
SRCS+=	boot.c cmd.c vars.c

.PATH:	${S}/lib/libsa
SRCS+=	alloc.c ctime.c exit.c getchar.c hexdump.c \
	memcmp.c memcpy.c memmove.c memset.c \
	printf.c putchar.c snprintf.c strchr.c strcmp.c strerror.c strncmp.c \
	strncpy.c strtol.c strtoll.c
SRCS+=	close.c closeall.c cons.c cread.c dev.c disklabel.c dkcksum.c fchmod.c \
	fstat.c lseek.c open.c read.c readdir.c stat.c
SRCS+=	loadfile.c arc4.c
SRCS+=	ufs.c ufs2.c cd9660.c
SRCS+=	arp.c ether.c globals.c in_cksum.c net.c netif.c netudp.c tftp.c
SRCS+=	aes_xts.c bcrypt_pbkdf.c blowfish.c explicit_bzero.c hmac_sha1.c \
	pkcs5_pbkdf2.c rijndael.c sha1.c sha2.c softraid.c

.PATH:	${S}/lib/libkern/arch/arm64 ${S}/lib/libkern
SRCS+=	divdi3.c moddi3.c qdivrem.c strlcat.c strlcpy.c strlen.c

.PATH:	${S}/lib/libz
SRCS+=	adler32.c crc32.c inflate.c inftrees.c

CPPFLAGS+=	-nostdinc
CPPFLAGS+=	-I${S} -I. -I${.CURDIR}
CPPFLAGS+=	-I${EFIDIR}/include -I${EFIDIR}/include/arm64
CPPFLAGS+=	-D_STANDALONE -DBOOT_STTY
CPPFLAGS+=	-DSMALL -DSLOW -D__INTERNAL_LIBSA_CREAD
CPPFLAGS+=	-DNEEDS_HEAP_H -DMDRANDOM -DFWRANDOM
COPTS+=		-Wno-attributes -Wno-format
COPTS+=		-ffreestanding -fno-stack-protector
COPTS+=		-fshort-wchar -fPIC -fno-builtin
COPTS+=		-Wall -Werror

PROG.elf=	${PROG:S/.EFI/.elf/}
CLEANFILES+=	${PROG.elf} ${PROG.elf}.tmp

${PROG}: ${PROG.elf}
	${OBJCOPY} -j .peheader -j .text -j .sdata -j .data \
		-j .dynamic -j .dynsym -j .dynstr -j .rel -j .rel.dyn \
		-j .rela -j .rela.dyn -j .reloc \
		--output-target=${OBJFMT} ${PROG.elf} ${.TARGET}

.include <bsd.prog.mk>

${PROG.elf}: ${OBJS}
	${LD} ${LDFLAGS} -o ${.TARGET}.tmp ${OBJS} ${LDADD}
	@if ${OBJDUMP} -t ${.TARGET}.tmp | grep 'UND'; then	\
		(echo Undefined symbols; false);		\
	fi
	mv ${.TARGET}.tmp ${.TARGET}

.if !make(clean) && !make(cleandir) && !make(includes) && !make(obj)
.BEGIN:
	@([ -h machine ] || ln -s ${.CURDIR}/../../../${MACHINE}/include machine)
.NOPATH: machine
CLEANFILES+=	machine
.endif

.else
NOPROG=yes
.include <bsd.prog.mk>
.endif

DTC=	dtc
DTCOPTS= -p 16384 -H epapr
update-blob:
	${DTC} ${DTCOPTS} -O asm -o ${.CURDIR}/dt_blob.S ${.CURDIR}/acpi.dts
