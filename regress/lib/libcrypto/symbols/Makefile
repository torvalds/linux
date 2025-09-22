#	$OpenBSD: Makefile,v 1.5 2024/07/10 13:11:22 tb Exp $

TESTS = \
	symbols

REGRESS_TARGETS= all_tests

.include <bsd.own.mk>					# for BSDSRCDIR

# libcrypto's Makefile stomps on globals, so only include it when needed.
.ifmake include_headers.c
. include "${BSDSRCDIR}/lib/libcrypto/Makefile"		# for HDRS

include_headers.c: ${BSDSRCDIR}/lib/libcrypto/Symbols.list
. for h in ${HDRS:T}
	@echo "#include <openssl/$h>" >> include_headers.c.tmp
. endfor
	mv include_headers.c.tmp include_headers.c
.endif

LDADD=		-lcrypto
DPADD=		${LIBCRYPTO}
LDFLAGS+=	-lcrypto
LDFLAGS+=	-Wl,--no-allow-shlib-undefined
CFLAGS+=	-Wno-deprecated-declarations -DUSE_LIBRESSL_NAMESPACE

CLEANFILES+= include_headers.c symbols.c symbols.c.tmp

symbols.c: symbols.awk ../../../../lib/libcrypto/Symbols.list
	make -f ${.CURDIR}/Makefile include_headers.c; \
	awk -f ${.CURDIR}/symbols.awk \
		< ${BSDSRCDIR}/lib/libcrypto/Symbols.list > $@.tmp; \
	mv -f $@.tmp $@

all_tests: ${TESTS}
	@for test in $>; do \
		./$$test 2>/dev/null; \
	done

.include <bsd.regress.mk>
