#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD$

.include <src.opts.mk>

# Do not include `info' in the SUBDIR list, it is handled separately.

SUBDIR=	${_colldef} \
	${_ctypedef} \
	${_dict} \
	${_doc} \
	${_dtrace} \
	${_examples} \
	${_i18n} \
	keys \
	${_man} \
	${_me} \
	misc \
	${_mk} \
	${_monetdef} \
	${_msgdef} \
	${_numericdef} \
	${_sendmail} \
	skel \
	${_snmp} \
	${_syscons} \
	tabset \
	termcap \
	${_timedef} \
	${_vt} \
	${_zoneinfo}

# NB: keep these sorted by MK_* knobs

.if ${MK_BSNMP} != "no"
_snmp=		snmp
.endif

.if ${MK_CDDL} != "no"
_dtrace=	dtrace
.endif

.if ${MK_DICT} != "no"
_dict=		dict
.endif

.if ${MK_EXAMPLES} != "no"
_examples=	examples
.endif

.if ${MK_ICONV} != "no"
_i18n=		i18n
.endif

.if ${MK_LOCALES} != "no"
_colldef=	colldef
_ctypedef=	ctypedef
_monetdef=	monetdef
_msgdef=	msgdef
_numericdef=	numericdef
_timedef=	timedef
.endif

.if ${MK_MAKE} != "no"
_mk=		mk
.endif

.if ${MK_MAN} != "no"
_man=		man
.endif

.if ${MK_SENDMAIL} != "no"
_sendmail=	 sendmail
.endif

.if ${MK_SHAREDOCS} != "no"
_doc=		doc
.endif

.if ${MK_SYSCONS} != "no"
_syscons=	syscons
.endif

SUBDIR.${MK_TESTS}+= tests

.if ${MK_VT} != "no"
_vt=		vt
.endif

.if ${MK_ZONEINFO} != "no"
_zoneinfo=	zoneinfo
.endif

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
