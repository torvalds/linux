dnl #
dnl # Checks if host toolchain supports SIMD instructions
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_TOOLCHAIN_SIMD], [
	case "$host_cpu" in
		x86_64 | x86 | i686)
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE2
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE3
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSSE3
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_1
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_2
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX2
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512F
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512CD
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512DQ
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512BW
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512IFMA
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512VBMI
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512PF
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512ER
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512VL
			;;
	esac
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE], [
	AC_MSG_CHECKING([whether host toolchain supports SSE])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			__asm__ __volatile__("xorps %xmm0, %xmm1");
		}
	]])], [
		AC_DEFINE([HAVE_SSE], 1, [Define if host toolchain supports SSE])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE2
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE2], [
	AC_MSG_CHECKING([whether host toolchain supports SSE2])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			__asm__ __volatile__("pxor %xmm0, %xmm1");
		}
	]])], [
		AC_DEFINE([HAVE_SSE2], 1, [Define if host toolchain supports SSE2])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE3
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE3], [
	AC_MSG_CHECKING([whether host toolchain supports SSE3])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			char v[16];
			__asm__ __volatile__("lddqu %0,%%xmm0" :: "m"(v[0]));
		}
	]])], [
		AC_DEFINE([HAVE_SSE3], 1, [Define if host toolchain supports SSE3])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSSE3
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSSE3], [
	AC_MSG_CHECKING([whether host toolchain supports SSSE3])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			__asm__ __volatile__("pshufb %xmm0,%xmm1");
		}
	]])], [
		AC_DEFINE([HAVE_SSSE3], 1, [Define if host toolchain supports SSSE3])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_1
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_1], [
	AC_MSG_CHECKING([whether host toolchain supports SSE4.1])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			__asm__ __volatile__("pmaxsb %xmm0,%xmm1");
		}
	]])], [
		AC_DEFINE([HAVE_SSE4_1], 1, [Define if host toolchain supports SSE4.1])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_2
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_2], [
	AC_MSG_CHECKING([whether host toolchain supports SSE4.2])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			__asm__ __volatile__("pcmpgtq %xmm0, %xmm1");
		}
	]])], [
		AC_DEFINE([HAVE_SSE4_2], 1, [Define if host toolchain supports SSE4.2])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX], [
	AC_MSG_CHECKING([whether host toolchain supports AVX])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			char v[32];
			__asm__ __volatile__("vmovdqa %0,%%ymm0" :: "m"(v[0]));
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX], 1, [Define if host toolchain supports AVX])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX2
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX2], [
	AC_MSG_CHECKING([whether host toolchain supports AVX2])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vpshufb %ymm0,%ymm1,%ymm2");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX2], 1, [Define if host toolchain supports AVX2])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512F
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512F], [
	AC_MSG_CHECKING([whether host toolchain supports AVX512F])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vpandd %zmm0,%zmm1,%zmm2");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX512F], 1, [Define if host toolchain supports AVX512F])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512CD
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512CD], [
	AC_MSG_CHECKING([whether host toolchain supports AVX512CD])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vplzcntd %zmm0,%zmm1");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX512CD], 1, [Define if host toolchain supports AVX512CD])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512DQ
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512DQ], [
	AC_MSG_CHECKING([whether host toolchain supports AVX512DQ])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vandpd %zmm0,%zmm1,%zmm2");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX512DQ], 1, [Define if host toolchain supports AVX512DQ])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512BW
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512BW], [
	AC_MSG_CHECKING([whether host toolchain supports AVX512BW])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vpshufb %zmm0,%zmm1,%zmm2");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX512BW], 1, [Define if host toolchain supports AVX512BW])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512IFMA
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512IFMA], [
	AC_MSG_CHECKING([whether host toolchain supports AVX512IFMA])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vpmadd52luq %zmm0,%zmm1,%zmm2");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX512IFMA], 1, [Define if host toolchain supports AVX512IFMA])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512VBMI
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512VBMI], [
	AC_MSG_CHECKING([whether host toolchain supports AVX512VBMI])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vpermb %zmm0,%zmm1,%zmm2");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX512VBMI], 1, [Define if host toolchain supports AVX512VBMI])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512PF
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512PF], [
	AC_MSG_CHECKING([whether host toolchain supports AVX512PF])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vgatherpf0dps (%rsi,%zmm0,4){%k1}");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX512PF], 1, [Define if host toolchain supports AVX512PF])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512ER
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512ER], [
	AC_MSG_CHECKING([whether host toolchain supports AVX512ER])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vexp2pd %zmm0,%zmm1");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX512ER], 1, [Define if host toolchain supports AVX512ER])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512VL
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX512VL], [
	AC_MSG_CHECKING([whether host toolchain supports AVX512VL])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vpabsq %zmm0,%zmm1");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX512VL], 1, [Define if host toolchain supports AVX512VL])
	], [
		AC_MSG_RESULT([no])
	])
])
