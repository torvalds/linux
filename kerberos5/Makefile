# $FreeBSD$

SUBDIR=	lib .WAIT \
	libexec tools usr.bin usr.sbin
SUBDIR_PARALLEL=

# These are the programs which depend on Kerberos.
KPROGS=	lib/libpam \
	secure/lib/libssh secure/usr.bin/ssh secure/usr.sbin/sshd

# This target is used to rebuild these programs WITH Kerberos.
kerberize:
.for entry in ${KPROGS}
	cd ${.CURDIR:H}/${entry}; \
	${MAKE} cleandir; \
	${MAKE} obj; \
	${MAKE} all; \
	${MAKE} install
.endfor

# This target is used to rebuild these programs WITHOUT Kerberos.
dekerberize:
.for entry in ${KPROGS}
	cd ${.CURDIR:H}/${entry}; \
	${MAKE} MK_KERBEROS=no cleandir; \
	${MAKE} MK_KERBEROS=no obj; \
	${MAKE} MK_KERBEROS=no all; \
	${MAKE} MK_KERBEROS=no install
.endfor

.include <bsd.subdir.mk>
