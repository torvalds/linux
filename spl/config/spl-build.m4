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

	SPL_AC_TEST_MODULE
	SPL_AC_ATOMIC_SPINLOCK
	SPL_AC_SHRINKER_CALLBACK
	SPL_AC_CTL_NAME
	SPL_AC_CONFIG_TRIM_UNUSED_KSYMS
	SPL_AC_PDE_DATA
	SPL_AC_SET_FS_PWD_WITH_CONST
	SPL_AC_2ARGS_VFS_FSYNC
	SPL_AC_INODE_TRUNCATE_RANGE
	SPL_AC_FS_STRUCT_SPINLOCK
	SPL_AC_KUIDGID_T
	SPL_AC_KERNEL_FALLOCATE
	SPL_AC_CONFIG_ZLIB_INFLATE
	SPL_AC_CONFIG_ZLIB_DEFLATE
	SPL_AC_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE
	SPL_AC_SHRINK_CONTROL_STRUCT
	SPL_AC_RWSEM_SPINLOCK_IS_RAW
	SPL_AC_RWSEM_ACTIVITY
	SPL_AC_RWSEM_ATOMIC_LONG_COUNT
	SPL_AC_SCHED_RT_HEADER
	SPL_AC_SCHED_SIGNAL_HEADER
	SPL_AC_4ARGS_VFS_GETATTR
	SPL_AC_3ARGS_VFS_GETATTR
	SPL_AC_2ARGS_VFS_GETATTR
	SPL_AC_USLEEP_RANGE
	SPL_AC_KMEM_CACHE_ALLOCFLAGS
	SPL_AC_KERNEL_INODE_TIMES
	SPL_AC_WAIT_ON_BIT
	SPL_AC_INODE_LOCK
	SPL_AC_GROUP_INFO_GID
	SPL_AC_KMEM_CACHE_CREATE_USERCOPY
	SPL_AC_WAIT_QUEUE_ENTRY_T
	SPL_AC_WAIT_QUEUE_HEAD_ENTRY
	SPL_AC_IO_SCHEDULE_TIMEOUT
	SPL_AC_KERNEL_WRITE
	SPL_AC_KERNEL_READ
	SPL_AC_KERNEL_TIMER_FUNCTION_TIMER_LIST
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
		withlinux=yes
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
		if test x$withlinux != xyes -a -e "/lib/modules/$(uname -r)/build"; then
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

	RPM_DEFINE_COMMON='--define "$(DEBUG_SPL) 1"'
	RPM_DEFINE_COMMON+=' --define "$(DEBUG_KMEM) 1"'
	RPM_DEFINE_COMMON+=' --define "$(DEBUG_KMEM_TRACKING) 1"'
	RPM_DEFINE_UTIL=
	RPM_DEFINE_KMOD='--define "kernels $(LINUX_VERSION)"'
	RPM_DEFINE_KMOD+=' --define "_wrong_version_format_terminate_build 0"'
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
