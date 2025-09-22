#	$OpenBSD: Makefile,v 1.19 2024/02/07 21:24:58 millert Exp $

# DATAFORM selects the data format.  OpenBSD always uses "main"
# Available formats represent essentially the same data, albeit
# possibly with minor discrepancies that users are not likely to notice.
# To get new features and the best data right away, use:
#	DATAFORM=	vanguard
# To wait a while before using new features, to give downstream users
# time to upgrade zic (the default), use:
#	DATAFORM=	main
# To wait even longer for new features, use:
#	DATAFORM=	rearguard
# Rearguard users might also want "ZFLAGS = -b fat"; see below.
DATAFORM=	main

# The POSIXRULES macro controls interpretation of POSIX-like TZ
# settings like TZ='EET-2EEST' that lack DST transition rules.
# If POSIXRULES is '-', no template is installed; this is the default.
# Any other value for POSIXRULES is obsolete and should not be relied on, as:
# * It does not work correctly in popular implementations such as GNU/Linux.
# * It does not work even in tzcode, except for historical timestamps
#   that precede the last explicit transition in the POSIXRULES file.
#   Hence it typically does not work for current and future timestamps.
# If, despite the above, you want a template for handling these settings,
# you can change the line below (after finding the timezone you want in the
# one of the $(TDATA) source files, or adding it to a source file).
# Alternatively, if you discover you've got the wrong timezone, you can just
# 'zic -p -' to remove it, or 'zic -p rightzone' to change it.
# Use the command
#	make zonenames
# to get a list of the values you can use for POSIXRULES.
POSIXRULES=	US/Pacific

# "Compiled" timezone information is placed in the "TZDIR" directory
# (and subdirectories).
# TZDIR_BASENAME should not contain "/" and should not be ".", ".." or empty.
TZDIR_BASENAME=	zoneinfo
TZDIR=	${DESTDIR}/usr/share/zoneinfo

# What kind of TZif data files to generate.  (TZif is the binary time
# zone data format that zic generates; see Internet RFC 8536.)
# If you want only POSIX time, with time values interpreted as
# seconds since the epoch (not counting leap seconds), use
#	REDO=		posix_only
# below.  If you want only "right" time, with values interpreted
# as seconds since the epoch (counting leap seconds), use
#	REDO=		right_only
# below.  If you want both sets of data available, with leap seconds not
# counted normally, use
#	REDO=		posix_right
# below.  If you want both sets of data available, with leap seconds counted
# normally, use
#	REDO=		right_posix
# below.  POSIX mandates that leap seconds not be counted; for compatibility
# with it, use "posix_only" or "posix_right".  Use POSIX time on systems with
# leap smearing; this can work better than unsmeared "right" time with
# applications that are not leap second aware, and is closer to unsmeared
# "right" time than unsmeared POSIX time is (e.g., 0.5 vs 1.0 s max error).
REDO=		posix_only

# Whether to put an "Expires" line in the leapseconds file.
# Use EXPIRES_LINE=1 to put the line in, 0 to omit it.
# The EXPIRES_LINE value matters only if REDO's value contains "right".
# If you change EXPIRES_LINE, remove the leapseconds file before running "make".
# zic's support for the Expires line was introduced in tzdb 2020a,
# and was modified in tzdb 2021b to generate version 4 TZif files.
# EXPIRES_LINE defaults to 0 for now so that the leapseconds file
# can be given to pre-2020a zic implementations and so that TZif files
# built by newer zic implementations can be read by pre-2021b libraries.
EXPIRES_LINE=	0

# To install data in text form that has all the information of the TZif data,
# (optionally incorporating leap second information), use
#	TZDATA_TEXT=	tzdata.zi leapseconds
# To install text data without leap second information (e.g., because
# REDO='posix_only'), use
#	TZDATA_TEXT=	tzdata.zi
# To avoid installing text data, use
#	TZDATA_TEXT=
TZDATA_TEXT=	leapseconds tzdata.zi

TDATA=		africa antarctica asia australasia \
		europe northamerica southamerica etcetera factory \
		backward
TABDATA=	iso3166.tab zone.tab zone1970.tab zonenow.tab $(TZDATA_TEXT)
DATA=		$(TDATA) $(TABDATA)
DSTDATA_ZI_DEPS= ziguard.awk $(TDATA)

ZIC=		zic $(ZFLAGS)

all: leapseconds tzdata.zi

$(DATAFORM).zi:	$(DSTDATA_ZI_DEPS)
		(cd ${.CURDIR}/datfiles && \
		    awk -v DATAFORM=`expr $@ : '\(.*\).zi'` -f ../ziguard.awk \
		    $(TDATA) >${.OBJDIR}/$@.out)
		mv -f ${.OBJDIR}/$@.out ${.OBJDIR}/$@

tzdata.zi:	$(DATAFORM).zi version zishrink.awk
		(cd ${.CURDIR}/datfiles && version=`sed 1q ../version` && \
		  LC_ALL=C awk \
		    -v dataform='$(DATAFORM)' \
		    -v deps='$(DSTDATA_ZI_DEPS) zishrink.awk' \
		    -v redo='$(REDO)' \
		    -v version="$$version" \
		    -f ../zishrink.awk \
		    ${.OBJDIR}/$(DATAFORM).zi >${.OBJDIR}/$@.out)
		mv -f ${.OBJDIR}/$@.out ${.OBJDIR}/$@

leapseconds: leapseconds.awk datfiles/leap-seconds.list
	awk -v EXPIRES_LINE=$(EXPIRES_LINE) -f ${.CURDIR}/leapseconds.awk \
	    ${.CURDIR}/datfiles/leap-seconds.list >${.OBJDIR}/$@.out
	mv -f ${.OBJDIR}/$@.out ${.OBJDIR}/$@

posix_only: ${TDATA}
	(cd ${.CURDIR}/datfiles; \
	    ${ZIC} -d ${TZDIR} -L /dev/null ${TDATA})

right_only: leapseconds ${TDATA}
	(cd ${.CURDIR}/datfiles; \
	    ${ZIC} -d ${TZDIR} -L leapseconds ${TDATA})

other_two: leapseconds ${TDATA}
	(cd ${.CURDIR}/datfiles; \
	    ${ZIC} -d ${TZDIR}/posix -L /dev/null ${TDATA})
	(cd ${.CURDIR}/datfiles; \
	    ${ZIC} -d ${TZDIR}/right -L ${.OBJDIR}/leapseconds ${TDATA})

posix_right: posix_only other_two

right_posix: right_only other_two

realinstall: ${DATA} ${REDO}
	(cd ${.CURDIR}/datfiles; \
	    ${ZIC} -d ${TZDIR} -p ${POSIXRULES})
	chown -R ${BINOWN}:${BINGRP} ${TZDIR}
	find ${TZDIR} \
		-type f -exec chmod a=r {} + -o \
		-type d -exec chmod a=rx,u+w {} +
	${INSTALL} -c -o root -g bin -m 644 ${.CURDIR}/datfiles/iso3166.tab \
	    ${DESTDIR}/usr/share/misc
	${INSTALL} -c -o root -g bin -m 644 leapseconds \
	    ${DESTDIR}/usr/share/zoneinfo
	${INSTALL} -c -o root -g bin -m 644 ${.CURDIR}/datfiles/leap-seconds.list \
	    ${DESTDIR}/usr/share/zoneinfo
	${INSTALL} -c -o root -g bin -m 644 tzdata.zi \
	    ${DESTDIR}/usr/share/zoneinfo
	${INSTALL} -c -o root -g bin -m 644 ${.CURDIR}/datfiles/zone.tab \
	    ${DESTDIR}/usr/share/zoneinfo
	${INSTALL} -c -o root -g bin -m 644 ${.CURDIR}/datfiles/zone1970.tab \
	    ${DESTDIR}/usr/share/zoneinfo
	${INSTALL} -c -o root -g bin -m 644 ${.CURDIR}/datfiles/zonenow.tab \
	    ${DESTDIR}/usr/share/zoneinfo

clean:
	rm -f leapseconds *.zi

.PATH:	${.CURDIR}/datfiles
.include <bsd.prog.mk>
