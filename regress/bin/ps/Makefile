# $OpenBSD: Makefile,v 1.3 2015/12/15 04:18:46 schwarze Exp $

REGRESS_TARGETS=	valid_keywords invalid_keywords command cwd user

# This test ensures that ps(1) will actually accept the keywords it claims to
# accept, and that the list of keywords in ps/keyword.c are in ascending order
# (which is required by the bsearch(3)-based keyword lookup code)
valid_keywords:
	ps -o "$$(ps -L)" >/dev/null

invalid_keywords:
	if ps -o "bogus" >/dev/null 2>&1; then \
		false; \
	fi

command: shortsleep
	sh ${.CURDIR}/command.sh

cwd: shortsleep
	sh ${.CURDIR}/cwd.sh

user: shortsleep
	sh ${.CURDIR}/user.sh

cleandir:
	rm -f shortsleep

.include <bsd.regress.mk>
