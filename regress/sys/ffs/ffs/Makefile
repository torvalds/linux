# $OpenBSD: Makefile,v 1.6 2025/09/02 16:26:42 helg Exp $

TESTS=		chflags chmod chown link mkdir mkfifo open rename rmdir \
		symlink truncate unlink
CLEANFILES=	diskimage

.PHONY: disk mount umount unconfig clean

disk: unconfig
	dd if=/dev/zero of=diskimage bs=512 count=4k
	vnconfig vnd0 diskimage
	newfs vnd0c

REGRESS_SETUP_ONCE+=	mount
mount: disk
	mkdir -p /mnt/regress-ffs
	mount /dev/vnd0c /mnt/regress-ffs

REGRESS_CLEANUP+=	umount
umount:
	umount /mnt/regress-ffs

REGRESS_CLEANUP+=	unconfig
unconfig:
	-umount -f /dev/vnd0c 2>/dev/null || true
	-rmdir /mnt/regress-ffs 2>/dev/null || true
	-vnconfig -u vnd0 2>/dev/null || true
	rm -f stamp-mount

REGRESS_SETUP+=		${.OBJDIR}/../fstest
${.OBJDIR}/../fstest:
	${.MAKE} -C ${.CURDIR}/.. fstest

.for t in ${TESTS}
REGRESS_TARGETS+=	run-$t
run-$t:
	cd /mnt/regress-ffs && env FSTEST=${.OBJDIR}/../fstest \
	    CHFLAGS=yes \
	    /bin/sh ${.CURDIR}/../run ${.CURDIR}/../tests/$t/*.t
.endfor

.include <bsd.regress.mk>
