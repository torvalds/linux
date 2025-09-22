#	$OpenBSD: Makefile,v 1.3 2023/10/12 16:59:23 anton Exp $

REGRESS_SETUP=		setup
REGRESS_CLEANUP=	cleanup
REGRESS_TARGETS=

CLEANFILES=		vnd

setup:
	mkdir -p /mnt/regress-vnd

cleanup: unconfig
	-rmdir /mnt/regress-vnd 2>/dev/null || true

unconfig:
	-vnconfig -u $$(<vnd) 2>/dev/null || true
	-umount -f /mnt/regress-vnd 2>/dev/null || true

REGRESS_TARGETS+=	run-regress-async-mount
run-regress-async-mount: unconfig
	mount_mfs -s1M -oasync swap /mnt/regress-vnd
	dd if=/dev/zero of=/mnt/regress-vnd/disk.img count=1024
	vnconfig /mnt/regress-vnd/disk.img >vnd
	fdisk -iy -b "960@64:0c" $$(<vnd)
	newfs -t msdos $$(<vnd)i

REGRESS_ROOT_TARGETS=	${REGRESS_TARGETS}

.include <bsd.regress.mk>
