#	$OpenBSD: Makefile,v 1.2 2023/10/13 19:30:18 anton Exp $

PROG=   nopermtest
CDIAGFLAGS=	-Wall
#CDIAGFLAGS+=	-Werror
CDIAGFLAGS+=	-Wpointer-arith
CDIAGFLAGS+=	-Wno-uninitialized
CDIAGFLAGS+=	-Wstrict-prototypes
CDIAGFLAGS+=	-Wmissing-prototypes
CDIAGFLAGS+=	-Wunused
CDIAGFLAGS+=	-Wsign-compare
CDIAGFLAGS+=	-Wshadow

REGRESS_SETUP_ONCE=	mount
REGRESS_CLEANUP=	umount
REGRESS_TARGETS=	run-regress-locked run-regress-unlocked

MNT=	/mnt/regress-mfs_noperm

mount:
	mkdir -p ${MNT}
	mount_mfs -s1M -o noperm swap ${MNT}

umount:
	umount -f ${MNT} || true
	rmdir ${MNT} || true

run-regress-locked: ${PROG}
	chown root:wheel ${MNT}
	chmod 700 ${MNT}
	su build -c './${PROG} ${MNT} locked'

run-regress-unlocked: ${PROG}
	chown build ${MNT}
	chmod 700 ${MNT}
	su build -c './${PROG} ${MNT} unlocked'

.include <bsd.regress.mk>
