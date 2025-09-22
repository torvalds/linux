#	$OpenBSD: Makefile,v 1.10 2022/12/02 01:09:04 tb Exp $

PROGS += handshake_table
PROGS += valid_handshakes_terminate

LDADD =		${SSL_INT} -lcrypto
DPADD =		${LIBCRYPTO} ${LIBSSL}
WARNINGS =	Yes
CFLAGS +=	-DLIBRESSL_INTERNAL -Wundef -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libssl

print:	handshake_table
	@./handshake_table -C

handshake.gv: handshake_table
	./handshake_table -g > $@.tmp
	mv $@.tmp $@

CLEANFILES += handshake.gv

.for _FMT in png ps svg
handshake.${_FMT}: handshake.gv
	@if [ ! -x /usr/local/bin/dot ]; then			\
		echo "pkg_add graphviz to generate png";	\
		false;						\
	fi
	dot -T${_FMT} handshake.gv -o $@

CLEANFILES += handshake.${_FMT}
.endfor

.PHONY: print

.include <bsd.regress.mk>
