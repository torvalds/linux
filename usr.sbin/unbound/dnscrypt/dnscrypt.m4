# dnscrypt.m4

# dnsc_DNSCRYPT([action-if-true], [action-if-false])
# --------------------------------------------------------------------------
# Check for required dnscrypt libraries and add dnscrypt configure args.
AC_DEFUN([dnsc_DNSCRYPT],
[
  AC_ARG_ENABLE([dnscrypt],
    AS_HELP_STRING([--enable-dnscrypt],
                   [Enable dnscrypt support (requires libsodium)]),
    [opt_dnscrypt=$enableval], [opt_dnscrypt=no])

  if test "x$opt_dnscrypt" != "xno"; then
    AC_ARG_WITH([libsodium], AS_HELP_STRING([--with-libsodium=path],
    	[Path where libsodium is installed, for dnscrypt]), [
	CFLAGS="$CFLAGS -I$withval/include"
	LDFLAGS="$LDFLAGS -L$withval/lib"
    ])
    AC_SEARCH_LIBS([sodium_init], [sodium], [],
      AC_MSG_ERROR([The sodium library was not found. Please install sodium!]))
    AC_SEARCH_LIBS([crypto_box_curve25519xchacha20poly1305_beforenm], [sodium],
        [
            AC_SUBST([ENABLE_DNSCRYPT_XCHACHA20], [1])
            AC_DEFINE(
                [USE_DNSCRYPT_XCHACHA20], [1],
                [Define to 1 to enable dnscrypt with xchacha20 support])
        ],
        [
            AC_SUBST([ENABLE_DNSCRYPT_XCHACHA20], [0])
        ])
    AC_SEARCH_LIBS([sodium_set_misuse_handler], [sodium],
        [
            AC_DEFINE(
                [SODIUM_MISUSE_HANDLER], [1],
                [Define to 1 if libsodium supports sodium_set_misuse_handler])
        ],
        [
        ])
    $1
  else
    AC_SUBST([ENABLE_DNSCRYPT_XCHACHA20], [0])
    $2
  fi
])
