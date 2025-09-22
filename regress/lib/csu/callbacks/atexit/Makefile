# $OpenBSD: Makefile,v 1.1 2014/11/23 08:46:49 guenther Exp $

.include <bsd.obj.mk>

AA_DIR=${.CURDIR}/../libaa
AA_OBJDIR!=    if [ -d $(AA_DIR)/${__objdir} ]; then \
                        echo "$(AA_DIR)/${__objdir}"; \
                else \
                        echo "$(AA_DIR)"; \
                fi

AB_DIR=${.CURDIR}/../libab
AB_OBJDIR!=    if [ -d $(AB_DIR)/${__objdir} ]; then \
                        echo "$(AB_DIR)/${__objdir}"; \
                else \
                        echo "$(AB_DIR)"; \
                fi

PROG=		atexit_test

SRCS=		atexit_test.c

CFLAGS+=	-DLIBAA="\"$(AA_OBJDIR)/libaa.so\""
CFLAGS+=	-DLIBAB="\"$(AB_OBJDIR)/libab.so\""
LDFLAGS+=	-Wl,-E

NOMAN=

regress-atexit: ${PROG}
	for i in 0 1 2 3; do \
		./${PROG} $$i || exit; \
		printf "finished $$i\n\n"; \
	done | cmp -s - ${.CURDIR}/expected.out

REGRESS_TARGETS=regress-atexit

install:

.include <bsd.regress.mk>
