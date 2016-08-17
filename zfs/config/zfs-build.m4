AC_DEFUN([ZFS_AC_LICENSE], [
	AC_MSG_CHECKING([zfs author])
	AC_MSG_RESULT([$ZFS_META_AUTHOR])

	AC_MSG_CHECKING([zfs license])
	AC_MSG_RESULT([$ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_DEBUG], [
	AC_MSG_CHECKING([whether debugging is enabled])
	AC_ARG_ENABLE([debug],
		[AS_HELP_STRING([--enable-debug],
		[Enable generic debug support @<:@default=no@:>@])],
		[],
		[enable_debug=no])

	AS_IF([test "x$enable_debug" = xyes],
	[
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG -Werror"
		HOSTCFLAGS="${HOSTCFLAGS} -DDEBUG -Werror"
		DEBUG_CFLAGS="-DDEBUG -Werror"
		DEBUG_STACKFLAGS="-fstack-check"
		DEBUG_ZFS="_with_debug"
		AC_DEFINE(ZFS_DEBUG, 1, [zfs debugging enabled])
	],
	[
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DNDEBUG "
		HOSTCFLAGS="${HOSTCFLAGS} -DNDEBUG "
		DEBUG_CFLAGS="-DNDEBUG"
		DEBUG_STACKFLAGS=""
		DEBUG_ZFS="_without_debug"
	])

	AC_SUBST(DEBUG_CFLAGS)
	AC_SUBST(DEBUG_STACKFLAGS)
	AC_SUBST(DEBUG_ZFS)
	AC_MSG_RESULT([$enable_debug])
])

AC_DEFUN([ZFS_AC_DEBUG_DMU_TX], [
	AC_ARG_ENABLE([debug-dmu-tx],
		[AS_HELP_STRING([--enable-debug-dmu-tx],
		[Enable dmu tx validation @<:@default=no@:>@])],
		[],
		[enable_debug_dmu_tx=no])

	AS_IF([test "x$enable_debug_dmu_tx" = xyes],
	[
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_DMU_TX"
		DEBUG_DMU_TX="_with_debug_dmu_tx"
		AC_DEFINE([DEBUG_DMU_TX], [1],
		[Define to 1 to enabled dmu tx validation])
	],
	[
		DEBUG_DMU_TX="_without_debug_dmu_tx"
	])

	AC_SUBST(DEBUG_DMU_TX)
	AC_MSG_CHECKING([whether dmu tx validation is enabled])
	AC_MSG_RESULT([$enable_debug_dmu_tx])
])

AC_DEFUN([ZFS_AC_CONFIG_ALWAYS], [
	ZFS_AC_CONFIG_ALWAYS_NO_UNUSED_BUT_SET_VARIABLE
	ZFS_AC_CONFIG_ALWAYS_NO_BOOL_COMPARE
])

AC_DEFUN([ZFS_AC_CONFIG], [
	TARGET_ASM_DIR=asm-generic
	AC_SUBST(TARGET_ASM_DIR)

	ZFS_CONFIG=all
	AC_ARG_WITH([config],
		AS_HELP_STRING([--with-config=CONFIG],
		[Config file 'kernel|user|all|srpm']),
		[ZFS_CONFIG="$withval"])
	AC_ARG_ENABLE([linux-builtin],
		[AC_HELP_STRING([--enable-linux-builtin],
		[Configure for builtin in-tree kernel modules @<:@default=no@:>@])],
		[],
		[enable_linux_builtin=no])

	AC_MSG_CHECKING([zfs config])
	AC_MSG_RESULT([$ZFS_CONFIG]);
	AC_SUBST(ZFS_CONFIG)

	ZFS_AC_CONFIG_ALWAYS

	case "$ZFS_CONFIG" in
		user)	ZFS_AC_CONFIG_USER   ;;
		kernel) ZFS_AC_CONFIG_KERNEL ;;
		all)    ZFS_AC_CONFIG_KERNEL
			ZFS_AC_CONFIG_USER   ;;
		srpm)                        ;;
		*)
		AC_MSG_RESULT([Error!])
		AC_MSG_ERROR([Bad value "$ZFS_CONFIG" for --with-config,
		              user kernel|user|all|srpm]) ;;
	esac

	AM_CONDITIONAL([CONFIG_USER],
		       [test "$ZFS_CONFIG" = user -o "$ZFS_CONFIG" = all])
	AM_CONDITIONAL([CONFIG_KERNEL],
		       [test "$ZFS_CONFIG" = kernel -o "$ZFS_CONFIG" = all] &&
		       [test "x$enable_linux_builtin" != xyes ])
])

dnl #
dnl # Check for rpm+rpmbuild to build RPM packages.  If these tools
dnl # are missing it is non-fatal but you will not be able to build
dnl # RPM packages and will be warned if you try too.
dnl #
dnl # By default the generic spec file will be used because it requires
dnl # minimal dependencies.  Distribution specific spec files can be
dnl # placed under the 'rpm/<distribution>' directory and enabled using
dnl # the --with-spec=<distribution> configure option.
dnl #
AC_DEFUN([ZFS_AC_RPM], [
	RPM=rpm
	RPMBUILD=rpmbuild

	AC_MSG_CHECKING([whether $RPM is available])
	AS_IF([tmp=$($RPM --version 2>/dev/null)], [
		RPM_VERSION=$(echo $tmp | $AWK '/RPM/ { print $[3] }')
		HAVE_RPM=yes
		AC_MSG_RESULT([$HAVE_RPM ($RPM_VERSION)])
	],[
		HAVE_RPM=no
		AC_MSG_RESULT([$HAVE_RPM])
	])

	AC_MSG_CHECKING([whether $RPMBUILD is available])
	AS_IF([tmp=$($RPMBUILD --version 2>/dev/null)], [
		RPMBUILD_VERSION=$(echo $tmp | $AWK '/RPM/ { print $[3] }')
		HAVE_RPMBUILD=yes
		AC_MSG_RESULT([$HAVE_RPMBUILD ($RPMBUILD_VERSION)])
	],[
		HAVE_RPMBUILD=no
		AC_MSG_RESULT([$HAVE_RPMBUILD])
	])

	RPM_DEFINE_COMMON='--define "$(DEBUG_ZFS) 1" --define "$(DEBUG_DMU_TX) 1"'
	RPM_DEFINE_UTIL='--define "_dracutdir $(dracutdir)" --define "_udevdir $(udevdir)" --define "_udevruledir $(udevruledir)" --define "_initconfdir $(DEFAULT_INITCONF_DIR)" $(DEFINE_INITRAMFS)'
	RPM_DEFINE_KMOD='--define "kernels $(LINUX_VERSION)" --define "require_spldir $(SPL)" --define "require_splobj $(SPL_OBJ)" --define "ksrc $(LINUX)" --define "kobj $(LINUX_OBJ)"'
	RPM_DEFINE_DKMS=

	SRPM_DEFINE_COMMON='--define "build_src_rpm 1"'
	SRPM_DEFINE_UTIL=
	SRPM_DEFINE_KMOD=
	SRPM_DEFINE_DKMS=

	RPM_SPEC_DIR="rpm/generic"
	AC_ARG_WITH([spec],
		AS_HELP_STRING([--with-spec=SPEC],
		[Spec files 'generic|redhat']),
		[RPM_SPEC_DIR="rpm/$withval"])

	AC_MSG_CHECKING([whether spec files are available])
	AC_MSG_RESULT([yes ($RPM_SPEC_DIR/*.spec.in)])

	AC_SUBST(HAVE_RPM)
	AC_SUBST(RPM)
	AC_SUBST(RPM_VERSION)

	AC_SUBST(HAVE_RPMBUILD)
	AC_SUBST(RPMBUILD)
	AC_SUBST(RPMBUILD_VERSION)

	AC_SUBST(RPM_SPEC_DIR)
	AC_SUBST(RPM_DEFINE_UTIL)
	AC_SUBST(RPM_DEFINE_KMOD)
	AC_SUBST(RPM_DEFINE_DKMS)
	AC_SUBST(RPM_DEFINE_COMMON)
	AC_SUBST(SRPM_DEFINE_UTIL)
	AC_SUBST(SRPM_DEFINE_KMOD)
	AC_SUBST(SRPM_DEFINE_DKMS)
	AC_SUBST(SRPM_DEFINE_COMMON)
])

dnl #
dnl # Check for dpkg+dpkg-buildpackage to build DEB packages.  If these
dnl # tools are missing it is non-fatal but you will not be able to build
dnl # DEB packages and will be warned if you try too.
dnl #
AC_DEFUN([ZFS_AC_DPKG], [
	DPKG=dpkg
	DPKGBUILD=dpkg-buildpackage

	AC_MSG_CHECKING([whether $DPKG is available])
	AS_IF([tmp=$($DPKG --version 2>/dev/null)], [
		DPKG_VERSION=$(echo $tmp | $AWK '/Debian/ { print $[7] }')
		HAVE_DPKG=yes
		AC_MSG_RESULT([$HAVE_DPKG ($DPKG_VERSION)])
	],[
		HAVE_DPKG=no
		AC_MSG_RESULT([$HAVE_DPKG])
	])

	AC_MSG_CHECKING([whether $DPKGBUILD is available])
	AS_IF([tmp=$($DPKGBUILD --version 2>/dev/null)], [
		DPKGBUILD_VERSION=$(echo $tmp | \
		    $AWK '/Debian/ { print $[4] }' | cut -f-4 -d'.')
		HAVE_DPKGBUILD=yes
		AC_MSG_RESULT([$HAVE_DPKGBUILD ($DPKGBUILD_VERSION)])
	],[
		HAVE_DPKGBUILD=no
		AC_MSG_RESULT([$HAVE_DPKGBUILD])
	])

	AC_SUBST(HAVE_DPKG)
	AC_SUBST(DPKG)
	AC_SUBST(DPKG_VERSION)

	AC_SUBST(HAVE_DPKGBUILD)
	AC_SUBST(DPKGBUILD)
	AC_SUBST(DPKGBUILD_VERSION)
])

dnl #
dnl # Until native packaging for various different packing systems
dnl # can be added the least we can do is attempt to use alien to
dnl # convert the RPM packages to the needed package type.  This is
dnl # a hack but so far it has worked reasonable well.
dnl #
AC_DEFUN([ZFS_AC_ALIEN], [
	ALIEN=alien

	AC_MSG_CHECKING([whether $ALIEN is available])
	AS_IF([tmp=$($ALIEN --version 2>/dev/null)], [
		ALIEN_VERSION=$(echo $tmp | $AWK '{ print $[3] }')
		HAVE_ALIEN=yes
		AC_MSG_RESULT([$HAVE_ALIEN ($ALIEN_VERSION)])
	],[
		HAVE_ALIEN=no
		AC_MSG_RESULT([$HAVE_ALIEN])
	])

	AC_SUBST(HAVE_ALIEN)
	AC_SUBST(ALIEN)
	AC_SUBST(ALIEN_VERSION)
])

dnl #
dnl # Using the VENDOR tag from config.guess set the default
dnl # package type for 'make pkg': (rpm | deb | tgz)
dnl #
AC_DEFUN([ZFS_AC_DEFAULT_PACKAGE], [
	AC_MSG_CHECKING([linux distribution])
	if test -f /etc/toss-release ; then
		VENDOR=toss ;
	elif test -f /etc/fedora-release ; then
		VENDOR=fedora ;
	elif test -f /etc/redhat-release ; then
		VENDOR=redhat ;
	elif test -f /etc/gentoo-release ; then
		VENDOR=gentoo ;
	elif test -f /etc/arch-release ; then
		VENDOR=arch ;
	elif test -f /etc/SuSE-release ; then
		VENDOR=sles ;
	elif test -f /etc/slackware-version ; then
		VENDOR=slackware ;
	elif test -f /etc/lunar.release ; then
		VENDOR=lunar ;
	elif test -f /etc/lsb-release ; then
		VENDOR=ubuntu ;
	elif test -f /etc/debian_version ; then
		VENDOR=debian ;
	else
		VENDOR= ;
	fi
	AC_MSG_RESULT([$VENDOR])
	AC_SUBST(VENDOR)

	AC_MSG_CHECKING([default package type])
	case "$VENDOR" in
		toss)       DEFAULT_PACKAGE=rpm  ;;
		redhat)     DEFAULT_PACKAGE=rpm  ;;
		fedora)     DEFAULT_PACKAGE=rpm  ;;
		gentoo)     DEFAULT_PACKAGE=tgz  ;;
		arch)       DEFAULT_PACKAGE=tgz  ;;
		sles)       DEFAULT_PACKAGE=rpm  ;;
		slackware)  DEFAULT_PACKAGE=tgz  ;;
		lunar)      DEFAULT_PACKAGE=tgz  ;;
		ubuntu)     DEFAULT_PACKAGE=deb  ;;
		debian)     DEFAULT_PACKAGE=deb  ;;
		*)          DEFAULT_PACKAGE=rpm  ;;
	esac
	AC_MSG_RESULT([$DEFAULT_PACKAGE])
	AC_SUBST(DEFAULT_PACKAGE)

	DEFAULT_INIT_DIR=$sysconfdir/init.d
	AC_MSG_CHECKING([default init directory])
	AC_MSG_RESULT([$DEFAULT_INIT_DIR])
	AC_SUBST(DEFAULT_INIT_DIR)

	AC_MSG_CHECKING([default init script type])
	case "$VENDOR" in
		toss)       DEFAULT_INIT_SCRIPT=redhat ;;
		redhat)     DEFAULT_INIT_SCRIPT=redhat ;;
		fedora)     DEFAULT_INIT_SCRIPT=fedora ;;
		gentoo)     DEFAULT_INIT_SCRIPT=gentoo ;;
		arch)       DEFAULT_INIT_SCRIPT=lsb    ;;
		sles)       DEFAULT_INIT_SCRIPT=lsb    ;;
		slackware)  DEFAULT_INIT_SCRIPT=lsb    ;;
		lunar)      DEFAULT_INIT_SCRIPT=lunar  ;;
		ubuntu)     DEFAULT_INIT_SCRIPT=lsb    ;;
		debian)     DEFAULT_INIT_SCRIPT=lsb    ;;
		*)          DEFAULT_INIT_SCRIPT=lsb    ;;
	esac
	AC_MSG_RESULT([$DEFAULT_INIT_SCRIPT])
	AC_SUBST(DEFAULT_INIT_SCRIPT)

	AC_MSG_CHECKING([default init config direectory])
	case "$VENDOR" in
		gentoo)     DEFAULT_INITCONF_DIR=/etc/conf.d    ;;
		toss)       DEFAULT_INITCONF_DIR=/etc/sysconfig ;;
		redhat)     DEFAULT_INITCONF_DIR=/etc/sysconfig ;;
		fedora)     DEFAULT_INITCONF_DIR=/etc/sysconfig ;;
		sles)       DEFAULT_INITCONF_DIR=/etc/sysconfig ;;
		ubuntu)     DEFAULT_INITCONF_DIR=/etc/default   ;;
		debian)     DEFAULT_INITCONF_DIR=/etc/default   ;;
		*)          DEFAULT_INITCONF_DIR=/etc/default   ;;
	esac
	AC_MSG_RESULT([$DEFAULT_INITCONF_DIR])
	AC_SUBST(DEFAULT_INITCONF_DIR)

	AC_MSG_CHECKING([whether initramfs-tools is available])
	if test -d /usr/share/initramfs-tools ; then
		DEFINE_INITRAMFS='--define "_initramfs 1"'
		AC_MSG_RESULT([yes])
	else
		DEFINE_INITRAMFS=''
		AC_MSG_RESULT([no])
	fi
	AC_SUBST(DEFINE_INITRAMFS)
])

dnl #
dnl # Default ZFS package configuration
dnl #
AC_DEFUN([ZFS_AC_PACKAGE], [
	ZFS_AC_DEFAULT_PACKAGE
	ZFS_AC_RPM
	ZFS_AC_DPKG
	ZFS_AC_ALIEN
])
