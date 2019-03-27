# $FreeBSD$

PACKAGE=runtime
DIST=		${SRCTOP}/contrib/openresolv
.PATH:		${DIST}

SCRIPTS=	resolvconf

FILES=		libc dnsmasq named pdnsd pdns_recursor unbound
FILESDIR=	/libexec/resolvconf

MAN=		resolvconf.conf.5 resolvconf.8

CLEANFILES=	${SCRIPTS} ${FILES} ${MAN}

SYSCONFDIR=	/etc
RCDIR=		${SYSCONFDIR}/rc.d
VARDIR=		/var/run/resolvconf
SBINDIR=	/sbin

# We don't assume to restart the services in /sbin.  So, though
# our service(8) is in /usr/sbin, we can use it, here.
CMD1=		\\$$1 onestatus >/dev/null 2>\&1
CMD2=		\\$$1 restart
RESTARTCMD=	"/usr/sbin/service ${CMD1} \&\& /usr/sbin/service ${CMD2}"

.for f in ${SCRIPTS} ${FILES} ${MAN}
${f}:	${f}.in
	sed	-e 's:@SYSCONFDIR@:${SYSCONFDIR}:g' \
		-e 's:@LIBEXECDIR@:${FILESDIR}:g' \
		-e 's:@VARDIR@:${VARDIR}:g' \
		-e 's:@RESTARTCMD@:${RESTARTCMD}:g' \
		-e 's:@RCDIR@:${RCDIR}:g' \
		-e 's:@SBINDIR@:${SBINDIR}:g' \
		-e 's: vpn : ng[0-9]*&:g' \
		${DIST}/$@.in > $@
.endfor

.include <bsd.prog.mk>
