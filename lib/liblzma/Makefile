# $FreeBSD$

PACKAGE=lib${LIB}
LIB=		lzma
LZMADIR=	${SRCTOP}/contrib/xz/src/liblzma

.PATH: ${LZMADIR:H}/common
SRCS+=	tuklib_physmem.c tuklib_cpucores.c

.PATH: ${LZMADIR}/api/lzma

MAININCS=	../lzma.h
MAININCSDIR=	${INCLUDEDIR}

LZMAINCS+=	base.h \
		bcj.h \
		block.h \
		check.h \
		container.h \
		delta.h \
		filter.h \
		hardware.h \
		index.h \
		index_hash.h \
		lzma12.h \
		stream_flags.h \
		version.h \
		vli.h

LZMAINCSDIR=	${INCLUDEDIR}/lzma

INCSGROUPS=	MAININCS LZMAINCS

.PATH: ${LZMADIR}/common
SRCS+=	common.c \
	block_util.c \
	easy_preset.c \
	filter_common.c \
	hardware_physmem.c \
	hardware_cputhreads.c \
	index.c \
	stream_flags_common.c \
	vli_size.c \
	alone_encoder.c \
	block_buffer_encoder.c \
	block_encoder.c \
	block_header_encoder.c \
	easy_buffer_encoder.c \
	easy_encoder.c \
	easy_encoder_memusage.c \
	filter_buffer_encoder.c \
	filter_encoder.c \
	filter_flags_encoder.c \
	index_encoder.c \
	stream_buffer_encoder.c \
	stream_encoder.c \
	stream_flags_encoder.c \
	vli_encoder.c \
	alone_decoder.c \
	auto_decoder.c \
	block_buffer_decoder.c \
	block_decoder.c \
	block_header_decoder.c \
	easy_decoder_memusage.c \
	filter_buffer_decoder.c \
	filter_decoder.c \
	filter_flags_decoder.c \
	index_decoder.c \
	index_hash.c \
	stream_buffer_decoder.c \
	stream_decoder.c \
	stream_flags_decoder.c \
	stream_encoder_mt.c \
	vli_decoder.c \
	outqueue.c


.PATH: ${LZMADIR}/check
SRCS+=	check.c \
	crc32_table.c \
	crc64_table.c \
	sha256.c
.if defined(MACHINE_ARCH) && ${MACHINE_ARCH} == "i386"
SRCS+=	crc32_x86.S \
	crc64_x86.S
ACFLAGS+=	-Wa,--noexecstack
.else
SRCS+=	crc32_fast.c \
	crc64_fast.c
.endif

.PATH: ${LZMADIR}/lz
SRCS+=	lz_encoder.c \
	lz_encoder_mf.c \
	lz_decoder.c

.PATH: ${LZMADIR}/lzma
SRCS+=	lzma_encoder.c \
	lzma_encoder_presets.c \
	lzma_encoder_optimum_fast.c \
	lzma_encoder_optimum_normal.c \
	fastpos_table.c \
	lzma_decoder.c \
	lzma2_encoder.c \
	lzma2_decoder.c

.PATH: ${LZMADIR}/rangecoder
SRCS+=	price_table.c

.PATH: ${LZMADIR}/delta
SRCS+=	delta_common.c \
	delta_encoder.c \
	delta_decoder.c

.PATH: ${LZMADIR}/simple
SRCS+=	simple_coder.c \
	simple_encoder.c \
	simple_decoder.c \
	x86.c \
	powerpc.c \
	ia64.c \
	arm.c \
	armthumb.c \
	sparc.c

.PATH: ${LZMADIR}

VERSION_MAJOR!=	awk '$$1 == "\#define" && $$2 == "LZMA_VERSION_MAJOR" {print $$3 } ' \
			${LZMADIR}/api/lzma/version.h
VERSION_MINOR!=	awk '$$1 == "\#define" && $$2 == "LZMA_VERSION_MINOR" {print $$3 } ' \
			${LZMADIR}/api/lzma/version.h
VERSION_PATCH!=	awk '$$1 == "\#define" && $$2 == "LZMA_VERSION_PATCH" {print $$3 } ' \
			${LZMADIR}/api/lzma/version.h

WARNS?=	3

CFLAGS+=	-DHAVE_CONFIG_H \
		-DTUKLIB_SYMBOL_PREFIX=lzma_ \
		-I${.CURDIR} \
		-I${LZMADIR}/api \
		-I${LZMADIR}/common \
		-I${LZMADIR}/check \
		-I${LZMADIR}/lz \
		-I${LZMADIR}/rangecoder \
		-I${LZMADIR}/lzma \
		-I${LZMADIR}/delta \
		-I${LZMADIR}/simple \
		-I${LZMADIR:H}/common

LIBADD+=	pthread

VERSION_DEF=	${.CURDIR}/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map
CFLAGS+=	-DSYMBOL_VERSIONING

CLEANFILES+=	liblzma.pc

FILES=		liblzma.pc
FILESDIR=	${LIBDATADIR}/pkgconfig

liblzma.pc: liblzma.pc.in
	sed -e 's,@prefix@,/usr,g ; \
		s,@exec_prefix@,/usr,g  ; \
		s,@libdir@,/usr/lib,g ; \
		s,@includedir@,/usr/include,g ; \
		s,@PACKAGE_URL@,http://tukaani.org/xz/,g ; \
		s,@PACKAGE_VERSION@,${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH},g ; \
		s,@PTHREAD_CFLAGS@,,g ; \
		s,@PTHREAD_LIBS@,,g' ${.ALLSRC} > ${.TARGET}

.include <bsd.lib.mk>
