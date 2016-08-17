###############################################################################
# Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
# Copyright (C) 2007 The Regents of the University of California.
# Written by Brian Behlendorf <behlendorf1@llnl.gov>.
###############################################################################
# SPL_AC_CONFIG_KERNEL: Default SPL kernel configuration.
###############################################################################

AC_DEFUN([SPL_AC_CONFIG_KERNEL], [
	SPL_AC_KERNEL

	if test "${LINUX_OBJ}" != "${LINUX}"; then
		KERNELMAKE_PARAMS="$KERNELMAKE_PARAMS O=$LINUX_OBJ"
	fi
	AC_SUBST(KERNELMAKE_PARAMS)

	KERNELCPPFLAGS="$KERNELCPPFLAGS -Wstrict-prototypes"
	AC_SUBST(KERNELCPPFLAGS)

	SPL_AC_DEBUG
	SPL_AC_DEBUG_KMEM
	SPL_AC_DEBUG_KMEM_TRACKING
	SPL_AC_TEST_MODULE
	SPL_AC_ATOMIC_SPINLOCK
	SPL_AC_SHRINKER_CALLBACK
	SPL_AC_CTL_NAME
	SPL_AC_CONFIG_TRIM_UNUSED_KSYMS
	SPL_AC_PDE_DATA
	SPL_AC_SET_FS_PWD_WITH_CONST
	SPL_AC_2ARGS_VFS_UNLINK
	SPL_AC_4ARGS_VFS_RENAME
	SPL_AC_2ARGS_VFS_FSYNC
	SPL_AC_INODE_TRUNCATE_RANGE
	SPL_AC_FS_STRUCT_SPINLOCK
	SPL_AC_KUIDGID_T
	SPL_AC_PUT_TASK_STRUCT
	SPL_AC_KERNEL_FALLOCATE
	SPL_AC_CONFIG_ZLIB_INFLATE
	SPL_AC_CONFIG_ZLIB_DEFLATE
	SPL_AC_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE
	SPL_AC_SHRINK_CONTROL_STRUCT
	SPL_AC_RWSEM_SPINLOCK_IS_RAW
	SPL_AC_RWSEM_ACTIVITY
	SPL_AC_RWSEM_ATOMIC_LONG_COUNT
	SPL_AC_SCHED_RT_HEADER
	SPL_AC_2ARGS_VFS_GETATTR
	SPL_AC_USLEEP_RANGE
	SPL_AC_KMEM_CACHE_ALLOCFLAGS
	SPL_AC_WAIT_ON_BIT
	SPL_AC_INODE_LOCK
	SPL_AC_MUTEX_OWNER
	SPL_AC_GROUP_INFO_GID
])

AC_DEFUN([SPL_AC_MODULE_SYMVERS], [
	modpost=$LINUX/scripts/Makefile.modpost
	AC_MSG_CHECKING([kernel file name for module symbols])
	if test "x$enable_linux_builtin" != xyes -a -f "$modpost"; then
		if grep -q Modules.symvers $modpost; then
			LINUX_SYMBOLS=Modules.symvers
		else
			LINUX_SYMBOLS=Module.symvers
		fi

		if ! test -f "$LINUX_OBJ/$LINUX_SYMBOLS"; then
			AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed.  If you are building with a custom kernel, make sure the
	*** kernel is configured, built, and the '--with-linux=PATH' configure
	*** option refers to the location of the kernel source.])
		fi
	else
		LINUX_SYMBOLS=NONE
	fi
	AC_MSG_RESULT($LINUX_SYMBOLS)
	AC_SUBST(LINUX_SYMBOLS)
])

AC_DEFUN([SPL_AC_KERNEL], [
	AC_ARG_WITH([linux],
		AS_HELP_STRING([--with-linux=PATH],
		[Path to kernel source]),
		[kernelsrc="$withval"])

	AC_ARG_WITH([linux-obj],
		AS_HELP_STRING([--with-linux-obj=PATH],
		[Path to kernel build objects]),
		[kernelbuild="$withval"])

	AC_MSG_CHECKING([kernel source directory])
	if test -z "$kernelsrc"; then
		if test -e "/lib/modules/$(uname -r)/source"; then
			headersdir="/lib/modules/$(uname -r)/source"
			sourcelink=$(readlink -f "$headersdir")
		elif test -e "/lib/modules/$(uname -r)/build"; then
			headersdir="/lib/modules/$(uname -r)/build"
			sourcelink=$(readlink -f "$headersdir")
		else
			sourcelink=$(ls -1d /usr/src/kernels/* \
			             /usr/src/linux-* \
			             2>/dev/null | grep -v obj | tail -1)
		fi

		if test -n "$sourcelink" && test -e ${sourcelink}; then
			kernelsrc=`readlink -f ${sourcelink}`
		else
			kernelsrc="[Not found]"
		fi
	else
		if test "$kernelsrc" = "NONE"; then
			kernsrcver=NONE
		fi
	fi

	AC_MSG_RESULT([$kernelsrc])
	if test ! -d "$kernelsrc"; then
		AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed and then try again.  If that fails, you can specify the
	*** location of the kernel source with the '--with-linux=PATH' option.])
	fi

	AC_MSG_CHECKING([kernel build directory])
	if test -z "$kernelbuild"; then
		if test -e "/lib/modules/$(uname -r)/build"; then
			kernelbuild=`readlink -f /lib/modules/$(uname -r)/build`
		elif test -d ${kernelsrc}-obj/${target_cpu}/${target_cpu}; then
			kernelbuild=${kernelsrc}-obj/${target_cpu}/${target_cpu}
		elif test -d ${kernelsrc}-obj/${target_cpu}/default; then
			kernelbuild=${kernelsrc}-obj/${target_cpu}/default
		elif test -d `dirname ${kernelsrc}`/build-${target_cpu}; then
			kernelbuild=`dirname ${kernelsrc}`/build-${target_cpu}
		else
			kernelbuild=${kernelsrc}
		fi
	fi
	AC_MSG_RESULT([$kernelbuild])

	AC_MSG_CHECKING([kernel source version])
	utsrelease1=$kernelbuild/include/linux/version.h
	utsrelease2=$kernelbuild/include/linux/utsrelease.h
	utsrelease3=$kernelbuild/include/generated/utsrelease.h
	if test -r $utsrelease1 && fgrep -q UTS_RELEASE $utsrelease1; then
		utsrelease=linux/version.h
	elif test -r $utsrelease2 && fgrep -q UTS_RELEASE $utsrelease2; then
		utsrelease=linux/utsrelease.h
	elif test -r $utsrelease3 && fgrep -q UTS_RELEASE $utsrelease3; then
		utsrelease=generated/utsrelease.h
	fi

	if test "$utsrelease"; then
		kernsrcver=`(echo "#include <$utsrelease>";
		             echo "kernsrcver=UTS_RELEASE") | 
		             cpp -I $kernelbuild/include |
		             grep "^kernsrcver=" | cut -d \" -f 2`

		if test -z "$kernsrcver"; then
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([*** Cannot determine kernel version.])
		fi
	else
		AC_MSG_RESULT([Not found])
		if test "x$enable_linux_builtin" != xyes; then
			AC_MSG_ERROR([*** Cannot find UTS_RELEASE definition.])
		else
			AC_MSG_ERROR([
	*** Cannot find UTS_RELEASE definition.
	*** Please run 'make prepare' inside the kernel source tree.])
		fi
	fi

	AC_MSG_RESULT([$kernsrcver])

	LINUX=${kernelsrc}
	LINUX_OBJ=${kernelbuild}
	LINUX_VERSION=${kernsrcver}

	AC_SUBST(LINUX)
	AC_SUBST(LINUX_OBJ)
	AC_SUBST(LINUX_VERSION)

	SPL_AC_MODULE_SYMVERS
])

dnl #
dnl # Default SPL user configuration
dnl #
AC_DEFUN([SPL_AC_CONFIG_USER], [])

dnl #
dnl # Check for rpm+rpmbuild to build RPM packages.  If these tools
dnl # are missing, it is non-fatal, but you will not be able to build
dnl # RPM packages and will be warned if you try too.
dnl #
dnl # By default, the generic spec file will be used because it requires
dnl # minimal dependencies.  Distribution specific spec files can be
dnl # placed under the 'rpm/<distribution>' directory and enabled using
dnl # the --with-spec=<distribution> configure option.
dnl #
AC_DEFUN([SPL_AC_RPM], [
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

	RPM_DEFINE_COMMON='--define "$(DEBUG_SPL) 1" --define "$(DEBUG_KMEM) 1" --define "$(DEBUG_KMEM_TRACKING) 1"'
	RPM_DEFINE_UTIL=
	RPM_DEFINE_KMOD='--define "kernels $(LINUX_VERSION)"'
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
AC_DEFUN([SPL_AC_DPKG], [
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
AC_DEFUN([SPL_AC_ALIEN], [
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
AC_DEFUN([SPL_AC_DEFAULT_PACKAGE], [
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
])

dnl #
dnl # Default SPL user configuration
dnl #
AC_DEFUN([SPL_AC_PACKAGE], [
	SPL_AC_DEFAULT_PACKAGE
	SPL_AC_RPM
	SPL_AC_DPKG
	SPL_AC_ALIEN
])

AC_DEFUN([SPL_AC_LICENSE], [
	AC_MSG_CHECKING([spl author])
	AC_MSG_RESULT([$SPL_META_AUTHOR])

	AC_MSG_CHECKING([spl license])
	AC_MSG_RESULT([$SPL_META_LICENSE])
])

AC_DEFUN([SPL_AC_CONFIG], [
	SPL_CONFIG=all
	AC_ARG_WITH([config],
		AS_HELP_STRING([--with-config=CONFIG],
		[Config file 'kernel|user|all|srpm']),
		[SPL_CONFIG="$withval"])
	AC_ARG_ENABLE([linux-builtin],
		[AC_HELP_STRING([--enable-linux-builtin],
		[Configure for builtin in-tree kernel modules @<:@default=no@:>@])],
		[],
		[enable_linux_builtin=no])

	AC_MSG_CHECKING([spl config])
	AC_MSG_RESULT([$SPL_CONFIG]);
	AC_SUBST(SPL_CONFIG)

	case "$SPL_CONFIG" in
		kernel) SPL_AC_CONFIG_KERNEL ;;
		user)   SPL_AC_CONFIG_USER   ;;
		all)    SPL_AC_CONFIG_KERNEL
		        SPL_AC_CONFIG_USER   ;;
		srpm)                        ;;
		*)
		AC_MSG_RESULT([Error!])
		AC_MSG_ERROR([Bad value "$SPL_CONFIG" for --with-config,
		             user kernel|user|all|srpm]) ;;
	esac

	AM_CONDITIONAL([CONFIG_USER],
	               [test "$SPL_CONFIG" = user -o "$SPL_CONFIG" = all])
	AM_CONDITIONAL([CONFIG_KERNEL],
	               [test "$SPL_CONFIG" = kernel -o "$SPL_CONFIG" = all] &&
	               [test "x$enable_linux_builtin" != xyes ])
])

dnl #
dnl # Enable if the SPL should be compiled with internal debugging enabled.
dnl # By default this support is disabled.
dnl #
AC_DEFUN([SPL_AC_DEBUG], [
	AC_MSG_CHECKING([whether debugging is enabled])
	AC_ARG_ENABLE([debug],
		[AS_HELP_STRING([--enable-debug],
		[Enable generic debug support @<:@default=no@:>@])],
		[],
		[enable_debug=no])

	AS_IF([test "x$enable_debug" = xyes],
	[
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG -Werror"
		DEBUG_CFLAGS="-DDEBUG -Werror"
		DEBUG_SPL="_with_debug"
	], [
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DNDEBUG"
		DEBUG_CFLAGS="-DNDEBUG"
		DEBUG_SPL="_without_debug"
	])

	AC_SUBST(DEBUG_CFLAGS)
	AC_SUBST(DEBUG_SPL)
	AC_MSG_RESULT([$enable_debug])
])

dnl #
dnl # Enabled by default it provides a minimal level of memory tracking.
dnl # A total count of bytes allocated is kept for each alloc and free.
dnl # Then at module unload time a report to the console will be printed
dnl # if memory was leaked.
dnl #
AC_DEFUN([SPL_AC_DEBUG_KMEM], [
	AC_ARG_ENABLE([debug-kmem],
		[AS_HELP_STRING([--enable-debug-kmem],
		[Enable basic kmem accounting @<:@default=no@:>@])],
		[],
		[enable_debug_kmem=no])

	AS_IF([test "x$enable_debug_kmem" = xyes],
	[
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_KMEM"
		DEBUG_KMEM="_with_debug_kmem"
		AC_DEFINE([DEBUG_KMEM], [1],
		[Define to 1 to enable basic kmem accounting])
	], [
		DEBUG_KMEM="_without_debug_kmem"
	])

	AC_SUBST(DEBUG_KMEM)
	AC_MSG_CHECKING([whether basic kmem accounting is enabled])
	AC_MSG_RESULT([$enable_debug_kmem])
])

dnl #
dnl # Disabled by default it provides detailed memory tracking.  This
dnl # feature also requires --enable-debug-kmem to be set.  When enabled
dnl # not only will total bytes be tracked but also the location of every
dnl # alloc and free.  When the SPL module is unloaded a list of all leaked
dnl # addresses and where they were allocated will be dumped to the console.
dnl # Enabling this feature has a significant impact on performance but it
dnl # makes finding memory leaks pretty straight forward.
dnl #
AC_DEFUN([SPL_AC_DEBUG_KMEM_TRACKING], [
	AC_ARG_ENABLE([debug-kmem-tracking],
		[AS_HELP_STRING([--enable-debug-kmem-tracking],
		[Enable detailed kmem tracking  @<:@default=no@:>@])],
		[],
		[enable_debug_kmem_tracking=no])

	AS_IF([test "x$enable_debug_kmem_tracking" = xyes],
	[
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_KMEM_TRACKING"
		DEBUG_KMEM_TRACKING="_with_debug_kmem_tracking"
		AC_DEFINE([DEBUG_KMEM_TRACKING], [1],
		[Define to 1 to enable detailed kmem tracking])
	], [
		DEBUG_KMEM_TRACKING="_without_debug_kmem_tracking"
	])

	AC_SUBST(DEBUG_KMEM_TRACKING)
	AC_MSG_CHECKING([whether detailed kmem tracking is enabled])
	AC_MSG_RESULT([$enable_debug_kmem_tracking])
])

dnl #
dnl # SPL_LINUX_CONFTEST
dnl #
AC_DEFUN([SPL_LINUX_CONFTEST], [
cat confdefs.h - <<_ACEOF >conftest.c
$1
_ACEOF
])

dnl #
dnl # SPL_LANG_PROGRAM(C)([PROLOGUE], [BODY])
dnl #
m4_define([SPL_LANG_PROGRAM], [
$1
int
main (void)
{
dnl Do *not* indent the following line: there may be CPP directives.
dnl Don't move the `;' right after for the same reason.
$2
  ;
  return 0;
}
])

dnl #
dnl # SPL_LINUX_COMPILE_IFELSE / like AC_COMPILE_IFELSE
dnl #
AC_DEFUN([SPL_LINUX_COMPILE_IFELSE], [
	m4_ifvaln([$1], [SPL_LINUX_CONFTEST([$1])])
	rm -Rf build && mkdir -p build && touch build/conftest.mod.c
	echo "obj-m := conftest.o" >build/Makefile
	modpost_flag=''
	test "x$enable_linux_builtin" = xyes && modpost_flag='modpost=true' # fake modpost stage
	AS_IF(
		[AC_TRY_COMMAND(cp conftest.c build && make [$2] -C $LINUX_OBJ EXTRA_CFLAGS="-Werror-implicit-function-declaration $EXTRA_KCFLAGS" $ARCH_UM M=$PWD/build $modpost_flag) >/dev/null && AC_TRY_COMMAND([$3])],
		[$4],
		[_AC_MSG_LOG_CONFTEST m4_ifvaln([$5],[$5])]
	)
	rm -Rf build
])

dnl #
dnl # SPL_LINUX_TRY_COMPILE like AC_TRY_COMPILE
dnl #
AC_DEFUN([SPL_LINUX_TRY_COMPILE],
	[SPL_LINUX_COMPILE_IFELSE(
	[AC_LANG_SOURCE([SPL_LANG_PROGRAM([[$1]], [[$2]])])],
	[modules],
	[test -s build/conftest.o],
	[$3], [$4])
])

dnl #
dnl # SPL_CHECK_SYMBOL_EXPORT
dnl # check symbol exported or not
dnl #
AC_DEFUN([SPL_CHECK_SYMBOL_EXPORT], [
	grep -q -E '[[[:space:]]]$1[[[:space:]]]' \
		$LINUX_OBJ/Module*.symvers 2>/dev/null
	rc=$?
	if test $rc -ne 0; then
		export=0
		for file in $2; do
			grep -q -E "EXPORT_SYMBOL.*($1)" \
				"$LINUX_OBJ/$file" 2>/dev/null
			rc=$?
			if test $rc -eq 0; then
				export=1
				break;
			fi
		done
		if test $export -eq 0; then :
			$4
		else :
			$3
		fi
	else :
		$3
	fi
])

dnl #
dnl # SPL_LINUX_TRY_COMPILE_SYMBOL
dnl # like SPL_LINUX_TRY_COMPILE, except SPL_CHECK_SYMBOL_EXPORT
dnl # is called if not compiling for builtin
dnl #
AC_DEFUN([SPL_LINUX_TRY_COMPILE_SYMBOL], [
	SPL_LINUX_TRY_COMPILE([$1], [$2], [rc=0], [rc=1])
	if test $rc -ne 0; then :
		$6
	else
		if test "x$enable_linux_builtin" != xyes; then
			SPL_CHECK_SYMBOL_EXPORT([$3], [$4], [rc=0], [rc=1])
		fi
		if test $rc -ne 0; then :
			$6
		else :
			$5
		fi
	fi
])

dnl #
dnl # SPL_CHECK_SYMBOL_HEADER
dnl # check if a symbol prototype is defined in listed headers.
dnl #
AC_DEFUN([SPL_CHECK_SYMBOL_HEADER], [
	AC_MSG_CHECKING([whether symbol $1 exists in header])
	header=0
	for file in $3; do
		grep -q "$2" "$LINUX/$file" 2>/dev/null
		rc=$?
		if test $rc -eq 0; then
			header=1
			break;
		fi
	done
	if test $header -eq 0; then
		AC_MSG_RESULT([no])
		$5
	else
		AC_MSG_RESULT([yes])
		$4
	fi
])

dnl #
dnl # SPL_CHECK_HEADER
dnl # check whether header exists and define HAVE_$2_HEADER
dnl #
AC_DEFUN([SPL_CHECK_HEADER],
	[AC_MSG_CHECKING([whether header $1 exists])
	SPL_LINUX_TRY_COMPILE([
		#include <$1>
	],[
		return 0;
	],[
		AC_DEFINE(HAVE_$2_HEADER, 1, [$1 exists])
		AC_MSG_RESULT(yes)
		$3
	],[
		AC_MSG_RESULT(no)
		$4
	])
])

dnl #
dnl # Basic toolchain sanity check.  Verify that kernel modules can
dnl # be built and which symbols can be used.
dnl #
AC_DEFUN([SPL_AC_TEST_MODULE],
	[AC_MSG_CHECKING([whether modules can be built])
	SPL_LINUX_TRY_COMPILE([],[],[
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		if test "x$enable_linux_builtin" != xyes; then
			AC_MSG_ERROR([*** Unable to build an empty module.])
		else
			AC_MSG_ERROR([
	*** Unable to build an empty module.
	*** Please run 'make scripts' inside the kernel source tree.])
		fi
	])

	AS_IF([test "x$cross_compiling" != xyes], [
		AC_RUN_IFELSE([
			AC_LANG_PROGRAM([
				#include "$LINUX/include/linux/license.h"
			], [
				return !license_is_gpl_compatible(
				    "$SPL_META_LICENSE");
			])
		], [
			AC_DEFINE([SPL_IS_GPL_COMPATIBLE], [1],
			    [Define to 1 if GPL-only symbols can be used])
		], [
		])
	])
])

dnl #
dnl # Use the atomic implemenation based on global spinlocks.  This
dnl # should only be needed by 32-bit kernels which do not provide
dnl # the atomic64_* API.  It may be optionally enabled as a fallback
dnl # if problems are observed with the direct mapping to the native
dnl # Linux atomic operations.  You may not disable atomic spinlocks
dnl # if you kernel does not an atomic64_* API.
dnl #
AC_DEFUN([SPL_AC_ATOMIC_SPINLOCK], [
	AC_ARG_ENABLE([atomic-spinlocks],
		[AS_HELP_STRING([--enable-atomic-spinlocks],
		[Atomic types use spinlocks @<:@default=check@:>@])],
		[],
		[enable_atomic_spinlocks=check])

	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		atomic64_t *ptr __attribute__ ((unused));
	],[
		have_atomic64_t=yes
		AC_DEFINE(HAVE_ATOMIC64_T, 1,
			[kernel defines atomic64_t])
	],[
		have_atomic64_t=no
	])

	AS_IF([test "x$enable_atomic_spinlocks" = xcheck], [
		AS_IF([test "x$have_atomic64_t" = xyes], [
			enable_atomic_spinlocks=no
		],[
			enable_atomic_spinlocks=yes
		])
	])

	AS_IF([test "x$enable_atomic_spinlocks" = xyes], [
		AC_DEFINE([ATOMIC_SPINLOCK], [1],
			[Atomic types use spinlocks])
	],[
		AS_IF([test "x$have_atomic64_t" = xno], [
			AC_MSG_FAILURE(
			[--disable-atomic-spinlocks given but required atomic64 support is unavailable])
		])
	])

	AC_MSG_CHECKING([whether atomic types use spinlocks])
	AC_MSG_RESULT([$enable_atomic_spinlocks])

	AC_MSG_CHECKING([whether kernel defines atomic64_t])
	AC_MSG_RESULT([$have_atomic64_t])
])

AC_DEFUN([SPL_AC_SHRINKER_CALLBACK],[
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	dnl #
	dnl # 2.6.23 to 2.6.34 API change
	dnl # ->shrink(int nr_to_scan, gfp_t gfp_mask)
	dnl #
	AC_MSG_CHECKING([whether old 2-argument shrinker exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>

		int shrinker_cb(int nr_to_scan, gfp_t gfp_mask);
	],[
		struct shrinker cache_shrinker = {
			.shrink = shrinker_cb,
			.seeks = DEFAULT_SEEKS,
		};
		register_shrinker(&cache_shrinker);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_OLD_SHRINKER_CALLBACK, 1,
			[old shrinker callback wants 2 args])
	],[
		AC_MSG_RESULT(no)
		dnl #
		dnl # 2.6.35 - 2.6.39 API change
		dnl # ->shrink(struct shrinker *,
		dnl #          int nr_to_scan, gfp_t gfp_mask)
		dnl #
		AC_MSG_CHECKING([whether old 3-argument shrinker exists])
		SPL_LINUX_TRY_COMPILE([
			#include <linux/mm.h>

			int shrinker_cb(struct shrinker *, int nr_to_scan,
					gfp_t gfp_mask);
		],[
			struct shrinker cache_shrinker = {
				.shrink = shrinker_cb,
				.seeks = DEFAULT_SEEKS,
			};
			register_shrinker(&cache_shrinker);
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_3ARGS_SHRINKER_CALLBACK, 1,
				[old shrinker callback wants 3 args])
		],[
			AC_MSG_RESULT(no)
			dnl #
			dnl # 3.0 - 3.11 API change
			dnl # ->shrink(struct shrinker *,
			dnl #          struct shrink_control *sc)
			dnl #
			AC_MSG_CHECKING(
				[whether new 2-argument shrinker exists])
			SPL_LINUX_TRY_COMPILE([
				#include <linux/mm.h>

				int shrinker_cb(struct shrinker *,
						struct shrink_control *sc);
			],[
				struct shrinker cache_shrinker = {
					.shrink = shrinker_cb,
					.seeks = DEFAULT_SEEKS,
				};
				register_shrinker(&cache_shrinker);
			],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_2ARGS_NEW_SHRINKER_CALLBACK, 1,
					[new shrinker callback wants 2 args])
			],[
				AC_MSG_RESULT(no)
				dnl #
				dnl # 3.12 API change,
				dnl # ->shrink() is logically split in to
				dnl # ->count_objects() and ->scan_objects()
				dnl #
				AC_MSG_CHECKING(
				    [whether ->count_objects callback exists])
				SPL_LINUX_TRY_COMPILE([
					#include <linux/mm.h>

					unsigned long shrinker_cb(
						struct shrinker *,
						struct shrink_control *sc);
				],[
					struct shrinker cache_shrinker = {
						.count_objects = shrinker_cb,
						.scan_objects = shrinker_cb,
						.seeks = DEFAULT_SEEKS,
					};
					register_shrinker(&cache_shrinker);
				],[
					AC_MSG_RESULT(yes)
					AC_DEFINE(HAVE_SPLIT_SHRINKER_CALLBACK,
						1, [->count_objects exists])
				],[
					AC_MSG_ERROR(error)
				])
			])
		])
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 2.6.33 API change,
dnl # Removed .ctl_name from struct ctl_table.
dnl #
AC_DEFUN([SPL_AC_CTL_NAME], [
	AC_MSG_CHECKING([whether struct ctl_table has ctl_name])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sysctl.h>
	],[
		struct ctl_table ctl __attribute__ ((unused));
		ctl.ctl_name = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CTL_NAME, 1, [struct ctl_table has ctl_name])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.10 API change,
dnl # PDE is replaced by PDE_DATA
dnl #
AC_DEFUN([SPL_AC_PDE_DATA], [
	AC_MSG_CHECKING([whether PDE_DATA() is available])
	SPL_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/proc_fs.h>
	], [
		PDE_DATA(NULL);
	], [PDE_DATA], [], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PDE_DATA, 1, [yes])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.9 API change
dnl # set_fs_pwd takes const struct path *
dnl #
AC_DEFUN([SPL_AC_SET_FS_PWD_WITH_CONST],
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	[AC_MSG_CHECKING([whether set_fs_pwd() requires const struct path *])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/spinlock.h>
		#include <linux/fs_struct.h>
		#include <linux/path.h>
		void (*const set_fs_pwd_func)
			(struct fs_struct *, const struct path *)
			= set_fs_pwd;
	],[
		return 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_FS_PWD_WITH_CONST, 1,
			[set_fs_pwd() needs const path *])
	],[
		SPL_LINUX_TRY_COMPILE([
			#include <linux/spinlock.h>
			#include <linux/fs_struct.h>
			#include <linux/path.h>
			void (*const set_fs_pwd_func)
				(struct fs_struct *, struct path *)
				= set_fs_pwd;
		],[
			return 0;
		],[
			AC_MSG_RESULT(no)
		],[
			AC_MSG_ERROR(unknown)
		])
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 3.13 API change
dnl # vfs_unlink() updated to take a third delegated_inode argument.
dnl #
AC_DEFUN([SPL_AC_2ARGS_VFS_UNLINK],
	[AC_MSG_CHECKING([whether vfs_unlink() wants 2 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_unlink((struct inode *) NULL, (struct dentry *) NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_VFS_UNLINK, 1,
		          [vfs_unlink() wants 2 args])
	],[
		AC_MSG_RESULT(no)
		dnl #
		dnl # Linux 3.13 API change
		dnl # Added delegated inode
		dnl #
		AC_MSG_CHECKING([whether vfs_unlink() wants 3 args])
		SPL_LINUX_TRY_COMPILE([
			#include <linux/fs.h>
		],[
			vfs_unlink((struct inode *) NULL,
				(struct dentry *) NULL,
				(struct inode **) NULL);
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_3ARGS_VFS_UNLINK, 1,
				  [vfs_unlink() wants 3 args])
		],[
			AC_MSG_ERROR(no)
		])

	])
])

dnl #
dnl # 3.13 and 3.15 API changes
dnl # Added delegated inode and flags argument.
dnl #
AC_DEFUN([SPL_AC_4ARGS_VFS_RENAME],
	[AC_MSG_CHECKING([whether vfs_rename() wants 4 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_rename((struct inode *) NULL, (struct dentry *) NULL,
			(struct inode *) NULL, (struct dentry *) NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_4ARGS_VFS_RENAME, 1,
		          [vfs_rename() wants 4 args])
	],[
		AC_MSG_RESULT(no)
		dnl #
		dnl # Linux 3.13 API change
		dnl # Added delegated inode
		dnl #
		AC_MSG_CHECKING([whether vfs_rename() wants 5 args])
		SPL_LINUX_TRY_COMPILE([
			#include <linux/fs.h>
		],[
			vfs_rename((struct inode *) NULL,
				(struct dentry *) NULL,
				(struct inode *) NULL,
				(struct dentry *) NULL,
				(struct inode **) NULL);
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_5ARGS_VFS_RENAME, 1,
				  [vfs_rename() wants 5 args])
		],[
			AC_MSG_RESULT(no)
			dnl #
			dnl # Linux 3.15 API change
			dnl # Added flags
			dnl #
			AC_MSG_CHECKING([whether vfs_rename() wants 6 args])
			SPL_LINUX_TRY_COMPILE([
				#include <linux/fs.h>
			],[
				vfs_rename((struct inode *) NULL,
					(struct dentry *) NULL,
					(struct inode *) NULL,
					(struct dentry *) NULL,
					(struct inode **) NULL,
					(unsigned int) 0);
			],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_6ARGS_VFS_RENAME, 1,
					  [vfs_rename() wants 6 args])
			],[
				AC_MSG_ERROR(no)
			])
		])
	])
])

dnl #
dnl # 2.6.36 API change,
dnl # The 'struct fs_struct->lock' was changed from a rwlock_t to
dnl # a spinlock_t to improve the fastpath performance.
dnl #
AC_DEFUN([SPL_AC_FS_STRUCT_SPINLOCK], [
	AC_MSG_CHECKING([whether struct fs_struct uses spinlock_t])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sched.h>
		#include <linux/fs_struct.h>
	],[
		static struct fs_struct fs;
		spin_lock_init(&fs.lock);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FS_STRUCT_SPINLOCK, 1,
		          [struct fs_struct uses spinlock_t])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # User namespaces, use kuid_t in place of uid_t
dnl # where available. Not strictly a user namespaces thing
dnl # but it should prevent surprises
dnl #
AC_DEFUN([SPL_AC_KUIDGID_T], [
	AC_MSG_CHECKING([whether kuid_t/kgid_t is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/uidgid.h>
	], [
		kuid_t userid = KUIDT_INIT(0);
		kgid_t groupid = KGIDT_INIT(0);
	],[
		SPL_LINUX_TRY_COMPILE([
			#include <linux/uidgid.h>
		], [
			kuid_t userid = 0;
			kgid_t groupid = 0;
		],[
			AC_MSG_RESULT(yes; optional)
		],[
			AC_MSG_RESULT(yes; mandatory)
			AC_DEFINE(HAVE_KUIDGID_T, 1, [kuid_t/kgid_t in use])
		])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.39 API change,
dnl # __put_task_struct() was exported by the mainline kernel.
dnl #
AC_DEFUN([SPL_AC_PUT_TASK_STRUCT],
	[AC_MSG_CHECKING([whether __put_task_struct() is available])
	SPL_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/sched.h>
	], [
		__put_task_struct(NULL);
	], [__put_task_struct], [], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PUT_TASK_STRUCT, 1,
		          [__put_task_struct() is available])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.35 API change,
dnl # Unused 'struct dentry *' removed from vfs_fsync() prototype.
dnl #
AC_DEFUN([SPL_AC_2ARGS_VFS_FSYNC], [
	AC_MSG_CHECKING([whether vfs_fsync() wants 2 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_fsync(NULL, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_VFS_FSYNC, 1, [vfs_fsync() wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.5 API change,
dnl # inode_operations.truncate_range removed
dnl #
AC_DEFUN([SPL_AC_INODE_TRUNCATE_RANGE], [
	AC_MSG_CHECKING([whether truncate_range() inode operation is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct inode_operations ops;
		ops.truncate_range = NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_TRUNCATE_RANGE, 1,
			[truncate_range() inode operation is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 2.6.38 - 3.x API
dnl #
AC_DEFUN([SPL_AC_KERNEL_FILE_FALLOCATE], [
	AC_MSG_CHECKING([whether fops->fallocate() exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		long (*fallocate) (struct file *, int, loff_t, loff_t) = NULL;
		struct file_operations fops __attribute__ ((unused)) = {
			.fallocate = fallocate,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_FALLOCATE, 1, [fops->fallocate() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 2.6.x - 2.6.37 API
dnl #
AC_DEFUN([SPL_AC_KERNEL_INODE_FALLOCATE], [
	AC_MSG_CHECKING([whether iops->fallocate() exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		long (*fallocate) (struct inode *, int, loff_t, loff_t) = NULL;
		struct inode_operations fops __attribute__ ((unused)) = {
			.fallocate = fallocate,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_FALLOCATE, 1, [fops->fallocate() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # PaX Linux 2.6.38 - 3.x API
dnl #
AC_DEFUN([SPL_AC_PAX_KERNEL_FILE_FALLOCATE], [
	AC_MSG_CHECKING([whether fops->fallocate() exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		long (*fallocate) (struct file *, int, loff_t, loff_t) = NULL;
		struct file_operations_no_const fops __attribute__ ((unused)) = {
			.fallocate = fallocate,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_FALLOCATE, 1, [fops->fallocate() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # The fallocate callback was moved from the inode_operations
dnl # structure to the file_operations structure.
dnl #
AC_DEFUN([SPL_AC_KERNEL_FALLOCATE], [
	SPL_AC_KERNEL_FILE_FALLOCATE
	SPL_AC_KERNEL_INODE_FALLOCATE
	SPL_AC_PAX_KERNEL_FILE_FALLOCATE
])

dnl #
dnl # zlib inflate compat,
dnl # Verify the kernel has CONFIG_ZLIB_INFLATE support enabled.
dnl #
AC_DEFUN([SPL_AC_CONFIG_ZLIB_INFLATE], [
	AC_MSG_CHECKING([whether CONFIG_ZLIB_INFLATE is defined])
	SPL_LINUX_TRY_COMPILE([
		#if !defined(CONFIG_ZLIB_INFLATE) && \
		    !defined(CONFIG_ZLIB_INFLATE_MODULE)
		#error CONFIG_ZLIB_INFLATE not defined
		#endif
	],[ ],[
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([
	*** This kernel does not include the required zlib inflate support.
	*** Rebuild the kernel with CONFIG_ZLIB_INFLATE=y|m set.])
	])
])

dnl #
dnl # zlib deflate compat,
dnl # Verify the kernel has CONFIG_ZLIB_DEFLATE support enabled.
dnl #
AC_DEFUN([SPL_AC_CONFIG_ZLIB_DEFLATE], [
	AC_MSG_CHECKING([whether CONFIG_ZLIB_DEFLATE is defined])
	SPL_LINUX_TRY_COMPILE([
		#if !defined(CONFIG_ZLIB_DEFLATE) && \
		    !defined(CONFIG_ZLIB_DEFLATE_MODULE)
		#error CONFIG_ZLIB_DEFLATE not defined
		#endif
	],[ ],[
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([
	*** This kernel does not include the required zlib deflate support.
	*** Rebuild the kernel with CONFIG_ZLIB_DEFLATE=y|m set.])
	])
])

dnl #
dnl # config trim unused symbols,
dnl # Verify the kernel has CONFIG_TRIM_UNUSED_KSYMS DISABLED.
dnl #
AC_DEFUN([SPL_AC_CONFIG_TRIM_UNUSED_KSYMS], [
	AC_MSG_CHECKING([whether CONFIG_TRIM_UNUSED_KSYM is disabled])
	SPL_LINUX_TRY_COMPILE([
		#if defined(CONFIG_TRIM_UNUSED_KSYMS)
		#error CONFIG_TRIM_UNUSED_KSYMS not defined
		#endif
	],[ ],[
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([
	*** This kernel has unused symbols trimming enabled, please disable.
	*** Rebuild the kernel with CONFIG_TRIM_UNUSED_KSYMS=n set.])
	])
])

dnl #
dnl # 2.6.39 API compat,
dnl # The function zlib_deflate_workspacesize() now take 2 arguments.
dnl # This was done to avoid always having to allocate the maximum size
dnl # workspace (268K).  The caller can now specific the windowBits and
dnl # memLevel compression parameters to get a smaller workspace.
dnl #
AC_DEFUN([SPL_AC_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE],
	[AC_MSG_CHECKING([whether zlib_deflate_workspacesize() wants 2 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/zlib.h>
	],[
		return zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE, 1,
		          [zlib_deflate_workspacesize() wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.39 API change,
dnl # Shrinker adjust to use common shrink_control structure.
dnl #
AC_DEFUN([SPL_AC_SHRINK_CONTROL_STRUCT], [
	AC_MSG_CHECKING([whether struct shrink_control exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		struct shrink_control sc __attribute__ ((unused));

		sc.nr_to_scan = 0;
		sc.gfp_mask = GFP_KERNEL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SHRINK_CONTROL_STRUCT, 1,
			[struct shrink_control exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.1 API Change
dnl #
dnl # The rw_semaphore.wait_lock member was changed from spinlock_t to
dnl # raw_spinlock_t at commit ddb6c9b58a19edcfac93ac670b066c836ff729f1.
dnl #
AC_DEFUN([SPL_AC_RWSEM_SPINLOCK_IS_RAW], [
	AC_MSG_CHECKING([whether struct rw_semaphore member wait_lock is raw])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/rwsem.h>
	],[
		struct rw_semaphore dummy_semaphore __attribute__ ((unused));
		raw_spinlock_t dummy_lock __attribute__ ((unused));
		dummy_semaphore.wait_lock = dummy_lock;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(RWSEM_SPINLOCK_IS_RAW, 1,
		[struct rw_semaphore member wait_lock is raw_spinlock_t])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 3.16 API Change
dnl #
dnl # rwsem-spinlock "->activity" changed to "->count"
dnl #
AC_DEFUN([SPL_AC_RWSEM_ACTIVITY], [
	AC_MSG_CHECKING([whether struct rw_semaphore has member activity])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/rwsem.h>
	],[
		struct rw_semaphore dummy_semaphore __attribute__ ((unused));
		dummy_semaphore.activity = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RWSEM_ACTIVITY, 1,
		[struct rw_semaphore has member activity])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 4.8 API Change
dnl #
dnl # rwsem "->count" changed to atomic_long_t type
dnl #
AC_DEFUN([SPL_AC_RWSEM_ATOMIC_LONG_COUNT], [
	AC_MSG_CHECKING(
	[whether struct rw_semaphore has atomic_long_t member count])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/rwsem.h>
	],[
		DECLARE_RWSEM(dummy_semaphore);
		(void) atomic_long_read(&dummy_semaphore.count);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RWSEM_ATOMIC_LONG_COUNT, 1,
		[struct rw_semaphore has atomic_long_t member count])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 3.9 API change,
dnl # Moved things from linux/sched.h to linux/sched/rt.h
dnl #
AC_DEFUN([SPL_AC_SCHED_RT_HEADER],
	[AC_MSG_CHECKING([whether header linux/sched/rt.h exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sched.h>
		#include <linux/sched/rt.h>
	],[
		return 0;
	],[
		AC_DEFINE(HAVE_SCHED_RT_HEADER, 1, [linux/sched/rt.h exists])
		AC_MSG_RESULT(yes)
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.9 API change,
dnl # vfs_getattr() uses 2 args
dnl # It takes struct path * instead of struct vfsmount * and struct dentry *
dnl #
AC_DEFUN([SPL_AC_2ARGS_VFS_GETATTR], [
	AC_MSG_CHECKING([whether vfs_getattr() wants])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_getattr((struct path *) NULL,
			(struct kstat *)NULL);
	],[
		AC_MSG_RESULT(2 args)
		AC_DEFINE(HAVE_2ARGS_VFS_GETATTR, 1,
		          [vfs_getattr wants 2 args])
	],[
		SPL_LINUX_TRY_COMPILE([
			#include <linux/fs.h>
		],[
			vfs_getattr((struct vfsmount *)NULL,
				(struct dentry *)NULL,
				(struct kstat *)NULL);
		],[
			AC_MSG_RESULT(3 args)
		],[
			AC_MSG_ERROR(unknown)
		])
	])
])

dnl #
dnl # 2.6.36 API compatibility.
dnl # Added usleep_range timer.
dnl # usleep_range is a finer precision implementation of msleep
dnl # designed to be a drop-in replacement for udelay where a precise
dnl # sleep / busy-wait is unnecessary.
dnl #
AC_DEFUN([SPL_AC_USLEEP_RANGE], [
	AC_MSG_CHECKING([whether usleep_range() is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/delay.h>
	],[
		usleep_range(0, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_USLEEP_RANGE, 1,
		          [usleep_range is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.35 API change,
dnl # The cachep->gfpflags member was renamed cachep->allocflags.  These are
dnl # private allocation flags which are applied when allocating a new slab
dnl # in kmem_getpages().  Unfortunately there is no public API for setting
dnl # non-default flags.
dnl #
AC_DEFUN([SPL_AC_KMEM_CACHE_ALLOCFLAGS], [
	AC_MSG_CHECKING([whether struct kmem_cache has allocflags])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/slab.h>
	],[
		struct kmem_cache cachep __attribute__ ((unused));
		cachep.allocflags = GFP_KERNEL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KMEM_CACHE_ALLOCFLAGS, 1,
			[struct kmem_cache has allocflags])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether struct kmem_cache has gfpflags])
		SPL_LINUX_TRY_COMPILE([
			#include <linux/slab.h>
		],[
			struct kmem_cache cachep __attribute__ ((unused));
			cachep.gfpflags = GFP_KERNEL;
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_KMEM_CACHE_GFPFLAGS, 1,
				[struct kmem_cache has gfpflags])
		],[
			AC_MSG_RESULT(no)
		])
	])
])

dnl #
dnl # 3.17 API change,
dnl # wait_on_bit() no longer requires an action argument. The former
dnl # "wait_on_bit" interface required an 'action' function to be provided
dnl # which does the actual waiting. There were over 20 such functions in the
dnl # kernel, many of them identical, though most cases can be satisfied by one
dnl # of just two functions: one which uses io_schedule() and one which just
dnl # uses schedule().  This API change was made to consolidate all of those
dnl # redundant wait functions.
dnl #
AC_DEFUN([SPL_AC_WAIT_ON_BIT], [
	AC_MSG_CHECKING([whether wait_on_bit() takes an action])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/wait.h>
	],[
		int (*action)(void *) = NULL;
		wait_on_bit(NULL, 0, action, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_WAIT_ON_BIT_ACTION, 1, [yes])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.7 API change
dnl # i_mutex is changed to i_rwsem. Instead of directly using
dnl # i_mutex/i_rwsem, we should use inode_lock() and inode_lock_shared()
dnl # We test inode_lock_shared because inode_lock is introduced earlier.
dnl #
AC_DEFUN([SPL_AC_INODE_LOCK], [
	AC_MSG_CHECKING([whether inode_lock_shared() exists])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct inode *inode = NULL;
		inode_lock_shared(inode);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_LOCK_SHARED, 1, [yes])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # Check whether mutex has owner with task_struct type.
dnl #
dnl # Note that before Linux 3.0, mutex owner is of type thread_info.
dnl #
dnl # Note that in Linux 3.18, the condition for owner is changed from
dnl # defined(CONFIG_DEBUG_MUTEXES) || defined(CONFIG_SMP) to
dnl # defined(CONFIG_DEBUG_MUTEXES) || defined(CONFIG_MUTEX_SPIN_ON_OWNER)
dnl #
AC_DEFUN([SPL_AC_MUTEX_OWNER], [
	AC_MSG_CHECKING([whether mutex has owner])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mutex.h>
		#include <linux/spinlock.h>
	],[
		DEFINE_MUTEX(m);
		struct task_struct *t __attribute__ ((unused));
		t = m.owner;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MUTEX_OWNER, 1, [yes])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 4.9 API change
dnl # group_info changed from 2d array via >blocks to 1d array via ->gid
dnl #
AC_DEFUN([SPL_AC_GROUP_INFO_GID], [
	AC_MSG_CHECKING([whether group_info->gid exists])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/cred.h>
	],[
		struct group_info *gi = groups_alloc(1);
		gi->gid[0] = KGIDT_INIT(0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GROUP_INFO_GID, 1, [group_info->gid exists])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
