# $FreeBSD$

.PATH: ${SRCTOP}/sys/libkern ${SRCTOP}/sys/sys

KMOD=	libiconv
SRCS=	iconv.c iconv_ucs.c iconv_xlat.c iconv_xlat16.c
SRCS+=	iconv.h
SRCS+=	iconv_converter_if.c iconv_converter_if.h

EXPORT_SYMS=	iconv_add	\
		iconv_open	\
		iconv_close	\
		iconv_conv	\
		iconv_conv_case	\
		iconv_convchr	\
		iconv_convchr_case	\
		iconv_convstr	\
		iconv_convmem	\
		iconv_vfs_refcount

.include <bsd.kmod.mk>
