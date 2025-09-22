# intl sister-directory configuration rules.
#

# The idea behind this macro is that there's no need to repeat all the
# autoconf probes done by the intl directory - it's already done them
# for us.  In fact, there's no need even to look at the cache for the
# answers.  All we need to do is nab a few pieces of information.
# The intl directory is set up to make this easy, by generating a
# small file which can be sourced as a shell script; then we produce
# the necessary substitutions and definitions for this directory.

AC_DEFUN([ZW_GNU_GETTEXT_SISTER_DIR],
[# If we haven't got the data from the intl directory,
# assume NLS is disabled.
USE_NLS=no	AC_SUBST(USE_NLS)
LIBINTL=	AC_SUBST(LIBINTL)
LIBINTL_DEP=	AC_SUBST(LIBINTL_DEP)
INCINTL=	AC_SUBST(INCINTL)
XGETTEXT=	AC_SUBST(XGETTEXT)
GMSGFMT=	AC_SUBST(GMSGFMT)
POSUB=		AC_SUBST(POSUB)

if test -f  ifelse([$1],,[../intl],[$1])/config.intl; then
  .  ifelse([$1],,[../intl],[$1])/config.intl
fi
AC_MSG_CHECKING([whether NLS is requested])
if test x"$USE_NLS" != xyes; then
  AC_MSG_RESULT(no)
else
  AC_MSG_RESULT(yes)
  AC_DEFINE(ENABLE_NLS, 1, 
 [Define to 1 if translation of program messages to the 
  user's native language is requested.])

  AC_MSG_CHECKING(for catalogs to be installed)
  # Look for .po and .gmo files in the source directory.
  CATALOGS=  AC_SUBST(CATALOGS)
  XLINGUAS=
  for cat in $srcdir/po/*.gmo $srcdir/po/*.po; do
    # If there aren't any .gmo files the shell will give us the
    # literal string "../path/to/srcdir/po/*.gmo" which has to be
    # weeded out.
    case "$cat" in *\**)
      continue;;
    esac
    # The quadruple backslash is collapsed to a double backslash
    # by the backticks, then collapsed again by the double quotes,
    # leaving us with one backslash in the sed expression (right
    # before the dot that mustn't act as a wildcard).
    cat=`echo $cat | sed -e "s!$srcdir/po/!!" -e "s!\\\\.po!.gmo!"`
    lang=`echo $cat | sed -e "s!\\\\.gmo!!"`
    # The user is allowed to set LINGUAS to a list of languages to
    # install catalogs for.  If it's empty that means "all of them."
    if test "x$LINGUAS" = x; then
      CATALOGS="$CATALOGS $cat"
      XLINGUAS="$XLINGUAS $lang"
    else
      case "$LINGUAS" in *$lang*)
        CATALOGS="$CATALOGS $cat"
        XLINGUAS="$XLINGUAS $lang"
        ;;
      esac
    fi
  done
  LINGUAS="$XLINGUAS"
  AC_MSG_RESULT($LINGUAS)

  dnl Set up some additional variables which our po/Make-in files
  dnl may need.

  dnl For backward compatibility. Some Makefiles may be using these.
  DATADIRNAME=share
  AC_SUBST(DATADIRNAME)
  INSTOBJEXT=.mo
  AC_SUBST(INSTOBJEXT)
  GENCAT=gencat
  AC_SUBST(GENCAT)
  CATOBJEXT=.gmo
  AC_SUBST(CATOBJEXT)
fi])
