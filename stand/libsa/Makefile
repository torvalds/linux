# $FreeBSD$
# Originally from	$NetBSD: Makefile,v 1.21 1997/10/26 22:08:38 lukem Exp $
#
# Notes:
# - We don't use the libc strerror/sys_errlist because the string table is
#   quite large.
#

.include <bsd.init.mk>

LIBSA_CPUARCH?=${MACHINE_CPUARCH}
LIBC_SRC=	${SRCTOP}/lib/libc

LIB?=		sa

# standalone components and stuff we have modified locally
SRCS+=	gzguts.h zutil.h __main.c abort.c assert.c bcd.c environment.c getopt.c gets.c \
	globals.c pager.c panic.c printf.c strdup.c strerror.c \
	random.c sbrk.c twiddle.c zalloc.c zalloc_malloc.c

# private (pruned) versions of libc string functions
SRCS+=	strcasecmp.c

.PATH: ${LIBC_SRC}/net

SRCS+= ntoh.c

# string functions from libc
.PATH: ${LIBC_SRC}/string
SRCS+=	bcmp.c bcopy.c bzero.c ffs.c fls.c \
	memccpy.c memchr.c memcmp.c memcpy.c memmove.c memset.c \
	qdivrem.c strcat.c strchr.c strcmp.c strcpy.c stpcpy.c stpncpy.c \
	strcspn.c strlcat.c strlcpy.c strlen.c strncat.c strncmp.c strncpy.c \
	strnlen.c strpbrk.c strrchr.c strsep.c strspn.c strstr.c strtok.c swab.c

# stdlib functions from libc
.PATH: ${LIBC_SRC}/stdlib
SRCS+=	abs.c strtol.c strtoll.c strtoul.c strtoull.c

# common boot code
.PATH:	${SYSDIR}/kern
SRCS+=	subr_boot.c

.if ${MACHINE_CPUARCH} == "arm"
.PATH: ${LIBC_SRC}/arm/gen

# Do not generate movt/movw, because the relocation fixup for them does not
# translate to the -Bsymbolic -pie format required by self_reloc() in loader(8).
# Also, the fpu is not available in a standalone environment.
.if ${COMPILER_VERSION} < 30800
CFLAGS.clang+=	-mllvm -arm-use-movt=0
.else
CFLAGS.clang+=	-mno-movt
.endif
CFLAGS.clang+=	-mfpu=none

# Compiler support functions
.PATH: ${SRCTOP}/contrib/compiler-rt/lib/builtins/
# __clzsi2 and ctzsi2 for various builtin functions
SRCS+=	clzsi2.c ctzsi2.c
# Divide and modulus functions called by the compiler
SRCS+=	 divmoddi4.c  divmodsi4.c  divdi3.c  divsi3.c  moddi3.c  modsi3.c
SRCS+=	udivmoddi4.c udivmodsi4.c udivdi3.c udivsi3.c umoddi3.c umodsi3.c

.PATH: ${SRCTOP}/contrib/compiler-rt/lib/builtins/arm/
SRCS+=	aeabi_idivmod.S aeabi_ldivmod.S aeabi_uidivmod.S aeabi_uldivmod.S
SRCS+=	aeabi_memcmp.S aeabi_memcpy.S aeabi_memmove.S aeabi_memset.S
.endif

.if ${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "riscv"
.PATH: ${LIBC_SRC}/${MACHINE_CPUARCH}/gen
.endif

.if ${MACHINE_CPUARCH} == "powerpc"
.PATH: ${LIBC_SRC}/quad
SRCS+=	ashldi3.c ashrdi3.c lshrdi3.c
SRCS+=	syncicache.c
.endif

.if ${MACHINE_CPUARCH} == "mips"
.PATH: ${LIBC_SRC}/quad
SRCS+=	ashldi3.c ashrdi3.c lshrdi3.c
.endif

# uuid functions from libc
.PATH: ${LIBC_SRC}/uuid
SRCS+= uuid_create_nil.c uuid_equal.c uuid_from_string.c uuid_is_nil.c uuid_to_string.c

# _setjmp/_longjmp
.PATH: ${SASRC}/${LIBSA_CPUARCH}
SRCS+=	_setjmp.S

# decompression functionality from libbz2
# NOTE: to actually test this functionality after libbz2 upgrade compile
# loader(8) with LOADER_BZIP2_SUPPORT defined
.PATH: ${SRCTOP}/contrib/bzip2
CFLAGS+= -DBZ_NO_STDIO -DBZ_NO_COMPRESS

SRCS+=bzlib.c crctable.c decompress.c huffman.c randtable.c

# decompression functionality from zlib
.PATH: ${SRCTOP}/contrib/zlib
CFLAGS+=-DHAVE_MEMCPY -I${SRCTOP}/contrib/zlib
SRCS+=	adler32.c crc32.c
SRCS+=	infback.c inffast.c inflate.c inftrees.c zutil.c

# Create a subset of includes that are safe, as well as adjusting those that aren't
# The lists may drive people nuts, but they are explicitly opt-in
FAKE_DIRS=xlocale arpa
SAFE_INCS=a.out.h assert.h elf.h limits.h nlist.h setjmp.h stddef.h stdbool.h string.h strings.h time.h unistd.h uuid.h
STAND_H_INC=ctype.h fcntl.h signal.h stdio.h stdlib.h
OTHER_INC=stdarg.h errno.h stdint.h

beforedepend:
	mkdir -p ${FAKE_DIRS}; \
	for i in ${SAFE_INCS}; do \
		ln -sf ${SRCTOP}/include/$$i $$i; \
	done; \
	ln -sf ${SYSDIR}/${MACHINE}/include/stdarg.h stdarg.h; \
	ln -sf ${SYSDIR}/sys/errno.h errno.h; \
	ln -sf ${SYSDIR}/sys/stdint.h stdint.h; \
	ln -sf ${SRCTOP}/include/arpa/inet.h arpa/inet.h; \
	ln -sf ${SRCTOP}/include/arpa/tftp.h arpa/tftp.h; \
	for i in _time.h _strings.h _string.h; do \
		[ -f xlocale/$$i ] || cp /dev/null xlocale/$$i; \
	done; \
	for i in ${STAND_H_INC}; do \
		ln -sf ${SASRC}/stand.h $$i; \
	done
CLEANDIRS+=${FAKE_DIRS}
CLEANFILES+= ${SAFE_INCS} ${STAND_H_INC} ${OTHER_INC}

# io routines
SRCS+=	closeall.c dev.c ioctl.c nullfs.c stat.c \
	fstat.c close.c lseek.c open.c read.c write.c readdir.c

# network routines
SRCS+=	arp.c ether.c ip.c inet_ntoa.c in_cksum.c net.c udp.c netif.c rpc.c

# network info services:
SRCS+=	bootp.c rarp.c bootparam.c

# boot filesystems
SRCS+=	ufs.c nfs.c cd9660.c tftp.c gzipfs.c bzipfs.c
SRCS+=	dosfs.c ext2fs.c
SRCS+=	splitfs.c
SRCS+=	pkgfs.c
.if ${MK_NAND} != "no"
SRCS+=	nandfs.c
.endif

# kernel ufs support
.PATH: ${SRCTOP}/sys/ufs/ffs
SRCS+=ffs_subr.c ffs_tables.c

CFLAGS.bzipfs.c+= -I${SRCTOP}/contrib/bzip2

# explicit_bzero and calculate_crc32c
.PATH: ${SYSDIR}/libkern
SRCS+=  explicit_bzero.c crc32_libkern.c

# Maybe GELI
.if ${MK_LOADER_GELI} == "yes"
.include "${SASRC}/geli/Makefile.inc"
.endif

.if ${MK_LOADER_VERIEXEC} == "yes" && ${MK_BEARSSL} == "yes"
.include "${SRCTOP}/lib/libbearssl/Makefile.libsa.inc"
.include "${SRCTOP}/lib/libsecureboot/Makefile.libsa.inc"
.endif

# Maybe ZFS
.if ${MK_LOADER_ZFS} == "yes"
.include "${SASRC}/zfs/Makefile.inc"
.endif

.include <bsd.lib.mk>
