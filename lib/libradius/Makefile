#	$OpenBSD: Makefile,v 1.4 2024/07/24 08:22:26 yasuoka Exp $

LIB=    	radius
SRCS=		radius.c radius_attr.c radius_msgauth.c radius_userpass.c \
		radius_mppe.c radius_eapmsk.c
INCS=		radius.h

CFLAGS+=	-Wall

MAN=		radius_new_request_packet.3

VERSION_SCRIPT=	Symbols.map
SYMBOL_LIST=	${.CURDIR}/Symbols.list

includes:
	@cd ${.CURDIR}; for i in $(INCS); do \
		j="cmp -s $$i ${DESTDIR}/usr/include/$$i || \
		    ${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} \
		    -m 444 $$i ${DESTDIR}/usr/include"; \
		echo $$j; \
		eval "$$j"; \
	done

${VERSION_SCRIPT}: ${SYMBOL_LIST}
	{ printf '{\n\tglobal:\n'; \
	  sed '/^[._a-zA-Z]/s/$$/;/; s/^/		/' ${SYMBOL_LIST}; \
	  printf '\n\tlocal:\n\t\t*;\n};\n'; } >$@.tmp && mv $@.tmp $@

.include <bsd.lib.mk>
