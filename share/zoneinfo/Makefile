# $FreeBSD$

#
# HOW TO UPDATE THE ZONEINFO DATA
#
# With the use of subversion, this is a little bit simpler than the CVS method.
#
# Import the new sources to the vendor branch:
#
# $ cd ~/svn/vendor/tzdata/dist
# $ tar zxvf /usr/ports/distfile/tzdata2008X.tar.gz
#   (check with "svn stat" and "svn diff" if it all makes sense)
# $ svn commit	# Commit message: "Vendor import of tzdata2008X (+details)" 
#
# Tag it
#
# $ cd ~/svn/vendor/tzdata
# $ svn cp svn+ssh://svn.freebsd.org/base/vendor/tzdata/dist \
#	svn+ssh://svn.freebsd.org/base/vendor/tzdata/tzdata2008X
# $ svn commit	# Commit message: "Tag of tzdata2008X"
#
# Merge-from-vendor
#
# $ cd ~/svn/head/contrib/tzdata
# $ svn update
# $ svn merge -c X --accept=postpone \
#	svn+ssh://svn.freebsd.org/base/vendor/tzdata/dist .
# $ svn commit	# Commit message: "MFV of tzdata2008X"
#

.include <src.opts.mk>

CLEANFILES+=	yearistype
CLEANDIRS+=	builddir
CONTRIBDIR=	${SRCTOP}/contrib/tzdata/
.PATH:		${CONTRIBDIR}

.if defined(LEAPSECONDS)
.warning "Using backwards compatibility variable for LEAPSECONDS; please use WITH_ZONEINFO_LEAPSECONDS_SUPPORT instead"
MK_ZONEINFO_LEAPSECONDS_SUPPORT= yes
.endif

.if defined(OLDTIMEZONES)
.warning "Using backwards compatibility variable for OLDTIMEZONES; please use WITH_ZONEINFO_OLD_TIMEZONES_SUPPORT instead"
MK_ZONEINFO_OLD_TIMEZONES_SUPPORT= yes
.endif

.if ${MK_ZONEINFO_LEAPSECONDS_SUPPORT} != "no"
LEAPFILE=	-L ${CONTRIBDIR}leapseconds
.else
LEAPFILE=
.endif

TZFILES=	africa antarctica asia australasia etcetera europe \
		factory northamerica southamerica
POSIXRULES=	America/New_York

.if ${MK_ZONEINFO_OLD_TIMEZONES_SUPPORT} != "no"
TZFILES+=	backward systemv
.endif

TZFILES:=	${TZFILES:S/^/${CONTRIBDIR}/}

TZBUILDDIR=	${.OBJDIR}/builddir
TZBUILDSUBDIRS=	\
		Africa \
		America/Argentina \
		America/Indiana \
		America/Kentucky \
		America/North_Dakota \
		Antarctica \
		Arctic \
		Asia \
		Atlantic \
		Australia \
		Etc \
		Europe \
		Indian \
		Pacific \
		SystemV

.if ${MK_ZONEINFO_OLD_TIMEZONES_SUPPORT} != "no"
TZBUILDSUBDIRS+= US Mexico Chile Canada Brazil
.endif

.if !defined(_SKIP_BUILD)
all: zoneinfo
.endif
META_TARGETS+=	zoneinfo install-zoneinfo

zoneinfo: yearistype ${TDATA}
	mkdir -p ${TZBUILDDIR}
	cd ${TZBUILDDIR}; mkdir -p ${TZBUILDSUBDIRS}
	umask 022; cd ${.CURDIR}; \
	zic -D -d ${TZBUILDDIR} -p ${POSIXRULES} -m ${NOBINMODE} \
	    ${LEAPFILE} -y ${.OBJDIR}/yearistype ${TZFILES}

#
# Sort TZS to ensure they are the same every build. find -s might
# be a shorter way to express this, but it's non-portable. Any
# differences between the two don't matter for this purpose.
#
.if make(*install*)
TZS!= cd ${TZBUILDDIR} && find * -type f | LC_ALL=C sort
.endif

beforeinstall: install-zoneinfo
install-zoneinfo:
	mkdir -p ${DESTDIR}/usr/share/zoneinfo
	cd ${DESTDIR}/usr/share/zoneinfo;  mkdir -p ${TZBUILDSUBDIRS}
.for f in ${TZS}
	${INSTALL} ${TAG_ARGS} \
	    -o ${BINOWN} -g ${BINGRP} -m ${NOBINMODE} \
	    ${TZBUILDDIR}/${f} ${DESTDIR}/usr/share/zoneinfo/${f}
.endfor
	${INSTALL} ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m ${NOBINMODE} \
	    ${CONTRIBDIR}/zone.tab ${DESTDIR}/usr/share/zoneinfo/

afterinstall:
#
# If the file /var/db/zoneinfo exists, and it is owned by root:wheel,
# and the contents of it exists in /usr/share/zoneinfo, then reinstall
# it.
#
	@if [ -f ${DESTDIR}/var/db/zoneinfo -a -O ${DESTDIR}/var/db/zoneinfo \
	    -a -G ${DESTDIR}/var/db/zoneinfo ]; then \
		zf=$$(cat ${DESTDIR}/var/db/zoneinfo); \
		if [ -f ${DESTDIR}/usr/share/zoneinfo/$${zf} ]; then \
			if [ ! -z "${DESTDIR}" ]; then \
				optC="-C ${DESTDIR}"; \
			fi; \
			echo "Updating /etc/localtime"; \
			tzsetup $${optC} -r; \
		fi; \
	else \
		echo "Run tzsetup(8) manually to update /etc/localtime."; \
	fi

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
