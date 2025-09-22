#	$OpenBSD: Makefile,v 1.45 2023/12/12 15:49:21 claudio Exp $
#	$NetBSD: Makefile,v 1.8 1996/05/16 07:03:28 thorpej Exp $

LIB=	util

VERSION_SCRIPT=	${.CURDIR}/Symbols.map

HDRS=	ber.h util.h imsg.h
SRCS=	bcrypt_pbkdf.c ber.c check_expire.c duid.c getmaxpartitions.c \
	getrawpartition.c login.c \
	login_tty.c logout.c logwtmp.c opendev.c passwd.c pty.c readlabel.c \
	login_fbtab.c uucplock.c fparseln.c opendisk.c pidfile.c \
	fmt_scaled.c imsg.c imsg-buffer.c pkcs5_pbkdf2.c

MAN=	bcrypt_pbkdf.3 \
	check_expire.3 getmaxpartitions.3 \
	getrawpartition.3 \
	isduid.3 login.3 \
	ober_add_string.3 ober_get_string.3 ober_oid_cmp.3 \
	ober_read_elements.3 ober_set_header.3 \
	opendev.3 openpty.3 pw_init.3 pw_lock.3 readlabelfs.3 uu_lock.3 \
	fparseln.3 opendisk.3 login_fbtab.3 pidfile.3 fmt_scaled.3 imsg_init.3 \
	ibuf_add.3 pkcs5_pbkdf2.3

SRCS+=	ohash.c
HDRS += ohash.h

MAN +=	ohash_init.3 ohash_interval.3

includes:
	@cd ${.CURDIR}; for i in $(HDRS); do \
	    j="cmp -s $$i ${DESTDIR}/usr/include/$$i || \
		${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} \
		-m 444 $$i ${DESTDIR}/usr/include"; \
	    echo $$j; \
	    eval "$$j"; \
	done

.include <bsd.lib.mk>
