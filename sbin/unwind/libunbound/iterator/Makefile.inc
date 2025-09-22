#	$OpenBSD: Makefile.inc,v 1.1 2019/01/23 13:05:27 florian Exp $

.PATH: ${.CURDIR}/libunbound/iterator

SRCS+=	iter_delegpt.c iter_donotq.c iter_fwd.c iter_hints.c iter_priv.c \
	iter_resptype.c iter_scrub.c iter_utils.c iterator.c
