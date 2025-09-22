# $OpenBSD: Makefile,v 1.2 2023/02/07 15:33:22 krw Exp $
#
# Copyright (c) 2021 Alexander Bluhm <bluhm@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

# This regress uses vnd devices to create a softraid volume.  Raid levels
# RAID 0, RAID 1, RAID 5, CRYPTO, CONCAT, RAID 1 + CRYPTO are created.
# The volume disk is then mounted and stressed a little bit.
# All tests have to be run as root.

REGRESS_TARGETS =
REGRESS_CLEANUP =	unconfig
CLEANFILES =		diskimage* passfile

LASTDISK =	bioctl softraid0 | awk '/^softraid0/{sd=$$5}END{print sd}'
ALLDISK =	bioctl softraid0 | awk '/^softraid0/{print $$5}'
ALLVND = vnconfig -l | awk -F'[ :]' '/^vnd[123]:/ && !/not in use/{print $$1}'

.PHONY: detach unconfig clean

.for v in 1 2 3
.PHONY: vnd-$v
vnd-$v: unconfig
	@echo '==== $@ ===='
	rm -f diskimage$v
	dd if=/dev/null of=diskimage$v bs=1m seek=1100
	vnconfig vnd$v diskimage$v
	printf "RAID *" | disklabel -wAT- vnd$v
	[ `disklabel vnd$v | grep -c '\<RAID\>'` -eq 1 ]
.endfor

.for r in 0 1 5 c
.PHONY: raid-$r
raid-$r: vnd-1 vnd-2 vnd-3
	@echo '==== $@ ===='
	bioctl -c $r -l vnd1a,vnd2a,vnd3a softraid0
.endfor

passfile:
	@echo '==== $@ ===='
	openssl rand -hex -out $@.tmp 16
	chmod 0600 $@.tmp
	mv $@.tmp $@

.PHONY: raid-C
raid-C: vnd-1 passfile
	@echo '==== $@ ===='
	bioctl -c C -l vnd1a -p passfile softraid0

.PHONY: raid-1C
raid-1C: vnd-1 vnd-2 vnd-3 passfile
	@echo '==== $@ ===='
	bioctl -c 1C -l vnd1a,vnd2a,vnd3a -p passfile softraid0

.for r in 0 1 5 C c 1C
.PHONY: mount-$r
mount-$r: raid-$r
	@echo '==== $@ ===='
	printf "/ *" | disklabel -wAT- "`${LASTDISK}`"
	newfs "/dev/r`${LASTDISK}`a"
	mkdir -p /mnt/regress-softraid
	mount "/dev/`${LASTDISK}`a" /mnt/regress-softraid
.endfor

detach:
	@echo '==== $@ ===='
	umount /mnt/regress-softraid
	bioctl -d `${LASTDISK}`

.for r in 0 1 5 C c 1C

REGRESS_TARGETS +=	run-mount-$r
run-mount-$r:
	${MAKE} -C ${.CURDIR} mount-$r
	${MAKE} -C ${.CURDIR} detach

REGRESS_TARGETS +=	run-fill-compare-$r
run-fill-compare-$r:
	${MAKE} -C ${.CURDIR} mount-$r
	@echo '==== fill ===='
	time cp -r /bin /mnt/regress-softraid/
	${MAKE} -C ${.CURDIR} detach
	@echo '==== compare ===='
.if "${r:MC}" == ""
	bioctl -c $r -l vnd1a,vnd2a,vnd3a -p passfile softraid0
.else
	bioctl -c $r -l vnd1a -p passfile softraid0
.endif
	bioctl softraid0
	mount "/dev/`${LASTDISK}`a" /mnt/regress-softraid
	diff -r /bin /mnt/regress-softraid/bin
	${MAKE} -C ${.CURDIR} detach

.endfor

unconfig:
	-umount -f /mnt/regress-softraid || true
	-rmdir /mnt/regress-softraid || true
	-for d in `${ALLDISK}`; do bioctl -d $$d; done
	-for v in `${ALLVND}`; do vnconfig -u $$v; done

.include <bsd.regress.mk>
