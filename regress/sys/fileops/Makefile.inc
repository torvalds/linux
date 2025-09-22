# $OpenBSD: Makefile.inc,v 1.7 2024/11/06 18:42:54 bluhm Exp $

PERL_REQUIRE != perl -e 'eval { require File::Slurp } or print $$@'

TESTS1=		create read mmap
TESTS2=		many_files_root many_files_subdir file_write
TESTS= ${TESTS1} ${TESTS2}

FILEOPS_MNT=	/mnt/regress-fileops-${FS}
FILEOPS_PROG=	${.OBJDIR}/../fileops
CLEANFILES=	diskimage vnd

.poison !defined (MOUNT)
.poison !defined (NEWFS)

.PHONY: disk mount unconfig clean

disk: unconfig
	dd if=/dev/urandom of=diskimage bs=1M count=64
	vnconfig diskimage >vnd
	${NEWFS} /dev/r$$(<vnd)c

REGRESS_SETUP_ONCE+=	mount
mount: disk
	mkdir -p ${FILEOPS_MNT}
	${MOUNT} /dev/$$(<vnd)c ${FILEOPS_MNT}

REGRESS_CLEANUP+=	umount
umount:
	umount -f ${FILEOPS_MNT} || true

REGRESS_CLEANUP+=	unconfig
unconfig:
	-umount -f /dev/$$(<vnd)c 2>/dev/null || true
	-rmdir ${FILEOPS_MNT} 2>/dev/null || true
	-vnconfig -u $$(<vnd) 2>/dev/null || true
	rm -f stamp-setup

# disk image takes up too much space in /usr/obj
REGRESS_CLEANUP+=      remove-diskimage
remove-diskimage:
	rm -f diskimage

${.OBJDIR}/../fileops:
	${.MAKE} -C ${.CURDIR}/.. fileops

.for t in ${TESTS1}
REGRESS_TARGETS+=	run-$t
run-$t: ${.OBJDIR}/../fileops
	cd ${FILEOPS_MNT} && \
	    ${FILEOPS_PROG} $t ${FILEOPS_MNT}/file
.endfor

.for t in ${TESTS2}
REGRESS_TARGETS+=	run-$t
run-$t: ${.CURDIR}/../fileops2.pl
.if ! empty(PERL_REQUIRE)
	@echo "${PERL_REQUIRE}"
	@echo "Install the p5-File-Slurp package to run fileops $t subtest"
	@echo SKIPPED
.else
	perl ${.CURDIR}/../fileops2.pl $t ${FILEOPS_MNT}
.endif
.endfor
