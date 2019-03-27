# $FreeBSD$

.include <src.opts.mk>

FILESDIR=	${SHAREDIR}/doc/ntp/scripts

.if ${MK_HTML} != "no"
FILES=	accopt.txt audio.txt authopt.txt clockopt.txt command.txt config.txt \
	confopt.txt external.txt footer.txt hand.txt install.txt manual.txt \
	misc.txt miscopt.txt monopt.txt refclock.txt special.txt style.css
.endif

.PATH: ${SRCTOP}/contrib/ntp/html/scripts

.include <bsd.prog.mk>
