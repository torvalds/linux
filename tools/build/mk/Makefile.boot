# $FreeBSD$

CFLAGS+=	-I${WORLDTMP}/legacy/usr/include
DPADD+=		${WORLDTMP}/legacy/usr/lib/libegacy.a
LDADD+=		-legacy
LDFLAGS+=	-L${WORLDTMP}/legacy/usr/lib

# we do not want to capture dependencies referring to the above
UPDATE_DEPENDFILE= no

# When building host tools we should never pull in headers from the source sys
# directory to avoid any ABI issues that might cause the built binary to crash.
# The only exceptions to this are sys/cddl/compat for dtrace bootstrap tools and
# sys/crypto for libmd bootstrap.
# We have to skip this check during make obj since bsd.crunchgen.mk will run
# make obj on every directory during the build-tools phase.
.if !make(obj)
.if !empty(CFLAGS:M*${SRCTOP}/sys)
.error Do not include $${SRCTOP}/sys when building bootstrap tools. \
    Copy the header to $${WORLDTMP}/legacy in tools/build/Makefile instead. \
    Error was caused by Makefile in ${.CURDIR}
.endif

# ${SRCTOP}/include should also never be used to avoid ABI issues
.if !empty(CFLAGS:M*${SRCTOP}/include*)
.error Do not include $${SRCTOP}/include when building bootstrap tools. \
    Copy the header to $${WORLDTMP}/legacy in tools/build/Makefile instead. \
    Error was caused by Makefile in ${.CURDIR}
.endif
.endif
