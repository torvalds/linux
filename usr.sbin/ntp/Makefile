# Makefile for ntpd.
# $FreeBSD$

SUBDIR=	libopts libntp libntpevent libparse ntpd ntpdc ntpq ntpdate \
	ntptime ntp-keygen sntp
SUBDIR+=	doc

SUBDIR_DEPEND_ntpd=	libntp libopts libparse
SUBDIR_DEPEND_ntpdate=	libntp
SUBDIR_DEPEND_ntpdc=	libntp libopts
SUBDIR_DEPEND_ntpq=	libntp libopts
SUBDIR_DEPEND_ntptime=	libntp
SUBDIR_DEPEND_ntp-keygen=	libntp libopts
SUBDIR_DEPEND_sntp=	libntp libntpevent libopts

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
