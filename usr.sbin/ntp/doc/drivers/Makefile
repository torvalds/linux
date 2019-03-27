# $FreeBSD$

.include <src.opts.mk>

SUBDIR=		icons scripts
FILESDIR=	${SHAREDIR}/doc/ntp/drivers

.if ${MK_HTML} != "no"
FILES=	driver1.html driver10.html driver11.html driver12.html driver16.html \
	driver18.html driver19.html driver20.html driver22.html driver26.html \
	driver27.html driver28.html driver29.html driver3.html driver30.html \
	driver31.html driver32.html driver33.html driver34.html driver35.html \
	driver36.html driver37.html driver38.html driver39.html driver4.html \
	driver40.html driver42.html driver43.html driver44.html driver45.html \
	driver46.html driver5.html driver6.html driver7.html driver8.html \
	driver9.html mx4200data.html oncore-shmem.html tf582_4.html
.endif

.PATH: ${SRCTOP}/contrib/ntp/html/drivers

.include <bsd.prog.mk>
