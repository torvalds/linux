#	$OpenBSD: Makefile,v 1.7 2020/06/22 09:28:04 mvs Exp $
#
# $Id: Makefile,v 1.7 2020/06/22 09:28:04 mvs Exp $
#

SUBDIR+=	npppd

distribution:
	${INSTALL} -C -o root -g wheel -m 0600 ${.CURDIR}/npppd/npppd.conf \
	    ${DESTDIR}/etc/npppd/npppd.conf
	${INSTALL} -C -o root -g wheel -m 0600 ${.CURDIR}/npppd/npppd-users \
	    ${DESTDIR}/etc/npppd/npppd-users

.include <bsd.subdir.mk>
