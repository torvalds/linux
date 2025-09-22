# $OpenBSD: Makefile,v 1.90 2025/09/14 18:41:11 afresh1 Exp $

REGRESS_TARGETS=pkgnames pkgpaths signatures depends-check longnames pkgcfl \
	collision-check1 collision-check2 collision-check3 \
	collision-check4 collision-check5 collision-check6 \
	collision-check7 \
	update-check1 partial-update-test conflict-update \
	merge-update split-update big-merge family-circus missing \
	lib-report1 lib-report2 lib-report3 loop1 \
	lib-report4 lib-report5 lib-report6 lib-report7 \
	qttest gstest exotest inter1 manual1 oldlib1 \
	usertest-1 usertest-2 usertest-3 cups-bug nettle-bug \
	tying-1 tying-2 tag-delete setsprint temp-error1 \
	temp-error2 temp-error2-as-root temp-error3 temp-error4 \
	cksum-error system-version XXX-warning symlink no-symlink \
	oldlib2 lib-error

REGRESS_ROOT_TARGETS = temp-error2-as-root

# lots of tests that really require a vstat overhaul
REGRESS_EXPECTED_FAILURES = collision-check1 collision-check2 \
	collision-check5 collision-check6 collision-check7 \
	exotest tying-2

PERLSCRIPT = perl -I ${.CURDIR} ${.CURDIR}

WAVE_PLIST=-DREGRESSION_TESTING+=plist_checks
STUB=-DREGRESSION_TESTING+=stub

ADD_PKG = ${PERLSCRIPT}/my add -Dchecksum
DELETE_PKG = ${PERLSCRIPT}/my delete -Dchecksum
INFO_PKG = ${PERLSCRIPT}/my info
CREATE_PKG = ${PERLSCRIPT}/mycreate
MKPLIST = ${PERLSCRIPT}/mkplist
POPULATE = ${PERLSCRIPT}/populate
EMPTY = -f /dev/null

# a lot of packages are populate/create_pkg with the same parameters.
WITH = set -A args --
CPKG = ; ${POPULATE} $${args[@]}; ${CREATE_PKG} $${args[@]}

LONG1 = iamareallongfilenamethatcantberepresentedcorrectlyinatararchivebutwewantittohappenanywaysohmygodaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa42
LONG2 = iamanotherreallygfilenamethatcantberepresentedcorrectlyinatararchivebutwewantittohappenanywaysohmygodaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa42
LONG3 = iamanotherreallylonglinkthatcantberepresentedcorrectlyinatararchivebutwewantittohappenanywaysohmygodaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa42
LONG4 = iamanotherreallylonghardlinkthatcantberepresentedcorrectlyinatararchivebutwewantittohappenanywaysohmygodaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa42

NUMBERS != jot 50
.for i in ${NUMBERS}
S$i ?= ${.OBJDIR}/src$i
SRC$i ?= ${S$i}/usr/local
D$i ?= ${.OBJDIR}/dest$i
DEST$i ?= ${D$i}/usr/local
.endfor

# current maxes (used): D49, SRC30, plist28, rep7
pkgnames:
	${PERLSCRIPT}/check-name

pkgcfl:
	${PERLSCRIPT}/check-cfl

setsprint:
	${PERLSCRIPT}/check-beautify

pkgpaths:
	${PERLSCRIPT}/check-path

signatures: 
	-rm -f signatures.out
	${PERLSCRIPT}/check-sig 2>signatures.out
	diff -u signatures.out ${.CURDIR}/signatures.ref

depends-check: rep0/a-0.tgz rep0/b-0.tgz rep1/a-1.tgz rep1/b-1.tgz
	@-rm -rf ${D1}
	@ROOT=${D1} ${ADD_PKG} rep0/a-0.tgz rep0/b-0.tgz
	@ROOT=${D1} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u a b
	@ROOT=${D1} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} -D downgrade -u a b

longnames: rep1/c-0.tgz
	@-rm -rf ${D2}
	@ROOT=${D2} ${ADD_PKG} rep1/c-0.tgz
	@test -f ${DEST2}/${LONG1}
	@test -f ${DEST2}/${LONG2}
	@cd ${DEST2} && test -h ${LONG3} && test `readlink ${LONG3}` = ${LONG2}
	@cd ${DEST2} && test `stat -f '%i' ${LONG1}` = `stat -f '%i' ${LONG4}`

collision-check1: rep1/d-0.tgz rep1/e-0.tgz
	@-rm -rf ${D3}
	@ROOT=${D3} ${ADD_PKG} rep1/d-0.tgz
	@ROOT=${D3} ${ADD_PKG} rep1/e-0.tgz

collision-check2: rep1/d-0.tgz rep1/e-0.tgz
	@-rm -rf ${D4}
	@ROOT=${D4} ${ADD_PKG} rep1/d-0.tgz
	@-rm -rf ${D4}/pkgdb/d-0
	@ROOT=${D4} ${ADD_PKG} -I rep1/e-0.tgz

collision-check3: rep1/d-0.tgz rep1/f-0.tgz
	@-rm -rf ${D5}
	@ROOT=${D5} ${ADD_PKG} rep1/d-0.tgz rep1/f-0.tgz
	@-rm -rf ${D5}/pkgdb/d-0
	@ROOT=${D5} ${ADD_PKG} -D repair rep1/d-0.tgz
	PKG_DBDIR=${D5}/pkgdb pkg_info -qR d

collision-check4: rep1/d-0.tgz rep1/e-0.tgz
	@-rm -rf ${D6}
	@ROOT=${D6} ${ADD_PKG} rep1/d-0.tgz
	@-rm -rf ${D6}/pkgdb/d-0
	@ROOT=${D6} ${ADD_PKG} -D repair rep1/e-0.tgz

collision-check5: rep1/d-0.tgz rep1/e-0.tgz
	@-rm -rf ${D21}
	@ROOT=${D21} ${ADD_PKG} -n rep1/d-0.tgz rep1/e-0.tgz

collision-check6: rep1/d-0.tgz rep1/e-0.tgz
	@-rm -rf ${D7}
	@ROOT=${D7} ${ADD_PKG} rep1/d-0.tgz
	@-rm -rf ${D7}/pkgdb/d-0
	@ROOT=${D7} ${ADD_PKG} -D removecollisions rep1/d-0.tgz

collision-check7: rep0/cola-0.tgz rep1/cola-1.tgz rep1/colb-0.tgz
	@-rm -rf ${D32}
	@ROOT=${D32} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} cola-0
	@ROOT=${D32} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -r cola-1

update-check1: rep0/g-0.tgz rep1/g-0.tgz rep0/ga-0.tgz rep1/ga-0.tgz \
	rep0/gb-0.tgz rep1/gb-0p0.tgz rep0/gc-0.tgz rep1/gc-0.tgz \
	rep0/gd-0.tgz rep1/gd-1.tgz rep0/ge-0.tgz rep1/ge-1.tgz \
	rep0/gf-1.tgz rep1/gf-0.tgz rep0/gg-0.tgz rep1/gg-0.tgz
	@-rm -rf ${D8}
	@ROOT=${D8} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} g ga gb gc gd ge gf gg
	@-ROOT=${D8} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -Iu
	@PKG_DBDIR=${D8}/pkgdb pkg_info |diff - ${.CURDIR}/list7.out
	@ROOT=${D8} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u -Dupdate_gd
	@PKG_DBDIR=${D8}/pkgdb pkg_info |diff - ${.CURDIR}/list8.out

list-check: rep1/a-1.tgz rep1/b-1.tgz rep1/c-0.tgz rep1/d-0.tgz \
	rep1/e-0.tgz rep1/f-0.tgz rep1/g-0.tgz
	@-rm -rf ${D9}
	@ROOT=${D9} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -l ${.CURDIR}/list
	@PKG_DBDIR=${D9}/pkgdb pkg_info -q|diff - ${.CURDIR}/list.out

partial-update-test: rep0/h-0.tgz rep0/i-0.tgz rep0/j-0.tgz \
	rep1/h-1.tgz rep1/i-1.tgz rep1/j-1.tgz
	@-rm -rf ${D10}
	@ROOT=${D10} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} h j
	@ROOT=${D10} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u h
	@PKG_DBDIR=${D10}/pkgdb pkg_info -q|diff - ${.CURDIR}/list2.out

conflict-update: rep0/k-0.tgz rep0/l-0.tgz rep1/k-1.tgz rep1/l-1.tgz
	@-rm -rf ${D11}
	@ROOT=${D11} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} k l
	@ROOT=${D11} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u k
	@PKG_DBDIR=${D11}/pkgdb pkg_info -q|diff - ${.CURDIR}/list3.out

merge-update: rep0/m-0.tgz rep0/n-0.tgz rep1/m-1.tgz rep1/n-1.tgz
	@-rm -rf ${D12}
	@ROOT=${D12} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} m n
	@ROOT=${D12} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u n
	@PKG_DBDIR=${D12}/pkgdb pkg_info -q|diff - ${.CURDIR}/list4.out

split-update: rep0/o-0.tgz rep1/o-1.tgz rep1/p-0.tgz
	@-rm -rf ${D13}
	@ROOT=${D13} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} o
	@ROOT=${D13} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u o
	@PKG_DBDIR=${D13}/pkgdb pkg_info -q |diff - ${.CURDIR}/list5.out

big-merge: rep0/q1-0.tgz rep0/q2-0.tgz rep0/q3-0.tgz rep0/q4-0.tgz \
	rep0/q5-0.tgz rep0/q6-0.tgz rep1/q5-1.tgz rep1/q6-1.tgz \
	rep1/q1-1.tgz rep1/q2-1.tgz rep1/q3-1.tgz rep1/q4-1.tgz
	@-rm -rf ${D14}
	@ROOT=${D14} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} q1 q2 q3 q4 q5 q6
	@ROOT=${D14} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -x -D update -u 2>&1 |fgrep XXX|fgrep -v @|diff - ${.CURDIR}/list6.out

family-circus: rep0/glib-0.tgz rep0/fam-0.tgz rep1/fam-1.tgz rep1/glib-1.tgz rep1/gamin-0.tgz
	@-rm -rf ${D15}
	@ROOT=${D15} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} glib
	@ROOT=${D15} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u glib

missing: rep1/missa-0.tgz rep1/missb-0.tgz rep1/missc-0.tgz rep1/missd-0.tgz rep1/missf-0.tgz
	@-rm -rf ${D16}
	@ROOT=${D16} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} missc
	@-ROOT=${D15} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} missa missb missf

lib-report1: rep0/o1-0.tgz rep1/o1-1.tgz rep1/p1-0.tgz
	@-rm -rf ${D17}
	@ROOT=${D17} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} o1
	-@ROOT=${D17} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u o1

lib-report2: rep0/o2-0.tgz rep1/o2-1.tgz rep1/p2-0.tgz
	@-rm -rf ${D18}
	@ROOT=${D18} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} o2
	-@ROOT=${D18} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u o2

lib-report3: rep0/o3-0.tgz
	@-rm -rf ${D19}
	-@ROOT=${D19} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} o3

lib-report4: rep0/o4-0.tgz rep0/p4-0.tgz
	@-rm -rf ${D22}
	-@ROOT=${D22} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} o4

lib-report5: rep0/o5-0.tgz rep0/p4-0.tgz
	@-rm -rf ${D23}
	-@ROOT=${D23} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} o5

lib-report6: rep0/o6-0.tgz rep0/p4-0.tgz rep0/oo6-0.tgz
	@-rm -rf ${D24}
	-@ROOT=${D24} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} o6 oo6

lib-report7: rep0/o7-0.tgz rep0/p4-0.tgz
	@-rm -rf ${D25}
	-@ROOT=${D25} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} o7

oldlib1: rep0/ol-0.tgz rep1/ol-1.tgz
	@-rm -rf ${D33}
	-@ROOT=${D33} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} ol
	-@ROOT=${D33} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u ol
	@ROOT=${D33} ${INFO_PKG} -f .libs-ol-0|grep '^lib/subdir/$$'

loop1: rep0/ocaml-3.11.1.tgz rep0/tcl-8.5.7.tgz rep0/tk-8.5.7.tgz
	@-rm -rf ${D20}
	-@ROOT=${D20} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} -z ocaml-3.11.1p0 tcl-8.5.8 tk-8.5.8

qttest: rep0/qt-0.tgz rep1/qt-1.tgz
	@-rm -rf ${D26}
	@ROOT=${D26} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} qt
	@ROOT=${D26} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u qt

gstest: rep0/gs-0.tgz rep0/gs-0-no_x11.tgz rep0/purple-0.tgz
	@-rm -rf ${D27}
	-@ROOT=${D27} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} purple-0
	@if ROOT=${D27} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} -rI gs-0-no_x11; then exit 1; fi

exotest: rep0/exo-0.tgz rep1/exo-1.tgz
	@-rm -rf ${D28}
	@ROOT=${D28} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} exo
	@ROOT=${D28} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -Iu exo

thunarvfstest: rep0/thunar-0.tgz rep0/q-0.tgz rep1/thunar-vfs-1.tgz rep1/q-1.tgz rep1/thunar-1.tgz
	@-rm -rf ${D29}
	@ROOT=${D29} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} q
	@ROOT=${D29} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u q

# test for manual install
manual1: rep0/ma-0.tgz rep0/mb-0.tgz rep0/mc-0.tgz rep0/md-0.tgz \
	rep1/ma-1.tgz rep1/mb-1.tgz rep1/me-1.tgz rep1/md-1.tgz
	@-rm -rf ${D30}
	@ROOT=${D30} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} md mb
	@ROOT=${D30} ${INFO_PKG} -mq|diff -u - ${.CURDIR}/list9.out
	@-ROOT=${D30} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u
	@ROOT=${D30} ${INFO_PKG} -mq|diff -u - ${.CURDIR}/list10.out
	@ROOT=${D30} ${DELETE_PKG} -a
	@ROOT=${D30} ${INFO_PKG} -q|diff -u - ${.CURDIR}/list11.out

inter1: rep0/inta-0.tgz rep0/intb-0.tgz
	-rm -rf ${D31}
	@ROOT=${D31} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} inta intb
	@ROOT=${D31} ${DELETE_PKG} inta intb

system-version: rep0/syst-0.tgz rep1/syst-0.tgz
	-rm -rf ${D45}
	@ROOT=${D45} PKG_PATH=${.OBJDIR}/rep0 ${ADD_PKG} syst
	@ROOT=${D45} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u
	@ROOT=${D45} ${INFO_PKG} -qS syst |diff -u - ${.CURDIR}/system-version.ref

plist1:
	@${MKPLIST} ${LONG1} ${LONG2} ${LONG3} ${LONG4} >$@

plist2:
	@${MKPLIST} a b c f g >$@

plist3:
	@${MKPLIST} a c d f g >$@

plist4:
	@${MKPLIST} "@option always-update" >$@

plist5:
	@${MKPLIST} "@ask-update gd-<1 Make sure yadada is backed up" >$@

plist6:
	@${MKPLIST} "@conflict l-<1" >$@

plist7:
	@${MKPLIST} "@conflict m-<1" "@pkgpath t/m" >$@

plist8:
	@${MKPLIST} "@conflict o-<1" "@pkgpath t/o" '@lib lib/libcoincoin.so.$${LIBcoincoin_VERSION}' >$@

plist9:
	@${MKPLIST} "@conflict gamin-*" >$@

plist10:
	@${MKPLIST} "@conflict fam-*" "@pkgpath t/fam" >$@

plist11:
	@${MKPLIST} "@conflict missc-*" >$@

plist12:
	@${MKPLIST} '@lib lib/liba.so.$${LIBa_VERSION}' >$@

plist13:
	@${MKPLIST} phonon/ phonon/a >$@

plist14:
	@${MKPLIST} phonon >$@

plist15:
	@${MKPLIST} foo/ foo/a bar >$@

plist16:
	@${MKPLIST} foo/ foo/a bar/ bar/a >$@

plist17:
	@${MKPLIST} '@lib lib/libthunar-vfs.so.$${LIBthunar-vfs_VERSION}' >$@

plist18:
	@${MKPLIST} "@conflict thunar-<1" '@lib lib/libthunar-vfs.so.$${LIBthunar-vfs_VERSION}' >$@

plist19:
	@${MKPLIST} a >$@

plist20:
	@${MKPLIST} "@conflict cola-<1" a >$@

plist21:
	@${MKPLIST} '@lib lib/subdir/libol.so.$${LIBol_VERSION}' >$@

rep0/a-0.tgz:
	@${CREATE_PKG} -P't/b:b-*:b-0' ${EMPTY} $@

rep1/a-1.tgz rep0/b-0.tgz rep0/i-0.tgz rep0/j-0.tgz rep1/j-1.tgz \
rep1/i-1.tgz rep0/k-0.tgz rep0/l-0.tgz rep1/l-1.tgz rep0/m-0.tgz \
rep0/n-0.tgz rep1/m-1.tgz rep0/o-0.tgz rep0/fam-0.tgz \
rep0/o1-0.tgz rep0/o2-0.tgz rep0/ga-0.tgz \
rep0/gb-0.tgz rep1/gb-0p0.tgz rep0/gd-0.tgz rep0/ge-0.tgz \
rep0/gf-1.tgz rep1/gf-0.tgz \
rep1/missc-0.tgz rep0/q5-0.tgz rep1/q5-1.tgz \
rep0/ma-0.tgz rep1/ma-1.tgz rep0/mc-0.tgz rep1/me-1.tgz \
rep0/tcl-8.5.7.tgz:
	@${CREATE_PKG} ${EMPTY} $@

rep0/md-0.tgz:
	@${CREATE_PKG} -P't/ma:ma-*:ma-0' -P't/mb:mb-*:mb-0' -P't/mc:mc-*:mc-0' ${EMPTY} $@
rep1/md-1.tgz:
	@${CREATE_PKG} -P't/ma:ma-*:ma-1' -P't/mb:mb-*:mb-1' -P't/me:me-*:me-1' ${EMPTY} $@
rep0/mb-0.tgz:
	@${CREATE_PKG} -P't/ma:ma-*:ma-0' ${EMPTY} $@
rep1/mb-1.tgz:
	@${CREATE_PKG} -P't/ma:ma-*:ma-1' ${EMPTY} $@

rep0/inta-0.tgz:
	@${CREATE_PKG} -P't/intb:intb-*:intb-0' ${EMPTY} $@
rep0/intb-0.tgz:
	@${CREATE_PKG} -P't/inta:inta-*:inta-0' ${EMPTY} $@
 
rep1/o-1.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -P't/p:p-*:p-0' -W'coincoin.0.0' ${EMPTY} $@

rep1/o1-1.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -P't/p1:p1-*:p1-0' -W'coincoin.0' ${EMPTY} $@

rep1/o2-1.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -P't/p2:p2-*:p2-0' -W'coincoin.0' ${EMPTY} $@

rep0/o3-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -W'unlikelylibraryname.0.0' ${EMPTY} $@

rep1/p-0.tgz rep1/p1-0.tgz: plist8
	@${WITH} -B src13 -DLIBcoincoin_VERSION=0.0 -f plist8 $@ ${CPKG}

rep1/p2-0.tgz: plist8
	@${WITH} ${WAVE_PLIST} -B src14 -DLIBcoincoin_VERSION=0 -f plist8 $@ ${CPKG}

rep0/p4-0.tgz: plist12
	@${WITH} -B src15 -DLIBa_VERSION=0.0 -f plist12 $@ ${CPKG}

rep1/p4-1.tgz: plist12
	@${WITH} -B src31 -DLIBa_VERSION=1.0 -f plist12 $@ ${CPKG}

rep0/boost-0.tgz: 
	@${CREATE_PKG} ${WAVE_PLIST} -W'a.0.0' -P't/p4:p4-*:p4-0' ${EMPTY} $@
	
rep1/boost-0.tgz: 
	@${CREATE_PKG} ${WAVE_PLIST} -W'a.1.0' -P't/p4:p4-*:p4-1' ${EMPTY} $@

rep0/Imath-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -P't/boots:boost-*:boost-0' ${EMPTY} $@

rep1/Imath-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -P't/boots:boost-*:boost-0' ${EMPTY} $@

rep0/o4-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -W'a.1.0' -P't/p4:p4-*:p4-0' ${EMPTY} $@

rep0/o5-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -W'a.0.2' -P't/p4:p4-*:p4-0' ${EMPTY} $@

rep0/o6-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -W'a.0.0' -P't/p4:p4-*:p4-0' ${EMPTY} $@

rep0/oo6-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -W'a.0.0' ${EMPTY} $@

rep0/o7-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -W'dir/a.0.0' -P't/p4:p4-*:p4-0' ${EMPTY} $@

rep1/b-1.tgz:
	@${CREATE_PKG} -P't/a:a-*:a-1' ${EMPTY} $@

rep0/qt-0.tgz: plist13
	@${WITH} -B src17 -f plist13 $@ ${CPKG}

rep1/qt-1.tgz: plist14
	@${WITH} -B src16 -f plist14 $@ ${CPKG}

rep0/exo-0.tgz: plist15
	@mkdir -p ${SRC18}/foo
	touch ${SRC18}/foo/a
	cd ${SRC18} && ln -s foo bar
	@${CREATE_PKG} -B src18 -f plist15 $@

rep1/exo-1.tgz: plist16
	@${WITH} -B src19 -f plist16 $@ ${CPKG}

rep0/thunar-0.tgz: plist17
	@${WITH} -B src20 -DLIBthunar-vfs_VERSION=0.0 -f plist17 $@ ${CPKG}

rep1/thunar-vfs-1.tgz: plist18
	@${WITH} -B src20 -DLIBthunar-vfs_VERSION=0.0 -f plist18 $@ ${CPKG}

rep1/thunar-1.tgz:
	@${CREATE_PKG} ${EMPTY} $@


rep0/q-0.tgz:
	@${CREATE_PKG} -P't/thunar:thunar-*:thunar-0' ${EMPTY} $@

rep1/q-1.tgz:
	@${CREATE_PKG} -P't/thunar-vfs:thunar-vfs-*:thunar-vfs-1' ${EMPTY} $@

rep0/gs-0.tgz:
	@${CREATE_PKG} ${EMPTY} $@
rep0/gs-0-no_x11.tgz:
	@${CREATE_PKG} ${EMPTY} $@

rep0/purple-0.tgz:
	@${CREATE_PKG} -P't/gs:gs-*-!no_x11:gs-0' ${EMPTY} $@

rep1/c-0.tgz: plist1
	@mkdir -p ${SRC1}
	@touch ${SRC1}/${LONG1}
	@touch ${SRC1}/${LONG2}
	@cd ${SRC1} && ln -sf ${LONG2} ${LONG3}
	@cd ${SRC1} && ln -f ${LONG1} ${LONG4}
	@${CREATE_PKG} -B src1 -f plist1 $@

rep1/k-1.tgz: plist6
	@${CREATE_PKG} -f plist6 $@

rep1/n-1.tgz: plist7
	@${CREATE_PKG} -f plist7 $@

rep1/d-0.tgz: plist2
	@mkdir -p ${SRC2}
	@touch ${SRC2}/a ${SRC2}/b ${SRC2}/c
	@echo "coucou" >${SRC2}/f
	@echo "not coucou" >${SRC2}/g
	@${CREATE_PKG} -B src2 -f plist2 $@

rep2/d-1.tgz: plist2
	@mkdir -p ${SRC29}
	@touch ${SRC29}/a ${SRC29}/b ${SRC29}/c
	@echo "zoinx" >${SRC29}/f
	@echo "not zoinx" >${SRC29}/g
	@${CREATE_PKG} -B src29 -f plist2 $@

rep1/e-0.tgz: plist3
	@mkdir -p ${SRC3}
	@touch ${SRC3}/a ${SRC3}/c ${SRC3}/d
	@echo "coucou" >${SRC3}/f
	@echo "coucou" >${SRC3}/g
	@${CREATE_PKG} -B src3 -f plist3 $@

rep1/f-0.tgz:
	@${CREATE_PKG} -P't/d:d-*:d-0' ${EMPTY} $@

rep0/g-0.tgz: plist4
	@${CREATE_PKG} -f plist4 $@

rep1/g-0.tgz: plist4
	@PACKAGE_COMMENT=updated ${CREATE_PKG} -f plist4 $@

rep0/gg-0.tgz: plist4 
	@${CREATE_PKG} -P't/g:g-*:g-1' -f plist4 $@

rep1/gg-0.tgz: plist4 
	@PACKAGE_COMMENT=updated ${CREATE_PKG} -P't/g:g-*:g-0' -f plist4 $@

rep1/gd-1.tgz rep1/ge-1.tgz: plist5
	@${CREATE_PKG} -f plist5 $@

rep1/ga-0.tgz:
	@PACKAGE_COMMENT=updated ${CREATE_PKG} ${EMPTY} $@

rep0/gc-0.tgz:
	@${CREATE_PKG} -P't/ga:gb-*:gb-0' ${EMPTY} $@

rep1/gc-0.tgz:
	@PACKAGE_COMMENT=updated ${CREATE_PKG} -P't/ga:gb-*:gb-0p0' ${EMPTY} $@

rep0/h-0.tgz:
	@${CREATE_PKG} -P't/i:i-*:i-0' ${EMPTY} $@

rep1/h-1.tgz:
	@${CREATE_PKG} -P't/j:j->=1:j-1' ${EMPTY} $@

rep0/cola-0.tgz: plist19
	@${WITH} -B src22 -f plist19 $@ ${CPKG}

rep1/cola-1.tgz: plist19
	@${WITH} -B src22 -P't/colb:colb-*:colb-0' -f plist19 $@ ${CPKG}

rep1/colb-0.tgz: plist20
	@${WITH} -B src22 -f plist20 $@ ${CPKG}

depend_q1=-P't/q5:q5-*:q5-0'
depend_q2=${depend_q1} -P't/q1:q1-*:q1-0'
depend_q3=${depend_q1} -P't/q2:q2-*:q2-0'
depend_q4=${depend_q1} -P't/q3:q3-*:q3-0'

.for n in q1 q2 q3 q4
plist-rep0-$n:
	@echo "@unexec echo 1>&2 'XXXrep0 $n'" >$@

rep0/$n-0.tgz: plist-rep0-$n
	@${CREATE_PKG} ${depend_$n} -f plist-rep0-$n $@
rep1/$n-1.tgz: plist-rep1-$n
	@${CREATE_PKG} ${depend_$n} -f plist-rep1-$n $@

plist-rep1-$n:
	@${MKPLIST} "@conflict q1-0" "@conflict q2-0" "@conflict q3-0" "@conflict q4-0" "@exec echo 1>&2 'XXXrep1 $n'" >$@

.endfor
	
rep0/q6-0.tgz:
	@${CREATE_PKG} -P't/q3:q3-*:q3-0' ${EMPTY} $@

rep1/q6-1.tgz:
	@${CREATE_PKG} -P't/q3:q3-*:q3-0' ${EMPTY} $@

rep0/glib-0.tgz:
	@${CREATE_PKG} -P't/fam:fam-*:fam-0' ${EMPTY} $@

rep1/glib-1.tgz:
	@${CREATE_PKG} -P't/gamin:gamin-*:gamin-0' ${EMPTY} $@

rep1/fam-1.tgz: plist9
	@${CREATE_PKG} -f plist9 $@

rep1/gamin-0.tgz: plist10
	@${CREATE_PKG} -f plist10 $@

rep1/missa-0.tgz:
	@${CREATE_PKG} -P't/misse:misse-*:misse-0' ${EMPTY} $@

rep1/missb-0.tgz:
	@${CREATE_PKG} -P't/missd:missd-*:missd-0' ${EMPTY} $@

rep1/missd-0.tgz: plist11
	@${CREATE_PKG} -f plist11 $@

rep1/missf-0.tgz:
	@${CREATE_PKG} -P't/misse:misse-*:misse-0' ${EMPTY} $@

rep0/ocaml-3.11.1.tgz:
	@${CREATE_PKG} -P'test/tcl:tcl->=8.5,<8.6:tcl-8.5.7' \
		-P'test/tk:tk->=8.5,<8.6:tk-8.5.7' ${EMPTY} $@

rep0/tk-8.5.7.tgz:
	@${CREATE_PKG} -P'test/tcl:tcl->=8.5.7,<8.5.8:tcl-8.5.7' \
		${EMPTY} $@

rep0/ol-0.tgz: plist21
	@${WITH} -B src23 -DLIBol_VERSION=0.0 -f plist21 $@ ${CPKG}

rep1/ol-1.tgz: plist21
	@${WITH} -B src23 -DLIBol_VERSION=1.0 -f plist21 $@ ${CPKG}

.for o in cups-2.2.7p1 cups-filters-1.20.3 cups-libs-2.2.7 ghostscript-9.07p7
CUPS_FILES += rep2/$o.tgz
rep2/$o.tgz: ${.CURDIR}/oldcups/$o/+CONTENTS
	mkdir -p rep2
	cd rep2 && pkg_create ${WAVE_PLIST} ${STUB} -f ${.CURDIR}/oldcups/$o/+CONTENTS
.endfor

.for n in cups-2.2.8 cups-filters-1.20.3 cups-libs-2.2.8 ghostscript-9.07p7
CUPS_FILES += rep3/$n.tgz
rep3/$n.tgz: ${.CURDIR}/newcups/$n/+CONTENTS
	mkdir -p rep3
	cd rep3 && pkg_create ${WAVE_PLIST} ${STUB} -f ${.CURDIR}/newcups/$n/+CONTENTS
.endfor

cups-bug: ${CUPS_FILES}
	rm -rf ${D36}
	mkdir -p ${D36}/vardbpkg ${D36}/usr/local
	ROOT=${D36} PKG_PATH=${.OBJDIR}/rep2 INJECT=inject-order ${ADD_PKG} ${STUB} cups
	ROOT=${D36} PKG_PATH=${.OBJDIR}/rep3 INJECT=inject-order ${ADD_PKG} ${STUB} -u

usertest-1:
	@if ${CREATE_PKG} -u ${.CURDIR}/baduser1 \
	    ${EMPTY} truc-0.tgz; then \
	    	exit 1; \
	else \
		exit 0; \
	fi

usertest-2:
	@if ${CREATE_PKG} -u ${.CURDIR}/baduser2 \
	    ${EMPTY} truc-0.tgz; then \
	    	exit 1; \
	else \
		exit 0; \
	fi

usertest-3:
	@if ${CREATE_PKG} -u ${.CURDIR}/baduser3 \
	    ${EMPTY} truc-0.tgz; then \
	    	exit 1; \
	else \
		exit 0; \
	fi
keys:
	mkdir signify
	signify -G -n -s signify/test-pkg.sec -p signify/test-pkg.pub

plist22:
	${MKPLIST} '@lib lib/libnettle.so.$${LIBnettle_VERSION}' >$@

rep4/libnettle-0.tgz: plist22
	@${WITH} -B src24 -DLIBnettle_VERSION=0.0 -f plist22 $@ ${CPKG}

rep4/cups-libs-0.tgz:
	@${CREATE_PKG} -P't/gnutls:gnutls-*:gnutls-0' ${EMPTY} $@

rep5/cups-libs-0.tgz:
	@${CREATE_PKG} -P't/gnutls:gnutls-*:gnutls-0' ${EMPTY} $@

rep4/gnutls-0.tgz:
	@${CREATE_PKG} -P't/libnettle:libnettle-*:libnettle-0' ${EMPTY} $@

rep5/gnutls-0.tgz:
	@${CREATE_PKG} -P't/libnettle:libnettle-*:libnettle-1' ${EMPTY} $@

rep4/gtk+3-cups-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -W nettle.0.0 -P't/cups-libs:cups-libs-*:cups-libs-0' ${EMPTY} $@

rep5/gtk+3-cups-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -W nettle.1.0 -P't/cups-libs:cups-libs-*:cups-libs-0' ${EMPTY} $@

rep5/libnettle-1.tgz: plist22
	@${WITH} -B src24 -DLIBnettle_VERSION=1.0 -f plist22 $@ ${CPKG}

nettle-bug: rep4/cups-libs-0.tgz rep4/gtk+3-cups-0.tgz rep4/gnutls-0.tgz \
	rep4/libnettle-0.tgz rep5/cups-libs-0.tgz rep5/gtk+3-cups-0.tgz \
	rep5/gnutls-0.tgz rep5/libnettle-1.tgz
	@-rm -rf ${D37}
	@ROOT=${D37} ${ADD_PKG} rep4/gtk+3-cups
	@ROOT=${D37} PKG_PATH=${.OBJDIR}/rep5 ${ADD_PKG} -u gtk+3-cups

plist23:
	@${MKPLIST} "share/" "share/bg/" "share/bg/portrait" "share/bg/pictures/" "share/bg/pictures/a/" "share/bg/pictures/a/zoinx" >$@

plist24:
	${MKPLIST} "share/" "share/bg/" "share/bg/portrait/" "share/bg/portrait/a/" "share/bg/portrait/a/zoinx" >$@

rep0/bg-0.tgz: plist23
	@${WITH} -B src25 -f plist23 $@ ${CPKG}

rep0/bg-1.tgz: plist24
	@${WITH} -B src26 -f plist24 $@ ${CPKG}

# these exemplify issues with directory moving around
tying-1: rep0/bg-0.tgz rep0/bg-1.tgz
	@-rm -rf ${D38}
	@ROOT=${D38} ${ADD_PKG} rep0/bg-0.tgz
	@ROOT=${D38} ${ADD_PKG} -r rep0/bg-1.tgz

tying-2: rep0/bg-0.tgz rep0/bg-1.tgz
	@-rm -rf ${D39}
	@ROOT=${D39} ${ADD_PKG} rep0/bg-1.tgz
	@ROOT=${D39} ${ADD_PKG} -r rep0/bg-0.tgz

# this comes from solve_tags
# pkg_delete finds new dependencies, and so the solver needs to be made
# "safe" when it can't find them
tag-delete: rep1/d-0.tgz
	@-rm -rf ${D40}
	@ROOT=${D40} ${ADD_PKG} rep1/d-0.tgz
	# needs a tag to trigger the bug
	echo "@tag zoinx" >> ${D40}/pkgdb/d-0/+CONTENTS
	# and an "unsolved" dependency for DeleteSet to look harder
	echo "@depend graphics/zoinx:zoinx-*:zoinx-2" >> ${D40}/pkgdb/d-0/+CONTENTS
	@ROOT=${D40} ${DELETE_PKG} d

SED_PIPE = sed -n -e s/`whoami`/USER/ -e 1p -e '$$p'
SED_PIPE2 = { u=`whoami`; case $$u in root) u=_pkgfetch;; esac; sed -n -e s/$$u/USER/ -e 1p -e '$$p'; }


XXXD=${D40}
XXXBASE=temp2

temp-error1: rep0/a-0.tgz rep0/b-0.tgz temp1.ref
	@-rm -rf ${D40}
	@{ INJECT=inject-temp1 ROOT=${D40} SILENT=1 ${ADD_PKG} rep0/a-0.tgz 1>/dev/null|| echo $$? ; } 2>&1|${SED_PIPE} >temp1.out
	@diff -u temp1.out ${.CURDIR}/temp1.ref

temp-error2: 
	@-rm -rf ${XXXD}
	@{ INJECT=inject-temp2 ROOT=${XXXD} SILENT=1 PKG_PATH=https://www.google.com/ ${ADD_PKG} rep0/a-0.tgz 1>/dev/null|| echo $$? ; } 2>&1|${SED_PIPE2} >${XXXBASE}.out
	@diff -u ${XXXBASE}.out ${.CURDIR}/temp2.ref

temp-error2-as-root: 
	@# XXX thx tedu for not allowing env thru
	@${SUDO} make -C ${.CURDIR} temp-error2 XXXBASE=temp2bis XXXD=${D41}

temp-error3: rep1/d-0.tgz rep2/d-1.tgz temp3.ref
	@-rm -rf ${D42}
	@ROOT=${D42} ${ADD_PKG} rep1/d-0.tgz
	@{ INJECT=inject-temp3 ROOT=${D42} SILENT=1 ${ADD_PKG} -r rep2/d-1.tgz 1>/dev/null|| echo $$?; } 2>&1|${SED_PIPE} >temp3.out
	@diff -u temp3.out ${.CURDIR}/temp3.ref

temp-error4: rep0/qt-0.tgz rep1/qt-1.tgz
	@-rm -rf ${D43}
	@ROOT=${D43} SILENT=1 ${ADD_PKG} rep0/qt-0.tgz
	@rm -f ${D43}/usr/local/phonon/a
	@echo zoinx >${D43}/usr/local/phonon/a
	@{ INJECT=inject-temp4 PKG_PATH=${.OBJDIR}/rep1 SILENT=1 ROOT=${D43} ${ADD_PKG} -Dchecksum -u qt || echo $$?; } 2>&1|${SED_PIPE} >temp4.out

rep5/a-0.tgz: plist19
	@mkdir -p ${SRC27}
	@echo "zoinx" > ${SRC27}/a
	@INJECT=inject-badsum ${CREATE_PKG} -B src27 -f plist19 $@

rep0/syst-0.tgz:
	@${CREATE_PKG} -V 1 ${EMPTY} $@

rep1/syst-0.tgz:
	@${CREATE_PKG} -V 3 ${EMPTY} $@

cksum-error: rep5/a-0.tgz
	@-rm -rf ${D44}
	@ROOT=${D44} SILENT=1 ${ADD_PKG} rep5/a-0.tgz
	@ROOT=${D44} SILENT=1 ${DELETE_PKG} a|fgrep 'Not deleting'

# data for XXX-warning test

plist25:
	${MKPLIST} '@lib lib/libavahi.so.$${LIBavahi_VERSION}' >$@

plist26:
	${MKPLIST} '@conflict avahi-<1' '@conflict avahi-libs-<1' '@lib lib/libavahi.so.$${LIBavahi_VERSION}' >$@

plist27:
	${MKPLIST} '@conflict avahi-<1' >$@

rep6/avahi-0.tgz: plist25
	@${WITH} -B src28 -DLIBavahi_VERSION=0.0 -f plist25 $@ ${CPKG}

rep6/cups-0.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -P't/avahi:avahi-*:avahi-0' -W avahi.0.0 ${EMPTY} $@

rep7/avahi-1.tgz: 
	@${CREATE_PKG} ${WAVE_PLIST} -P't/avahi-libs:avahi-libs-*:avahi-libs-1' ${EMPTY} $@

rep7/avahi-libs-1.tgz: plist27
	@${WITH} -B src28 -f plist27 $@ ${CPKG}

rep7/avahi-glib-1.tgz: plist26
	@${WITH} -B src28 -DLIBavahi_VERSION=0.0 -f plist26 $@ ${CPKG}

rep7/cups-1.tgz:
	@${CREATE_PKG} ${WAVE_PLIST} -P't/avahi-libs:avahi-libs-*:avahi-libs-0' -P't/avahi-glib:avahi-glib-*:avahi-glib-1' -W avahi.0.0 ${EMPTY} $@


rep0/sym-0.tgz: plist19
	@mkdir -p ${SRC30}
	@ln -s /tmp ${SRC30}/a
	@${CREATE_PKG} -B src30 -f plist19 $@


XXX-warning: rep6/avahi-0.tgz rep6/cups-0.tgz rep7/avahi-1.tgz rep7/avahi-libs-1.tgz rep7/avahi-glib-1.tgz rep7/cups-1.tgz
	@-rm -rf ${D46}
	@ROOT=${D46} SILENT=1 ${ADD_PKG} rep6/cups-0.tgz
	@ROOT=${D46} PKG_PATH=${.OBJDIR}/rep7 ${ADD_PKG} -u 2>warning
	@if fgrep XXX warning; then exit 1; fi

symlink: rep0/sym-0.tgz
	@-rm -rf ${D47}
	@ROOT=${D47} ${ADD_PKG} rep0/sym-0.tgz
	@rm ${D47}/usr/local/a
	@ROOT=${D47} ${ADD_PKG} -r -Dinstalled rep0/sym-0.tgz

no-symlink: rep1/d-0.tgz
	@-rm -rf ${D48}
	@ROOT=${D48} ${ADD_PKG} rep1/d-0.tgz
	@rm -f ${D48}/usr/local/f /usr/local/a
	@ROOT=${D48} ${ADD_PKG} -r -Dinstalled rep1/d-0.tgz
	@test -f ${D48}/usr/local/f
	@test -f ${D48}/usr/local/a

# this is a "temporary" test
# oldlibs currently do a transitive closure because people might have
# forgotten some wantlib
# update is supposed to detect that and try to remove any dependency
# on oldlibs.

# in the following case, Imath does NOT depend on the libs in p4,
# but pkg_add tries to be thorough. So Imath should get updated to the
# version in rep1, which is supposed to be "fresh" and not (possibly)
# depend on the old lib.
# The name IS significant: the error only happens because Imath is before
# boost (lexicographically)

oldlib2: rep0/p4-0.tgz rep0/boost-0.tgz rep0/Imath-0.tgz \
	rep1/p4-1.tgz rep1/boost-0.tgz rep1/Imath-0.tgz
	@-rm -rf ${D49}
	@ROOT=${D49} ${ADD_PKG} rep0/Imath-0.tgz
	@ROOT=${D49} PKG_PATH=${.OBJDIR}/rep1 ${ADD_PKG} -u
	@if fgrep -q .libs ${D49}/pkgdb/Imath-0/+REQUIRING; then exit 1; fi

plist28:
	@${MKPLIST} '@lib lib/subdir/libbad.so.$${LIBbaad_VERSION}' >$@

lib-error: plist28
	@rm -f lib-error.out
	@if ${CREATE_PKG} -B src30 -f plist28 bad-package-0 2>lib-error.out; then exit 1; fi
	@diff -u lib-error.out ${.CURDIR}/lib-error.ref

clean:
	-rm -rf rep* dest* plist* src* signatures.out *.ref *.out
	-${SUDO} rm -rf rep* dest* plist* src* signatures.out *.ref *.out

.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
