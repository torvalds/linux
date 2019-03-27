# $FreeBSD$

PROG=	acpidb
SRCS=	acpidb.c

# common
SRCS+=	acfileio.c acgetline.c ahids.c ahuuids.c cmfsize.c

# components/debugger
SRCS+=	dbcmds.c dbconvert.c dbdisply.c dbexec.c dbfileio.c	\
	dbhistry.c dbinput.c dbmethod.c dbnames.c dbobject.c	\
	dbstats.c dbtest.c dbutils.c dbxface.c

# components/disassembler
SRCS+=	dmbuffer.c dmcstyle.c dmdeferred.c dmnames.c dmopcode.c	\
	dmresrc.c dmresrcl.c dmresrcl2.c dmresrcs.c dmutils.c	\
	dmwalk.c

# components/dispatcher
SRCS+=	dsargs.c dscontrol.c dsdebug.c dsfield.c dsinit.c	\
	dsmethod.c dsmthdat.c dsobject.c dsopcode.c dspkginit.c	\
	dsutils.c dswexec.c dswload.c dswload2.c dswscope.c	\
	dswstate.c

# components/events
SRCS+=	evevent.c evglock.c evgpe.c evgpeblk.c evgpeinit.c	\
	evgpeutil.c evhandler.c evmisc.c evregion.c evrgnini.c	\
	evsci.c evxface.c evxfevnt.c evxfregn.c

# components/executer
SRCS+=	exconcat.c exconfig.c exconvrt.c excreate.c exdebug.c	\
	exdump.c exfield.c exfldio.c exmisc.c exmutex.c		\
	exnames.c exoparg1.c exoparg2.c exoparg3.c exoparg6.c	\
	exprep.c exregion.c exresnte.c exresolv.c exresop.c	\
	exserial.c exstore.c exstoren.c exstorob.c exsystem.c	\
	extrace.c exutils.c

# components/hardware
SRCS+=	hwacpi.c hwesleep.c hwgpe.c hwpci.c hwregs.c hwsleep.c	\
	hwvalid.c hwxface.c hwxfsleep.c

# components/namespace
SRCS+=	nsaccess.c nsalloc.c nsarguments.c nsconvert.c nsdump.c	\
	nseval.c nsinit.c nsload.c nsnames.c nsobject.c		\
	nsparse.c nspredef.c nsprepkg.c nsrepair.c nsrepair2.c	\
	nssearch.c nsutils.c nswalk.c nsxfeval.c nsxfname.c	\
	nsxfobj.c

# components/parser
SRCS+=	psargs.c psloop.c psobject.c psopcode.c psopinfo.c	\
	psparse.c psscope.c pstree.c psutils.c pswalk.c		\
	psxface.c

# components/resources
SRCS+=	rsaddr.c rscalc.c rscreate.c rsdump.c rsdumpinfo.c	\
	rsinfo.c rsio.c rsirq.c rslist.c rsmemory.c rsmisc.c	\
	rsserial.c rsutils.c rsxface.c

# components/tables
SRCS+=	tbdata.c tbfadt.c tbfind.c tbinstal.c tbprint.c		\
	tbutils.c tbxface.c tbxfload.c

# components/utilities
SRCS+=	utaddress.c utalloc.c utascii.c utbuffer.c utcache.c	\
	utcopy.c utdebug.c utdecode.c utdelete.c uterror.c	\
	uteval.c utexcep.c utglobal.c uthex.c utids.c utinit.c	\
	utlock.c utmath.c utmisc.c utmutex.c utnonansi.c	\
	utobject.c utosi.c utownerid.c utpredef.c utresdecode.c	\
	utresrc.c utstate.c utstring.c utstrsuppt.c		\
	utstrtoul64.c uttrack.c utuuid.c utxface.c utxferror.c	\
	utxfinit.c

# os_specific/service_layers
SRCS+=	osgendbg.c osunixxf.c

MAN=	acpidb.8
WARNS?=	3

CFLAGS+= -DACPI_DB_APP -DACPI_EXEC_APP -fno-strict-aliasing
LIBADD=	pthread

.include <bsd.prog.mk>

# clang 6.0.0 and higher warn about the ACPI_ROOT_OBJECT and
# ACPI_TO_POINTER macros from sys/contrib/dev/acpica/include/actypes.h,
# that they use arithmetic on a null pointer treated as a cast from
# integer to pointer, which is a GNU extension.
#
# Turn off the warning, because this is in contributed code.
.if ${COMPILER_TYPE} == "clang" && ${COMPILER_VERSION} >= 60000
CWARNFLAGS+=	-Wno-null-pointer-arithmetic
.endif
