# $OpenBSD: Makefile,v 1.4 2025/06/03 14:35:27 yasuoka Exp $

PROGS=		test_fflush
CLEANFILES=	test_fflush.tmp

PROGS+=		test_ungetwc
CLEANFILES+=	test_ungetwc.tmp

PROGS+=		test___freading
CLEANFILES+=	test___freading.tmp

PROGS+=		test___fwriting
CLEANFILES+=	test___fwriting.tmp

PROGS+=		test___fpending
CLEANFILES+=	test___fpending.tmp

PROGS+=		test___freadahead
CLEANFILES+=	test___freadahead.tmp

PROGS+=		test___freadptr
CLEANFILES+=	test___freadptr.tmp

PROGS+=		test___fseterr
CLEANFILES+=	test___fseterr.tmp

WARNINGS=	yes

.include <bsd.regress.mk>
