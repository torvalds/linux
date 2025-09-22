# $OpenBSD: Makefile,v 1.2 2015/04/07 01:27:07 guenther Exp $

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

PROG=		pthread_atfork_test

SRCS=		pthread_atfork_test.c

CFLAGS+=	-DLIBAA="\"$(AA_OBJDIR)/libaa.so\""
CFLAGS+=	-DLIBAB="\"$(AB_OBJDIR)/libab.so\""
LDFLAGS+=	-Wl,-E

LDADD+=		-lpthread
DPADD+=		${LIBPTHREAD}

NOMAN=

CLEANFILES=	parent_out child_out

TESTS=		0 1 2 3

regress-pthread_atfork: ${PROG}
	for i in ${TESTS}; do \
		./${PROG} $$i || exit; \
		printf "finished $$i\n\n"; \
		printf "finished $$i\n\n" >&3; \
	done >parent_out 3>child_out
	cmp -s parent_out ${.CURDIR}/expected_parent.out
	cmp -s child_out ${.CURDIR}/expected_child.out

REGRESS_TARGETS=regress-pthread_atfork

install:

.include <bsd.regress.mk>
