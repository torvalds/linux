# $FreeBSD$

.PATH: ${SRCTOP}/sys/kern

KMOD=	libmchain
SRCS=	subr_mchain.c

EXPORT_SYMS=	mb_init		\
		mb_initm	\
		mb_done		\
		mb_detach	\
		mb_fixhdr	\
		mb_reserve	\
		mb_put_padbyte	\
		mb_put_uint8	\
		mb_put_uint16be	\
		mb_put_uint16le	\
		mb_put_uint32be	\
		mb_put_uint32le	\
		mb_put_int64be	\
		mb_put_int64le	\
		mb_put_mem	\
		mb_put_mbuf	\
		mb_put_uio	\
		md_init		\
		md_initm	\
		md_done		\
		md_append_record \
		md_next_record	\
		md_get_uint8	\
		md_get_uint16	\
		md_get_uint16le	\
		md_get_uint16be	\
		md_get_uint32	\
		md_get_uint32be	\
		md_get_uint32le	\
		md_get_int64	\
		md_get_int64be	\
		md_get_int64le	\
		md_get_mem	\
		md_get_mbuf	\
		md_get_uio	\

.include <bsd.kmod.mk>
