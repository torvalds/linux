# $OpenBSD: Makefile,v 1.38 2021/09/27 18:27:14 bluhm Exp $

ARCHS =
ARCHS +=	alpha amd64 arm64 armv7
ARCHS +=	hppa
ARCHS +=	i386
ARCHS +=	landisk loongson luna88k
ARCHS +=	macppc
ARCHS +=	octeon
ARCHS +=	powerpc64
ARCHS +=	riscv64
ARCHS +=	sparc64

DEVS =		all ramdisk

ETCSRCDIR ?=	${.CURDIR}/../../../etc

REGRESS_TARGETS =

.for a in ${ARCHS}
.for d in ${DEVS}

REGRESS_TARGETS +=	run-$a-$d
run-$a-$d:
	${SUDO} rm -rf -- $a-$d.dir
	mkdir -m 700 $a-$d.dir
	cp ${ETCSRCDIR}/etc.$a/MAKEDEV $a-$d.dir/
	${SUDO} chown root:wheel $a-$d.dir
	${SUDO} time sh -c 'cd $a-$d.dir && sh ./MAKEDEV $d'
	${SUDO} ls -ln $a-$d.dir/ | \
	    awk '/^[bcps]/ {printf "%s %x.%x %x,%x%s\n",$$1,$$3,$$4,$$5,$$6,$$10} \
		 /^l/      {printf "%s %s.%s%s>%s\n",$$1,$$3,$$4,$$9,$$11}' | \
	    sort +5 -n | \
	    sed -e 's/rwx/7/g;s/rw-/6/g;s/r-x/5/g;s/r--/4/g' \
		-e 's/-wx/3/g;s/-w-/2/g;s/--x/1/g;s/---/0/g' \
		-e 's/^\([bcpsl]\)\([0-9][0-9]*\) /\2\1/' \
	    >$a-$d.out

.endfor
.endfor

REGRESS_ROOT_TARGETS=${REGRESS_TARGETS}

clean:
	${SUDO} rm -rf -- *-*.dir/
	${SUDO} rm -f -- *-*.out

.include <bsd.regress.mk>
