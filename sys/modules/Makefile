# $FreeBSD$

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

SUBDIR_PARALLEL=

# Modules that include binary-only blobs of microcode should be selectable by
# MK_SOURCELESS_UCODE option (see below).

.if defined(MODULES_OVERRIDE) && !defined(ALL_MODULES)
SUBDIR=${MODULES_OVERRIDE}
.else
SUBDIR=	\
	${_3dfx} \
	${_3dfx_linux} \
	${_aac} \
	${_aacraid} \
	accf_data \
	accf_dns \
	accf_http \
	acl_nfs4 \
	acl_posix1e \
	${_acpi} \
	ae \
	${_aesni} \
	age \
	${_agp} \
	ahci \
	aic7xxx \
	alc \
	ale \
	alq \
	${_amd_ecc_inject} \
	${_amdgpio} \
	${_amdsbwd} \
	${_amdsmn} \
	${_amdtemp} \
	amr \
	${_an} \
	${_aout} \
	${_apm} \
	${_arcmsr} \
	${_allwinner} \
	${_armv8crypto} \
	${_asmc} \
	ata \
	ath \
	ath_dfs \
	ath_hal \
	ath_hal_ar5210 \
	ath_hal_ar5211 \
	ath_hal_ar5212 \
	ath_hal_ar5416 \
	ath_hal_ar9300 \
	ath_main \
	ath_rate \
	ath_pci \
	${_autofs} \
	${_auxio} \
	${_bce} \
	${_bcm283x_clkman} \
	${_bcm283x_pwm} \
	bfe \
	bge \
	bhnd \
	${_bxe} \
	${_bios} \
	${_bktr} \
	${_blake2} \
	${_bm} \
	bnxt \
	bridgestp \
	bwi \
	bwn \
	${_bytgpio} \
	${_chvgpio} \
	cam \
	${_cardbus} \
	${_carp} \
	cas \
	${_cbb} \
	cc \
	${_ccp} \
	cd9660 \
	cd9660_iconv \
	${_ce} \
	${_cfi} \
	${_chromebook_platform} \
	${_ciss} \
	cloudabi \
	${_cloudabi32} \
	${_cloudabi64} \
	${_cmx} \
	${_coretemp} \
	${_cp} \
	${_cpsw} \
	${_cpuctl} \
	${_cpufreq} \
	${_crypto} \
	${_cryptodev} \
	${_cs} \
	${_ctau} \
	ctl \
	${_cxgb} \
	${_cxgbe} \
	dc \
	dcons \
	dcons_crom \
	de \
	${_dpms} \
	dummynet \
	${_ed} \
	${_efirt} \
	${_em} \
	${_ena} \
	${_ep} \
	${_epic} \
	esp \
	${_et} \
	evdev \
	${_ex} \
	${_exca} \
	ext2fs \
	fdc \
	fdescfs \
	${_fe} \
	${_ffec} \
	filemon \
	firewire \
	firmware \
	fusefs \
	${_fxp} \
	gem \
	geom \
	${_glxiic} \
	${_glxsb} \
	gpio \
	hifn \
	hme \
	${_hpt27xx} \
	${_hptiop} \
	${_hptmv} \
	${_hptnr} \
	${_hptrr} \
	hwpmc \
	${_hwpmc_mips24k} \
	${_hwpmc_mips74k} \
	${_hyperv} \
	i2c \
	${_iavf} \
        ${_ibcore} \
	${_ichwd} \
	${_ida} \
	if_bridge \
	if_disc \
	if_edsc \
	${_if_enc} \
	if_epair \
	${_if_gif} \
	${_if_gre} \
	${_if_me} \
	if_lagg \
	${_if_ndis} \
	${_if_stf} \
	if_tap \
	if_tun \
	if_vlan \
	if_vxlan \
	iflib \
	${_iir} \
	imgact_binmisc \
	${_intelspi} \
	${_io} \
	${_ioat} \
        ${_ipoib} \
	${_ipdivert} \
	${_ipfilter} \
	${_ipfw} \
	ipfw_nat \
	${_ipfw_nat64} \
	${_ipfw_nptv6} \
	${_ipfw_pmod} \
	${_ipmi} \
	ip6_mroute_mod \
	ip_mroute_mod \
	${_ips} \
	${_ipsec} \
	${_ipw} \
	${_ipwfw} \
	${_isci} \
	${_iser} \
	isp \
	${_ispfw} \
	${_iwi} \
	${_iwifw} \
	${_iwm} \
	${_iwmfw} \
	${_iwn} \
	${_iwnfw} \
	${_ix} \
	${_ixv} \
	${_ixl} \
	jme \
	kbdmux \
	kgssapi \
	kgssapi_krb5 \
	khelp \
	krpc \
	ksyms \
	le \
	lge \
	libalias \
	libiconv \
	libmchain \
	${_linux} \
	${_linux_common} \
	${_linux64} \
	linuxkpi \
	${_lio} \
	lpt \
	mac_biba \
	mac_bsdextended \
	mac_ifoff \
	mac_lomac \
	mac_mls \
	mac_none \
	mac_ntpd \
	mac_partition \
	mac_portacl \
	mac_seeotheruids \
	mac_stub \
	mac_test \
	malo \
	md \
	mdio \
	mem \
	mfi \
	mii \
	mlx \
	${_mlx4} \
	${_mlx4ib} \
	${_mlx4en} \
	${_mlx5} \
	${_mlx5en} \
	${_mlx5ib} \
	${_mly} \
	mmc \
	mmcsd \
	${_mpr} \
	${_mps} \
	mpt \
	mqueue \
	mrsas \
	msdosfs \
	msdosfs_iconv \
	msk \
	${_mthca} \
	mvs \
	mwl \
	${_mwlfw} \
	mxge \
	my \
	${_nandfs} \
	${_nandsim} \
	${_nctgpio} \
	${_ndis} \
	${_netgraph} \
	${_nfe} \
	nfscl \
	nfscommon \
	nfsd \
	nfslock \
	nfslockd \
	nfssvc \
	nge \
	nmdm \
	nullfs \
	${_ntb} \
	${_nvd} \
	${_nvdimm} \
	${_nvme} \
	${_nvram} \
	oce \
	${_ocs_fc} \
	otus \
	${_otusfw} \
	ow \
	${_padlock} \
	${_padlock_rng} \
	${_pccard} \
	${_pcfclock} \
	pcn \
	${_pf} \
	${_pflog} \
	${_pfsync} \
	plip \
	${_pms} \
	ppbus \
	ppc \
	ppi \
	pps \
	procfs \
	proto \
	pseudofs \
	${_pst} \
	pty  \
	puc \
	${_qlxge} \
	${_qlxgb} \
	${_qlxgbe} \
	${_qlnx} \
	ral \
	${_ralfw} \
	${_random_fortuna} \
	${_random_other} \
	rc4 \
	${_rdma} \
	${_rdrand_rng} \
	re \
	rl \
	${_rockchip} \
	rtwn \
	rtwn_pci \
	rtwn_usb \
	${_rtwnfw} \
	${_s3} \
	${_safe} \
	${_sbni} \
	scc \
	sdhci \
	${_sdhci_acpi} \
	sdhci_pci \
	sem \
	send \
	${_sf} \
	${_sfxge} \
	sge \
	${_sgx} \
	${_sgx_linux} \
	siftr \
	siis \
	sis \
	sk \
	${_smartpqi} \
	smbfs \
	sn \
	snp \
	sound \
	${_speaker} \
	spi \
	${_splash} \
	${_sppp} \
	ste \
	stge \
	${_sym} \
	${_syscons} \
	sysvipc \
	tcp \
	${_ti} \
	tl \
	tmpfs \
	${_toecore} \
	${_tpm} \
	trm \
	${_twa} \
	twe \
	tws \
	tx \
	${_txp} \
	uart \
	ubsec \
	udf \
	udf_iconv \
	ufs \
	uinput \
	unionfs \
	usb \
	${_vesa} \
	${_virtio} \
	vge \
	${_viawd} \
	videomode \
	vkbd \
	${_vmm} \
	${_vmware} \
	${_vpo} \
	vr \
	vte \
	vx \
	wb \
	${_wbwd} \
	${_wi} \
	wlan \
	wlan_acl \
	wlan_amrr \
	wlan_ccmp \
	wlan_rssadapt \
	wlan_tkip \
	wlan_wep \
	wlan_xauth \
	${_wpi} \
	${_wpifw} \
	${_x86bios} \
	${_xe} \
	xl \
	xz \
	zlib

.if ${MK_AUTOFS} != "no" || defined(ALL_MODULES)
_autofs=	autofs
.endif

.if ${MK_CDDL} != "no" || defined(ALL_MODULES)
.if (${MACHINE_CPUARCH} != "arm" || ${MACHINE_ARCH:Marmv[67]*} != "") && \
	${MACHINE_CPUARCH} != "mips" && \
	${MACHINE_CPUARCH} != "sparc64"
SUBDIR+=	dtrace
.endif
SUBDIR+=	opensolaris
.endif

.if ${MK_CRYPT} != "no" || defined(ALL_MODULES)
.if exists(${SRCTOP}/sys/opencrypto)
_crypto=	crypto
_cryptodev=	cryptodev
_random_fortuna=random_fortuna
_random_other=	random_other
.endif
.endif

.if ${MK_CUSE} != "no" || defined(ALL_MODULES)
SUBDIR+=	cuse
.endif

.if (${MK_INET_SUPPORT} != "no" || ${MK_INET6_SUPPORT} != "no") || \
	defined(ALL_MODULES)
_carp=		carp
_toecore=	toecore
_if_enc=	if_enc
_if_gif=	if_gif
_if_gre=	if_gre
_ipfw_pmod=	ipfw_pmod
.if ${MK_IPSEC_SUPPORT} != "no"
_ipsec=		ipsec
.endif
.endif

.if (${MK_INET_SUPPORT} != "no" && ${MK_INET6_SUPPORT} != "no") || \
	defined(ALL_MODULES)
_if_stf=	if_stf
.endif

.if ${MK_INET_SUPPORT} != "no" || defined(ALL_MODULES)
_if_me=		if_me
_ipdivert=	ipdivert
_ipfw=		ipfw
.if ${MK_INET6_SUPPORT} != "no" || defined(ALL_MODULES)
_ipfw_nat64=	ipfw_nat64
.endif
.endif

.if ${MK_INET6_SUPPORT} != "no" || defined(ALL_MODULES)
_ipfw_nptv6=	ipfw_nptv6
.endif

.if ${MK_IPFILTER} != "no" || defined(ALL_MODULES)
_ipfilter=	ipfilter
.endif

.if ${MK_ISCSI} != "no" || defined(ALL_MODULES)
SUBDIR+=	cfiscsi
SUBDIR+=	iscsi
SUBDIR+=	iscsi_initiator
.endif

.if !empty(OPT_FDT)
SUBDIR+=	fdt
.endif

.if ${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "amd64" || \
    ${MACHINE_CPUARCH} == "i386"
SUBDIR+=	linprocfs
SUBDIR+=	linsysfs
_ena=		ena
.endif

.if ${MK_NAND} != "no" || defined(ALL_MODULES)
_nandfs=	nandfs
_nandsim=	nandsim
.endif

.if ${MK_NETGRAPH} != "no" || defined(ALL_MODULES)
_netgraph=	netgraph
.endif

.if (${MK_PF} != "no" && (${MK_INET_SUPPORT} != "no" || \
	${MK_INET6_SUPPORT} != "no")) || defined(ALL_MODULES)
_pf=		pf
_pflog=		pflog
.if ${MK_INET_SUPPORT} != "no"
_pfsync=	pfsync
.endif
.endif

.if ${MK_SOURCELESS_UCODE} != "no"
_bce=		bce
_fxp=		fxp
_ispfw=		ispfw
_sf=		sf
_ti=		ti
_txp=		txp

.if ${MACHINE_CPUARCH} != "mips"
_mwlfw=		mwlfw
_otusfw=	otusfw
_ralfw=		ralfw
_rtwnfw=	rtwnfw
.endif
.endif

.if ${MK_SOURCELESS_UCODE} != "no" && ${MACHINE_CPUARCH} != "arm" && \
	${MACHINE_CPUARCH} != "mips" && \
	${MACHINE_ARCH} != "powerpc" && ${MACHINE_ARCH} != "powerpcspe" && \
	${MACHINE_CPUARCH} != "riscv"
_cxgbe=		cxgbe
.endif

# These rely on 64bit atomics
.if ${MACHINE_ARCH} != "powerpc" && ${MACHINE_ARCH} != "powerpcspe" && \
	${MACHINE_CPUARCH} != "mips"
_mps=		mps
_mpr=		mpr
.endif

.if ${MK_TESTS} != "no" || defined(ALL_MODULES)
SUBDIR+=	tests
.endif

.if ${MK_ZFS} != "no" || defined(ALL_MODULES)
SUBDIR+=	zfs
.endif

.if (${MACHINE_CPUARCH} == "mips" && ${MACHINE_ARCH:Mmips64} == "")
_hwpmc_mips24k=	hwpmc_mips24k
_hwpmc_mips74k=	hwpmc_mips74k
.endif

.if ${MACHINE_CPUARCH} != "aarch64" && ${MACHINE_CPUARCH} != "arm" && \
	${MACHINE_CPUARCH} != "mips" && ${MACHINE_CPUARCH} != "powerpc" && \
	${MACHINE_CPUARCH} != "riscv"
_syscons=	syscons
_vpo=		vpo
.endif

.if ${MACHINE_CPUARCH} != "mips"
# no BUS_SPACE_UNSPECIFIED
# No barrier instruction support (specific to this driver)
_sym=		sym
# intr_disable() is a macro, causes problems
.if ${MK_SOURCELESS_UCODE} != "no"
_cxgb=		cxgb
.endif
.endif

.if ${MACHINE_CPUARCH} == "aarch64"
_allwinner=	allwinner
_armv8crypto=	armv8crypto
_efirt=		efirt
_em=		em
_rockchip=	rockchip
.endif

.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
_agp=		agp
_an=		an
_aout=		aout
_bios=		bios
_bktr=		bktr
.if ${MK_SOURCELESS_UCODE} != "no"
_bxe=		bxe
.endif
_cardbus=	cardbus
_cbb=		cbb
_cpuctl=	cpuctl
_cpufreq=	cpufreq
_cs=		cs
_dpms=		dpms
_ed=		ed
_em=		em
_ep=		ep
_et=		et
_exca=		exca
_fe=		fe
.if ${MK_OFED} != "no" || defined(ALL_MODULES)
_ibcore=        ibcore
.endif
_if_ndis=	if_ndis
_io=		io
.if ${MK_OFED} != "no" || defined(ALL_MODULES)
_ipoib=         ipoib
_iser=		iser
.endif
_ix=		ix
_ixv=		ixv
_linux=		linux
.if ${MK_SOURCELESS_UCODE} != "no"
_lio=		lio
.endif
_nctgpio=	nctgpio
_ndis=		ndis
_ocs_fc=	ocs_fc
_pccard=	pccard
.if ${MK_OFED} != "no" || defined(ALL_MODULES)
_rdma=		rdma
.endif
_safe=		safe
_speaker=	speaker
_splash=	splash
_sppp=		sppp
_vmware=	vmware
_wbwd=		wbwd
_wi=		wi
_xe=		xe

_aac=		aac
_aacraid=	aacraid
_acpi=		acpi
.if ${MK_CRYPT} != "no" || defined(ALL_MODULES)
.if ${COMPILER_TYPE} != "gcc" || ${COMPILER_VERSION} > 40201
_aesni=		aesni
.endif
.endif
_amd_ecc_inject=amd_ecc_inject
_amdsbwd=	amdsbwd
_amdsmn=	amdsmn
_amdtemp=	amdtemp
_arcmsr=	arcmsr
_asmc=		asmc
.if ${MK_CRYPT} != "no" || defined(ALL_MODULES)
_blake2=	blake2
.endif
_bytgpio=	bytgpio
_chvgpio=	chvgpio
_ciss=		ciss
_chromebook_platform=	chromebook_platform
_cmx=		cmx
_coretemp=	coretemp
.if ${MK_SOURCELESS_HOST} != "no"
_hpt27xx=	hpt27xx
.endif
_hptiop=	hptiop
.if ${MK_SOURCELESS_HOST} != "no"
_hptmv=		hptmv
_hptnr=		hptnr
_hptrr=		hptrr
.endif
_hyperv=	hyperv
_ichwd=		ichwd
_ida=		ida
_iir=		iir
_intelspi=	intelspi
_ipmi=		ipmi
_ips=		ips
_isci=		isci
_ipw=		ipw
_iwi=		iwi
_iwm=		iwm
_iwn=		iwn
.if ${MK_SOURCELESS_UCODE} != "no"
_ipwfw=		ipwfw
_iwifw=		iwifw
_iwmfw=		iwmfw
_iwnfw=		iwnfw
.endif
_mlx4=		mlx4
_mlx5=		mlx5
.if (${MK_INET_SUPPORT} != "no" && ${MK_INET6_SUPPORT} != "no") || \
	defined(ALL_MODULES)
_mlx4en=	mlx4en
_mlx5en=	mlx5en
.endif
.if ${MK_OFED} != "no" || defined(ALL_MODULES)
_mthca=		mthca
_mlx4ib=	mlx4ib
_mlx5ib=	mlx5ib
.endif
_mly=		mly
_nfe=		nfe
_nvd=		nvd
_nvme=		nvme
_nvram=		nvram
.if ${MK_CRYPT} != "no" || defined(ALL_MODULES)
_padlock=	padlock
_padlock_rng=	padlock_rng
_rdrand_rng=	rdrand_rng
.endif
_s3=		s3
_sdhci_acpi=	sdhci_acpi
_tpm=		tpm
_twa=		twa
_vesa=		vesa
_viawd=		viawd
_virtio=	virtio
_wpi=		wpi
.if ${MK_SOURCELESS_UCODE} != "no"
_wpifw=		wpifw
.endif
_x86bios=	x86bios
.endif

.if ${MACHINE_CPUARCH} == "amd64"
_amdgpio=	amdgpio
_ccp=		ccp
_efirt=		efirt
_iavf=		iavf
_ioat=		ioat
_ixl=		ixl
_linux64=	linux64
_linux_common=	linux_common
_ntb=		ntb
_nvdimm=	nvdimm
_pms=		pms
_qlxge=		qlxge
_qlxgb=		qlxgb
.if ${MK_SOURCELESS_UCODE} != "no"
_qlxgbe=	qlxgbe
_qlnx=		qlnx
.endif
_sfxge=		sfxge
_sgx=		sgx
_sgx_linux=	sgx_linux
_smartpqi=	smartpqi

.if ${MK_BHYVE} != "no" || defined(ALL_MODULES)
_vmm=		vmm
.endif
.endif

.if ${MACHINE_CPUARCH} == "i386"
# XXX some of these can move to the general case when de-i386'ed
# XXX some of these can move now, but are untested on other architectures.
_3dfx=		3dfx
_3dfx_linux=	3dfx_linux
_apm=		apm
.if ${MK_SOURCELESS_UCODE} != "no"
_ce=		ce
.endif
.if ${MK_SOURCELESS_UCODE} != "no"
_cp=		cp
.endif
_glxiic=	glxiic
_glxsb=		glxsb
_pcfclock=	pcfclock
_pst=		pst
_sbni=		sbni
.if ${MK_SOURCELESS_UCODE} != "no"
_ctau=		ctau
.endif
_ex=		ex
.endif

.if ${MACHINE_CPUARCH} == "arm"
_cfi=		cfi
_cpsw=		cpsw
.endif

.if ${MACHINE_CPUARCH} == "powerpc"
_agp=		agp
_an=		an
_bm=		bm
_cardbus=	cardbus
_cbb=		cbb
_cfi=		cfi
_cpufreq=	cpufreq
_exca=		exca
_ffec=		ffec
_nvd=		nvd
_nvme=		nvme
_pccard=	pccard
_wi=		wi
.endif

.if ${MACHINE_ARCH} == "powerpc64"
_ipmi=		ipmi
.endif
.if ${MACHINE_ARCH} == "powerpc64" || ${MACHINE_ARCH} == "powerpc"
# Don't build powermac_nvram for powerpcspe, it's never supported.
_nvram=		powermac_nvram
.endif

.if ${MACHINE_CPUARCH} == "sparc64"
_auxio=		auxio
_em=		em
_epic=		epic
.endif

.if (${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "amd64" || \
     ${MACHINE_ARCH:Marmv[67]*} != "" || ${MACHINE_CPUARCH} == "i386")
_cloudabi32=	cloudabi32
.endif
.if ${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "amd64"
_cloudabi64=	cloudabi64
.endif

.endif

.if ${MACHINE_ARCH:Marmv[67]*} != "" || ${MACHINE_CPUARCH} == "aarch64"
_bcm283x_clkman=  bcm283x_clkman
_bcm283x_pwm=  bcm283x_pwm
.endif

SUBDIR+=${MODULES_EXTRA}

.for reject in ${WITHOUT_MODULES}
SUBDIR:= ${SUBDIR:N${reject}}
.endfor

# Calling kldxref(8) for each module is expensive.
.if !defined(NO_XREF)
.MAKEFLAGS+=	-DNO_XREF
afterinstall: .PHONY
	@if type kldxref >/dev/null 2>&1; then \
		${ECHO} kldxref ${DESTDIR}${KMODDIR}; \
		kldxref ${DESTDIR}${KMODDIR}; \
	fi
.endif

.include "${SYSDIR}/conf/config.mk"

SUBDIR:= ${SUBDIR:u:O}

.include <bsd.subdir.mk>
