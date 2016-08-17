dnl #
dnl # Set the target arch for libspl atomic implementation
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_ARCH], [
	AC_MSG_CHECKING(for target asm dir)
	TARGET_ARCH=`echo ${target_cpu} | sed -e s/i.86/i386/`

	case $TARGET_ARCH in
	i386|x86_64)
		TARGET_ASM_DIR=asm-${TARGET_ARCH}
		;;
	*)
		TARGET_ASM_DIR=asm-generic
		;;
	esac

	AC_SUBST([TARGET_ASM_DIR])
	AC_MSG_RESULT([$TARGET_ASM_DIR])
])
