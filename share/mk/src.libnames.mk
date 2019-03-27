# $FreeBSD$
#
# The include file <src.libnames.mk> define library names suitable
# for INTERNALLIB and PRIVATELIB definition

.if !target(__<bsd.init.mk>__)
.error src.libnames.mk cannot be included directly.
.endif

.if !target(__<src.libnames.mk>__)
__<src.libnames.mk>__:

.include <src.opts.mk>

_PRIVATELIBS=	\
		atf_c \
		atf_cxx \
		bsdstat \
		devdctl \
		event \
		gmock \
		gtest \
		gmock_main \
		gtest_main \
		heimipcc \
		heimipcs \
		ldns \
		sqlite3 \
		ssh \
		ucl \
		unbound \
		zstd

_INTERNALLIBS=	\
		amu \
		bsnmptools \
		c_nossp_pic \
		cron \
		elftc \
		fifolog \
		ifconfig \
		ipf \
		lpr \
		netbsd \
		ntp \
		ntpevent \
		openbsd \
		opts \
		parse \
		pe \
		pmcstat \
		sl \
		sm \
		smdb \
		smutil \
		telnet \
		vers

_LIBRARIES=	\
		${_PRIVATELIBS} \
		${_INTERNALLIBS} \
		${LOCAL_LIBRARIES} \
		80211 \
		alias \
		archive \
		asn1 \
		auditd \
		avl \
		be \
		begemot \
		bluetooth \
		bsdxml \
		bsm \
		bsnmp \
		bz2 \
		c \
		c_pic \
		calendar \
		cam \
		casper \
		cap_dns \
		cap_fileargs \
		cap_grp \
		cap_pwd \
		cap_random \
		cap_sysctl \
		cap_syslog \
		com_err \
		compiler_rt \
		crypt \
		crypto \
		ctf \
		cuse \
		cxxrt \
		devctl \
		devdctl \
		devinfo \
		devstat \
		dialog \
		dl \
		dpv \
		dtrace \
		dwarf \
		edit \
		efivar \
		elf \
		execinfo \
		fetch \
		figpar \
		geom \
		gnuregex \
		gpio \
		gssapi \
		gssapi_krb5 \
		hdb \
		heimbase \
		heimntlm \
		heimsqlite \
		hx509 \
		ipsec \
		ipt \
		jail \
		kadm5clnt \
		kadm5srv \
		kafs5 \
		kdc \
		kiconv \
		krb5 \
		kvm \
		l \
		lzma \
		m \
		magic \
		md \
		memstat \
		mp \
		mt \
		nandfs \
		ncurses \
		ncursesw \
		netgraph \
		ngatm \
		nv \
		nvpair \
		opencsd \
		opie \
		pam \
		panel \
		panelw \
		pcap \
		pcsclite \
		pjdlog \
		pmc \
		proc \
		procstat \
		pthread \
		radius \
		regex \
		roken \
		rpcsec_gss \
		rpcsvc \
		rt \
		rtld_db \
		sbuf \
		sdp \
		sm \
		smb \
		ssl \
		ssp_nonshared \
		stdthreads \
		supcplusplus \
		sysdecode \
		tacplus \
		termcap \
		termcapw \
		ufs \
		ugidfw \
		ulog \
		umem \
		usb \
		usbhid \
		util \
		uutil \
		vmmapi \
		wind \
		wrap \
		xo \
		y \
		ypclnt \
		z \
		zfs_core \
		zfs \
		zpool \

.if ${MK_BLACKLIST} != "no"
_LIBRARIES+= \
		blacklist \

.endif

.if ${MK_OFED} != "no"
_LIBRARIES+= \
		cxgb4 \
		ibcm \
		ibmad \
		ibnetdisc \
		ibumad \
		ibverbs \
		mlx4 \
		mlx5 \
		rdmacm \
		osmcomp \
		opensm \
		osmvendor
.endif

.if ${MK_BEARSSL} == "yes"
_INTERNALLIBS+= \
		bearssl \
		secureboot \

LIBBEARSSL?=	${LIBBEARSSLDIR}/libbearssl${PIE_SUFFIX}.a
LIBSECUREBOOT?=	${LIBSECUREBOOTDIR}/libsecureboot${PIE_SUFFIX}.a
.endif

.if ${MK_VERIEXEC} == "yes"
_INTERNALLIBS+= veriexec

LIBVERIEXEC?=	${LIBVERIEXECDIR}/libveriexec${PIE_SUFFIX}.a
.endif

# Each library's LIBADD needs to be duplicated here for static linkage of
# 2nd+ order consumers.  Auto-generating this would be better.
_DP_80211=	sbuf bsdxml
_DP_archive=	z bz2 lzma bsdxml
_DP_zstd=	pthread
.if ${MK_BLACKLIST} != "no"
_DP_blacklist+=	pthread
.endif
_DP_crypto=	pthread
.if ${MK_OPENSSL} != "no"
_DP_archive+=	crypto
.else
_DP_archive+=	md
.endif
_DP_sqlite3=	pthread
_DP_ssl=	crypto
_DP_ssh=	crypto crypt z
.if ${MK_LDNS} != "no"
_DP_ssh+=	ldns
.endif
_DP_edit=	ncursesw
.if ${MK_OPENSSL} != "no"
_DP_bsnmp=	crypto
.endif
_DP_geom=	bsdxml sbuf
_DP_cam=	sbuf
_DP_kvm=	elf
_DP_casper=	nv
_DP_cap_dns=	nv
_DP_cap_fileargs=	nv
_DP_cap_grp=	nv
_DP_cap_pwd=	nv
_DP_cap_random=	nv
_DP_cap_sysctl=	nv
_DP_cap_syslog=	nv
.if ${MK_OFED} != "no"
_DP_pcap=	ibverbs mlx5
.endif
_DP_pjdlog=	util
_DP_opie=	md
_DP_usb=	pthread
_DP_unbound=	ssl crypto pthread
_DP_rt=	pthread
.if ${MK_OPENSSL} == "no"
_DP_radius=	md
.else
_DP_radius=	crypto
.endif
_DP_rtld_db=	elf procstat
_DP_procstat=	kvm util elf
.if ${MK_CXX} == "yes"
.if ${MK_LIBCPLUSPLUS} != "no"
_DP_proc=	cxxrt
.else
_DP_proc=	supcplusplus
.endif
.endif
.if ${MK_CDDL} != "no"
_DP_proc+=	ctf
.endif
_DP_proc+=	elf procstat rtld_db util
_DP_mp=	crypto
_DP_memstat=	kvm
_DP_magic=	z
_DP_mt=		sbuf bsdxml
_DP_ldns=	ssl crypto
.if ${MK_OPENSSL} != "no"
_DP_fetch=	ssl crypto
.else
_DP_fetch=	md
.endif
_DP_execinfo=	elf
_DP_dwarf=	elf
_DP_dpv=	dialog figpar util ncursesw
_DP_dialog=	ncursesw m
_DP_cuse=	pthread
_DP_atf_cxx=	atf_c
_DP_gtest=	pthread
_DP_gmock=	gtest
_DP_gmock_main=	gmock
_DP_gtest_main=	gtest
_DP_devstat=	kvm
_DP_pam=	radius tacplus opie md util
.if ${MK_KERBEROS} != "no"
_DP_pam+=	krb5
.endif
.if ${MK_OPENSSH} != "no"
_DP_pam+=	ssh
.endif
.if ${MK_NIS} != "no"
_DP_pam+=	ypclnt
.endif
_DP_roken=	crypt
_DP_kadm5clnt=	com_err krb5 roken
_DP_kadm5srv=	com_err hdb krb5 roken
_DP_heimntlm=	crypto com_err krb5 roken
_DP_hx509=	asn1 com_err crypto roken wind
_DP_hdb=	asn1 com_err krb5 roken sqlite3
_DP_asn1=	com_err roken
_DP_kdc=	roken hdb hx509 krb5 heimntlm asn1 crypto
_DP_wind=	com_err roken
_DP_heimbase=	pthread
_DP_heimipcc=	heimbase roken pthread
_DP_heimipcs=	heimbase roken pthread
_DP_kafs5=	asn1 krb5 roken
_DP_krb5+=	asn1 com_err crypt crypto hx509 roken wind heimbase heimipcc
_DP_gssapi_krb5+=	gssapi krb5 crypto roken asn1 com_err
_DP_lzma=	pthread
_DP_ucl=	m
_DP_vmmapi=	util
_DP_opencsd=	cxxrt
_DP_ctf=	z
_DP_dtrace=	ctf elf proc pthread rtld_db
_DP_xo=		util
# The libc dependencies are not strictly needed but are defined to make the
# assert happy.
_DP_c=		compiler_rt
.if ${MK_SSP} != "no"
_DP_c+=		ssp_nonshared
.endif
_DP_stdthreads=	pthread
_DP_tacplus=	md
_DP_panel=	ncurses
_DP_panelw=	ncursesw
_DP_rpcsec_gss=	gssapi
_DP_smb=	kiconv
_DP_ulog=	md
_DP_fifolog=	z
_DP_ipf=	kvm
_DP_zfs=	md pthread umem util uutil m nvpair avl bsdxml geom nvpair z \
		zfs_core
_DP_zfs_core=	nvpair
_DP_zpool=	md pthread z nvpair avl umem
_DP_be=		zfs nvpair

# OFED support
.if ${MK_OFED} != "no"
_DP_cxgb4=	ibverbs pthread
_DP_ibcm=	ibverbs
_DP_ibmad=	ibumad
_DP_ibnetdisc=	osmcomp ibmad ibumad
_DP_ibumad=	
_DP_ibverbs=
_DP_mlx4=	ibverbs pthread
_DP_mlx5=	ibverbs pthread
_DP_rdmacm=	ibverbs
_DP_osmcomp=	pthread
_DP_opensm=	pthread
_DP_osmvendor=	ibumad pthread
.endif

# Define special cases
LDADD_supcplusplus=	-lsupc++
LIBATF_C=	${LIBDESTDIR}${LIBDIR_BASE}/libprivateatf-c.a
LIBATF_CXX=	${LIBDESTDIR}${LIBDIR_BASE}/libprivateatf-c++.a
LDADD_atf_c=	-lprivateatf-c
LDADD_atf_cxx=	-lprivateatf-c++

LIBGMOCK=	${LIBDESTDIR}${LIBDIR_BASE}/libprivategmock.a
LIBGMOCK_MAIN=	${LIBDESTDIR}${LIBDIR_BASE}/libprivategmock_main.a
LIBGTEST=	${LIBDESTDIR}${LIBDIR_BASE}/libprivategtest.a
LIBGTEST_MAIN=	${LIBDESTDIR}${LIBDIR_BASE}/libprivategtest_main.a
LDADD_gmock=	-lprivategmock
LDADD_gtest=	-lprivategtest
LDADD_gmock_main= -lprivategmock_main
LDADD_gtest_main= -lprivategtest_main

.for _l in ${_PRIVATELIBS}
LIB${_l:tu}?=	${LIBDESTDIR}${LIBDIR_BASE}/libprivate${_l}.a
.endfor

.if ${MK_PIE} != "no"
PIE_SUFFIX=	_pie
.endif

.for _l in ${_LIBRARIES}
.if ${_INTERNALLIBS:M${_l}} || !defined(SYSROOT)
LDADD_${_l}_L+=		-L${LIB${_l:tu}DIR}
.endif
DPADD_${_l}?=	${LIB${_l:tu}}
.if ${_PRIVATELIBS:M${_l}}
LDADD_${_l}?=	-lprivate${_l}
.elif ${_INTERNALLIBS:M${_l}}
LDADD_${_l}?=	${LDADD_${_l}_L} -l${_l:S/${PIE_SUFFIX}//}${PIE_SUFFIX}
.else
LDADD_${_l}?=	${LDADD_${_l}_L} -l${_l}
.endif
# Add in all dependencies for static linkage.
.if defined(_DP_${_l}) && (${_INTERNALLIBS:M${_l}} || \
    (defined(NO_SHARED) && ${NO_SHARED:tl} != "no"))
.for _d in ${_DP_${_l}}
DPADD_${_l}+=	${DPADD_${_d}}
LDADD_${_l}+=	${LDADD_${_d}}
.endfor
.endif
.endfor

# These are special cases where the library is broken and anything that uses
# it needs to add more dependencies.  Broken usually means that it has a
# cyclic dependency and cannot link its own dependencies.  This is bad, please
# fix the library instead.
# Unless the library itself is broken then the proper place to define
# dependencies is _DP_* above.

# libatf-c++ exposes libatf-c abi hence we need to explicit link to atf_c for
# atf_cxx
DPADD_atf_cxx+=	${DPADD_atf_c}
LDADD_atf_cxx+=	${LDADD_atf_c}

DPADD_gmock+=	${DPADD_gtest}
LDADD_gmock+=	${LDADD_gtest}

DPADD_gmock_main+=	${DPADD_gmock}
LDADD_gmock_main+=	${LDADD_gmock}

DPADD_gtest_main+=	${DPADD_gtest}
LDADD_gtest_main+=	${LDADD_gtest}

# Detect LDADD/DPADD that should be LIBADD, before modifying LDADD here.
_BADLDADD=
.for _l in ${LDADD:M-l*:N-l*/*:C,^-l,,}
.if ${_LIBRARIES:M${_l}} && !${_PRIVATELIBS:M${_l}}
_BADLDADD+=	${_l}
.endif
.endfor
.if !empty(_BADLDADD)
.error ${.CURDIR}: These libraries should be LIBADD+=foo rather than DPADD/LDADD+=-lfoo: ${_BADLDADD}
.endif

.for _l in ${LIBADD}
DPADD+=		${DPADD_${_l}}
LDADD+=		${LDADD_${_l}}
.endfor

# INTERNALLIB definitions.
LIBELFTCDIR=	${OBJTOP}/lib/libelftc
LIBELFTC?=	${LIBELFTCDIR}/libelftc${PIE_SUFFIX}.a

LIBPEDIR=	${OBJTOP}/lib/libpe
LIBPE?=		${LIBPEDIR}/libpe${PIE_SUFFIX}.a

LIBOPENBSDDIR=	${OBJTOP}/lib/libopenbsd
LIBOPENBSD?=	${LIBOPENBSDDIR}/libopenbsd${PIE_SUFFIX}.a

LIBSMDIR=	${OBJTOP}/lib/libsm
LIBSM?=		${LIBSMDIR}/libsm${PIE_SUFFIX}.a

LIBSMDBDIR=	${OBJTOP}/lib/libsmdb
LIBSMDB?=	${LIBSMDBDIR}/libsmdb${PIE_SUFFIX}.a

LIBSMUTILDIR=	${OBJTOP}/lib/libsmutil
LIBSMUTIL?=	${LIBSMUTILDIR}/libsmutil${PIE_SUFFIX}.a

LIBNETBSDDIR?=	${OBJTOP}/lib/libnetbsd
LIBNETBSD?=	${LIBNETBSDDIR}/libnetbsd${PIE_SUFFIX}.a

LIBVERSDIR?=	${OBJTOP}/kerberos5/lib/libvers
LIBVERS?=	${LIBVERSDIR}/libvers${PIE_SUFFIX}.a

LIBSLDIR=	${OBJTOP}/kerberos5/lib/libsl
LIBSL?=		${LIBSLDIR}/libsl${PIE_SUFFIX}.a

LIBIFCONFIGDIR=	${OBJTOP}/lib/libifconfig
LIBIFCONFIG?=	${LIBIFCONFIGDIR}/libifconfig${PIE_SUFFIX}.a

LIBIPFDIR=	${OBJTOP}/sbin/ipf/libipf
LIBIPF?=	${LIBIPFDIR}/libipf${PIE_SUFFIX}.a

LIBTELNETDIR=	${OBJTOP}/lib/libtelnet
LIBTELNET?=	${LIBTELNETDIR}/libtelnet${PIE_SUFFIX}.a

LIBCRONDIR=	${OBJTOP}/usr.sbin/cron/lib
LIBCRON?=	${LIBCRONDIR}/libcron${PIE_SUFFIX}.a

LIBNTPDIR=	${OBJTOP}/usr.sbin/ntp/libntp
LIBNTP?=	${LIBNTPDIR}/libntp${PIE_SUFFIX}.a

LIBNTPEVENTDIR=	${OBJTOP}/usr.sbin/ntp/libntpevent
LIBNTPEVENT?=	${LIBNTPEVENTDIR}/libntpevent${PIE_SUFFIX}.a

LIBOPTSDIR=	${OBJTOP}/usr.sbin/ntp/libopts
LIBOPTS?=	${LIBOPTSDIR}/libopts${PIE_SUFFIX}.a

LIBPARSEDIR=	${OBJTOP}/usr.sbin/ntp/libparse
LIBPARSE?=	${LIBPARSEDIR}/libparse${PIE_SUFFIX}.a

LIBLPRDIR=	${OBJTOP}/usr.sbin/lpr/common_source
LIBLPR?=	${LIBLPRDIR}/liblpr${PIE_SUFFIX}.a

LIBFIFOLOGDIR=	${OBJTOP}/usr.sbin/fifolog/lib
LIBFIFOLOG?=	${LIBFIFOLOGDIR}/libfifolog${PIE_SUFFIX}.a

LIBBSNMPTOOLSDIR=	${OBJTOP}/usr.sbin/bsnmpd/tools/libbsnmptools
LIBBSNMPTOOLS?=	${LIBBSNMPTOOLSDIR}/libbsnmptools${PIE_SUFFIX}.a

LIBAMUDIR=	${OBJTOP}/usr.sbin/amd/libamu
LIBAMU?=	${LIBAMUDIR}/libamu${PIE_SUFFIX}.a

LIBBE?=		${LIBBEDIR}/libbe${PIE_SUFFIX}.a

LIBPMCSTATDIR=	${OBJTOP}/lib/libpmcstat
LIBPMCSTAT?=	${LIBPMCSTATDIR}/libpmcstat${PIE_SUFFIX}.a

LIBC_NOSSP_PICDIR=	${OBJTOP}/lib/libc
LIBC_NOSSP_PIC?=	${LIBC_NOSSP_PICDIR}/libc_nossp_pic.a

# Define a directory for each library.  This is useful for adding -L in when
# not using a --sysroot or for meta mode bootstrapping when there is no
# Makefile.depend.  These are sorted by directory.
LIBAVLDIR=	${OBJTOP}/cddl/lib/libavl
LIBCTFDIR=	${OBJTOP}/cddl/lib/libctf
LIBDTRACEDIR=	${OBJTOP}/cddl/lib/libdtrace
LIBNVPAIRDIR=	${OBJTOP}/cddl/lib/libnvpair
LIBUMEMDIR=	${OBJTOP}/cddl/lib/libumem
LIBUUTILDIR=	${OBJTOP}/cddl/lib/libuutil
LIBZFSDIR=	${OBJTOP}/cddl/lib/libzfs
LIBZFS_COREDIR=	${OBJTOP}/cddl/lib/libzfs_core
LIBZPOOLDIR=	${OBJTOP}/cddl/lib/libzpool

# OFED support
LIBCXGB4DIR=	${OBJTOP}/lib/ofed/libcxgb4
LIBIBCMDIR=	${OBJTOP}/lib/ofed/libibcm
LIBIBMADDIR=	${OBJTOP}/lib/ofed/libibmad
LIBIBNETDISCDIR=${OBJTOP}/lib/ofed/libibnetdisc
LIBIBUMADDIR=	${OBJTOP}/lib/ofed/libibumad
LIBIBVERBSDIR=	${OBJTOP}/lib/ofed/libibverbs
LIBMLX4DIR=	${OBJTOP}/lib/ofed/libmlx4
LIBMLX5DIR=	${OBJTOP}/lib/ofed/libmlx5
LIBRDMACMDIR=	${OBJTOP}/lib/ofed/librdmacm
LIBOSMCOMPDIR=	${OBJTOP}/lib/ofed/complib
LIBOPENSMDIR=	${OBJTOP}/lib/ofed/libopensm
LIBOSMVENDORDIR=${OBJTOP}/lib/ofed/libvendor

LIBDIALOGDIR=	${OBJTOP}/gnu/lib/libdialog
LIBGCOVDIR=	${OBJTOP}/gnu/lib/libgcov
LIBGOMPDIR=	${OBJTOP}/gnu/lib/libgomp
LIBGNUREGEXDIR=	${OBJTOP}/gnu/lib/libregex
LIBSSPDIR=	${OBJTOP}/gnu/lib/libssp
LIBSSP_NONSHAREDDIR=	${OBJTOP}/gnu/lib/libssp/libssp_nonshared
LIBSUPCPLUSPLUSDIR=	${OBJTOP}/gnu/lib/libsupc++
LIBASN1DIR=	${OBJTOP}/kerberos5/lib/libasn1
LIBGSSAPI_KRB5DIR=	${OBJTOP}/kerberos5/lib/libgssapi_krb5
LIBGSSAPI_NTLMDIR=	${OBJTOP}/kerberos5/lib/libgssapi_ntlm
LIBGSSAPI_SPNEGODIR=	${OBJTOP}/kerberos5/lib/libgssapi_spnego
LIBHDBDIR=	${OBJTOP}/kerberos5/lib/libhdb
LIBHEIMBASEDIR=	${OBJTOP}/kerberos5/lib/libheimbase
LIBHEIMIPCCDIR=	${OBJTOP}/kerberos5/lib/libheimipcc
LIBHEIMIPCSDIR=	${OBJTOP}/kerberos5/lib/libheimipcs
LIBHEIMNTLMDIR=	${OBJTOP}/kerberos5/lib/libheimntlm
LIBHX509DIR=	${OBJTOP}/kerberos5/lib/libhx509
LIBKADM5CLNTDIR=	${OBJTOP}/kerberos5/lib/libkadm5clnt
LIBKADM5SRVDIR=	${OBJTOP}/kerberos5/lib/libkadm5srv
LIBKAFS5DIR=	${OBJTOP}/kerberos5/lib/libkafs5
LIBKDCDIR=	${OBJTOP}/kerberos5/lib/libkdc
LIBKRB5DIR=	${OBJTOP}/kerberos5/lib/libkrb5
LIBROKENDIR=	${OBJTOP}/kerberos5/lib/libroken
LIBWINDDIR=	${OBJTOP}/kerberos5/lib/libwind
LIBATF_CDIR=	${OBJTOP}/lib/atf/libatf-c
LIBATF_CXXDIR=	${OBJTOP}/lib/atf/libatf-c++
LIBGMOCKDIR=	${OBJTOP}/lib/googletest/gmock
LIBGMOCK_MAINDIR=	${OBJTOP}/lib/googletest/gmock_main
LIBGTESTDIR=	${OBJTOP}/lib/googletest/gtest
LIBGTEST_MAINDIR=	${OBJTOP}/lib/googletest/gtest_main
LIBALIASDIR=	${OBJTOP}/lib/libalias/libalias
LIBBLACKLISTDIR=	${OBJTOP}/lib/libblacklist
LIBBLOCKSRUNTIMEDIR=	${OBJTOP}/lib/libblocksruntime
LIBBSNMPDIR=	${OBJTOP}/lib/libbsnmp/libbsnmp
LIBCASPERDIR=	${OBJTOP}/lib/libcasper/libcasper
LIBCAP_DNSDIR=	${OBJTOP}/lib/libcasper/services/cap_dns
LIBCAP_GRPDIR=	${OBJTOP}/lib/libcasper/services/cap_grp
LIBCAP_PWDDIR=	${OBJTOP}/lib/libcasper/services/cap_pwd
LIBCAP_RANDOMDIR=	${OBJTOP}/lib/libcasper/services/cap_random
LIBCAP_SYSCTLDIR=	${OBJTOP}/lib/libcasper/services/cap_sysctl
LIBCAP_SYSLOGDIR=	${OBJTOP}/lib/libcasper/services/cap_syslog
LIBBSDXMLDIR=	${OBJTOP}/lib/libexpat
LIBKVMDIR=	${OBJTOP}/lib/libkvm
LIBPTHREADDIR=	${OBJTOP}/lib/libthr
LIBMDIR=	${OBJTOP}/lib/msun
LIBFORMDIR=	${OBJTOP}/lib/ncurses/form
LIBFORMLIBWDIR=	${OBJTOP}/lib/ncurses/formw
LIBMENUDIR=	${OBJTOP}/lib/ncurses/menu
LIBMENULIBWDIR=	${OBJTOP}/lib/ncurses/menuw
LIBNCURSESDIR=	${OBJTOP}/lib/ncurses/ncurses
LIBNCURSESWDIR=	${OBJTOP}/lib/ncurses/ncursesw
LIBPANELDIR=	${OBJTOP}/lib/ncurses/panel
LIBPANELWDIR=	${OBJTOP}/lib/ncurses/panelw
LIBCRYPTODIR=	${OBJTOP}/secure/lib/libcrypto
LIBSSHDIR=	${OBJTOP}/secure/lib/libssh
LIBSSLDIR=	${OBJTOP}/secure/lib/libssl
LIBTEKENDIR=	${OBJTOP}/sys/teken/libteken
LIBEGACYDIR=	${OBJTOP}/tools/build
LIBLNDIR=	${OBJTOP}/usr.bin/lex/lib

LIBTERMCAPDIR=	${LIBNCURSESDIR}
LIBTERMCAPWDIR=	${LIBNCURSESWDIR}

# Default other library directories to lib/libNAME.
.for lib in ${_LIBRARIES}
LIB${lib:tu}DIR?=	${OBJTOP}/lib/lib${lib}
.endfor

# Validate that listed LIBADD are valid.
.for _l in ${LIBADD}
.if empty(_LIBRARIES:M${_l})
_BADLIBADD+= ${_l}
.endif
.endfor
.if !empty(_BADLIBADD)
.error ${.CURDIR}: Invalid LIBADD used which may need to be added to ${_this:T}: ${_BADLIBADD}
.endif

# Sanity check that libraries are defined here properly when building them.
.if defined(LIB) && ${_LIBRARIES:M${LIB}} != ""
.if !empty(LIBADD) && \
    (!defined(_DP_${LIB}) || ${LIBADD:O:u} != ${_DP_${LIB}:O:u})
.error ${.CURDIR}: Missing or incorrect _DP_${LIB} entry in ${_this:T}.  Should match LIBADD for ${LIB} ('${LIBADD}' vs '${_DP_${LIB}}')
.endif
# Note that OBJTOP is not yet defined here but for the purpose of the check
# it is fine as it resolves to the SRC directory.
.if !defined(LIB${LIB:tu}DIR) || !exists(${SRCTOP}/${LIB${LIB:tu}DIR:S,^${OBJTOP}/,,})
.error ${.CURDIR}: Missing or incorrect value for LIB${LIB:tu}DIR in ${_this:T}: ${LIB${LIB:tu}DIR:S,^${OBJTOP}/,,}
.endif
.if ${_INTERNALLIBS:M${LIB}} != "" && !defined(LIB${LIB:tu})
.error ${.CURDIR}: Missing value for LIB${LIB:tu} in ${_this:T}.  Likely should be: LIB${LIB:tu}?= $${LIB${LIB:tu}DIR}/lib${LIB}.a
.endif
.endif

.endif	# !target(__<src.libnames.mk>__)
