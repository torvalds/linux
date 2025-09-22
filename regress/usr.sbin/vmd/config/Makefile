#	$OpenBSD: Makefile,v 1.12 2023/09/27 05:18:40 anton Exp $

VMD ?= /usr/sbin/vmd

VMD_PASS=boot-keyword memory-round memory-just-enough cdrom-keyword \
	 format-keyword
VMD_FAIL=kernel-keyword too-few-ram too-many-ifs too-many-disks \
	 switch-no-interface switch-no-add bad-format missing-format

.for n in ${VMD_PASS}
REGRESS_TARGETS +=	run-pass-$n
run-pass-$n:
	${VMD} -n -f ${.CURDIR}/vmd-pass-$n.conf 2>&1 | \
	    diff -u ${.CURDIR}/vmd-pass-$n.ok /dev/stdin
.endfor

.for n in ${VMD_FAIL}
REGRESS_TARGETS +=	run-fail-$n
run-fail-$n:
	${VMD} -n -f ${.CURDIR}/vmd-fail-$n.conf 2>&1 | \
	    sed -e 's/^vmd: //' -e 's|${.CURDIR}/vmd-fail-$n.conf:||' | \
	    diff -u ${.CURDIR}/vmd-fail-$n.ok /dev/stdin
.endfor

run-fail-too-many-disks: disk0.img disk1.img disk2.img disk3.img disk4.img
run-fail-bad-format run-fail-missing-format run-pass-format-keyword: foo.img

foo.img disk0.img disk1.img disk2.img disk3.img disk4.img:
	touch $@

clean:
	rm -f -- *.img
	rm -rf some

.include <bsd.regress.mk>
