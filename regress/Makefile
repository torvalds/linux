#	$OpenBSD: Makefile,v 1.22 2017/07/07 23:55:21 bluhm Exp $

#
# To run full regression tests you just need one step:
# make regress
#
# A normal "make" is not necessary and will simply do the same thing
# as "regress" (possibly with more bugs). This allows us to include
# compilation as a part of the regression test.
#
# To get a log of test results set the REGRESS_LOG make variable to
# a log file, use an absolute path.
# To mail the test results set the REGRESS_MAIL make variable to an
# email address.
# To skip the really slow tests, set the REGRESS_SKIP_SLOW variable.

SUBDIR+= etc bin include lib libexec sbin sys usr.bin usr.sbin misc gnu

install:

REGRESS_MAIL?=

.if !empty(REGRESS_MAIL)
_REGRESS_TMP!=/usr/bin/mktemp
.endif

.if empty(REGRESS_MAIL) || empty(_REGRESS_TMP)
_REGRESS_TMP=
_REGRESS_CLEAN=exit 0
.else
_REGRESS_CLEAN=rm -f ${_REGRESS_TMP}
.endif
MAKE_FLAGS+= _REGRESS_TMP=${_REGRESS_TMP}

.INTERRUPT:
	${_REGRESS_CLEAN}

.END: .SILENT
.if ${.TARGETS:Mall} || ${.TARGETS:Mregress} 
	echo ====================================================================
.if empty(REGRESS_MAIL) || empty(_REGRESS_TMP)
	echo You can set REGRESS_LOG to point to a file and mail it manually.
	echo $$ make REGRESS_LOG=/var/log/regress-tests regress
.else
	echo Mailing regress results to ${REGRESS_MAIL} ...
	echo If you want them to be also logged locally, you can set the
	echo REGRESS_LOG make variable to the full path of a log file.
	(sysctl -n kern.version && cat ${_REGRESS_TMP}) | /usr/bin/mail -s "Regress: `uname -a`" ${REGRESS_MAIL}
.endif
	echo ====================================================================
	${_REGRESS_CLEAN}
.endif

.include <bsd.subdir.mk>
