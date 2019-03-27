# $FreeBSD$

LIB=	zstd
SRCS=	entropy_common.c \
	error_private.c \
	fse_decompress.c \
	pool.c \
	threading.c \
	xxhash.c \
	zstd_common.c \
	fse_compress.c \
	huf_compress.c \
	zstd_compress.c \
	zstdmt_compress.c \
	huf_decompress.c \
	zstd_ddict.c \
	zstd_decompress.c \
	zstd_decompress_block.c \
	zbuff_common.c \
	zbuff_compress.c \
	zbuff_decompress.c \
	cover.c \
	divsufsort.c \
	zdict.c \
	zstd_fast.c \
	zstd_lazy.c \
	zstd_ldm.c \
	zstd_opt.c \
	zstd_double_fast.c \
	debug.c \
	hist.c \
	fastcover.c
WARNS=	2
INCS=	zstd.h
CFLAGS+=	-I${ZSTDDIR}/lib -I${ZSTDDIR}/lib/common -DXXH_NAMESPACE=ZSTD_ \
		-DZSTD_MULTITHREAD=1
LIBADD=	pthread

PRIVATELIB=	yes

ZSTDDIR=	${SRCTOP}/sys/contrib/zstd
.PATH:	${ZSTDDIR}/lib/common ${ZSTDDIR}/lib/compress \
	${ZSTDDIR}/lib/decompress ${ZSTDDIR}/lib/deprecated \
	${ZSTDDIR}/lib/dictBuilder ${ZSTDDIR}/lib

# Work around for LLVM bug 35023, https://bugs.llvm.org/show_bug.cgi?id=35023
.if ${MACHINE_ARCH:Marm*} != ""
CFLAGS.zstd_compress.c+= -O0
.endif

# Work around lack of __bswap[sd]i2() intrinsics on riscv.  Probably should be
# added to one of the runtime C libraries instead.
.if ${MACHINE_CPUARCH} == "riscv"
.PATH:	${ZSTDDIR}/lib/freebsd
SRCS+=	zstd_kfreebsd.c
.endif

.include <bsd.lib.mk>
