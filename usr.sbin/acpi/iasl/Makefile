# $FreeBSD$

PROG=	iasl

# common
SRCS=	acfileio.c adfile.c adisasm.c adwalk.c ahids.c		\
	ahpredef.c ahtable.c ahuuids.c cmfsize.c dmextern.c	\
	dmrestag.c dmswitch.c dmtable.c dmtables.c dmtbdump.c	\
	dmtbdump1.c dmtbdump2.c dmtbdump3.c dmtbinfo.c		\
	dmtbinfo1.c dmtbinfo2.c dmtbinfo3.c getopt.c

# compiler
SRCS+=	aslallocate.c aslanalyze.c aslascii.c aslbtypes.c	\
	aslcache.c aslcodegen.c aslcompile.c aslcompiler.y.h	\
	aslcompilerlex.c aslcompilerparse.c asldebug.c		\
	aslerror.c aslexternal.c aslfileio.c aslfiles.c		\
	aslfold.c aslhelp.c aslhex.c asllength.c asllisting.c	\
	asllistsup.c aslload.c asllookup.c aslmain.c aslmap.c	\
	aslmapenter.c aslmapoutput.c aslmaputils.c		\
	aslmessages.c aslmethod.c aslnamesp.c asloffset.c	\
	aslopcodes.c asloperands.c aslopt.c asloptions.c	\
	aslparseop.c aslpld.c aslpredef.c aslprepkg.c		\
	aslprintf.c aslprune.c aslresource.c aslrestype1.c	\
	aslrestype1i.c aslrestype2.c aslrestype2d.c		\
	aslrestype2e.c aslrestype2q.c aslrestype2s.c		\
	aslrestype2w.c aslstartup.c aslstubs.c asltransform.c	\
	asltree.c aslutils.c asluuid.c aslwalks.c aslxref.c	\
	aslxrefout.c cvcompiler.c cvdisasm.c cvparser.c		\
	dtcompile.c dtexpress.c dtfield.c dtio.c dtparser.y.h	\
	dtparserlex.c dtparserparse.c dtsubtable.c dttable.c	\
	dttable1.c dttable2.c dttemplate.c dtutils.c		\
	prexpress.c prmacros.c prparser.y.h prparserlex.c	\
	prparserparse.c prscan.c prutils.c

# components/debugger
SRCS+=	dbfileio.c

# components/disassembler
SRCS+=	dmbuffer.c dmcstyle.c dmdeferred.c dmnames.c dmopcode.c	\
	dmresrc.c dmresrcl.c dmresrcl2.c dmresrcs.c dmutils.c	\
	dmwalk.c

# components/dispatcher
SRCS+=	dsargs.c dscontrol.c dsfield.c dsobject.c dsopcode.c	\
	dspkginit.c dsutils.c dswexec.c dswload.c dswload2.c	\
	dswscope.c dswstate.c

# components/executer
SRCS+=	exconcat.c exconvrt.c excreate.c exdump.c exmisc.c	\
	exmutex.c exnames.c exoparg1.c exoparg2.c exoparg3.c	\
	exoparg6.c exprep.c exresnte.c exresolv.c exresop.c	\
	exstore.c exstoren.c exstorob.c exsystem.c exutils.c

# components/parser
SRCS+=	psargs.c psloop.c psobject.c psopcode.c psopinfo.c	\
	psparse.c psscope.c pstree.c psutils.c pswalk.c

# components/namespace
SRCS+=	nsaccess.c nsalloc.c nsdump.c nsnames.c nsobject.c	\
	nsparse.c nssearch.c nsutils.c nswalk.c

# components/tables
SRCS+=	tbdata.c tbfadt.c tbinstal.c tbprint.c tbutils.c	\
	tbxface.c tbxfload.c

# components/utilities
SRCS+=	utaddress.c utalloc.c utascii.c utbuffer.c utcache.c	\
	utcopy.c utdebug.c utdecode.c utdelete.c uterror.c	\
	utexcep.c utglobal.c uthex.c utinit.c utlock.c utmath.c	\
	utmisc.c utmutex.c utnonansi.c utobject.c utownerid.c	\
	utpredef.c utresdecode.c utresrc.c utstate.c utstring.c	\
	utstrsuppt.c utstrtoul64.c utuuid.c utxface.c		\
	utxferror.c

# os_specific/service_layers
SRCS+=	osunixxf.c

WARNS?=	2

MAN=	iasl.8

CFLAGS+= -DACPI_ASL_COMPILER -I.
LFLAGS= -i -s
YFLAGS= -d

CLEANFILES= aslcompiler.y aslcompiler.y.h aslcompilerlex.c	\
	aslcompilerparse.c aslcompilerparse.h dtparser.y.h	\
	dtparserlex.c dtparserparse.c dtparserparse.h		\
	prparser.y.h prparserlex.c prparserparse.c		\
	prparserparse.h

PARSER=	aslcstyle.y aslhelpers.y aslkeywords.y aslparser.y	\
	aslprimaries.y aslresources.y aslrules.y aslsupport.y	\
	asltokens.y asltypes.y

aslcompilerlex.c: aslcompiler.l aslsupport.l
	${LEX} ${LFLAGS} -PAslCompiler -o${.TARGET} \
	    ${ACPICA_DIR}/compiler/aslcompiler.l

aslcompiler.y: ${PARSER}
	m4 -P -I${ACPICA_DIR}/compiler \
	    ${ACPICA_DIR}/compiler/aslparser.y > ${.TARGET}

.ORDER: aslcompilerparse.c aslcompilerparse.h
aslcompilerparse.h: .NOMETA
aslcompilerparse.c aslcompilerparse.h: aslcompiler.y
	${YACC} ${YFLAGS} -pAslCompiler -oaslcompilerparse.c ${.ALLSRC}

aslcompiler.y.h: aslcompilerparse.h .NOMETA
	ln -f ${.ALLSRC} ${.TARGET}

dtparserlex.c: dtparser.l
	${LEX} ${LFLAGS} -PDtParser -o${.TARGET} ${.ALLSRC}

.ORDER: dtparserparse.c dtparserparse.h
dtparserparse.h: .NOMETA
dtparserparse.c dtparserparse.h: dtparser.y
	${YACC} ${YFLAGS} -pDtParser -odtparserparse.c ${.ALLSRC}

dtparser.y.h: dtparserparse.h .NOMETA
	ln -f ${.ALLSRC} ${.TARGET}

prparserlex.c: prparser.l
	${LEX} ${LFLAGS} -PPrParser -o${.TARGET} ${.ALLSRC}

.ORDER: prparserparse.c prparserparse.h
prparserparse.h: .NOMETA
prparserparse.c prparserparse.h: prparser.y
	${YACC} ${YFLAGS} -pPrParser -oprparserparse.c ${.ALLSRC}

prparser.y.h: prparserparse.h .NOMETA
	ln -f ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
