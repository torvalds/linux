# $OpenBSD: Makefile,v 1.7 2025/09/02 16:26:42 helg Exp $

TESTS=		chmod chown link mkdir mkfifo open rename rmdir \
		symlink truncate unlink
CLEANFILES=	diskimage

.PHONY: disk nfs mount umount unconfig clean

disk: unconfig
	dd if=/dev/zero of=diskimage bs=512 count=4k
	vnconfig vnd0 diskimage
	newfs vnd0c

nfs:
	grep '/mnt/regress-nfs-server\>' /etc/exports || \
	    echo /mnt/regress-nfs-server -maproot=0:0 127.0.0.1 >>/etc/exports
	rcctl -f start portmap
	rcctl -f start nfsd
	rcctl -f start mountd

REGRESS_SETUP_ONCE+=	mount
mount: disk nfs
	mkdir -p /mnt/regress-nfs-server
	mount /dev/vnd0c /mnt/regress-nfs-server
	# wait until mountd(8) has exported the directory
	for i in `jot 100`; do \
	    mount | grep 'regress-nfs-server .*NFS exported' && break; \
	    [ $$i = 100 ] && exit 1; \
	    sleep .1; \
	done
	mkdir -p /mnt/regress-nfs-client
	mount -t nfs 127.0.0.1:/mnt/regress-nfs-server /mnt/regress-nfs-client

REGRESS_CLEANUP+=	umount
umount:
	umount /mnt/regress-nfs-client

REGRESS_CLEANUP+=	unconfig
unconfig:
	-umount -f -t nfs -h 127.0.0.1 -a
	-rmdir /mnt/regress-nfs-client 2>/dev/null || true
	-pkill -KILL mountd || true
	-rcctl -f stop nfsd
	-rcctl -f stop portmap
	-umount -f /dev/vnd0c 2>/dev/null || true
	-rmdir /mnt/regress-nfs-server 2>/dev/null || true
	-vnconfig -u vnd0 2>/dev/null || true
	rm -f stamp-mount

REGRESS_SETUP+=		${.OBJDIR}/../fstest
${.OBJDIR}/../fstest:
	${.MAKE} -C ${.CURDIR}/.. fstest

.for t in ${TESTS}
REGRESS_TARGETS+=	run-$t
run-$t:
	cd /mnt/regress-nfs-client && env FSTEST=${.OBJDIR}/../fstest \
	    CHFLAGS=no \
	    /bin/sh ${.CURDIR}/../run ${.CURDIR}/../tests/$t/*.t
.endfor

.include <bsd.regress.mk>
