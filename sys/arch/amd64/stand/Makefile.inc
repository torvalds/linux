#	$OpenBSD: Makefile.inc,v 1.22 2023/09/06 01:47:36 jsg Exp $

CFLAGS=${DEBUG} ${COPTS} -Oz -Wall -Werror
CFLAGS+=	-ffreestanding -fno-stack-protector -DMDRANDOM -DBOOT_STTY
CPPFLAGS+=-I${S} -I${SADIR}/libsa -I. -I${.CURDIR}
SACFLAGS=-D_STANDALONE
DEBUGFLAGS=
# DEBUGFLAGS+=-DDEBUG
# DEBUGFLAGS+=-DGIDT_DEBUG
# DEBUGFLAGS+=-DBIOS_DEBUG
# DEBUGFLAGS+=-DEXEC_DEBUG
# DEBUGFLAGS+=-DALLOC_TRACE
# DEBUGFLAGS+=-DUNIX_DEBUG
# DEBUGFLAGS+=-DBOOTP_DEBUG -DNETIF_DEBUG -DETHER_DEBUG
# DEBUGFLAGS+=-DNFS_DEBUG -DRPC_DEBUG -DRARP_DEBUG
LINKADDR=0x40120
LOADADDR=0x40000
HEAP_LIMIT=0xA0000
BOOTREL=0x60000
BOOTMAGIC=0xc001d00d
#ROM_SIZE=32768
CLEANFILES+=	assym.h machine

SACFLAGS+=-nostdinc -fno-builtin -fpack-struct

.include <bsd.own.mk>
.if ${COMPILER_VERSION:Mclang}
NO_INTEGR_AS=	-no-integrated-as
.endif

.if !make(clean) && !make(cleandir) && !make(includes) && !make(libdep) && \
    !make(sadep) && !make(salibdir) && !make(obj)
.BEGIN:
	@([ X$(S) = X -o -h machine ] || ln -s $(S)/arch/amd64/include machine)

assym.h: ${S}/kern/genassym.sh ${SADIR}/etc/genassym.cf
	sh ${S}/kern/genassym.sh ${CC} ${NO_INTEGR_AS} ${CFLAGS} ${CPPFLAGS} \
	    ${PARAM} < ${SADIR}/etc/genassym.cf > assym.h.tmp && \
	    mv -f assym.h.tmp assym.h
.endif

# NO_NET=no_net
BINDIR=	/usr/mdec

MANSUBDIR=amd64
