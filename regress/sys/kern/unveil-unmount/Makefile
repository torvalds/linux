# $OpenBSD: Makefile,v 1.7 2023/10/12 16:59:24 anton Exp $

# Call unveil(2) in combination with unlink(2), chroot(2), chdir(2).
# Use umount(8) to check that the mountpoint leaks no vnode.
# There were vnode reference counting bugs in the kernel.

PROGS=		unveil-unlink unveil-chroot unveil-perm unveil-chdir
CLEANFILES=	diskimage vnd

.PHONY: mount unconfig clean

diskimage: unconfig
	${SUDO} dd if=/dev/zero of=diskimage bs=512 count=4k
	${SUDO} vnconfig diskimage >vnd
	${SUDO} newfs $$(<vnd)c

mount: diskimage
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} mount /dev/$$(<vnd)c /mnt/regress-unveil

unconfig:
	-${SUDO} umount -f /dev/$$(<vnd)c 2>/dev/null || true
	-${SUDO} rmdir /mnt/regress-unveil 2>/dev/null || true
	-${SUDO} vnconfig -u $$(<vnd) 2>/dev/null || true
	-${SUDO} rm -f stamp-setup

REGRESS_SETUP	=	${PROGS} mount
REGRESS_CLEANUP =	unconfig
REGRESS_TARGETS =

REGRESS_TARGETS +=	run-unlink
run-unlink:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} ./unveil-unlink /mnt/regress-unveil/foo bar
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot
run-chroot:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} ./unveil-chroot /mnt/regress-unveil /
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-dir
run-chroot-dir:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} ./unveil-chroot /mnt/regress-unveil/foo /
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-unveil-dir
run-chroot-unveil-dir:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} ./unveil-chroot /mnt/regress-unveil /foo
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-dir-unveil-dir
run-chroot-dir-unveil-dir:
	${SUDO} mkdir -p /mnt/regress-unveil/foo/bar
	${SUDO} ./unveil-chroot /mnt/regress-unveil/foo /bar
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-rootslash
run-chroot-rootslash:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} ./unveil-chroot /mnt/regress-unveil //
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-open
run-chroot-open:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-chroot /mnt/regress-unveil / /baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-dir-open
run-chroot-dir-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} touch /mnt/regress-unveil/foo/baz
	${SUDO} ./unveil-chroot /mnt/regress-unveil/foo / /baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-unveil-dir-open
run-chroot-unveil-dir-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} touch /mnt/regress-unveil/foo/baz
	${SUDO} ./unveil-chroot /mnt/regress-unveil /foo /baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-dir-unveil-dir-open
run-chroot-dir-unveil-dir-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo/bar
	${SUDO} touch /mnt/regress-unveil/foo/bar/baz
	${SUDO} ./unveil-chroot /mnt/regress-unveil/foo /bar /baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-rootslash-open
run-chroot-rootslash-open:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-chroot /mnt/regress-unveil // /baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-open-rootslash
run-chroot-open-rootslash:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-chroot /mnt/regress-unveil / //baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-unveil-rootdot-open
run-chroot-unveil-rootdot-open:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-chroot /mnt/regress-unveil /. /baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chroot-unveil-rootdotdot-open
run-chroot-unveil-rootdotdot-open:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-chroot /mnt/regress-unveil /.. /baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-perm
run-perm:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} ./unveil-perm "" /mnt/regress-unveil
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-perm-dir
run-perm-dir:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} ./unveil-perm "" /mnt/regress-unveil/foo
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-perm-open
run-perm-open:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-perm "" /mnt/regress-unveil baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-perm-dir-open
run-perm-dir-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} touch /mnt/regress-unveil/foo/baz
	${SUDO} ./unveil-perm "" /mnt/regress-unveil/foo baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-perm-create-open
run-perm-create-open:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-perm "c" /mnt/regress-unveil baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-perm-dir-create-open
run-perm-dir-create-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} touch /mnt/regress-unveil/foo/baz
	${SUDO} ./unveil-perm "c" /mnt/regress-unveil/foo baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-perm-write-open
run-perm-write-open:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-perm "w" /mnt/regress-unveil baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-perm-dir-write-open
run-perm-dir-write-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} touch /mnt/regress-unveil/foo/baz
	${SUDO} ./unveil-perm "w" /mnt/regress-unveil/foo baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir
run-chdir:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} ./unveil-chdir /mnt/regress-unveil .
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-dir
run-chdir-dir:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} ./unveil-chdir /mnt/regress-unveil/foo .
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-unveil-dir
run-chdir-unveil-dir:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} ./unveil-chdir /mnt/regress-unveil foo
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-unveil-backdir
run-chdir-unveil-backdir:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} ./unveil-chdir /mnt/regress-unveil foo/..
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-unveil-dotdot
run-chdir-unveil-dotdot:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} ./unveil-chdir /mnt/regress-unveil/foo ..
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-dir-unveil-dir
run-chdir-dir-unveil-dir:
	${SUDO} mkdir -p /mnt/regress-unveil/foo/bar
	${SUDO} ./unveil-chdir /mnt/regress-unveil/foo bar
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-dir-unveil-backdir
run-chdir-dir-unveil-backdir:
	${SUDO} mkdir -p /mnt/regress-unveil/foo/bar
	${SUDO} ./unveil-chdir /mnt/regress-unveil/foo bar/..
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-dir-unveil-dotdot
run-chdir-dir-unveil-dotdot:
	${SUDO} mkdir -p /mnt/regress-unveil/foo/bar
	${SUDO} ./unveil-chdir /mnt/regress-unveil/foo/bar ..
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-open
run-chdir-open:
	${SUDO} mkdir -p /mnt/regress-unveil
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-chdir /mnt/regress-unveil . baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-dir-open
run-chdir-dir-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} touch /mnt/regress-unveil/foo/baz
	${SUDO} ./unveil-chdir /mnt/regress-unveil/foo . baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-unveil-dir-open
run-chdir-unveil-dir-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} touch /mnt/regress-unveil/foo/baz
	${SUDO} ./unveil-chdir /mnt/regress-unveil foo baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-unveil-backdir-open
run-chdir-unveil-backdir-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-chdir /mnt/regress-unveil foo/.. baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-unveil-dotdot-open
run-chdir-unveil-dotdot-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo
	${SUDO} touch /mnt/regress-unveil/baz
	${SUDO} ./unveil-chdir /mnt/regress-unveil/foo .. baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-dir-unveil-dir-open
run-chdir-dir-unveil-dir-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo/bar
	${SUDO} touch /mnt/regress-unveil/foo/bar/baz
	${SUDO} ./unveil-chdir /mnt/regress-unveil/foo bar baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-dir-unveil-backdir-open
run-chdir-dir-unveil-backdir-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo/bar
	${SUDO} touch /mnt/regress-unveil/foo/baz
	${SUDO} ./unveil-chdir /mnt/regress-unveil/foo bar/.. baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_TARGETS +=	run-chdir-dir-unveil-dotdot-open
run-chdir-dir-unveil-dotdot-open:
	${SUDO} mkdir -p /mnt/regress-unveil/foo/bar
	${SUDO} touch /mnt/regress-unveil/foo/baz
	${SUDO} ./unveil-chdir /mnt/regress-unveil/foo/bar .. baz
	${SUDO} umount /mnt/regress-unveil

REGRESS_ROOT_TARGETS =	${REGRESS_TARGETS}

.include <bsd.regress.mk>
