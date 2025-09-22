dnl Usage: TL_AC_GXX_INCLUDE_DIR
dnl
dnl Set $gxx_include_dir to the location of the installed C++ include
dnl directory.  The value depends on $gcc_version and the configuration
dnl options --with-gxx-include-dir and --enable-version-specific-runtime-libs.
dnl
dnl If you change the default here, you'll need to change the gcc and
dnl libstdc++-v3 subdirectories too.
AC_DEFUN([TL_AC_GXX_INCLUDE_DIR],
[
case "${with_gxx_include_dir}" in
  yes)
    AC_MSG_ERROR([--with-gxx-include-dir=[[dir]] requires a directory])
    ;;
  no | "")
    case "${enable_version_specific_runtime_libs}" in
      yes) gxx_include_dir='$(libsubdir)/include/c++' ;;
      *)
	libstdcxx_incdir='c++/$(gcc_version)'
	gxx_include_dir='include/$(libstdcxx_incdir)'
	if test -n "$with_cross_host" && 
           test x"$with_cross_host" != x"no"; then	
          gxx_include_dir='${prefix}/${target_alias}/'"$gxx_include_dir"
        else
          gxx_include_dir='${prefix}/'"$gxx_include_dir"
        fi;;
    esac ;;
  *) gxx_include_dir=${with_gxx_include_dir} ;;
esac
AC_SUBST(gxx_include_dir)
AC_SUBST(libstdcxx_incdir)
])
