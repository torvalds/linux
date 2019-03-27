# $FreeBSD$

PACKAGE=lib${LIB}
SHLIBDIR?=	/lib

.include <src.opts.mk>

PTSRC=	${SRCTOP}/contrib/processor-trace

.PATH:	${PTSRC}/libipt/src			\
	${PTSRC}/libipt/src/posix		\
	${PTSRC}/libipt/internal/include	\
	${PTSRC}/libipt/include			\
	${PTSRC}/include

LIB=	ipt
SHLIB_MAJOR=0

SRCS=	\
	init.c				\
	pt_asid.c			\
	pt_block_cache.c		\
	pt_block_decoder.c		\
	pt_config.c			\
	pt_cpu.c			\
	pt_cpuid.c			\
	pt_decoder_function.c		\
	pt_encoder.c			\
	pt_error.c			\
	pt_event_queue.c		\
	pt_ild.c			\
	pt_image_section_cache.c	\
	pt_image.c			\
	pt_insn_decoder.c		\
	pt_insn.c			\
	pt_last_ip.c			\
	pt_packet_decoder.c		\
	pt_packet.c			\
	pt_query_decoder.c		\
	pt_retstack.c			\
	pt_section_file.c		\
	pt_section_posix.c		\
	pt_section.c			\
	pt_sync.c			\
	pt_time.c			\
	pt_tnt_cache.c			\
	pt_version.c

CFLAGS+=	\
	-I${PTSRC}/libipt/internal/include/posix	\
	-I${PTSRC}/libipt/internal/include		\
	-I${PTSRC}/libipt/include			\
	-I${PTSRC}/include				\
	-I${.CURDIR}

CFLAGS+=	\
	-DPT_VERSION_BUILD=0	\
	-DPT_VERSION_EXT=\"\"	\
	-DPT_VERSION_MAJOR=1	\
	-DPT_VERSION_MINOR=6

INCS=	\
	intel-pt.h	\
	pt_cpu.h	\
	pt_last_ip.h	\
	pt_time.h	\
	pt_compiler.h

INCSDIR=${INCLUDEDIR}/libipt

LIBADD=	

WARNS?= 1

HAS_TESTS=

.include <bsd.lib.mk>
