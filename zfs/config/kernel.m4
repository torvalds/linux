dnl #
dnl # Default ZFS kernel configuration 
dnl #
AC_DEFUN([ZFS_AC_CONFIG_KERNEL], [
	ZFS_AC_KERNEL
	ZFS_AC_SPL
	ZFS_AC_TEST_MODULE
	ZFS_AC_KERNEL_CONFIG
	ZFS_AC_KERNEL_DECLARE_EVENT_CLASS
	ZFS_AC_KERNEL_CURRENT_BIO_TAIL
	ZFS_AC_KERNEL_SUBMIT_BIO
	ZFS_AC_KERNEL_BDEV_BLOCK_DEVICE_OPERATIONS
	ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID
	ZFS_AC_KERNEL_TYPE_FMODE_T
	ZFS_AC_KERNEL_KOBJ_NAME_LEN
	ZFS_AC_KERNEL_3ARG_BLKDEV_GET
	ZFS_AC_KERNEL_BLKDEV_GET_BY_PATH
	ZFS_AC_KERNEL_OPEN_BDEV_EXCLUSIVE
	ZFS_AC_KERNEL_LOOKUP_BDEV
	ZFS_AC_KERNEL_INVALIDATE_BDEV_ARGS
	ZFS_AC_KERNEL_BDEV_LOGICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_BDEV_PHYSICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_BIO_BVEC_ITER
	ZFS_AC_KERNEL_BIO_FAILFAST_DTD
	ZFS_AC_KERNEL_REQ_FAILFAST_MASK
	ZFS_AC_KERNEL_REQ_OP_DISCARD
	ZFS_AC_KERNEL_REQ_OP_SECURE_ERASE
	ZFS_AC_KERNEL_REQ_OP_FLUSH
	ZFS_AC_KERNEL_BIO_BI_OPF
	ZFS_AC_KERNEL_BIO_END_IO_T_ARGS
	ZFS_AC_KERNEL_BIO_RW_BARRIER
	ZFS_AC_KERNEL_BIO_RW_DISCARD
	ZFS_AC_KERNEL_BLK_QUEUE_FLUSH
	ZFS_AC_KERNEL_BLK_QUEUE_MAX_HW_SECTORS
	ZFS_AC_KERNEL_BLK_QUEUE_MAX_SEGMENTS
	ZFS_AC_KERNEL_BLK_QUEUE_HAVE_BIO_RW_UNPLUG
	ZFS_AC_KERNEL_BLK_QUEUE_HAVE_BLK_PLUG
	ZFS_AC_KERNEL_GET_DISK_RO
	ZFS_AC_KERNEL_GET_GENDISK
	ZFS_AC_KERNEL_HAVE_BIO_SET_OP_ATTRS
	ZFS_AC_KERNEL_GENERIC_READLINK_GLOBAL
	ZFS_AC_KERNEL_DISCARD_GRANULARITY
	ZFS_AC_KERNEL_CONST_XATTR_HANDLER
	ZFS_AC_KERNEL_XATTR_HANDLER_NAME
	ZFS_AC_KERNEL_XATTR_HANDLER_GET
	ZFS_AC_KERNEL_XATTR_HANDLER_SET
	ZFS_AC_KERNEL_XATTR_HANDLER_LIST
	ZFS_AC_KERNEL_INODE_OWNER_OR_CAPABLE
	ZFS_AC_KERNEL_POSIX_ACL_FROM_XATTR_USERNS
	ZFS_AC_KERNEL_POSIX_ACL_RELEASE
	ZFS_AC_KERNEL_SET_CACHED_ACL_USABLE
	ZFS_AC_KERNEL_POSIX_ACL_CHMOD
	ZFS_AC_KERNEL_POSIX_ACL_EQUIV_MODE_WANTS_UMODE_T
	ZFS_AC_KERNEL_POSIX_ACL_VALID_WITH_NS
	ZFS_AC_KERNEL_INODE_OPERATIONS_PERMISSION
	ZFS_AC_KERNEL_INODE_OPERATIONS_PERMISSION_WITH_NAMEIDATA
	ZFS_AC_KERNEL_INODE_OPERATIONS_CHECK_ACL
	ZFS_AC_KERNEL_INODE_OPERATIONS_CHECK_ACL_WITH_FLAGS
	ZFS_AC_KERNEL_INODE_OPERATIONS_GET_ACL
	ZFS_AC_KERNEL_INODE_OPERATIONS_SET_ACL
	ZFS_AC_KERNEL_GET_ACL_HANDLE_CACHE
	ZFS_AC_KERNEL_SHOW_OPTIONS
	ZFS_AC_KERNEL_FILE_INODE
	ZFS_AC_KERNEL_FSYNC
	ZFS_AC_KERNEL_EVICT_INODE
	ZFS_AC_KERNEL_DIRTY_INODE_WITH_FLAGS
	ZFS_AC_KERNEL_NR_CACHED_OBJECTS
	ZFS_AC_KERNEL_FREE_CACHED_OBJECTS
	ZFS_AC_KERNEL_FALLOCATE
	ZFS_AC_KERNEL_AIO_FSYNC
	ZFS_AC_KERNEL_MKDIR_UMODE_T
	ZFS_AC_KERNEL_LOOKUP_NAMEIDATA
	ZFS_AC_KERNEL_CREATE_NAMEIDATA
	ZFS_AC_KERNEL_GET_LINK
	ZFS_AC_KERNEL_PUT_LINK
	ZFS_AC_KERNEL_TRUNCATE_RANGE
	ZFS_AC_KERNEL_AUTOMOUNT
	ZFS_AC_KERNEL_ENCODE_FH_WITH_INODE
	ZFS_AC_KERNEL_COMMIT_METADATA
	ZFS_AC_KERNEL_CLEAR_INODE
	ZFS_AC_KERNEL_SETATTR_PREPARE
	ZFS_AC_KERNEL_INSERT_INODE_LOCKED
	ZFS_AC_KERNEL_D_MAKE_ROOT
	ZFS_AC_KERNEL_D_OBTAIN_ALIAS
	ZFS_AC_KERNEL_D_PRUNE_ALIASES
	ZFS_AC_KERNEL_D_SET_D_OP
	ZFS_AC_KERNEL_D_REVALIDATE_NAMEIDATA
	ZFS_AC_KERNEL_CONST_DENTRY_OPERATIONS
	ZFS_AC_KERNEL_CHECK_DISK_SIZE_CHANGE
	ZFS_AC_KERNEL_TRUNCATE_SETSIZE
	ZFS_AC_KERNEL_6ARGS_SECURITY_INODE_INIT_SECURITY
	ZFS_AC_KERNEL_CALLBACK_SECURITY_INODE_INIT_SECURITY
	ZFS_AC_KERNEL_MOUNT_NODEV
	ZFS_AC_KERNEL_SHRINK
	ZFS_AC_KERNEL_SHRINK_CONTROL_HAS_NID
	ZFS_AC_KERNEL_S_INSTANCES_LIST_HEAD
	ZFS_AC_KERNEL_S_D_OP
	ZFS_AC_KERNEL_BDI_SETUP_AND_REGISTER
	ZFS_AC_KERNEL_SET_NLINK
	ZFS_AC_KERNEL_ELEVATOR_CHANGE
	ZFS_AC_KERNEL_5ARG_SGET
	ZFS_AC_KERNEL_LSEEK_EXECUTE
	ZFS_AC_KERNEL_VFS_ITERATE
	ZFS_AC_KERNEL_VFS_RW_ITERATE
	ZFS_AC_KERNEL_GENERIC_WRITE_CHECKS
	ZFS_AC_KERNEL_KMAP_ATOMIC_ARGS
	ZFS_AC_KERNEL_FOLLOW_DOWN_ONE
	ZFS_AC_KERNEL_MAKE_REQUEST_FN
	ZFS_AC_KERNEL_GENERIC_IO_ACCT
	ZFS_AC_KERNEL_RENAME_WANTS_FLAGS
	ZFS_AC_KERNEL_HAVE_GENERIC_SETXATTR

	AS_IF([test "$LINUX_OBJ" != "$LINUX"], [
		KERNELMAKE_PARAMS="$KERNELMAKE_PARAMS O=$LINUX_OBJ"
	])
	AC_SUBST(KERNELMAKE_PARAMS)


	dnl # -Wall -fno-strict-aliasing -Wstrict-prototypes and other
	dnl # compiler options are added by the kernel build system.
	KERNELCPPFLAGS="$KERNELCPPFLAGS $NO_UNUSED_BUT_SET_VARIABLE"
	KERNELCPPFLAGS="$KERNELCPPFLAGS $NO_BOOL_COMPARE"
	KERNELCPPFLAGS="$KERNELCPPFLAGS -DHAVE_SPL -D_KERNEL"
	KERNELCPPFLAGS="$KERNELCPPFLAGS -DTEXT_DOMAIN=\\\"zfs-linux-kernel\\\""

	AC_SUBST(KERNELCPPFLAGS)
])

dnl #
dnl # Detect name used for Module.symvers file in kernel
dnl #
AC_DEFUN([ZFS_AC_MODULE_SYMVERS], [
	modpost=$LINUX/scripts/Makefile.modpost
	AC_MSG_CHECKING([kernel file name for module symbols])
	AS_IF([test "x$enable_linux_builtin" != xyes -a -f "$modpost"], [
		AS_IF([grep -q Modules.symvers $modpost], [
			LINUX_SYMBOLS=Modules.symvers
		], [
			LINUX_SYMBOLS=Module.symvers
		])

		AS_IF([test ! -f "$LINUX_OBJ/$LINUX_SYMBOLS"], [
			AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed.  If you are building with a custom kernel, make sure the
	*** kernel is configured, built, and the '--with-linux=PATH' configure
	*** option refers to the location of the kernel source.])
		])
	], [
		LINUX_SYMBOLS=NONE
	])
	AC_MSG_RESULT($LINUX_SYMBOLS)
	AC_SUBST(LINUX_SYMBOLS)
])

dnl #
dnl # Detect the kernel to be built against
dnl #
AC_DEFUN([ZFS_AC_KERNEL], [
	AC_ARG_WITH([linux],
		AS_HELP_STRING([--with-linux=PATH],
		[Path to kernel source]),
		[kernelsrc="$withval"])

	AC_ARG_WITH(linux-obj,
		AS_HELP_STRING([--with-linux-obj=PATH],
		[Path to kernel build objects]),
		[kernelbuild="$withval"])

	AC_MSG_CHECKING([kernel source directory])
	AS_IF([test -z "$kernelsrc"], [
		AS_IF([test -e "/lib/modules/$(uname -r)/source"], [
			headersdir="/lib/modules/$(uname -r)/source"
			sourcelink=$(readlink -f "$headersdir")
		], [test -e "/lib/modules/$(uname -r)/build"], [
			headersdir="/lib/modules/$(uname -r)/build"
			sourcelink=$(readlink -f "$headersdir")
		], [
			sourcelink=$(ls -1d /usr/src/kernels/* \
			             /usr/src/linux-* \
			             2>/dev/null | grep -v obj | tail -1)
		])

		AS_IF([test -n "$sourcelink" && test -e ${sourcelink}], [
			kernelsrc=`readlink -f ${sourcelink}`
		], [
			kernelsrc="[Not found]"
		])
	], [
		AS_IF([test "$kernelsrc" = "NONE"], [
			kernsrcver=NONE
		])
	])

	AC_MSG_RESULT([$kernelsrc])
	AS_IF([test ! -d "$kernelsrc"], [
		AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed and then try again.  If that fails, you can specify the
	*** location of the kernel source with the '--with-linux=PATH' option.])
	])

	AC_MSG_CHECKING([kernel build directory])
	AS_IF([test -z "$kernelbuild"], [
		AS_IF([test -e "/lib/modules/$(uname -r)/build"], [
			kernelbuild=`readlink -f /lib/modules/$(uname -r)/build`
		], [test -d ${kernelsrc}-obj/${target_cpu}/${target_cpu}], [
			kernelbuild=${kernelsrc}-obj/${target_cpu}/${target_cpu}
		], [test -d ${kernelsrc}-obj/${target_cpu}/default], [
			kernelbuild=${kernelsrc}-obj/${target_cpu}/default
		], [test -d `dirname ${kernelsrc}`/build-${target_cpu}], [
			kernelbuild=`dirname ${kernelsrc}`/build-${target_cpu}
		], [
			kernelbuild=${kernelsrc}
		])
	])
	AC_MSG_RESULT([$kernelbuild])

	AC_MSG_CHECKING([kernel source version])
	utsrelease1=$kernelbuild/include/linux/version.h
	utsrelease2=$kernelbuild/include/linux/utsrelease.h
	utsrelease3=$kernelbuild/include/generated/utsrelease.h
	AS_IF([test -r $utsrelease1 && fgrep -q UTS_RELEASE $utsrelease1], [
		utsrelease=linux/version.h
	], [test -r $utsrelease2 && fgrep -q UTS_RELEASE $utsrelease2], [
		utsrelease=linux/utsrelease.h
	], [test -r $utsrelease3 && fgrep -q UTS_RELEASE $utsrelease3], [
		utsrelease=generated/utsrelease.h
	])

	AS_IF([test "$utsrelease"], [
		kernsrcver=`(echo "#include <$utsrelease>";
		             echo "kernsrcver=UTS_RELEASE") |
		             cpp -I $kernelbuild/include |
		             grep "^kernsrcver=" | cut -d \" -f 2`

		AS_IF([test -z "$kernsrcver"], [
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([*** Cannot determine kernel version.])
		])
	], [
		AC_MSG_RESULT([Not found])
		if test "x$enable_linux_builtin" != xyes; then
			AC_MSG_ERROR([*** Cannot find UTS_RELEASE definition.])
		else
			AC_MSG_ERROR([
	*** Cannot find UTS_RELEASE definition.
	*** Please run 'make prepare' inside the kernel source tree.])
		fi
	])

	AC_MSG_RESULT([$kernsrcver])

	LINUX=${kernelsrc}
	LINUX_OBJ=${kernelbuild}
	LINUX_VERSION=${kernsrcver}

	AC_SUBST(LINUX)
	AC_SUBST(LINUX_OBJ)
	AC_SUBST(LINUX_VERSION)

	ZFS_AC_MODULE_SYMVERS
])


dnl #
dnl # Detect the SPL module to be built against
dnl #
AC_DEFUN([ZFS_AC_SPL], [
	AC_ARG_WITH([spl],
		AS_HELP_STRING([--with-spl=PATH],
		[Path to spl source]),
		[splsrc="$withval"])

	AC_ARG_WITH([spl-obj],
		AS_HELP_STRING([--with-spl-obj=PATH],
		[Path to spl build objects]),
		[splbuild="$withval"])

	AC_ARG_WITH([spl-timeout],
		AS_HELP_STRING([--with-spl-timeout=SECS],
		[Wait SECS for SPL header and symver file @<:@default=0@:>@]),
		[timeout="$withval"], [timeout=0])

	dnl #
	dnl # The existence of spl.release.in is used to identify a valid
	dnl # source directory.  In order of preference:
	dnl #
	splsrc0="/var/lib/dkms/spl/${VERSION}/build"
	splsrc1="/usr/local/src/spl-${VERSION}/${LINUX_VERSION}"
	splsrc2="/usr/local/src/spl-${VERSION}"
	splsrc3="/usr/src/spl-${VERSION}/${LINUX_VERSION}"
	splsrc4="/usr/src/spl-${VERSION}"
	splsrc5="../spl/"
	splsrc6="$LINUX"

	AC_MSG_CHECKING([spl source directory])
	AS_IF([test -z "${splsrc}"], [
		AS_IF([ test -e "${splsrc0}/spl.release.in"], [
			splsrc=${splsrc0}
		], [ test -e "${splsrc1}/spl.release.in"], [
			splsrc=${splsrc1}
		], [ test -e "${splsrc2}/spl.release.in"], [
			splsrc=${splsrc2}
		], [ test -e "${splsrc3}/spl.release.in"], [
			splsrc=$(readlink -f "${splsrc3}")
		], [ test -e "${splsrc4}/spl.release.in" ], [
			splsrc=${splsrc4}
		], [ test -e "${splsrc5}/spl.release.in"], [
			splsrc=$(readlink -f "${splsrc5}")
		], [ test -e "${splsrc6}/spl.release.in" ], [
			splsrc=${splsrc6}
		], [
			splsrc="[Not found]"
		])
	], [
		AS_IF([test "$splsrc" = "NONE"], [
			splbuild=NONE
			splsrcver=NONE
		])
	])

	AC_MSG_RESULT([$splsrc])
	AS_IF([ test ! -e "$splsrc/spl.release.in"], [
		AC_MSG_ERROR([
	*** Please make sure the kmod spl devel package for your distribution
	*** is installed then try again.  If that fails you can specify the
	*** location of the spl source with the '--with-spl=PATH' option.])
	])

	dnl #
	dnl # The existence of the spl_config.h is used to identify a valid
	dnl # spl object directory.  In many cases the object and source
	dnl # directory are the same, however the objects may also reside
	dnl # is a subdirectory named after the kernel version.
	dnl #
	dnl # This file is supposed to be available after DKMS finishes
	dnl # building the SPL kernel module for the target kernel.  The
	dnl # '--with-spl-timeout' option can be passed to pause here,
	dnl # waiting for the file to appear from a concurrently building
	dnl # SPL package.
	dnl #
	AC_MSG_CHECKING([spl build directory])
	while true; do
		AS_IF([test -z "$splbuild"], [
			AS_IF([ test -e "${splsrc}/${LINUX_VERSION}/spl_config.h" ], [
				splbuild="${splsrc}/${LINUX_VERSION}"
			], [ test -e "${splsrc}/spl_config.h" ], [
				splbuild="${splsrc}"
			], [ find -L "${splsrc}" -name spl_config.h 2> /dev/null | grep -wq spl_config.h ], [
				splbuild=$(find -L "${splsrc}" -name spl_config.h | sed 's,/spl_config.h,,')
			], [
				splbuild="[Not found]"
			])
		])
		AS_IF([test -e "$splbuild/spl_config.h" -o $timeout -le 0], [
			break;
		], [
			sleep 1
			timeout=$((timeout-1))
		])
	done

	AC_MSG_RESULT([$splbuild])
	AS_IF([ ! test -e "$splbuild/spl_config.h"], [
		AC_MSG_ERROR([
	*** Please make sure the kmod spl devel <kernel> package for your
	*** distribution is installed then try again.  If that fails you
	*** can specify the location of the spl objects with the
	*** '--with-spl-obj=PATH' option.])
	])

	AC_MSG_CHECKING([spl source version])
	AS_IF([test -r $splbuild/spl_config.h &&
		fgrep -q SPL_META_VERSION $splbuild/spl_config.h], [

		splsrcver=`(echo "#include <spl_config.h>";
		            echo "splsrcver=SPL_META_VERSION-SPL_META_RELEASE") |
		            cpp -I $splbuild |
		            grep "^splsrcver=" | tr -d \" | cut -d= -f2`
	])

	AS_IF([test -z "$splsrcver"], [
		AC_MSG_RESULT([Not found])
		AC_MSG_ERROR([
	*** Cannot determine the version of the spl source.
	*** Please prepare the spl source before running this script])
	])

	AC_MSG_RESULT([$splsrcver])

	SPL=${splsrc}
	SPL_OBJ=${splbuild}
	SPL_VERSION=${splsrcver}

	AC_SUBST(SPL)
	AC_SUBST(SPL_OBJ)
	AC_SUBST(SPL_VERSION)

	dnl #
	dnl # Detect the name used for the SPL Module.symvers file.  If one
	dnl # does not exist this is likely because the SPL has been configured
	dnl # but not built.  The '--with-spl-timeout' option can be passed
	dnl # to pause here, waiting for the file to appear from a concurrently
	dnl # building SPL package.  If the file does not appear in time, a good
	dnl # guess is made as to what this file will be named based on what it
	dnl # is named in the kernel build products.  This file will first be
	dnl # used at link time so if the guess is wrong the build will fail
	dnl # then.  This unfortunately means the ZFS package does not contain a
	dnl # reliable mechanism to detect symbols exported by the SPL at
	dnl # configure time.
	dnl #
	AC_MSG_CHECKING([spl file name for module symbols])
	SPL_SYMBOLS=NONE

	while true; do
		AS_IF([test -r $SPL_OBJ/Module.symvers], [
			SPL_SYMBOLS=Module.symvers
		], [test -r $SPL_OBJ/Modules.symvers], [
			SPL_SYMBOLS=Modules.symvers
		], [test -r $SPL_OBJ/module/Module.symvers], [
			SPL_SYMBOLS=Module.symvers
		], [test -r $SPL_OBJ/module/Modules.symvers], [
			SPL_SYMBOLS=Modules.symvers
		])

		AS_IF([test $SPL_SYMBOLS != NONE -o $timeout -le 0], [
			break;
		], [
			sleep 1
			timeout=$((timeout-1))
		])
	done

	AS_IF([test "$SPL_SYMBOLS" = NONE], [
		SPL_SYMBOLS=$LINUX_SYMBOLS
	])

	AC_MSG_RESULT([$SPL_SYMBOLS])
	AC_SUBST(SPL_SYMBOLS)
])

dnl #
dnl # Basic toolchain sanity check.
dnl #
AC_DEFUN([ZFS_AC_TEST_MODULE], [
	AC_MSG_CHECKING([whether modules can be built])
	ZFS_LINUX_TRY_COMPILE([],[],[
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
])

dnl #
dnl # Certain kernel build options are not supported.  These must be
dnl # detected at configure time and cause a build failure.  Otherwise
dnl # modules may be successfully built that behave incorrectly.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG], [
	AS_IF([test "x$cross_compiling" != xyes], [
		AC_RUN_IFELSE([
			AC_LANG_PROGRAM([
				#include "$LINUX/include/linux/license.h"
			], [
				return !license_is_gpl_compatible("$ZFS_META_LICENSE");
			])
		], [
			AC_DEFINE([ZFS_IS_GPL_COMPATIBLE], [1],
			    [Define to 1 if GPL-only symbols can be used])
		], [
		])
	])

	ZFS_AC_KERNEL_CONFIG_THREAD_SIZE
	ZFS_AC_KERNEL_CONFIG_DEBUG_LOCK_ALLOC
])

dnl #
dnl # Check configured THREAD_SIZE
dnl #
dnl # The stack size will vary by architecture, but as of Linux 3.15 on x86_64
dnl # the default thread stack size was increased to 16K from 8K.  Therefore,
dnl # on newer kernels and some architectures stack usage optimizations can be
dnl # conditionally applied to improve performance without negatively impacting
dnl # stability.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG_THREAD_SIZE], [
	AC_MSG_CHECKING([whether kernel was built with 16K or larger stacks])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/module.h>
	],[
		#if (THREAD_SIZE < 16384)
		#error "THREAD_SIZE is less than 16K"
		#endif
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_LARGE_STACKS, 1, [kernel has large stacks])
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # Check CONFIG_DEBUG_LOCK_ALLOC
dnl #
dnl # This is typically only set for debug kernels because it comes with
dnl # a performance penalty.  However, when it is set it maps the non-GPL
dnl # symbol mutex_lock() to the GPL-only mutex_lock_nested() symbol.
dnl # This will cause a failure at link time which we'd rather know about
dnl # at compile time.
dnl #
dnl # Since we plan to pursue making mutex_lock_nested() a non-GPL symbol
dnl # with the upstream community we add a check to detect this case.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG_DEBUG_LOCK_ALLOC], [

	ZFS_LINUX_CONFIG([DEBUG_LOCK_ALLOC], [
		AC_MSG_CHECKING([whether mutex_lock() is GPL-only])
		tmp_flags="$EXTRA_KCFLAGS"
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/module.h>
			#include <linux/mutex.h>

			MODULE_LICENSE("$ZFS_META_LICENSE");
		],[
			struct mutex lock;

			mutex_init(&lock);
			mutex_lock(&lock);
			mutex_unlock(&lock);
		],[
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_MSG_ERROR([
	*** Kernel built with CONFIG_DEBUG_LOCK_ALLOC which is incompatible
	*** with the CDDL license and will prevent the module linking stage
	*** from succeeding.  You must rebuild your kernel without this
	*** option enabled.])
		])
		EXTRA_KCFLAGS="$tmp_flags"
	], [])
])

dnl #
dnl # ZFS_LINUX_CONFTEST_H
dnl #
AC_DEFUN([ZFS_LINUX_CONFTEST_H], [
cat - <<_ACEOF >conftest.h
$1
_ACEOF
])

dnl #
dnl # ZFS_LINUX_CONFTEST_C
dnl #
AC_DEFUN([ZFS_LINUX_CONFTEST_C], [
cat confdefs.h - <<_ACEOF >conftest.c
$1
_ACEOF
])

dnl #
dnl # ZFS_LANG_PROGRAM(C)([PROLOGUE], [BODY])
dnl #
m4_define([ZFS_LANG_PROGRAM], [
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
dnl # ZFS_LINUX_COMPILE_IFELSE / like AC_COMPILE_IFELSE
dnl #
AC_DEFUN([ZFS_LINUX_COMPILE_IFELSE], [
	m4_ifvaln([$1], [ZFS_LINUX_CONFTEST_C([$1])])
	m4_ifvaln([$6], [ZFS_LINUX_CONFTEST_H([$6])], [ZFS_LINUX_CONFTEST_H([])])
	rm -Rf build && mkdir -p build && touch build/conftest.mod.c
	echo "obj-m := conftest.o" >build/Makefile
	modpost_flag=''
	test "x$enable_linux_builtin" = xyes && modpost_flag='modpost=true' # fake modpost stage
	AS_IF(
		[AC_TRY_COMMAND(cp conftest.c conftest.h build && make [$2] -C $LINUX_OBJ EXTRA_CFLAGS="-Werror $EXTRA_KCFLAGS" $ARCH_UM M=$PWD/build $modpost_flag) >/dev/null && AC_TRY_COMMAND([$3])],
		[$4],
		[_AC_MSG_LOG_CONFTEST m4_ifvaln([$5],[$5])]
	)
	rm -Rf build
])

dnl #
dnl # ZFS_LINUX_TRY_COMPILE like AC_TRY_COMPILE
dnl #
AC_DEFUN([ZFS_LINUX_TRY_COMPILE],
	[ZFS_LINUX_COMPILE_IFELSE(
	[AC_LANG_SOURCE([ZFS_LANG_PROGRAM([[$1]], [[$2]])])],
	[modules],
	[test -s build/conftest.o],
	[$3], [$4])
])

dnl #
dnl # ZFS_LINUX_CONFIG
dnl #
AC_DEFUN([ZFS_LINUX_CONFIG],
	[AC_MSG_CHECKING([whether kernel was built with CONFIG_$1])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/module.h>
	],[
		#ifndef CONFIG_$1
		#error CONFIG_$1 not #defined
		#endif
	],[
		AC_MSG_RESULT([yes])
		$2
	],[
		AC_MSG_RESULT([no])
		$3
	])
])

dnl #
dnl # ZFS_CHECK_SYMBOL_EXPORT
dnl # check symbol exported or not
dnl #
AC_DEFUN([ZFS_CHECK_SYMBOL_EXPORT], [
	grep -q -E '[[[:space:]]]$1[[[:space:]]]' \
		$LINUX_OBJ/$LINUX_SYMBOLS 2>/dev/null
	rc=$?
	if test $rc -ne 0; then
		export=0
		for file in $2; do
			grep -q -E "EXPORT_SYMBOL.*($1)" \
				"$LINUX/$file" 2>/dev/null
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
dnl # ZFS_LINUX_TRY_COMPILE_SYMBOL
dnl # like ZFS_LINUX_TRY_COMPILE, except ZFS_CHECK_SYMBOL_EXPORT
dnl # is called if not compiling for builtin
dnl #
AC_DEFUN([ZFS_LINUX_TRY_COMPILE_SYMBOL], [
	ZFS_LINUX_TRY_COMPILE([$1], [$2], [rc=0], [rc=1])
	if test $rc -ne 0; then :
		$6
	else
		if test "x$enable_linux_builtin" != xyes; then
			ZFS_CHECK_SYMBOL_EXPORT([$3], [$4], [rc=0], [rc=1])
		fi
		if test $rc -ne 0; then :
			$6
		else :
			$5
		fi
	fi
])

dnl #
dnl # ZFS_LINUX_TRY_COMPILE_HEADER
dnl # like ZFS_LINUX_TRY_COMPILE, except the contents conftest.h are
dnl # provided via the fifth parameter
dnl #
AC_DEFUN([ZFS_LINUX_TRY_COMPILE_HEADER],
	[ZFS_LINUX_COMPILE_IFELSE(
	[AC_LANG_SOURCE([ZFS_LANG_PROGRAM([[$1]], [[$2]])])],
	[modules],
	[test -s build/conftest.o],
	[$3], [$4], [$5])
])
