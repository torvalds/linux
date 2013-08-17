
# libtool (GNU libtool) 2.4.2
# Written by Gordon Matzigkeit <gord@gnu.ai.mit.edu>, 1996

# Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2003, 2004, 2005, 2006,
# 2007, 2008, 2009, 2010, 2011 Free Software Foundation, Inc.
# This is free software; see the source for copying conditions.  There is NO
# warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# GNU Libtool is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# As a special exception to the GNU General Public License,
# if you distribute this file as part of a program or library that
# is built using GNU Libtool, you may include this file under the
# same distribution terms that you use for the rest of that program.
#
# GNU Libtool is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU Libtool; see the file COPYING.  If not, a copy
# can be downloaded from http://www.gnu.org/licenses/gpl.html,
# or obtained by writing to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

# Usage: $progname [OPTION]... [MODE-ARG]...
#
# Provide generalized library-building support services.
#
#       --config             show all configuration variables
#       --debug              enable verbose shell tracing
#   -n, --dry-run            display commands without modifying any files
#       --features           display basic configuration information and exit
#       --mode=MODE          use operation mode MODE
#       --preserve-dup-deps  don't remove duplicate dependency libraries
#       --quiet, --silent    don't print informational messages
#       --no-quiet, --no-silent
#                            print informational messages (default)
#       --no-warn            don't display warning messages
#       --tag=TAG            use configuration variables from tag TAG
#   -v, --verbose            print more informational messages than default
#       --no-verbose         don't print the extra informational messages
#       --version            print version information
#   -h, --help, --help-all   print short, long, or detailed help message
#
# MODE must be one of the following:
#
#         clean              remove files from the build directory
#         compile            compile a source file into a libtool object
#         execute            automatically set library path, then run a program
#         finish             complete the installation of libtool libraries
#         install            install libraries or executables
#         link               create a library or an executable
#         uninstall          remove libraries from an installed directory
#
# MODE-ARGS vary depending on the MODE.  When passed as first option,
# `--mode=MODE' may be abbreviated as `MODE' or a unique abbreviation of that.
# Try `$progname --help --mode=MODE' for a more detailed description of MODE.
#
# When reporting a bug, please describe a test case to reproduce it and
# include the following information:
#
#         host-triplet:	$host
#         shell:		$SHELL
#         compiler:		$LTCC
#         compiler flags:		$LTCFLAGS
#         linker:		$LD (gnu? $with_gnu_ld)
#         $progname:	(GNU libtool) 2.4.2 Debian-2.4.2-1.2ubuntu1
#         automake:	$automake_version
#         autoconf:	$autoconf_version
#
# Report bugs to <bug-libtool@gnu.org>.
# GNU libtool home page: <http://www.gnu.org/software/libtool/>.
# General help using GNU software: <http://www.gnu.org/gethelp/>.

PROGRAM=libtool
PACKAGE=libtool
VERSION="2.4.2 Debian-2.4.2-1.2ubuntu1"
TIMESTAMP=""
package_revision=1.3337

# Be Bourne compatible
if test -n "${ZSH_VERSION+set}" && (emulate sh) >/dev/null 2>&1; then
  emulate sh
  NULLCMD=:
  # Zsh 3.x and 4.x performs word splitting on ${1+"$@"}, which
  # is contrary to our usage.  Disable this feature.
  alias -g '${1+"$@"}'='"$@"'
  setopt NO_GLOB_SUBST
else
  case `(set -o) 2>/dev/null` in *posix*) set -o posix;; esac
fi
BIN_SH=xpg4; export BIN_SH # for Tru64
DUALCASE=1; export DUALCASE # for MKS sh

# A function that is used when there is no print builtin or printf.
func_fallback_echo ()
{
  eval 'cat <<_LTECHO_EOF
$1
_LTECHO_EOF'
}

# NLS nuisances: We save the old values to restore during execute mode.
lt_user_locale=
lt_safe_locale=
for lt_var in LANG LANGUAGE LC_ALL LC_CTYPE LC_COLLATE LC_MESSAGES
do
  eval "if test \"\${$lt_var+set}\" = set; then
          save_$lt_var=\$$lt_var
          $lt_var=C
	  export $lt_var
	  lt_user_locale=\"$lt_var=\\\$save_\$lt_var; \$lt_user_locale\"
	  lt_safe_locale=\"$lt_var=C; \$lt_safe_locale\"
	fi"
done
LC_ALL=C
LANGUAGE=C
export LANGUAGE LC_ALL

$lt_unset CDPATH


# Work around backward compatibility issue on IRIX 6.5. On IRIX 6.4+, sh
# is ksh but when the shell is invoked as "sh" and the current value of
# the _XPG environment variable is not equal to 1 (one), the special
# positional parameter $0, within a function call, is the name of the
# function.
progpath="$0"



: ${CP="cp -f"}
test "${ECHO+set}" = set || ECHO=${as_echo-'printf %s\n'}
: ${MAKE="make"}
: ${MKDIR="mkdir"}
: ${MV="mv -f"}
: ${RM="rm -f"}
: ${SHELL="${CONFIG_SHELL-/bin/sh}"}
: ${Xsed="$SED -e 1s/^X//"}

# Global variables:
EXIT_SUCCESS=0
EXIT_FAILURE=1
EXIT_MISMATCH=63  # $? = 63 is used to indicate version mismatch to missing.
EXIT_SKIP=77	  # $? = 77 is used to indicate a skipped test to automake.

exit_status=$EXIT_SUCCESS

# Make sure IFS has a sensible default
lt_nl='
'
IFS=" 	$lt_nl"

dirname="s,/[^/]*$,,"
basename="s,^.*/,,"

# func_dirname file append nondir_replacement
# Compute the dirname of FILE.  If nonempty, add APPEND to the result,
# otherwise set result to NONDIR_REPLACEMENT.
func_dirname ()
{
    func_dirname_result=`$ECHO "${1}" | $SED "$dirname"`
    if test "X$func_dirname_result" = "X${1}"; then
      func_dirname_result="${3}"
    else
      func_dirname_result="$func_dirname_result${2}"
    fi
} # func_dirname may be replaced by extended shell implementation


# func_basename file
func_basename ()
{
    func_basename_result=`$ECHO "${1}" | $SED "$basename"`
} # func_basename may be replaced by extended shell implementation


# func_dirname_and_basename file append nondir_replacement
# perform func_basename and func_dirname in a single function
# call:
#   dirname:  Compute the dirname of FILE.  If nonempty,
#             add APPEND to the result, otherwise set result
#             to NONDIR_REPLACEMENT.
#             value returned in "$func_dirname_result"
#   basename: Compute filename of FILE.
#             value retuned in "$func_basename_result"
# Implementation must be kept synchronized with func_dirname
# and func_basename. For efficiency, we do not delegate to
# those functions but instead duplicate the functionality here.
func_dirname_and_basename ()
{
    # Extract subdirectory from the argument.
    func_dirname_result=`$ECHO "${1}" | $SED -e "$dirname"`
    if test "X$func_dirname_result" = "X${1}"; then
      func_dirname_result="${3}"
    else
      func_dirname_result="$func_dirname_result${2}"
    fi
    func_basename_result=`$ECHO "${1}" | $SED -e "$basename"`
} # func_dirname_and_basename may be replaced by extended shell implementation


# func_stripname prefix suffix name
# strip PREFIX and SUFFIX off of NAME.
# PREFIX and SUFFIX must not contain globbing or regex special
# characters, hashes, percent signs, but SUFFIX may contain a leading
# dot (in which case that matches only a dot).
# func_strip_suffix prefix name
func_stripname ()
{
    case ${2} in
      .*) func_stripname_result=`$ECHO "${3}" | $SED "s%^${1}%%; s%\\\\${2}\$%%"`;;
      *)  func_stripname_result=`$ECHO "${3}" | $SED "s%^${1}%%; s%${2}\$%%"`;;
    esac
} # func_stripname may be replaced by extended shell implementation


# These SED scripts presuppose an absolute path with a trailing slash.
pathcar='s,^/\([^/]*\).*$,\1,'
pathcdr='s,^/[^/]*,,'
removedotparts=':dotsl
		s@/\./@/@g
		t dotsl
		s,/\.$,/,'
collapseslashes='s@/\{1,\}@/@g'
finalslash='s,/*$,/,'

# func_normal_abspath PATH
# Remove doubled-up and trailing slashes, "." path components,
# and cancel out any ".." path components in PATH after making
# it an absolute path.
#             value returned in "$func_normal_abspath_result"
func_normal_abspath ()
{
  # Start from root dir and reassemble the path.
  func_normal_abspath_result=
  func_normal_abspath_tpath=$1
  func_normal_abspath_altnamespace=
  case $func_normal_abspath_tpath in
    "")
      # Empty path, that just means $cwd.
      func_stripname '' '/' "`pwd`"
      func_normal_abspath_result=$func_stripname_result
      return
    ;;
    # The next three entries are used to spot a run of precisely
    # two leading slashes without using negated character classes;
    # we take advantage of case's first-match behaviour.
    ///*)
      # Unusual form of absolute path, do nothing.
    ;;
    //*)
      # Not necessarily an ordinary path; POSIX reserves leading '//'
      # and for example Cygwin uses it to access remote file shares
      # over CIFS/SMB, so we conserve a leading double slash if found.
      func_normal_abspath_altnamespace=/
    ;;
    /*)
      # Absolute path, do nothing.
    ;;
    *)
      # Relative path, prepend $cwd.
      func_normal_abspath_tpath=`pwd`/$func_normal_abspath_tpath
    ;;
  esac
  # Cancel out all the simple stuff to save iterations.  We also want
  # the path to end with a slash for ease of parsing, so make sure
  # there is one (and only one) here.
  func_normal_abspath_tpath=`$ECHO "$func_normal_abspath_tpath" | $SED \
        -e "$removedotparts" -e "$collapseslashes" -e "$finalslash"`
  while :; do
    # Processed it all yet?
    if test "$func_normal_abspath_tpath" = / ; then
      # If we ascended to the root using ".." the result may be empty now.
      if test -z "$func_normal_abspath_result" ; then
        func_normal_abspath_result=/
      fi
      break
    fi
    func_normal_abspath_tcomponent=`$ECHO "$func_normal_abspath_tpath" | $SED \
        -e "$pathcar"`
    func_normal_abspath_tpath=`$ECHO "$func_normal_abspath_tpath" | $SED \
        -e "$pathcdr"`
    # Figure out what to do with it
    case $func_normal_abspath_tcomponent in
      "")
        # Trailing empty path component, ignore it.
      ;;
      ..)
        # Parent dir; strip last assembled component from result.
        func_dirname "$func_normal_abspath_result"
        func_normal_abspath_result=$func_dirname_result
      ;;
      *)
        # Actual path component, append it.
        func_normal_abspath_result=$func_normal_abspath_result/$func_normal_abspath_tcomponent
      ;;
    esac
  done
  # Restore leading double-slash if one was found on entry.
  func_normal_abspath_result=$func_normal_abspath_altnamespace$func_normal_abspath_result
}

# func_relative_path SRCDIR DSTDIR
# generates a relative path from SRCDIR to DSTDIR, with a trailing
# slash if non-empty, suitable for immediately appending a filename
# without needing to append a separator.
#             value returned in "$func_relative_path_result"
func_relative_path ()
{
  func_relative_path_result=
  func_normal_abspath "$1"
  func_relative_path_tlibdir=$func_normal_abspath_result
  func_normal_abspath "$2"
  func_relative_path_tbindir=$func_normal_abspath_result

  # Ascend the tree starting from libdir
  while :; do
    # check if we have found a prefix of bindir
    case $func_relative_path_tbindir in
      $func_relative_path_tlibdir)
        # found an exact match
        func_relative_path_tcancelled=
        break
        ;;
      $func_relative_path_tlibdir*)
        # found a matching prefix
        func_stripname "$func_relative_path_tlibdir" '' "$func_relative_path_tbindir"
        func_relative_path_tcancelled=$func_stripname_result
        if test -z "$func_relative_path_result"; then
          func_relative_path_result=.
        fi
        break
        ;;
      *)
        func_dirname $func_relative_path_tlibdir
        func_relative_path_tlibdir=${func_dirname_result}
        if test "x$func_relative_path_tlibdir" = x ; then
          # Have to descend all the way to the root!
          func_relative_path_result=../$func_relative_path_result
          func_relative_path_tcancelled=$func_relative_path_tbindir
          break
        fi
        func_relative_path_result=../$func_relative_path_result
        ;;
    esac
  done

  # Now calculate path; take care to avoid doubling-up slashes.
  func_stripname '' '/' "$func_relative_path_result"
  func_relative_path_result=$func_stripname_result
  func_stripname '/' '/' "$func_relative_path_tcancelled"
  if test "x$func_stripname_result" != x ; then
    func_relative_path_result=${func_relative_path_result}/${func_stripname_result}
  fi

  # Normalisation. If bindir is libdir, return empty string,
  # else relative path ending with a slash; either way, target
  # file name can be directly appended.
  if test ! -z "$func_relative_path_result"; then
    func_stripname './' '' "$func_relative_path_result/"
    func_relative_path_result=$func_stripname_result
  fi
}

# The name of this program:
func_dirname_and_basename "$progpath"
progname=$func_basename_result

# Make sure we have an absolute path for reexecution:
case $progpath in
  [\\/]*|[A-Za-z]:\\*) ;;
  *[\\/]*)
     progdir=$func_dirname_result
     progdir=`cd "$progdir" && pwd`
     progpath="$progdir/$progname"
     ;;
  *)
     save_IFS="$IFS"
     IFS=${PATH_SEPARATOR-:}
     for progdir in $PATH; do
       IFS="$save_IFS"
       test -x "$progdir/$progname" && break
     done
     IFS="$save_IFS"
     test -n "$progdir" || progdir=`pwd`
     progpath="$progdir/$progname"
     ;;
esac

# Sed substitution that helps us do robust quoting.  It backslashifies
# metacharacters that are still active within double-quoted strings.
Xsed="${SED}"' -e 1s/^X//'
sed_quote_subst='s/\([`"$\\]\)/\\\1/g'

# Same as above, but do not quote variable references.
double_quote_subst='s/\(["`\\]\)/\\\1/g'

# Sed substitution that turns a string into a regex matching for the
# string literally.
sed_make_literal_regex='s,[].[^$\\*\/],\\&,g'

# Sed substitution that converts a w32 file name or path
# which contains forward slashes, into one that contains
# (escaped) backslashes.  A very naive implementation.
lt_sed_naive_backslashify='s|\\\\*|\\|g;s|/|\\|g;s|\\|\\\\|g'

# Re-`\' parameter expansions in output of double_quote_subst that were
# `\'-ed in input to the same.  If an odd number of `\' preceded a '$'
# in input to double_quote_subst, that '$' was protected from expansion.
# Since each input `\' is now two `\'s, look for any number of runs of
# four `\'s followed by two `\'s and then a '$'.  `\' that '$'.
bs='\\'
bs2='\\\\'
bs4='\\\\\\\\'
dollar='\$'
sed_double_backslash="\
  s/$bs4/&\\
/g
  s/^$bs2$dollar/$bs&/
  s/\\([^$bs]\\)$bs2$dollar/\\1$bs2$bs$dollar/g
  s/\n//g"

# Standard options:
opt_dry_run=false
opt_help=false
opt_quiet=false
opt_verbose=false
opt_warning=:

# func_echo arg...
# Echo program name prefixed message, along with the current mode
# name if it has been set yet.
func_echo ()
{
    $ECHO "$progname: ${opt_mode+$opt_mode: }$*"
}

# func_verbose arg...
# Echo program name prefixed message in verbose mode only.
func_verbose ()
{
    $opt_verbose && func_echo ${1+"$@"}

    # A bug in bash halts the script if the last line of a function
    # fails when set -e is in force, so we need another command to
    # work around that:
    :
}

# func_echo_all arg...
# Invoke $ECHO with all args, space-separated.
func_echo_all ()
{
    $ECHO "$*"
}

# func_error arg...
# Echo program name prefixed message to standard error.
func_error ()
{
    $ECHO "$progname: ${opt_mode+$opt_mode: }"${1+"$@"} 1>&2
}

# func_warning arg...
# Echo program name prefixed warning message to standard error.
func_warning ()
{
    $opt_warning && $ECHO "$progname: ${opt_mode+$opt_mode: }warning: "${1+"$@"} 1>&2

    # bash bug again:
    :
}

# func_fatal_error arg...
# Echo program name prefixed message to standard error, and exit.
func_fatal_error ()
{
    func_error ${1+"$@"}
    exit $EXIT_FAILURE
}

# func_fatal_help arg...
# Echo program name prefixed message to standard error, followed by
# a help hint, and exit.
func_fatal_help ()
{
    func_error ${1+"$@"}
    func_fatal_error "$help"
}
help="Try \`$progname --help' for more information."  ## default


# func_grep expression filename
# Check whether EXPRESSION matches any line of FILENAME, without output.
func_grep ()
{
    $GREP "$1" "$2" >/dev/null 2>&1
}


# func_mkdir_p directory-path
# Make sure the entire path to DIRECTORY-PATH is available.
func_mkdir_p ()
{
    my_directory_path="$1"
    my_dir_list=

    if test -n "$my_directory_path" && test "$opt_dry_run" != ":"; then

      # Protect directory names starting with `-'
      case $my_directory_path in
        -*) my_directory_path="./$my_directory_path" ;;
      esac

      # While some portion of DIR does not yet exist...
      while test ! -d "$my_directory_path"; do
        # ...make a list in topmost first order.  Use a colon delimited
	# list incase some portion of path contains whitespace.
        my_dir_list="$my_directory_path:$my_dir_list"

        # If the last portion added has no slash in it, the list is done
        case $my_directory_path in */*) ;; *) break ;; esac

        # ...otherwise throw away the child directory and loop
        my_directory_path=`$ECHO "$my_directory_path" | $SED -e "$dirname"`
      done
      my_dir_list=`$ECHO "$my_dir_list" | $SED 's,:*$,,'`

      save_mkdir_p_IFS="$IFS"; IFS=':'
      for my_dir in $my_dir_list; do
	IFS="$save_mkdir_p_IFS"
        # mkdir can fail with a `File exist' error if two processes
        # try to create one of the directories concurrently.  Don't
        # stop in that case!
        $MKDIR "$my_dir" 2>/dev/null || :
      done
      IFS="$save_mkdir_p_IFS"

      # Bail out if we (or some other process) failed to create a directory.
      test -d "$my_directory_path" || \
        func_fatal_error "Failed to create \`$1'"
    fi
}


# func_mktempdir [string]
# Make a temporary directory that won't clash with other running
# libtool processes, and avoids race conditions if possible.  If
# given, STRING is the basename for that directory.
func_mktempdir ()
{
    my_template="${TMPDIR-/tmp}/${1-$progname}"

    if test "$opt_dry_run" = ":"; then
      # Return a directory name, but don't create it in dry-run mode
      my_tmpdir="${my_template}-$$"
    else

      # If mktemp works, use that first and foremost
      my_tmpdir=`mktemp -d "${my_template}-XXXXXXXX" 2>/dev/null`

      if test ! -d "$my_tmpdir"; then
        # Failing that, at least try and use $RANDOM to avoid a race
        my_tmpdir="${my_template}-${RANDOM-0}$$"

        save_mktempdir_umask=`umask`
        umask 0077
        $MKDIR "$my_tmpdir"
        umask $save_mktempdir_umask
      fi

      # If we're not in dry-run mode, bomb out on failure
      test -d "$my_tmpdir" || \
        func_fatal_error "cannot create temporary directory \`$my_tmpdir'"
    fi

    $ECHO "$my_tmpdir"
}


# func_quote_for_eval arg
# Aesthetically quote ARG to be evaled later.
# This function returns two values: FUNC_QUOTE_FOR_EVAL_RESULT
# is double-quoted, suitable for a subsequent eval, whereas
# FUNC_QUOTE_FOR_EVAL_UNQUOTED_RESULT has merely all characters
# which are still active within double quotes backslashified.
func_quote_for_eval ()
{
    case $1 in
      *[\\\`\"\$]*)
	func_quote_for_eval_unquoted_result=`$ECHO "$1" | $SED "$sed_quote_subst"` ;;
      *)
        func_quote_for_eval_unquoted_result="$1" ;;
    esac

    case $func_quote_for_eval_unquoted_result in
      # Double-quote args containing shell metacharacters to delay
      # word splitting, command substitution and and variable
      # expansion for a subsequent eval.
      # Many Bourne shells cannot handle close brackets correctly
      # in scan sets, so we specify it separately.
      *[\[\~\#\^\&\*\(\)\{\}\|\;\<\>\?\'\ \	]*|*]*|"")
        func_quote_for_eval_result="\"$func_quote_for_eval_unquoted_result\""
        ;;
      *)
        func_quote_for_eval_result="$func_quote_for_eval_unquoted_result"
    esac
}


# func_quote_for_expand arg
# Aesthetically quote ARG to be evaled later; same as above,
# but do not quote variable references.
func_quote_for_expand ()
{
    case $1 in
      *[\\\`\"]*)
	my_arg=`$ECHO "$1" | $SED \
	    -e "$double_quote_subst" -e "$sed_double_backslash"` ;;
      *)
        my_arg="$1" ;;
    esac

    case $my_arg in
      # Double-quote args containing shell metacharacters to delay
      # word splitting and command substitution for a subsequent eval.
      # Many Bourne shells cannot handle close brackets correctly
      # in scan sets, so we specify it separately.
      *[\[\~\#\^\&\*\(\)\{\}\|\;\<\>\?\'\ \	]*|*]*|"")
        my_arg="\"$my_arg\""
        ;;
    esac

    func_quote_for_expand_result="$my_arg"
}


# func_show_eval cmd [fail_exp]
# Unless opt_silent is true, then output CMD.  Then, if opt_dryrun is
# not true, evaluate CMD.  If the evaluation of CMD fails, and FAIL_EXP
# is given, then evaluate it.
func_show_eval ()
{
    my_cmd="$1"
    my_fail_exp="${2-:}"

    ${opt_silent-false} || {
      func_quote_for_expand "$my_cmd"
      eval "func_echo $func_quote_for_expand_result"
    }

    if ${opt_dry_run-false}; then :; else
      eval "$my_cmd"
      my_status=$?
      if test "$my_status" -eq 0; then :; else
	eval "(exit $my_status); $my_fail_exp"
      fi
    fi
}


# func_show_eval_locale cmd [fail_exp]
# Unless opt_silent is true, then output CMD.  Then, if opt_dryrun is
# not true, evaluate CMD.  If the evaluation of CMD fails, and FAIL_EXP
# is given, then evaluate it.  Use the saved locale for evaluation.
func_show_eval_locale ()
{
    my_cmd="$1"
    my_fail_exp="${2-:}"

    ${opt_silent-false} || {
      func_quote_for_expand "$my_cmd"
      eval "func_echo $func_quote_for_expand_result"
    }

    if ${opt_dry_run-false}; then :; else
      eval "$lt_user_locale
	    $my_cmd"
      my_status=$?
      eval "$lt_safe_locale"
      if test "$my_status" -eq 0; then :; else
	eval "(exit $my_status); $my_fail_exp"
      fi
    fi
}

# func_tr_sh
# Turn $1 into a string suitable for a shell variable name.
# Result is stored in $func_tr_sh_result.  All characters
# not in the set a-zA-Z0-9_ are replaced with '_'. Further,
# if $1 begins with a digit, a '_' is prepended as well.
func_tr_sh ()
{
  case $1 in
  [0-9]* | *[!a-zA-Z0-9_]*)
    func_tr_sh_result=`$ECHO "$1" | $SED 's/^\([0-9]\)/_\1/; s/[^a-zA-Z0-9_]/_/g'`
    ;;
  * )
    func_tr_sh_result=$1
    ;;
  esac
}


# func_version
# Echo version message to standard output and exit.
func_version ()
{
    $opt_debug

    $SED -n '/(C)/!b go
	:more
	/\./!{
	  N
	  s/\n# / /
	  b more
	}
	:go
	/^# '$PROGRAM' (GNU /,/# warranty; / {
        s/^# //
	s/^# *$//
        s/\((C)\)[ 0-9,-]*\( [1-9][0-9]*\)/\1\2/
        p
     }' < "$progpath"
     exit $?
}

# func_usage
# Echo short help message to standard output and exit.
func_usage ()
{
    $opt_debug

    $SED -n '/^# Usage:/,/^#  *.*--help/ {
        s/^# //
	s/^# *$//
	s/\$progname/'$progname'/
	p
    }' < "$progpath"
    echo
    $ECHO "run \`$progname --help | more' for full usage"
    exit $?
}

# func_help [NOEXIT]
# Echo long help message to standard output and exit,
# unless 'noexit' is passed as argument.
func_help ()
{
    $opt_debug

    $SED -n '/^# Usage:/,/# Report bugs to/ {
	:print
        s/^# //
	s/^# *$//
	s*\$progname*'$progname'*
	s*\$host*'"$host"'*
	s*\$SHELL*'"$SHELL"'*
	s*\$LTCC*'"$LTCC"'*
	s*\$LTCFLAGS*'"$LTCFLAGS"'*
	s*\$LD*'"$LD"'*
	s/\$with_gnu_ld/'"$with_gnu_ld"'/
	s/\$automake_version/'"`(${AUTOMAKE-automake} --version) 2>/dev/null |$SED 1q`"'/
	s/\$autoconf_version/'"`(${AUTOCONF-autoconf} --version) 2>/dev/null |$SED 1q`"'/
	p
	d
     }
     /^# .* home page:/b print
     /^# General help using/b print
     ' < "$progpath"
    ret=$?
    if test -z "$1"; then
      exit $ret
    fi
}

# func_missing_arg argname
# Echo program name prefixed message to standard error and set global
# exit_cmd.
func_missing_arg ()
{
    $opt_debug

    func_error "missing argument for $1."
    exit_cmd=exit
}


# func_split_short_opt shortopt
# Set func_split_short_opt_name and func_split_short_opt_arg shell
# variables after splitting SHORTOPT after the 2nd character.
func_split_short_opt ()
{
    my_sed_short_opt='1s/^\(..\).*$/\1/;q'
    my_sed_short_rest='1s/^..\(.*\)$/\1/;q'

    func_split_short_opt_name=`$ECHO "$1" | $SED "$my_sed_short_opt"`
    func_split_short_opt_arg=`$ECHO "$1" | $SED "$my_sed_short_rest"`
} # func_split_short_opt may be replaced by extended shell implementation


# func_split_long_opt longopt
# Set func_split_long_opt_name and func_split_long_opt_arg shell
# variables after splitting LONGOPT at the `=' sign.
func_split_long_opt ()
{
    my_sed_long_opt='1s/^\(--[^=]*\)=.*/\1/;q'
    my_sed_long_arg='1s/^--[^=]*=//'

    func_split_long_opt_name=`$ECHO "$1" | $SED "$my_sed_long_opt"`
    func_split_long_opt_arg=`$ECHO "$1" | $SED "$my_sed_long_arg"`
} # func_split_long_opt may be replaced by extended shell implementation

exit_cmd=:





magic="%%%MAGIC variable%%%"
magic_exe="%%%MAGIC EXE variable%%%"

# Global variables.
nonopt=
preserve_args=
lo2o="s/\\.lo\$/.${objext}/"
o2lo="s/\\.${objext}\$/.lo/"
extracted_archives=
extracted_serial=0

# If this variable is set in any of the actions, the command in it
# will be execed at the end.  This prevents here-documents from being
# left over by shells.
exec_cmd=

# func_append var value
# Append VALUE to the end of shell variable VAR.
func_append ()
{
    eval "${1}=\$${1}\${2}"
} # func_append may be replaced by extended shell implementation

# func_append_quoted var value
# Quote VALUE and append to the end of shell variable VAR, separated
# by a space.
func_append_quoted ()
{
    func_quote_for_eval "${2}"
    eval "${1}=\$${1}\\ \$func_quote_for_eval_result"
} # func_append_quoted may be replaced by extended shell implementation


# func_arith arithmetic-term...
func_arith ()
{
    func_arith_result=`expr "${@}"`
} # func_arith may be replaced by extended shell implementation


# func_len string
# STRING may not start with a hyphen.
func_len ()
{
    func_len_result=`expr "${1}" : ".*" 2>/dev/null || echo $max_cmd_len`
} # func_len may be replaced by extended shell implementation


# func_lo2o object
func_lo2o ()
{
    func_lo2o_result=`$ECHO "${1}" | $SED "$lo2o"`
} # func_lo2o may be replaced by extended shell implementation


# func_xform libobj-or-source
func_xform ()
{
    func_xform_result=`$ECHO "${1}" | $SED 's/\.[^.]*$/.lo/'`
} # func_xform may be replaced by extended shell implementation


# func_fatal_configuration arg...
# Echo program name prefixed message to standard error, followed by
# a configuration failure hint, and exit.
func_fatal_configuration ()
{
    func_error ${1+"$@"}
    func_error "See the $PACKAGE documentation for more information."
    func_fatal_error "Fatal configuration error."
}


# func_config
# Display the configuration for all the tags in this script.
func_config ()
{
    re_begincf='^# ### BEGIN LIBTOOL'
    re_endcf='^# ### END LIBTOOL'

    # Default configuration.
    $SED "1,/$re_begincf CONFIG/d;/$re_endcf CONFIG/,\$d" < "$progpath"

    # Now print the configurations for the tags.
    for tagname in $taglist; do
      $SED -n "/$re_begincf TAG CONFIG: $tagname\$/,/$re_endcf TAG CONFIG: $tagname\$/p" < "$progpath"
    done

    exit $?
}

# func_features
# Display the features supported by this script.
func_features ()
{
    echo "host: $host"
    if test "$build_libtool_libs" = yes; then
      echo "enable shared libraries"
    else
      echo "disable shared libraries"
    fi
    if test "$build_old_libs" = yes; then
      echo "enable static libraries"
    else
      echo "disable static libraries"
    fi

    exit $?
}

# func_enable_tag tagname
# Verify that TAGNAME is valid, and either flag an error and exit, or
# enable the TAGNAME tag.  We also add TAGNAME to the global $taglist
# variable here.
func_enable_tag ()
{
  # Global variable:
  tagname="$1"

  re_begincf="^# ### BEGIN LIBTOOL TAG CONFIG: $tagname\$"
  re_endcf="^# ### END LIBTOOL TAG CONFIG: $tagname\$"
  sed_extractcf="/$re_begincf/,/$re_endcf/p"

  # Validate tagname.
  case $tagname in
    *[!-_A-Za-z0-9,/]*)
      func_fatal_error "invalid tag name: $tagname"
      ;;
  esac

  # Don't test for the "default" C tag, as we know it's
  # there but not specially marked.
  case $tagname in
    CC) ;;
    *)
      if $GREP "$re_begincf" "$progpath" >/dev/null 2>&1; then
	taglist="$taglist $tagname"

	# Evaluate the configuration.  Be careful to quote the path
	# and the sed script, to avoid splitting on whitespace, but
	# also don't use non-portable quotes within backquotes within
	# quotes we have to do it in 2 steps:
	extractedcf=`$SED -n -e "$sed_extractcf" < "$progpath"`
	eval "$extractedcf"
      else
	func_error "ignoring unknown tag $tagname"
      fi
      ;;
  esac
}

# func_check_version_match
# Ensure that we are using m4 macros, and libtool script from the same
# release of libtool.
func_check_version_match ()
{
  if test "$package_revision" != "$macro_revision"; then
    if test "$VERSION" != "$macro_version"; then
      if test -z "$macro_version"; then
        cat >&2 <<_LT_EOF
$progname: Version mismatch error.  This is $PACKAGE $VERSION, but the
$progname: definition of this LT_INIT comes from an older release.
$progname: You should recreate aclocal.m4 with macros from $PACKAGE $VERSION
$progname: and run autoconf again.
_LT_EOF
      else
        cat >&2 <<_LT_EOF
$progname: Version mismatch error.  This is $PACKAGE $VERSION, but the
$progname: definition of this LT_INIT comes from $PACKAGE $macro_version.
$progname: You should recreate aclocal.m4 with macros from $PACKAGE $VERSION
$progname: and run autoconf again.
_LT_EOF
      fi
    else
      cat >&2 <<_LT_EOF
$progname: Version mismatch error.  This is $PACKAGE $VERSION, revision $package_revision,
$progname: but the definition of this LT_INIT comes from revision $macro_revision.
$progname: You should recreate aclocal.m4 with macros from revision $package_revision
$progname: of $PACKAGE $VERSION and run autoconf again.
_LT_EOF
    fi

    exit $EXIT_MISMATCH
  fi
}


# Shorthand for --mode=foo, only valid as the first argument
case $1 in
clean|clea|cle|cl)
  shift; set dummy --mode clean ${1+"$@"}; shift
  ;;
compile|compil|compi|comp|com|co|c)
  shift; set dummy --mode compile ${1+"$@"}; shift
  ;;
execute|execut|execu|exec|exe|ex|e)
  shift; set dummy --mode execute ${1+"$@"}; shift
  ;;
finish|finis|fini|fin|fi|f)
  shift; set dummy --mode finish ${1+"$@"}; shift
  ;;
install|instal|insta|inst|ins|in|i)
  shift; set dummy --mode install ${1+"$@"}; shift
  ;;
link|lin|li|l)
  shift; set dummy --mode link ${1+"$@"}; shift
  ;;
uninstall|uninstal|uninsta|uninst|unins|unin|uni|un|u)
  shift; set dummy --mode uninstall ${1+"$@"}; shift
  ;;
esac



# Option defaults:
opt_debug=:
opt_dry_run=false
opt_config=false
opt_preserve_dup_deps=false
opt_features=false
opt_finish=false
opt_help=false
opt_help_all=false
opt_silent=:
opt_warning=:
opt_verbose=:
opt_silent=false
opt_verbose=false


# Parse options once, thoroughly.  This comes as soon as possible in the
# script to make things like `--version' happen as quickly as we can.
{
  # this just eases exit handling
  while test $# -gt 0; do
    opt="$1"
    shift
    case $opt in
      --debug|-x)	opt_debug='set -x'
			func_echo "enabling shell trace mode"
			$opt_debug
			;;
      --dry-run|--dryrun|-n)
			opt_dry_run=:
			;;
      --config)
			opt_config=:
func_config
			;;
      --dlopen|-dlopen)
			optarg="$1"
			opt_dlopen="${opt_dlopen+$opt_dlopen
}$optarg"
			shift
			;;
      --preserve-dup-deps)
			opt_preserve_dup_deps=:
			;;
      --features)
			opt_features=:
func_features
			;;
      --finish)
			opt_finish=:
set dummy --mode finish ${1+"$@"}; shift
			;;
      --help)
			opt_help=:
			;;
      --help-all)
			opt_help_all=:
opt_help=': help-all'
			;;
      --mode)
			test $# = 0 && func_missing_arg $opt && break
			optarg="$1"
			opt_mode="$optarg"
case $optarg in
  # Valid mode arguments:
  clean|compile|execute|finish|install|link|relink|uninstall) ;;

  # Catch anything else as an error
  *) func_error "invalid argument for $opt"
     exit_cmd=exit
     break
     ;;
esac
			shift
			;;
      --no-silent|--no-quiet)
			opt_silent=false
func_append preserve_args " $opt"
			;;
      --no-warning|--no-warn)
			opt_warning=false
func_append preserve_args " $opt"
			;;
      --no-verbose)
			opt_verbose=false
func_append preserve_args " $opt"
			;;
      --silent|--quiet)
			opt_silent=:
func_append preserve_args " $opt"
        opt_verbose=false
			;;
      --verbose|-v)
			opt_verbose=:
func_append preserve_args " $opt"
opt_silent=false
			;;
      --tag)
			test $# = 0 && func_missing_arg $opt && break
			optarg="$1"
			opt_tag="$optarg"
func_append preserve_args " $opt $optarg"
func_enable_tag "$optarg"
			shift
			;;

      -\?|-h)		func_usage				;;
      --help)		func_help				;;
      --version)	func_version				;;

      # Separate optargs to long options:
      --*=*)
			func_split_long_opt "$opt"
			set dummy "$func_split_long_opt_name" "$func_split_long_opt_arg" ${1+"$@"}
			shift
			;;

      # Separate non-argument short options:
      -\?*|-h*|-n*|-v*)
			func_split_short_opt "$opt"
			set dummy "$func_split_short_opt_name" "-$func_split_short_opt_arg" ${1+"$@"}
			shift
			;;

      --)		break					;;
      -*)		func_fatal_help "unrecognized option \`$opt'" ;;
      *)		set dummy "$opt" ${1+"$@"};	shift; break  ;;
    esac
  done

  # Validate options:

  # save first non-option argument
  if test "$#" -gt 0; then
    nonopt="$opt"
    shift
  fi

  # preserve --debug
  test "$opt_debug" = : || func_append preserve_args " --debug"

  case $host in
    *cygwin* | *mingw* | *pw32* | *cegcc*)
      # don't eliminate duplications in $postdeps and $predeps
      opt_duplicate_compiler_generated_deps=:
      ;;
    *)
      opt_duplicate_compiler_generated_deps=$opt_preserve_dup_deps
      ;;
  esac

  $opt_help || {
    # Sanity checks first:
    func_check_version_match

    if test "$build_libtool_libs" != yes && test "$build_old_libs" != yes; then
      func_fatal_configuration "not configured to build any kind of library"
    fi

    # Darwin sucks
    eval std_shrext=\"$shrext_cmds\"

    # Only execute mode is allowed to have -dlopen flags.
    if test -n "$opt_dlopen" && test "$opt_mode" != execute; then
      func_error "unrecognized option \`-dlopen'"
      $ECHO "$help" 1>&2
      exit $EXIT_FAILURE
    fi

    # Change the help message to a mode-specific one.
    generic_help="$help"
    help="Try \`$progname --help --mode=$opt_mode' for more information."
  }


  # Bail if the options were screwed
  $exit_cmd $EXIT_FAILURE
}




## ----------- ##
##    Main.    ##
## ----------- ##

# func_lalib_p file
# True iff FILE is a libtool `.la' library or `.lo' object file.
# This function is only a basic sanity check; it will hardly flush out
# determined imposters.
func_lalib_p ()
{
    test -f "$1" &&
      $SED -e 4q "$1" 2>/dev/null \
        | $GREP "^# Generated by .*$PACKAGE" > /dev/null 2>&1
}

# func_lalib_unsafe_p file
# True iff FILE is a libtool `.la' library or `.lo' object file.
# This function implements the same check as func_lalib_p without
# resorting to external programs.  To this end, it redirects stdin and
# closes it afterwards, without saving the original file descriptor.
# As a safety measure, use it only where a negative result would be
# fatal anyway.  Works if `file' does not exist.
func_lalib_unsafe_p ()
{
    lalib_p=no
    if test -f "$1" && test -r "$1" && exec 5<&0 <"$1"; then
	for lalib_p_l in 1 2 3 4
	do
	    read lalib_p_line
	    case "$lalib_p_line" in
		\#\ Generated\ by\ *$PACKAGE* ) lalib_p=yes; break;;
	    esac
	done
	exec 0<&5 5<&-
    fi
    test "$lalib_p" = yes
}

# func_ltwrapper_script_p file
# True iff FILE is a libtool wrapper script
# This function is only a basic sanity check; it will hardly flush out
# determined imposters.
func_ltwrapper_script_p ()
{
    func_lalib_p "$1"
}

# func_ltwrapper_executable_p file
# True iff FILE is a libtool wrapper executable
# This function is only a basic sanity check; it will hardly flush out
# determined imposters.
func_ltwrapper_executable_p ()
{
    func_ltwrapper_exec_suffix=
    case $1 in
    *.exe) ;;
    *) func_ltwrapper_exec_suffix=.exe ;;
    esac
    $GREP "$magic_exe" "$1$func_ltwrapper_exec_suffix" >/dev/null 2>&1
}

# func_ltwrapper_scriptname file
# Assumes file is an ltwrapper_executable
# uses $file to determine the appropriate filename for a
# temporary ltwrapper_script.
func_ltwrapper_scriptname ()
{
    func_dirname_and_basename "$1" "" "."
    func_stripname '' '.exe' "$func_basename_result"
    func_ltwrapper_scriptname_result="$func_dirname_result/$objdir/${func_stripname_result}_ltshwrapper"
}

# func_ltwrapper_p file
# True iff FILE is a libtool wrapper script or wrapper executable
# This function is only a basic sanity check; it will hardly flush out
# determined imposters.
func_ltwrapper_p ()
{
    func_ltwrapper_script_p "$1" || func_ltwrapper_executable_p "$1"
}


# func_execute_cmds commands fail_cmd
# Execute tilde-delimited COMMANDS.
# If FAIL_CMD is given, eval that upon failure.
# FAIL_CMD may read-access the current command in variable CMD!
func_execute_cmds ()
{
    $opt_debug
    save_ifs=$IFS; IFS='~'
    for cmd in $1; do
      IFS=$save_ifs
      eval cmd=\"$cmd\"
      func_show_eval "$cmd" "${2-:}"
    done
    IFS=$save_ifs
}


# func_source file
# Source FILE, adding directory component if necessary.
# Note that it is not necessary on cygwin/mingw to append a dot to
# FILE even if both FILE and FILE.exe exist: automatic-append-.exe
# behavior happens only for exec(3), not for open(2)!  Also, sourcing
# `FILE.' does not work on cygwin managed mounts.
func_source ()
{
    $opt_debug
    case $1 in
    */* | *\\*)	. "$1" ;;
    *)		. "./$1" ;;
    esac
}


# func_resolve_sysroot PATH
# Replace a leading = in PATH with a sysroot.  Store the result into
# func_resolve_sysroot_result
func_resolve_sysroot ()
{
  func_resolve_sysroot_result=$1
  case $func_resolve_sysroot_result in
  =*)
    func_stripname '=' '' "$func_resolve_sysroot_result"
    func_resolve_sysroot_result=$lt_sysroot$func_stripname_result
    ;;
  esac
}

# func_replace_sysroot PATH
# If PATH begins with the sysroot, replace it with = and
# store the result into func_replace_sysroot_result.
func_replace_sysroot ()
{
  case "$lt_sysroot:$1" in
  ?*:"$lt_sysroot"*)
    func_stripname "$lt_sysroot" '' "$1"
    func_replace_sysroot_result="=$func_stripname_result"
    ;;
  *)
    # Including no sysroot.
    func_replace_sysroot_result=$1
    ;;
  esac
}

# func_infer_tag arg
# Infer tagged configuration to use if any are available and
# if one wasn't chosen via the "--tag" command line option.
# Only attempt this if the compiler in the base compile
# command doesn't match the default compiler.
# arg is usually of the form 'gcc ...'
func_infer_tag ()
{
    $opt_debug
    if test -n "$available_tags" && test -z "$tagname"; then
      CC_quoted=
      for arg in $CC; do
	func_append_quoted CC_quoted "$arg"
      done
      CC_expanded=`func_echo_all $CC`
      CC_quoted_expanded=`func_echo_all $CC_quoted`
      case $@ in
      # Blanks in the command may have been stripped by the calling shell,
      # but not from the CC environment variable when configure was run.
      " $CC "* | "$CC "* | " $CC_expanded "* | "$CC_expanded "* | \
      " $CC_quoted"* | "$CC_quoted "* | " $CC_quoted_expanded "* | "$CC_quoted_expanded "*) ;;
      # Blanks at the start of $base_compile will cause this to fail
      # if we don't check for them as well.
      *)
	for z in $available_tags; do
	  if $GREP "^# ### BEGIN LIBTOOL TAG CONFIG: $z$" < "$progpath" > /dev/null; then
	    # Evaluate the configuration.
	    eval "`${SED} -n -e '/^# ### BEGIN LIBTOOL TAG CONFIG: '$z'$/,/^# ### END LIBTOOL TAG CONFIG: '$z'$/p' < $progpath`"
	    CC_quoted=
	    for arg in $CC; do
	      # Double-quote args containing other shell metacharacters.
	      func_append_quoted CC_quoted "$arg"
	    done
	    CC_expanded=`func_echo_all $CC`
	    CC_quoted_expanded=`func_echo_all $CC_quoted`
	    case "$@ " in
	    " $CC "* | "$CC "* | " $CC_expanded "* | "$CC_expanded "* | \
	    " $CC_quoted"* | "$CC_quoted "* | " $CC_quoted_expanded "* | "$CC_quoted_expanded "*)
	      # The compiler in the base compile command matches
	      # the one in the tagged configuration.
	      # Assume this is the tagged configuration we want.
	      tagname=$z
	      break
	      ;;
	    esac
	  fi
	done
	# If $tagname still isn't set, then no tagged configuration
	# was found and let the user know that the "--tag" command
	# line option must be used.
	if test -z "$tagname"; then
	  func_echo "unable to infer tagged configuration"
	  func_fatal_error "specify a tag with \`--tag'"
#	else
#	  func_verbose "using $tagname tagged configuration"
	fi
	;;
      esac
    fi
}



# func_write_libtool_object output_name pic_name nonpic_name
# Create a libtool object file (analogous to a ".la" file),
# but don't create it if we're doing a dry run.
func_write_libtool_object ()
{
    write_libobj=${1}
    if test "$build_libtool_libs" = yes; then
      write_lobj=\'${2}\'
    else
      write_lobj=none
    fi

    if test "$build_old_libs" = yes; then
      write_oldobj=\'${3}\'
    else
      write_oldobj=none
    fi

    $opt_dry_run || {
      cat >${write_libobj}T <<EOF
# $write_libobj - a libtool object file
# Generated by $PROGRAM (GNU $PACKAGE$TIMESTAMP) $VERSION
#
# Please DO NOT delete this file!
# It is necessary for linking the library.

# Name of the PIC object.
pic_object=$write_lobj

# Name of the non-PIC object
non_pic_object=$write_oldobj

EOF
      $MV "${write_libobj}T" "${write_libobj}"
    }
}


##################################################
# FILE NAME AND PATH CONVERSION HELPER FUNCTIONS #
##################################################

# func_convert_core_file_wine_to_w32 ARG
# Helper function used by file name conversion functions when $build is *nix,
# and $host is mingw, cygwin, or some other w32 environment. Relies on a
# correctly configured wine environment available, with the winepath program
# in $build's $PATH.
#
# ARG is the $build file name to be converted to w32 format.
# Result is available in $func_convert_core_file_wine_to_w32_result, and will
# be empty on error (or when ARG is empty)
func_convert_core_file_wine_to_w32 ()
{
  $opt_debug
  func_convert_core_file_wine_to_w32_result="$1"
  if test -n "$1"; then
    # Unfortunately, winepath does not exit with a non-zero error code, so we
    # are forced to check the contents of stdout. On the other hand, if the
    # command is not found, the shell will set an exit code of 127 and print
    # *an error message* to stdout. So we must check for both error code of
    # zero AND non-empty stdout, which explains the odd construction:
    func_convert_core_file_wine_to_w32_tmp=`winepath -w "$1" 2>/dev/null`
    if test "$?" -eq 0 && test -n "${func_convert_core_file_wine_to_w32_tmp}"; then
      func_convert_core_file_wine_to_w32_result=`$ECHO "$func_convert_core_file_wine_to_w32_tmp" |
        $SED -e "$lt_sed_naive_backslashify"`
    else
      func_convert_core_file_wine_to_w32_result=
    fi
  fi
}
# end: func_convert_core_file_wine_to_w32


# func_convert_core_path_wine_to_w32 ARG
# Helper function used by path conversion functions when $build is *nix, and
# $host is mingw, cygwin, or some other w32 environment. Relies on a correctly
# configured wine environment available, with the winepath program in $build's
# $PATH. Assumes ARG has no leading or trailing path separator characters.
#
# ARG is path to be converted from $build format to win32.
# Result is available in $func_convert_core_path_wine_to_w32_result.
# Unconvertible file (directory) names in ARG are skipped; if no directory names
# are convertible, then the result may be empty.
func_convert_core_path_wine_to_w32 ()
{
  $opt_debug
  # unfortunately, winepath doesn't convert paths, only file names
  func_convert_core_path_wine_to_w32_result=""
  if test -n "$1"; then
    oldIFS=$IFS
    IFS=:
    for func_convert_core_path_wine_to_w32_f in $1; do
      IFS=$oldIFS
      func_convert_core_file_wine_to_w32 "$func_convert_core_path_wine_to_w32_f"
      if test -n "$func_convert_core_file_wine_to_w32_result" ; then
        if test -z "$func_convert_core_path_wine_to_w32_result"; then
          func_convert_core_path_wine_to_w32_result="$func_convert_core_file_wine_to_w32_result"
        else
          func_append func_convert_core_path_wine_to_w32_result ";$func_convert_core_file_wine_to_w32_result"
        fi
      fi
    done
    IFS=$oldIFS
  fi
}
# end: func_convert_core_path_wine_to_w32


# func_cygpath ARGS...
# Wrapper around calling the cygpath program via LT_CYGPATH. This is used when
# when (1) $build is *nix and Cygwin is hosted via a wine environment; or (2)
# $build is MSYS and $host is Cygwin, or (3) $build is Cygwin. In case (1) or
# (2), returns the Cygwin file name or path in func_cygpath_result (input
# file name or path is assumed to be in w32 format, as previously converted
# from $build's *nix or MSYS format). In case (3), returns the w32 file name
# or path in func_cygpath_result (input file name or path is assumed to be in
# Cygwin format). Returns an empty string on error.
#
# ARGS are passed to cygpath, with the last one being the file name or path to
# be converted.
#
# Specify the absolute *nix (or w32) name to cygpath in the LT_CYGPATH
# environment variable; do not put it in $PATH.
func_cygpath ()
{
  $opt_debug
  if test -n "$LT_CYGPATH" && test -f "$LT_CYGPATH"; then
    func_cygpath_result=`$LT_CYGPATH "$@" 2>/dev/null`
    if test "$?" -ne 0; then
      # on failure, ensure result is empty
      func_cygpath_result=
    fi
  else
    func_cygpath_result=
    func_error "LT_CYGPATH is empty or specifies non-existent file: \`$LT_CYGPATH'"
  fi
}
#end: func_cygpath


# func_convert_core_msys_to_w32 ARG
# Convert file name or path ARG from MSYS format to w32 format.  Return
# result in func_convert_core_msys_to_w32_result.
func_convert_core_msys_to_w32 ()
{
  $opt_debug
  # awkward: cmd appends spaces to result
  func_convert_core_msys_to_w32_result=`( cmd //c echo "$1" ) 2>/dev/null |
    $SED -e 's/[ ]*$//' -e "$lt_sed_naive_backslashify"`
}
#end: func_convert_core_msys_to_w32


# func_convert_file_check ARG1 ARG2
# Verify that ARG1 (a file name in $build format) was converted to $host
# format in ARG2. Otherwise, emit an error message, but continue (resetting
# func_to_host_file_result to ARG1).
func_convert_file_check ()
{
  $opt_debug
  if test -z "$2" && test -n "$1" ; then
    func_error "Could not determine host file name corresponding to"
    func_error "  \`$1'"
    func_error "Continuing, but uninstalled executables may not work."
    # Fallback:
    func_to_host_file_result="$1"
  fi
}
# end func_convert_file_check


# func_convert_path_check FROM_PATHSEP TO_PATHSEP FROM_PATH TO_PATH
# Verify that FROM_PATH (a path in $build format) was converted to $host
# format in TO_PATH. Otherwise, emit an error message, but continue, resetting
# func_to_host_file_result to a simplistic fallback value (see below).
func_convert_path_check ()
{
  $opt_debug
  if test -z "$4" && test -n "$3"; then
    func_error "Could not determine the host path corresponding to"
    func_error "  \`$3'"
    func_error "Continuing, but uninstalled executables may not work."
    # Fallback.  This is a deliberately simplistic "conversion" and
    # should not be "improved".  See libtool.info.
    if test "x$1" != "x$2"; then
      lt_replace_pathsep_chars="s|$1|$2|g"
      func_to_host_path_result=`echo "$3" |
        $SED -e "$lt_replace_pathsep_chars"`
    else
      func_to_host_path_result="$3"
    fi
  fi
}
# end func_convert_path_check


# func_convert_path_front_back_pathsep FRONTPAT BACKPAT REPL ORIG
# Modifies func_to_host_path_result by prepending REPL if ORIG matches FRONTPAT
# and appending REPL if ORIG matches BACKPAT.
func_convert_path_front_back_pathsep ()
{
  $opt_debug
  case $4 in
  $1 ) func_to_host_path_result="$3$func_to_host_path_result"
    ;;
  esac
  case $4 in
  $2 ) func_append func_to_host_path_result "$3"
    ;;
  esac
}
# end func_convert_path_front_back_pathsep


##################################################
# $build to $host FILE NAME CONVERSION FUNCTIONS #
##################################################
# invoked via `$to_host_file_cmd ARG'
#
# In each case, ARG is the path to be converted from $build to $host format.
# Result will be available in $func_to_host_file_result.


# func_to_host_file ARG
# Converts the file name ARG from $build format to $host format. Return result
# in func_to_host_file_result.
func_to_host_file ()
{
  $opt_debug
  $to_host_file_cmd "$1"
}
# end func_to_host_file


# func_to_tool_file ARG LAZY
# converts the file name ARG from $build format to toolchain format. Return
# result in func_to_tool_file_result.  If the conversion in use is listed
# in (the comma separated) LAZY, no conversion takes place.
func_to_tool_file ()
{
  $opt_debug
  case ,$2, in
    *,"$to_tool_file_cmd",*)
      func_to_tool_file_result=$1
      ;;
    *)
      $to_tool_file_cmd "$1"
      func_to_tool_file_result=$func_to_host_file_result
      ;;
  esac
}
# end func_to_tool_file


# func_convert_file_noop ARG
# Copy ARG to func_to_host_file_result.
func_convert_file_noop ()
{
  func_to_host_file_result="$1"
}
# end func_convert_file_noop


# func_convert_file_msys_to_w32 ARG
# Convert file name ARG from (mingw) MSYS to (mingw) w32 format; automatic
# conversion to w32 is not available inside the cwrapper.  Returns result in
# func_to_host_file_result.
func_convert_file_msys_to_w32 ()
{
  $opt_debug
  func_to_host_file_result="$1"
  if test -n "$1"; then
    func_convert_core_msys_to_w32 "$1"
    func_to_host_file_result="$func_convert_core_msys_to_w32_result"
  fi
  func_convert_file_check "$1" "$func_to_host_file_result"
}
# end func_convert_file_msys_to_w32


# func_convert_file_cygwin_to_w32 ARG
# Convert file name ARG from Cygwin to w32 format.  Returns result in
# func_to_host_file_result.
func_convert_file_cygwin_to_w32 ()
{
  $opt_debug
  func_to_host_file_result="$1"
  if test -n "$1"; then
    # because $build is cygwin, we call "the" cygpath in $PATH; no need to use
    # LT_CYGPATH in this case.
    func_to_host_file_result=`cygpath -m "$1"`
  fi
  func_convert_file_check "$1" "$func_to_host_file_result"
}
# end func_convert_file_cygwin_to_w32


# func_convert_file_nix_to_w32 ARG
# Convert file name ARG from *nix to w32 format.  Requires a wine environment
# and a working winepath. Returns result in func_to_host_file_result.
func_convert_file_nix_to_w32 ()
{
  $opt_debug
  func_to_host_file_result="$1"
  if test -n "$1"; then
    func_convert_core_file_wine_to_w32 "$1"
    func_to_host_file_result="$func_convert_core_file_wine_to_w32_result"
  fi
  func_convert_file_check "$1" "$func_to_host_file_result"
}
# end func_convert_file_nix_to_w32


# func_convert_file_msys_to_cygwin ARG
# Convert file name ARG from MSYS to Cygwin format.  Requires LT_CYGPATH set.
# Returns result in func_to_host_file_result.
func_convert_file_msys_to_cygwin ()
{
  $opt_debug
  func_to_host_file_result="$1"
  if test -n "$1"; then
    func_convert_core_msys_to_w32 "$1"
    func_cygpath -u "$func_convert_core_msys_to_w32_result"
    func_to_host_file_result="$func_cygpath_result"
  fi
  func_convert_file_check "$1" "$func_to_host_file_result"
}
# end func_convert_file_msys_to_cygwin


# func_convert_file_nix_to_cygwin ARG
# Convert file name ARG from *nix to Cygwin format.  Requires Cygwin installed
# in a wine environment, working winepath, and LT_CYGPATH set.  Returns result
# in func_to_host_file_result.
func_convert_file_nix_to_cygwin ()
{
  $opt_debug
  func_to_host_file_result="$1"
  if test -n "$1"; then
    # convert from *nix to w32, then use cygpath to convert from w32 to cygwin.
    func_convert_core_file_wine_to_w32 "$1"
    func_cygpath -u "$func_convert_core_file_wine_to_w32_result"
    func_to_host_file_result="$func_cygpath_result"
  fi
  func_convert_file_check "$1" "$func_to_host_file_result"
}
# end func_convert_file_nix_to_cygwin


#############################################
# $build to $host PATH CONVERSION FUNCTIONS #
#############################################
# invoked via `$to_host_path_cmd ARG'
#
# In each case, ARG is the path to be converted from $build to $host format.
# The result will be available in $func_to_host_path_result.
#
# Path separators are also converted from $build format to $host format.  If
# ARG begins or ends with a path separator character, it is preserved (but
# converted to $host format) on output.
#
# All path conversion functions are named using the following convention:
#   file name conversion function    : func_convert_file_X_to_Y ()
#   path conversion function         : func_convert_path_X_to_Y ()
# where, for any given $build/$host combination the 'X_to_Y' value is the
# same.  If conversion functions are added for new $build/$host combinations,
# the two new functions must follow this pattern, or func_init_to_host_path_cmd
# will break.


# func_init_to_host_path_cmd
# Ensures that function "pointer" variable $to_host_path_cmd is set to the
# appropriate value, based on the value of $to_host_file_cmd.
to_host_path_cmd=
func_init_to_host_path_cmd ()
{
  $opt_debug
  if test -z "$to_host_path_cmd"; then
    func_stripname 'func_convert_file_' '' "$to_host_file_cmd"
    to_host_path_cmd="func_convert_path_${func_stripname_result}"
  fi
}


# func_to_host_path ARG
# Converts the path ARG from $build format to $host format. Return result
# in func_to_host_path_result.
func_to_host_path ()
{
  $opt_debug
  func_init_to_host_path_cmd
  $to_host_path_cmd "$1"
}
# end func_to_host_path


# func_convert_path_noop ARG
# Copy ARG to func_to_host_path_result.
func_convert_path_noop ()
{
  func_to_host_path_result="$1"
}
# end func_convert_path_noop


# func_convert_path_msys_to_w32 ARG
# Convert path ARG from (mingw) MSYS to (mingw) w32 format; automatic
# conversion to w32 is not available inside the cwrapper.  Returns result in
# func_to_host_path_result.
func_convert_path_msys_to_w32 ()
{
  $opt_debug
  func_to_host_path_result="$1"
  if test -n "$1"; then
    # Remove leading and trailing path separator characters from ARG.  MSYS
    # behavior is inconsistent here; cygpath turns them into '.;' and ';.';
    # and winepath ignores them completely.
    func_stripname : : "$1"
    func_to_host_path_tmp1=$func_stripname_result
    func_convert_core_msys_to_w32 "$func_to_host_path_tmp1"
    func_to_host_path_result="$func_convert_core_msys_to_w32_result"
    func_convert_path_check : ";" \
      "$func_to_host_path_tmp1" "$func_to_host_path_result"
    func_convert_path_front_back_pathsep ":*" "*:" ";" "$1"
  fi
}
# end func_convert_path_msys_to_w32


# func_convert_path_cygwin_to_w32 ARG
# Convert path ARG from Cygwin to w32 format.  Returns result in
# func_to_host_file_result.
func_convert_path_cygwin_to_w32 ()
{
  $opt_debug
  func_to_host_path_result="$1"
  if test -n "$1"; then
    # See func_convert_path_msys_to_w32:
    func_stripname : : "$1"
    func_to_host_path_tmp1=$func_stripname_result
    func_to_host_path_result=`cygpath -m -p "$func_to_host_path_tmp1"`
    func_convert_path_check : ";" \
      "$func_to_host_path_tmp1" "$func_to_host_path_result"
    func_convert_path_front_back_pathsep ":*" "*:" ";" "$1"
  fi
}
# end func_convert_path_cygwin_to_w32


# func_convert_path_nix_to_w32 ARG
# Convert path ARG from *nix to w32 format.  Requires a wine environment and
# a working winepath.  Returns result in func_to_host_file_result.
func_convert_path_nix_to_w32 ()
{
  $opt_debug
  func_to_host_path_result="$1"
  if test -n "$1"; then
    # See func_convert_path_msys_to_w32:
    func_stripname : : "$1"
    func_to_host_path_tmp1=$func_stripname_result
    func_convert_core_path_wine_to_w32 "$func_to_host_path_tmp1"
    func_to_host_path_result="$func_convert_core_path_wine_to_w32_result"
    func_convert_path_check : ";" \
      "$func_to_host_path_tmp1" "$func_to_host_path_result"
    func_convert_path_front_back_pathsep ":*" "*:" ";" "$1"
  fi
}
# end func_convert_path_nix_to_w32


# func_convert_path_msys_to_cygwin ARG
# Convert path ARG from MSYS to Cygwin format.  Requires LT_CYGPATH set.
# Returns result in func_to_host_file_result.
func_convert_path_msys_to_cygwin ()
{
  $opt_debug
  func_to_host_path_result="$1"
  if test -n "$1"; then
    # See func_convert_path_msys_to_w32:
    func_stripname : : "$1"
    func_to_host_path_tmp1=$func_stripname_result
    func_convert_core_msys_to_w32 "$func_to_host_path_tmp1"
    func_cygpath -u -p "$func_convert_core_msys_to_w32_result"
    func_to_host_path_result="$func_cygpath_result"
    func_convert_path_check : : \
      "$func_to_host_path_tmp1" "$func_to_host_path_result"
    func_convert_path_front_back_pathsep ":*" "*:" : "$1"
  fi
}
# end func_convert_path_msys_to_cygwin


# func_convert_path_nix_to_cygwin ARG
# Convert path ARG from *nix to Cygwin format.  Requires Cygwin installed in a
# a wine environment, working winepath, and LT_CYGPATH set.  Returns result in
# func_to_host_file_result.
func_convert_path_nix_to_cygwin ()
{
  $opt_debug
  func_to_host_path_result="$1"
  if test -n "$1"; then
    # Remove leading and trailing path separator characters from
    # ARG. msys behavior is inconsistent here, cygpath turns them
    # into '.;' and ';.', and winepath ignores them completely.
    func_stripname : : "$1"
    func_to_host_path_tmp1=$func_stripname_result
    func_convert_core_path_wine_to_w32 "$func_to_host_path_tmp1"
    func_cygpath -u -p "$func_convert_core_path_wine_to_w32_result"
    func_to_host_path_result="$func_cygpath_result"
    func_convert_path_check : : \
      "$func_to_host_path_tmp1" "$func_to_host_path_result"
    func_convert_path_front_back_pathsep ":*" "*:" : "$1"
  fi
}
# end func_convert_path_nix_to_cygwin


# func_mode_compile arg...
func_mode_compile ()
{
    $opt_debug
    # Get the compilation command and the source file.
    base_compile=
    srcfile="$nonopt"  #  always keep a non-empty value in "srcfile"
    suppress_opt=yes
    suppress_output=
    arg_mode=normal
    libobj=
    later=
    pie_flag=

    for arg
    do
      case $arg_mode in
      arg  )
	# do not "continue".  Instead, add this to base_compile
	lastarg="$arg"
	arg_mode=normal
	;;

      target )
	libobj="$arg"
	arg_mode=normal
	continue
	;;

      normal )
	# Accept any command-line options.
	case $arg in
	-o)
	  test -n "$libobj" && \
	    func_fatal_error "you cannot specify \`-o' more than once"
	  arg_mode=target
	  continue
	  ;;

	-pie | -fpie | -fPIE)
          func_append pie_flag " $arg"
	  continue
	  ;;

	-shared | -static | -prefer-pic | -prefer-non-pic)
	  func_append later " $arg"
	  continue
	  ;;

	-no-suppress)
	  suppress_opt=no
	  continue
	  ;;

	-Xcompiler)
	  arg_mode=arg  #  the next one goes into the "base_compile" arg list
	  continue      #  The current "srcfile" will either be retained or
	  ;;            #  replaced later.  I would guess that would be a bug.

	-Wc,*)
	  func_stripname '-Wc,' '' "$arg"
	  args=$func_stripname_result
	  lastarg=
	  save_ifs="$IFS"; IFS=','
	  for arg in $args; do
	    IFS="$save_ifs"
	    func_append_quoted lastarg "$arg"
	  done
	  IFS="$save_ifs"
	  func_stripname ' ' '' "$lastarg"
	  lastarg=$func_stripname_result

	  # Add the arguments to base_compile.
	  func_append base_compile " $lastarg"
	  continue
	  ;;

	*)
	  # Accept the current argument as the source file.
	  # The previous "srcfile" becomes the current argument.
	  #
	  lastarg="$srcfile"
	  srcfile="$arg"
	  ;;
	esac  #  case $arg
	;;
      esac    #  case $arg_mode

      # Aesthetically quote the previous argument.
      func_append_quoted base_compile "$lastarg"
    done # for arg

    case $arg_mode in
    arg)
      func_fatal_error "you must specify an argument for -Xcompile"
      ;;
    target)
      func_fatal_error "you must specify a target with \`-o'"
      ;;
    *)
      # Get the name of the library object.
      test -z "$libobj" && {
	func_basename "$srcfile"
	libobj="$func_basename_result"
      }
      ;;
    esac

    # Recognize several different file suffixes.
    # If the user specifies -o file.o, it is replaced with file.lo
    case $libobj in
    *.[cCFSifmso] | \
    *.ada | *.adb | *.ads | *.asm | \
    *.c++ | *.cc | *.ii | *.class | *.cpp | *.cxx | \
    *.[fF][09]? | *.for | *.java | *.go | *.obj | *.sx | *.cu | *.cup)
      func_xform "$libobj"
      libobj=$func_xform_result
      ;;
    esac

    case $libobj in
    *.lo) func_lo2o "$libobj"; obj=$func_lo2o_result ;;
    *)
      func_fatal_error "cannot determine name of library object from \`$libobj'"
      ;;
    esac

    func_infer_tag $base_compile

    for arg in $later; do
      case $arg in
      -shared)
	test "$build_libtool_libs" != yes && \
	  func_fatal_configuration "can not build a shared library"
	build_old_libs=no
	continue
	;;

      -static)
	build_libtool_libs=no
	build_old_libs=yes
	continue
	;;

      -prefer-pic)
	pic_mode=yes
	continue
	;;

      -prefer-non-pic)
	pic_mode=no
	continue
	;;
      esac
    done

    func_quote_for_eval "$libobj"
    test "X$libobj" != "X$func_quote_for_eval_result" \
      && $ECHO "X$libobj" | $GREP '[]~#^*{};<>?"'"'"'	 &()|`$[]' \
      && func_warning "libobj name \`$libobj' may not contain shell special characters."
    func_dirname_and_basename "$obj" "/" ""
    objname="$func_basename_result"
    xdir="$func_dirname_result"
    lobj=${xdir}$objdir/$objname

    test -z "$base_compile" && \
      func_fatal_help "you must specify a compilation command"

    # Delete any leftover library objects.
    if test "$build_old_libs" = yes; then
      removelist="$obj $lobj $libobj ${libobj}T"
    else
      removelist="$lobj $libobj ${libobj}T"
    fi

    # On Cygwin there's no "real" PIC flag so we must build both object types
    case $host_os in
    cygwin* | mingw* | pw32* | os2* | cegcc*)
      pic_mode=default
      ;;
    esac
    if test "$pic_mode" = no && test "$deplibs_check_method" != pass_all; then
      # non-PIC code in shared libraries is not supported
      pic_mode=default
    fi

    # Calculate the filename of the output object if compiler does
    # not support -o with -c
    if test "$compiler_c_o" = no; then
      output_obj=`$ECHO "$srcfile" | $SED 's%^.*/%%; s%\.[^.]*$%%'`.${objext}
      lockfile="$output_obj.lock"
    else
      output_obj=
      need_locks=no
      lockfile=
    fi

    # Lock this critical section if it is needed
    # We use this script file to make the link, it avoids creating a new file
    if test "$need_locks" = yes; then
      until $opt_dry_run || ln "$progpath" "$lockfile" 2>/dev/null; do
	func_echo "Waiting for $lockfile to be removed"
	sleep 2
      done
    elif test "$need_locks" = warn; then
      if test -f "$lockfile"; then
	$ECHO "\
*** ERROR, $lockfile exists and contains:
`cat $lockfile 2>/dev/null`

This indicates that another process is trying to use the same
temporary object file, and libtool could not work around it because
your compiler does not support \`-c' and \`-o' together.  If you
repeat this compilation, it may succeed, by chance, but you had better
avoid parallel builds (make -j) in this platform, or get a better
compiler."

	$opt_dry_run || $RM $removelist
	exit $EXIT_FAILURE
      fi
      func_append removelist " $output_obj"
      $ECHO "$srcfile" > "$lockfile"
    fi

    $opt_dry_run || $RM $removelist
    func_append removelist " $lockfile"
    trap '$opt_dry_run || $RM $removelist; exit $EXIT_FAILURE' 1 2 15

    func_to_tool_file "$srcfile" func_convert_file_msys_to_w32
    srcfile=$func_to_tool_file_result
    func_quote_for_eval "$srcfile"
    qsrcfile=$func_quote_for_eval_result

    # Only build a PIC object if we are building libtool libraries.
    if test "$build_libtool_libs" = yes; then
      # Without this assignment, base_compile gets emptied.
      fbsd_hideous_sh_bug=$base_compile

      if test "$pic_mode" != no; then
	command="$base_compile $qsrcfile $pic_flag"
      else
	# Don't build PIC code
	command="$base_compile $qsrcfile"
      fi

      func_mkdir_p "$xdir$objdir"

      if test -z "$output_obj"; then
	# Place PIC objects in $objdir
	func_append command " -o $lobj"
      fi

      func_show_eval_locale "$command"	\
          'test -n "$output_obj" && $RM $removelist; exit $EXIT_FAILURE'

      if test "$need_locks" = warn &&
	 test "X`cat $lockfile 2>/dev/null`" != "X$srcfile"; then
	$ECHO "\
*** ERROR, $lockfile contains:
`cat $lockfile 2>/dev/null`

but it should contain:
$srcfile

This indicates that another process is trying to use the same
temporary object file, and libtool could not work around it because
your compiler does not support \`-c' and \`-o' together.  If you
repeat this compilation, it may succeed, by chance, but you had better
avoid parallel builds (make -j) in this platform, or get a better
compiler."

	$opt_dry_run || $RM $removelist
	exit $EXIT_FAILURE
      fi

      # Just move the object if needed, then go on to compile the next one
      if test -n "$output_obj" && test "X$output_obj" != "X$lobj"; then
	func_show_eval '$MV "$output_obj" "$lobj"' \
	  'error=$?; $opt_dry_run || $RM $removelist; exit $error'
      fi

      # Allow error messages only from the first compilation.
      if test "$suppress_opt" = yes; then
	suppress_output=' >/dev/null 2>&1'
      fi
    fi

    # Only build a position-dependent object if we build old libraries.
    if test "$build_old_libs" = yes; then
      if test "$pic_mode" != yes; then
	# Don't build PIC code
	command="$base_compile $qsrcfile$pie_flag"
      else
	command="$base_compile $qsrcfile $pic_flag"
      fi
      if test "$compiler_c_o" = yes; then
	func_append command " -o $obj"
      fi

      # Suppress compiler output if we already did a PIC compilation.
      func_append command "$suppress_output"
      func_show_eval_locale "$command" \
        '$opt_dry_run || $RM $removelist; exit $EXIT_FAILURE'

      if test "$need_locks" = warn &&
	 test "X`cat $lockfile 2>/dev/null`" != "X$srcfile"; then
	$ECHO "\
*** ERROR, $lockfile contains:
`cat $lockfile 2>/dev/null`

but it should contain:
$srcfile

This indicates that another process is trying to use the same
temporary object file, and libtool could not work around it because
your compiler does not support \`-c' and \`-o' together.  If you
repeat this compilation, it may succeed, by chance, but you had better
avoid parallel builds (make -j) in this platform, or get a better
compiler."

	$opt_dry_run || $RM $removelist
	exit $EXIT_FAILURE
      fi

      # Just move the object if needed
      if test -n "$output_obj" && test "X$output_obj" != "X$obj"; then
	func_show_eval '$MV "$output_obj" "$obj"' \
	  'error=$?; $opt_dry_run || $RM $removelist; exit $error'
      fi
    fi

    $opt_dry_run || {
      func_write_libtool_object "$libobj" "$objdir/$objname" "$objname"

      # Unlock the critical section if it was locked
      if test "$need_locks" != no; then
	removelist=$lockfile
        $RM "$lockfile"
      fi
    }

    exit $EXIT_SUCCESS
}

$opt_help || {
  test "$opt_mode" = compile && func_mode_compile ${1+"$@"}
}

func_mode_help ()
{
    # We need to display help for each of the modes.
    case $opt_mode in
      "")
        # Generic help is extracted from the usage comments
        # at the start of this file.
        func_help
        ;;

      clean)
        $ECHO \
"Usage: $progname [OPTION]... --mode=clean RM [RM-OPTION]... FILE...

Remove files from the build directory.

RM is the name of the program to use to delete files associated with each FILE
(typically \`/bin/rm').  RM-OPTIONS are options (such as \`-f') to be passed
to RM.

If FILE is a libtool library, object or program, all the files associated
with it are deleted. Otherwise, only FILE itself is deleted using RM."
        ;;

      compile)
      $ECHO \
"Usage: $progname [OPTION]... --mode=compile COMPILE-COMMAND... SOURCEFILE

Compile a source file into a libtool library object.

This mode accepts the following additional options:

  -o OUTPUT-FILE    set the output file name to OUTPUT-FILE
  -no-suppress      do not suppress compiler output for multiple passes
  -prefer-pic       try to build PIC objects only
  -prefer-non-pic   try to build non-PIC objects only
  -shared           do not build a \`.o' file suitable for static linking
  -static           only build a \`.o' file suitable for static linking
  -Wc,FLAG          pass FLAG directly to the compiler

COMPILE-COMMAND is a command to be used in creating a \`standard' object file
from the given SOURCEFILE.

The output file name is determined by removing the directory component from
SOURCEFILE, then substituting the C source code suffix \`.c' with the
library object suffix, \`.lo'."
        ;;

      execute)
        $ECHO \
"Usage: $progname [OPTION]... --mode=execute COMMAND [ARGS]...

Automatically set library path, then run a program.

This mode accepts the following additional options:

  -dlopen FILE      add the directory containing FILE to the library path

This mode sets the library path environment variable according to \`-dlopen'
flags.

If any of the ARGS are libtool executable wrappers, then they are translated
into their corresponding uninstalled binary, and any of their required library
directories are added to the library path.

Then, COMMAND is executed, with ARGS as arguments."
        ;;

      finish)
        $ECHO \
"Usage: $progname [OPTION]... --mode=finish [LIBDIR]...

Complete the installation of libtool libraries.

Each LIBDIR is a directory that contains libtool libraries.

The commands that this mode executes may require superuser privileges.  Use
the \`--dry-run' option if you just want to see what would be executed."
        ;;

      install)
        $ECHO \
"Usage: $progname [OPTION]... --mode=install INSTALL-COMMAND...

Install executables or libraries.

INSTALL-COMMAND is the installation command.  The first component should be
either the \`install' or \`cp' program.

The following components of INSTALL-COMMAND are treated specially:

  -inst-prefix-dir PREFIX-DIR  Use PREFIX-DIR as a staging area for installation

The rest of the components are interpreted as arguments to that command (only
BSD-compatible install options are recognized)."
        ;;

      link)
        $ECHO \
"Usage: $progname [OPTION]... --mode=link LINK-COMMAND...

Link object files or libraries together to form another library, or to
create an executable program.

LINK-COMMAND is a command using the C compiler that you would use to create
a program from several object files.

The following components of LINK-COMMAND are treated specially:

  -all-static       do not do any dynamic linking at all
  -avoid-version    do not add a version suffix if possible
  -bindir BINDIR    specify path to binaries directory (for systems where
                    libraries must be found in the PATH setting at runtime)
  -dlopen FILE      \`-dlpreopen' FILE if it cannot be dlopened at runtime
  -dlpreopen FILE   link in FILE and add its symbols to lt_preloaded_symbols
  -export-dynamic   allow symbols from OUTPUT-FILE to be resolved with dlsym(3)
  -export-symbols SYMFILE
                    try to export only the symbols listed in SYMFILE
  -export-symbols-regex REGEX
                    try to export only the symbols matching REGEX
  -LLIBDIR          search LIBDIR for required installed libraries
  -lNAME            OUTPUT-FILE requires the installed library libNAME
  -module           build a library that can dlopened
  -no-fast-install  disable the fast-install mode
  -no-install       link a not-installable executable
  -no-undefined     declare that a library does not refer to external symbols
  -o OUTPUT-FILE    create OUTPUT-FILE from the specified objects
  -objectlist FILE  Use a list of object files found in FILE to specify objects
  -precious-files-regex REGEX
                    don't remove output files matching REGEX
  -release RELEASE  specify package release information
  -rpath LIBDIR     the created library will eventually be installed in LIBDIR
  -R[ ]LIBDIR       add LIBDIR to the runtime path of programs and libraries
  -shared           only do dynamic linking of libtool libraries
  -shrext SUFFIX    override the standard shared library file extension
  -static           do not do any dynamic linking of uninstalled libtool libraries
  -static-libtool-libs
                    do not do any dynamic linking of libtool libraries
  -version-info CURRENT[:REVISION[:AGE]]
                    specify library version info [each variable defaults to 0]
  -weak LIBNAME     declare that the target provides the LIBNAME interface
  -Wc,FLAG
  -Xcompiler FLAG   pass linker-specific FLAG directly to the compiler
  -Wl,FLAG
  -Xlinker FLAG     pass linker-specific FLAG directly to the linker
  -XCClinker FLAG   pass link-specific FLAG to the compiler driver (CC)

All other options (arguments beginning with \`-') are ignored.

Every other argument is treated as a filename.  Files ending in \`.la' are
treated as uninstalled libtool libraries, other files are standard or library
object files.

If the OUTPUT-FILE ends in \`.la', then a libtool library is created,
only library objects (\`.lo' files) may be specified, and \`-rpath' is
required, except when creating a convenience library.

If OUTPUT-FILE ends in \`.a' or \`.lib', then a standard library is created
using \`ar' and \`ranlib', or on Windows using \`lib'.

If OUTPUT-FILE ends in \`.lo' or \`.${objext}', then a reloadable object file
is created, otherwise an executable program is created."
        ;;

      uninstall)
        $ECHO \
"Usage: $progname [OPTION]... --mode=uninstall RM [RM-OPTION]... FILE...

Remove libraries from an installation directory.

RM is the name of the program to use to delete files associated with each FILE
(typically \`/bin/rm').  RM-OPTIONS are options (such as \`-f') to be passed
to RM.

If FILE is a libtool library, all the files associated with it are deleted.
Otherwise, only FILE itself is deleted using RM."
        ;;

      *)
        func_fatal_help "invalid operation mode \`$opt_mode'"
        ;;
    esac

    echo
    $ECHO "Try \`$progname --help' for more information about other modes."
}

# Now that we've collected a possible --mode arg, show help if necessary
if $opt_help; then
  if test "$opt_help" = :; then
    func_mode_help
  else
    {
      func_help noexit
      for opt_mode in compile link execute install finish uninstall clean; do
	func_mode_help
      done
    } | sed -n '1p; 2,$s/^Usage:/  or: /p'
    {
      func_help noexit
      for opt_mode in compile link execute install finish uninstall clean; do
	echo
	func_mode_help
      done
    } |
    sed '1d
      /^When reporting/,/^Report/{
	H
	d
      }
      $x
      /information about other modes/d
      /more detailed .*MODE/d
      s/^Usage:.*--mode=\([^ ]*\) .*/Description of \1 mode:/'
  fi
  exit $?
fi


# func_mode_execute arg...
func_mode_execute ()
{
    $opt_debug
    # The first argument is the command name.
    cmd="$nonopt"
    test -z "$cmd" && \
      func_fatal_help "you must specify a COMMAND"

    # Handle -dlopen flags immediately.
    for file in $opt_dlopen; do
      test -f "$file" \
	|| func_fatal_help "\`$file' is not a file"

      dir=
      case $file in
      *.la)
	func_resolve_sysroot "$file"
	file=$func_resolve_sysroot_result

	# Check to see that this really is a libtool archive.
	func_lalib_unsafe_p "$file" \
	  || func_fatal_help "\`$lib' is not a valid libtool archive"

	# Read the libtool library.
	dlname=
	library_names=
	func_source "$file"

	# Skip this library if it cannot be dlopened.
	if test -z "$dlname"; then
	  # Warn if it was a shared library.
	  test -n "$library_names" && \
	    func_warning "\`$file' was not linked with \`-export-dynamic'"
	  continue
	fi

	func_dirname "$file" "" "."
	dir="$func_dirname_result"

	if test -f "$dir/$objdir/$dlname"; then
	  func_append dir "/$objdir"
	else
	  if test ! -f "$dir/$dlname"; then
	    func_fatal_error "cannot find \`$dlname' in \`$dir' or \`$dir/$objdir'"
	  fi
	fi
	;;

      *.lo)
	# Just add the directory containing the .lo file.
	func_dirname "$file" "" "."
	dir="$func_dirname_result"
	;;

      *)
	func_warning "\`-dlopen' is ignored for non-libtool libraries and objects"
	continue
	;;
      esac

      # Get the absolute pathname.
      absdir=`cd "$dir" && pwd`
      test -n "$absdir" && dir="$absdir"

      # Now add the directory to shlibpath_var.
      if eval "test -z \"\$$shlibpath_var\""; then
	eval "$shlibpath_var=\"\$dir\""
      else
	eval "$shlibpath_var=\"\$dir:\$$shlibpath_var\""
      fi
    done

    # This variable tells wrapper scripts just to set shlibpath_var
    # rather than running their programs.
    libtool_execute_magic="$magic"

    # Check if any of the arguments is a wrapper script.
    args=
    for file
    do
      case $file in
      -* | *.la | *.lo ) ;;
      *)
	# Do a test to see if this is really a libtool program.
	if func_ltwrapper_script_p "$file"; then
	  func_source "$file"
	  # Transform arg to wrapped name.
	  file="$progdir/$program"
	elif func_ltwrapper_executable_p "$file"; then
	  func_ltwrapper_scriptname "$file"
	  func_source "$func_ltwrapper_scriptname_result"
	  # Transform arg to wrapped name.
	  file="$progdir/$program"
	fi
	;;
      esac
      # Quote arguments (to preserve shell metacharacters).
      func_append_quoted args "$file"
    done

    if test "X$opt_dry_run" = Xfalse; then
      if test -n "$shlibpath_var"; then
	# Export the shlibpath_var.
	eval "export $shlibpath_var"
      fi

      # Restore saved environment variables
      for lt_var in LANG LANGUAGE LC_ALL LC_CTYPE LC_COLLATE LC_MESSAGES
      do
	eval "if test \"\${save_$lt_var+set}\" = set; then
                $lt_var=\$save_$lt_var; export $lt_var
	      else
		$lt_unset $lt_var
	      fi"
      done

      # Now prepare to actually exec the command.
      exec_cmd="\$cmd$args"
    else
      # Display what would be done.
      if test -n "$shlibpath_var"; then
	eval "\$ECHO \"\$shlibpath_var=\$$shlibpath_var\""
	echo "export $shlibpath_var"
      fi
      $ECHO "$cmd$args"
      exit $EXIT_SUCCESS
    fi
}

test "$opt_mode" = execute && func_mode_execute ${1+"$@"}


# func_mode_finish arg...
func_mode_finish ()
{
    $opt_debug
    libs=
    libdirs=
    admincmds=

    for opt in "$nonopt" ${1+"$@"}
    do
      if test -d "$opt"; then
	func_append libdirs " $opt"

      elif test -f "$opt"; then
	if func_lalib_unsafe_p "$opt"; then
	  func_append libs " $opt"
	else
	  func_warning "\`$opt' is not a valid libtool archive"
	fi

      else
	func_fatal_error "invalid argument \`$opt'"
      fi
    done

    if test -n "$libs"; then
      if test -n "$lt_sysroot"; then
        sysroot_regex=`$ECHO "$lt_sysroot" | $SED "$sed_make_literal_regex"`
        sysroot_cmd="s/\([ ']\)$sysroot_regex/\1/g;"
      else
        sysroot_cmd=
      fi

      # Remove sysroot references
      if $opt_dry_run; then
        for lib in $libs; do
          echo "removing references to $lt_sysroot and \`=' prefixes from $lib"
        done
      else
        tmpdir=`func_mktempdir`
        for lib in $libs; do
	  sed -e "${sysroot_cmd} s/\([ ']-[LR]\)=/\1/g; s/\([ ']\)=/\1/g" $lib \
	    > $tmpdir/tmp-la
	  mv -f $tmpdir/tmp-la $lib
	done
        ${RM}r "$tmpdir"
      fi
    fi

    if test -n "$finish_cmds$finish_eval" && test -n "$libdirs"; then
      for libdir in $libdirs; do
	if test -n "$finish_cmds"; then
	  # Do each command in the finish commands.
	  func_execute_cmds "$finish_cmds" 'admincmds="$admincmds
'"$cmd"'"'
	fi
	if test -n "$finish_eval"; then
	  # Do the single finish_eval.
	  eval cmds=\"$finish_eval\"
	  $opt_dry_run || eval "$cmds" || func_append admincmds "
       $cmds"
	fi
      done
    fi

    # Exit here if they wanted silent mode.
    $opt_silent && exit $EXIT_SUCCESS

    if test -n "$finish_cmds$finish_eval" && test -n "$libdirs"; then
      echo "----------------------------------------------------------------------"
      echo "Libraries have been installed in:"
      for libdir in $libdirs; do
	$ECHO "   $libdir"
      done
      echo
      echo "If you ever happen to want to link against installed libraries"
      echo "in a given directory, LIBDIR, you must either use libtool, and"
      echo "specify the full pathname of the library, or use the \`-LLIBDIR'"
      echo "flag during linking and do at least one of the following:"
      if test -n "$shlibpath_var"; then
	echo "   - add LIBDIR to the \`$shlibpath_var' environment variable"
	echo "     during execution"
      fi
      if test -n "$runpath_var"; then
	echo "   - add LIBDIR to the \`$runpath_var' environment variable"
	echo "     during linking"
      fi
      if test -n "$hardcode_libdir_flag_spec"; then
	libdir=LIBDIR
	eval flag=\"$hardcode_libdir_flag_spec\"

	$ECHO "   - use the \`$flag' linker flag"
      fi
      if test -n "$admincmds"; then
	$ECHO "   - have your system administrator run these commands:$admincmds"
      fi
      if test -f /etc/ld.so.conf; then
	echo "   - have your system administrator add LIBDIR to \`/etc/ld.so.conf'"
      fi
      echo

      echo "See any operating system documentation about shared libraries for"
      case $host in
	solaris2.[6789]|solaris2.1[0-9])
	  echo "more information, such as the ld(1), crle(1) and ld.so(8) manual"
	  echo "pages."
	  ;;
	*)
	  echo "more information, such as the ld(1) and ld.so(8) manual pages."
	  ;;
      esac
      echo "----------------------------------------------------------------------"
    fi
    exit $EXIT_SUCCESS
}

test "$opt_mode" = finish && func_mode_finish ${1+"$@"}


# func_mode_install arg...
func_mode_install ()
{
    $opt_debug
    # There may be an optional sh(1) argument at the beginning of
    # install_prog (especially on Windows NT).
    if test "$nonopt" = "$SHELL" || test "$nonopt" = /bin/sh ||
       # Allow the use of GNU shtool's install command.
       case $nonopt in *shtool*) :;; *) false;; esac; then
      # Aesthetically quote it.
      func_quote_for_eval "$nonopt"
      install_prog="$func_quote_for_eval_result "
      arg=$1
      shift
    else
      install_prog=
      arg=$nonopt
    fi

    # The real first argument should be the name of the installation program.
    # Aesthetically quote it.
    func_quote_for_eval "$arg"
    func_append install_prog "$func_quote_for_eval_result"
    install_shared_prog=$install_prog
    case " $install_prog " in
      *[\\\ /]cp\ *) install_cp=: ;;
      *) install_cp=false ;;
    esac

    # We need to accept at least all the BSD install flags.
    dest=
    files=
    opts=
    prev=
    install_type=
    isdir=no
    stripme=
    no_mode=:
    for arg
    do
      arg2=
      if test -n "$dest"; then
	func_append files " $dest"
	dest=$arg
	continue
      fi

      case $arg in
      -d) isdir=yes ;;
      -f)
	if $install_cp; then :; else
	  prev=$arg
	fi
	;;
      -g | -m | -o)
	prev=$arg
	;;
      -s)
	stripme=" -s"
	continue
	;;
      -*)
	;;
      *)
	# If the previous option needed an argument, then skip it.
	if test -n "$prev"; then
	  if test "x$prev" = x-m && test -n "$install_override_mode"; then
	    arg2=$install_override_mode
	    no_mode=false
	  fi
	  prev=
	else
	  dest=$arg
	  continue
	fi
	;;
      esac

      # Aesthetically quote the argument.
      func_quote_for_eval "$arg"
      func_append install_prog " $func_quote_for_eval_result"
      if test -n "$arg2"; then
	func_quote_for_eval "$arg2"
      fi
      func_append install_shared_prog " $func_quote_for_eval_result"
    done

    test -z "$install_prog" && \
      func_fatal_help "you must specify an install program"

    test -n "$prev" && \
      func_fatal_help "the \`$prev' option requires an argument"

    if test -n "$install_override_mode" && $no_mode; then
      if $install_cp; then :; else
	func_quote_for_eval "$install_override_mode"
	func_append install_shared_prog " -m $func_quote_for_eval_result"
      fi
    fi

    if test -z "$files"; then
      if test -z "$dest"; then
	func_fatal_help "no file or destination specified"
      else
	func_fatal_help "you must specify a destination"
      fi
    fi

    # Strip any trailing slash from the destination.
    func_stripname '' '/' "$dest"
    dest=$func_stripname_result

    # Check to see that the destination is a directory.
    test -d "$dest" && isdir=yes
    if test "$isdir" = yes; then
      destdir="$dest"
      destname=
    else
      func_dirname_and_basename "$dest" "" "."
      destdir="$func_dirname_result"
      destname="$func_basename_result"

      # Not a directory, so check to see that there is only one file specified.
      set dummy $files; shift
      test "$#" -gt 1 && \
	func_fatal_help "\`$dest' is not a directory"
    fi
    case $destdir in
    [\\/]* | [A-Za-z]:[\\/]*) ;;
    *)
      for file in $files; do
	case $file in
	*.lo) ;;
	*)
	  func_fatal_help "\`$destdir' must be an absolute directory name"
	  ;;
	esac
      done
      ;;
    esac

    # This variable tells wrapper scripts just to set variables rather
    # than running their programs.
    libtool_install_magic="$magic"

    staticlibs=
    future_libdirs=
    current_libdirs=
    for file in $files; do

      # Do each installation.
      case $file in
      *.$libext)
	# Do the static libraries later.
	func_append staticlibs " $file"
	;;

      *.la)
	func_resolve_sysroot "$file"
	file=$func_resolve_sysroot_result

	# Check to see that this really is a libtool archive.
	func_lalib_unsafe_p "$file" \
	  || func_fatal_help "\`$file' is not a valid libtool archive"

	library_names=
	old_library=
	relink_command=
	func_source "$file"

	# Add the libdir to current_libdirs if it is the destination.
	if test "X$destdir" = "X$libdir"; then
	  case "$current_libdirs " in
	  *" $libdir "*) ;;
	  *) func_append current_libdirs " $libdir" ;;
	  esac
	else
	  # Note the libdir as a future libdir.
	  case "$future_libdirs " in
	  *" $libdir "*) ;;
	  *) func_append future_libdirs " $libdir" ;;
	  esac
	fi

	func_dirname "$file" "/" ""
	dir="$func_dirname_result"
	func_append dir "$objdir"

	if test -n "$relink_command"; then
	  # Determine the prefix the user has applied to our future dir.
	  inst_prefix_dir=`$ECHO "$destdir" | $SED -e "s%$libdir\$%%"`

	  # Don't allow the user to place us outside of our expected
	  # location b/c this prevents finding dependent libraries that
	  # are installed to the same prefix.
	  # At present, this check doesn't affect windows .dll's that
	  # are installed into $libdir/../bin (currently, that works fine)
	  # but it's something to keep an eye on.
	  test "$inst_prefix_dir" = "$destdir" && \
	    func_fatal_error "error: cannot install \`$file' to a directory not ending in $libdir"

	  if test -n "$inst_prefix_dir"; then
	    # Stick the inst_prefix_dir data into the link command.
	    relink_command=`$ECHO "$relink_command" | $SED "s%@inst_prefix_dir@%-inst-prefix-dir $inst_prefix_dir%"`
	  else
	    relink_command=`$ECHO "$relink_command" | $SED "s%@inst_prefix_dir@%%"`
	  fi

	  func_warning "relinking \`$file'"
	  func_show_eval "$relink_command" \
	    'func_fatal_error "error: relink \`$file'\'' with the above command before installing it"'
	fi

	# See the names of the shared library.
	set dummy $library_names; shift
	if test -n "$1"; then
	  realname="$1"
	  shift

	  srcname="$realname"
	  test -n "$relink_command" && srcname="$realname"T

	  # Install the shared library and build the symlinks.
	  func_show_eval "$install_shared_prog $dir/$srcname $destdir/$realname" \
	      'exit $?'
	  tstripme="$stripme"
	  case $host_os in
	  cygwin* | mingw* | pw32* | cegcc*)
	    case $realname in
	    *.dll.a)
	      tstripme=""
	      ;;
	    esac
	    ;;
	  esac
	  if test -n "$tstripme" && test -n "$striplib"; then
	    func_show_eval "$striplib $destdir/$realname" 'exit $?'
	  fi

	  if test "$#" -gt 0; then
	    # Delete the old symlinks, and create new ones.
	    # Try `ln -sf' first, because the `ln' binary might depend on
	    # the symlink we replace!  Solaris /bin/ln does not understand -f,
	    # so we also need to try rm && ln -s.
	    for linkname
	    do
	      test "$linkname" != "$realname" \
		&& func_show_eval "(cd $destdir && { $LN_S -f $realname $linkname || { $RM $linkname && $LN_S $realname $linkname; }; })"
	    done
	  fi

	  # Do each command in the postinstall commands.
	  lib="$destdir/$realname"
	  func_execute_cmds "$postinstall_cmds" 'exit $?'
	fi

	# Install the pseudo-library for information purposes.
	func_basename "$file"
	name="$func_basename_result"
	instname="$dir/$name"i
	func_show_eval "$install_prog $instname $destdir/$name" 'exit $?'

	# Maybe install the static library, too.
	test -n "$old_library" && func_append staticlibs " $dir/$old_library"
	;;

      *.lo)
	# Install (i.e. copy) a libtool object.

	# Figure out destination file name, if it wasn't already specified.
	if test -n "$destname"; then
	  destfile="$destdir/$destname"
	else
	  func_basename "$file"
	  destfile="$func_basename_result"
	  destfile="$destdir/$destfile"
	fi

	# Deduce the name of the destination old-style object file.
	case $destfile in
	*.lo)
	  func_lo2o "$destfile"
	  staticdest=$func_lo2o_result
	  ;;
	*.$objext)
	  staticdest="$destfile"
	  destfile=
	  ;;
	*)
	  func_fatal_help "cannot copy a libtool object to \`$destfile'"
	  ;;
	esac

	# Install the libtool object if requested.
	test -n "$destfile" && \
	  func_show_eval "$install_prog $file $destfile" 'exit $?'

	# Install the old object if enabled.
	if test "$build_old_libs" = yes; then
	  # Deduce the name of the old-style object file.
	  func_lo2o "$file"
	  staticobj=$func_lo2o_result
	  func_show_eval "$install_prog \$staticobj \$staticdest" 'exit $?'
	fi
	exit $EXIT_SUCCESS
	;;

      *)
	# Figure out destination file name, if it wasn't already specified.
	if test -n "$destname"; then
	  destfile="$destdir/$destname"
	else
	  func_basename "$file"
	  destfile="$func_basename_result"
	  destfile="$destdir/$destfile"
	fi

	# If the file is missing, and there is a .exe on the end, strip it
	# because it is most likely a libtool script we actually want to
	# install
	stripped_ext=""
	case $file in
	  *.exe)
	    if test ! -f "$file"; then
	      func_stripname '' '.exe' "$file"
	      file=$func_stripname_result
	      stripped_ext=".exe"
	    fi
	    ;;
	esac

	# Do a test to see if this is really a libtool program.
	case $host in
	*cygwin* | *mingw*)
	    if func_ltwrapper_executable_p "$file"; then
	      func_ltwrapper_scriptname "$file"
	      wrapper=$func_ltwrapper_scriptname_result
	    else
	      func_stripname '' '.exe' "$file"
	      wrapper=$func_stripname_result
	    fi
	    ;;
	*)
	    wrapper=$file
	    ;;
	esac
	if func_ltwrapper_script_p "$wrapper"; then
	  notinst_deplibs=
	  relink_command=

	  func_source "$wrapper"

	  # Check the variables that should have been set.
	  test -z "$generated_by_libtool_version" && \
	    func_fatal_error "invalid libtool wrapper script \`$wrapper'"

	  finalize=yes
	  for lib in $notinst_deplibs; do
	    # Check to see that each library is installed.
	    libdir=
	    if test -f "$lib"; then
	      func_source "$lib"
	    fi
	    libfile="$libdir/"`$ECHO "$lib" | $SED 's%^.*/%%g'` ### testsuite: skip nested quoting test
	    if test -n "$libdir" && test ! -f "$libfile"; then
	      func_warning "\`$lib' has not been installed in \`$libdir'"
	      finalize=no
	    fi
	  done

	  relink_command=
	  func_source "$wrapper"

	  outputname=
	  if test "$fast_install" = no && test -n "$relink_command"; then
	    $opt_dry_run || {
	      if test "$finalize" = yes; then
	        tmpdir=`func_mktempdir`
		func_basename "$file$stripped_ext"
		file="$func_basename_result"
	        outputname="$tmpdir/$file"
	        # Replace the output file specification.
	        relink_command=`$ECHO "$relink_command" | $SED 's%@OUTPUT@%'"$outputname"'%g'`

	        $opt_silent || {
	          func_quote_for_expand "$relink_command"
		  eval "func_echo $func_quote_for_expand_result"
	        }
	        if eval "$relink_command"; then :
	          else
		  func_error "error: relink \`$file' with the above command before installing it"
		  $opt_dry_run || ${RM}r "$tmpdir"
		  continue
	        fi
	        file="$outputname"
	      else
	        func_warning "cannot relink \`$file'"
	      fi
	    }
	  else
	    # Install the binary that we compiled earlier.
	    file=`$ECHO "$file$stripped_ext" | $SED "s%\([^/]*\)$%$objdir/\1%"`
	  fi
	fi

	# remove .exe since cygwin /usr/bin/install will append another
	# one anyway
	case $install_prog,$host in
	*/usr/bin/install*,*cygwin*)
	  case $file:$destfile in
	  *.exe:*.exe)
	    # this is ok
	    ;;
	  *.exe:*)
	    destfile=$destfile.exe
	    ;;
	  *:*.exe)
	    func_stripname '' '.exe' "$destfile"
	    destfile=$func_stripname_result
	    ;;
	  esac
	  ;;
	esac
	func_show_eval "$install_prog\$stripme \$file \$destfile" 'exit $?'
	$opt_dry_run || if test -n "$outputname"; then
	  ${RM}r "$tmpdir"
	fi
	;;
      esac
    done

    for file in $staticlibs; do
      func_basename "$file"
      name="$func_basename_result"

      # Set up the ranlib parameters.
      oldlib="$destdir/$name"
      func_to_tool_file "$oldlib" func_convert_file_msys_to_w32
      tool_oldlib=$func_to_tool_file_result

      func_show_eval "$install_prog \$file \$oldlib" 'exit $?'

      if test -n "$stripme" && test -n "$old_striplib"; then
	func_show_eval "$old_striplib $tool_oldlib" 'exit $?'
      fi

      # Do each command in the postinstall commands.
      func_execute_cmds "$old_postinstall_cmds" 'exit $?'
    done

    test -n "$future_libdirs" && \
      func_warning "remember to run \`$progname --finish$future_libdirs'"

    if test -n "$current_libdirs"; then
      # Maybe just do a dry run.
      $opt_dry_run && current_libdirs=" -n$current_libdirs"
      exec_cmd='$SHELL $progpath $preserve_args --finish$current_libdirs'
    else
      exit $EXIT_SUCCESS
    fi
}

test "$opt_mode" = install && func_mode_install ${1+"$@"}


# func_generate_dlsyms outputname originator pic_p
# Extract symbols from dlprefiles and create ${outputname}S.o with
# a dlpreopen symbol table.
func_generate_dlsyms ()
{
    $opt_debug
    my_outputname="$1"
    my_originator="$2"
    my_pic_p="${3-no}"
    my_prefix=`$ECHO "$my_originator" | sed 's%[^a-zA-Z0-9]%_%g'`
    my_dlsyms=

    if test -n "$dlfiles$dlprefiles" || test "$dlself" != no; then
      if test -n "$NM" && test -n "$global_symbol_pipe"; then
	my_dlsyms="${my_outputname}S.c"
      else
	func_error "not configured to extract global symbols from dlpreopened files"
      fi
    fi

    if test -n "$my_dlsyms"; then
      case $my_dlsyms in
      "") ;;
      *.c)
	# Discover the nlist of each of the dlfiles.
	nlist="$output_objdir/${my_outputname}.nm"

	func_show_eval "$RM $nlist ${nlist}S ${nlist}T"

	# Parse the name list into a source file.
	func_verbose "creating $output_objdir/$my_dlsyms"

	$opt_dry_run || $ECHO > "$output_objdir/$my_dlsyms" "\
/* $my_dlsyms - symbol resolution table for \`$my_outputname' dlsym emulation. */
/* Generated by $PROGRAM (GNU $PACKAGE$TIMESTAMP) $VERSION */

#ifdef __cplusplus
extern \"C\" {
#endif

#if defined(__GNUC__) && (((__GNUC__ == 4) && (__GNUC_MINOR__ >= 4)) || (__GNUC__ > 4))
#pragma GCC diagnostic ignored \"-Wstrict-prototypes\"
#endif

/* Keep this code in sync between libtool.m4, ltmain, lt_system.h, and tests.  */
#if defined(_WIN32) || defined(__CYGWIN__) || defined(_WIN32_WCE)
/* DATA imports from DLLs on WIN32 con't be const, because runtime
   relocations are performed -- see ld's documentation on pseudo-relocs.  */
# define LT_DLSYM_CONST
#elif defined(__osf__)
/* This system does not cope well with relocations in const data.  */
# define LT_DLSYM_CONST
#else
# define LT_DLSYM_CONST const
#endif

/* External symbol declarations for the compiler. */\
"

	if test "$dlself" = yes; then
	  func_verbose "generating symbol list for \`$output'"

	  $opt_dry_run || echo ': @PROGRAM@ ' > "$nlist"

	  # Add our own program objects to the symbol list.
	  progfiles=`$ECHO "$objs$old_deplibs" | $SP2NL | $SED "$lo2o" | $NL2SP`
	  for progfile in $progfiles; do
	    func_to_tool_file "$progfile" func_convert_file_msys_to_w32
	    func_verbose "extracting global C symbols from \`$func_to_tool_file_result'"
	    $opt_dry_run || eval "$NM $func_to_tool_file_result | $global_symbol_pipe >> '$nlist'"
	  done

	  if test -n "$exclude_expsyms"; then
	    $opt_dry_run || {
	      eval '$EGREP -v " ($exclude_expsyms)$" "$nlist" > "$nlist"T'
	      eval '$MV "$nlist"T "$nlist"'
	    }
	  fi

	  if test -n "$export_symbols_regex"; then
	    $opt_dry_run || {
	      eval '$EGREP -e "$export_symbols_regex" "$nlist" > "$nlist"T'
	      eval '$MV "$nlist"T "$nlist"'
	    }
	  fi

	  # Prepare the list of exported symbols
	  if test -z "$export_symbols"; then
	    export_symbols="$output_objdir/$outputname.exp"
	    $opt_dry_run || {
	      $RM $export_symbols
	      eval "${SED} -n -e '/^: @PROGRAM@ $/d' -e 's/^.* \(.*\)$/\1/p' "'< "$nlist" > "$export_symbols"'
	      case $host in
	      *cygwin* | *mingw* | *cegcc* )
                eval "echo EXPORTS "'> "$output_objdir/$outputname.def"'
                eval 'cat "$export_symbols" >> "$output_objdir/$outputname.def"'
	        ;;
	      esac
	    }
	  else
	    $opt_dry_run || {
	      eval "${SED} -e 's/\([].[*^$]\)/\\\\\1/g' -e 's/^/ /' -e 's/$/$/'"' < "$export_symbols" > "$output_objdir/$outputname.exp"'
	      eval '$GREP -f "$output_objdir/$outputname.exp" < "$nlist" > "$nlist"T'
	      eval '$MV "$nlist"T "$nlist"'
	      case $host in
	        *cygwin* | *mingw* | *cegcc* )
	          eval "echo EXPORTS "'> "$output_objdir/$outputname.def"'
	          eval 'cat "$nlist" >> "$output_objdir/$outputname.def"'
	          ;;
	      esac
	    }
	  fi
	fi

	for dlprefile in $dlprefiles; do
	  func_verbose "extracting global C symbols from \`$dlprefile'"
	  func_basename "$dlprefile"
	  name="$func_basename_result"
          case $host in
	    *cygwin* | *mingw* | *cegcc* )
	      # if an import library, we need to obtain dlname
	      if func_win32_import_lib_p "$dlprefile"; then
	        func_tr_sh "$dlprefile"
	        eval "curr_lafile=\$libfile_$func_tr_sh_result"
	        dlprefile_dlbasename=""
	        if test -n "$curr_lafile" && func_lalib_p "$curr_lafile"; then
	          # Use subshell, to avoid clobbering current variable values
	          dlprefile_dlname=`source "$curr_lafile" && echo "$dlname"`
	          if test -n "$dlprefile_dlname" ; then
	            func_basename "$dlprefile_dlname"
	            dlprefile_dlbasename="$func_basename_result"
	          else
	            # no lafile. user explicitly requested -dlpreopen <import library>.
	            $sharedlib_from_linklib_cmd "$dlprefile"
	            dlprefile_dlbasename=$sharedlib_from_linklib_result
	          fi
	        fi
	        $opt_dry_run || {
	          if test -n "$dlprefile_dlbasename" ; then
	            eval '$ECHO ": $dlprefile_dlbasename" >> "$nlist"'
	          else
	            func_warning "Could not compute DLL name from $name"
	            eval '$ECHO ": $name " >> "$nlist"'
	          fi
	          func_to_tool_file "$dlprefile" func_convert_file_msys_to_w32
	          eval "$NM \"$func_to_tool_file_result\" 2>/dev/null | $global_symbol_pipe |
	            $SED -e '/I __imp/d' -e 's/I __nm_/D /;s/_nm__//' >> '$nlist'"
	        }
	      else # not an import lib
	        $opt_dry_run || {
	          eval '$ECHO ": $name " >> "$nlist"'
	          func_to_tool_file "$dlprefile" func_convert_file_msys_to_w32
	          eval "$NM \"$func_to_tool_file_result\" 2>/dev/null | $global_symbol_pipe >> '$nlist'"
	        }
	      fi
	    ;;
	    *)
	      $opt_dry_run || {
	        eval '$ECHO ": $name " >> "$nlist"'
	        func_to_tool_file "$dlprefile" func_convert_file_msys_to_w32
	        eval "$NM \"$func_to_tool_file_result\" 2>/dev/null | $global_symbol_pipe >> '$nlist'"
	      }
	    ;;
          esac
	done

	$opt_dry_run || {
	  # Make sure we have at least an empty file.
	  test -f "$nlist" || : > "$nlist"

	  if test -n "$exclude_expsyms"; then
	    $EGREP -v " ($exclude_expsyms)$" "$nlist" > "$nlist"T
	    $MV "$nlist"T "$nlist"
	  fi

	  # Try sorting and uniquifying the output.
	  if $GREP -v "^: " < "$nlist" |
	      if sort -k 3 </dev/null >/dev/null 2>&1; then
		sort -k 3
	      else
		sort +2
	      fi |
	      uniq > "$nlist"S; then
	    :
	  else
	    $GREP -v "^: " < "$nlist" > "$nlist"S
	  fi

	  if test -f "$nlist"S; then
	    eval "$global_symbol_to_cdecl"' < "$nlist"S >> "$output_objdir/$my_dlsyms"'
	  else
	    echo '/* NONE */' >> "$output_objdir/$my_dlsyms"
	  fi

	  echo >> "$output_objdir/$my_dlsyms" "\

/* The mapping between symbol names and symbols.  */
typedef struct {
  const char *name;
  void *address;
} lt_dlsymlist;
extern LT_DLSYM_CONST lt_dlsymlist
lt_${my_prefix}_LTX_preloaded_symbols[];
LT_DLSYM_CONST lt_dlsymlist
lt_${my_prefix}_LTX_preloaded_symbols[] =
{\
  { \"$my_originator\", (void *) 0 },"

	  case $need_lib_prefix in
	  no)
	    eval "$global_symbol_to_c_name_address" < "$nlist" >> "$output_objdir/$my_dlsyms"
	    ;;
	  *)
	    eval "$global_symbol_to_c_name_address_lib_prefix" < "$nlist" >> "$output_objdir/$my_dlsyms"
	    ;;
	  esac
	  echo >> "$output_objdir/$my_dlsyms" "\
  {0, (void *) 0}
};

/* This works around a problem in FreeBSD linker */
#ifdef FREEBSD_WORKAROUND
static const void *lt_preloaded_setup() {
  return lt_${my_prefix}_LTX_preloaded_symbols;
}
#endif

#ifdef __cplusplus
}
#endif\
"
	} # !$opt_dry_run

	pic_flag_for_symtable=
	case "$compile_command " in
	*" -static "*) ;;
	*)
	  case $host in
	  # compiling the symbol table file with pic_flag works around
	  # a FreeBSD bug that causes programs to crash when -lm is
	  # linked before any other PIC object.  But we must not use
	  # pic_flag when linking with -static.  The problem exists in
	  # FreeBSD 2.2.6 and is fixed in FreeBSD 3.1.
	  *-*-freebsd2.*|*-*-freebsd3.0*|*-*-freebsdelf3.0*)
	    pic_flag_for_symtable=" $pic_flag -DFREEBSD_WORKAROUND" ;;
	  *-*-hpux*)
	    pic_flag_for_symtable=" $pic_flag"  ;;
	  *)
	    if test "X$my_pic_p" != Xno; then
	      pic_flag_for_symtable=" $pic_flag"
	    fi
	    ;;
	  esac
	  ;;
	esac
	symtab_cflags=
	for arg in $LTCFLAGS; do
	  case $arg in
	  -pie | -fpie | -fPIE) ;;
	  *) func_append symtab_cflags " $arg" ;;
	  esac
	done

	# Now compile the dynamic symbol file.
	func_show_eval '(cd $output_objdir && $LTCC$symtab_cflags -c$no_builtin_flag$pic_flag_for_symtable "$my_dlsyms")' 'exit $?'

	# Clean up the generated files.
	func_show_eval '$RM "$output_objdir/$my_dlsyms" "$nlist" "${nlist}S" "${nlist}T"'

	# Transform the symbol file into the correct name.
	symfileobj="$output_objdir/${my_outputname}S.$objext"
	case $host in
	*cygwin* | *mingw* | *cegcc* )
	  if test -f "$output_objdir/$my_outputname.def"; then
	    compile_command=`$ECHO "$compile_command" | $SED "s%@SYMFILE@%$output_objdir/$my_outputname.def $symfileobj%"`
	    finalize_command=`$ECHO "$finalize_command" | $SED "s%@SYMFILE@%$output_objdir/$my_outputname.def $symfileobj%"`
	  else
	    compile_command=`$ECHO "$compile_command" | $SED "s%@SYMFILE@%$symfileobj%"`
	    finalize_command=`$ECHO "$finalize_command" | $SED "s%@SYMFILE@%$symfileobj%"`
	  fi
	  ;;
	*)
	  compile_command=`$ECHO "$compile_command" | $SED "s%@SYMFILE@%$symfileobj%"`
	  finalize_command=`$ECHO "$finalize_command" | $SED "s%@SYMFILE@%$symfileobj%"`
	  ;;
	esac
	;;
      *)
	func_fatal_error "unknown suffix for \`$my_dlsyms'"
	;;
      esac
    else
      # We keep going just in case the user didn't refer to
      # lt_preloaded_symbols.  The linker will fail if global_symbol_pipe
      # really was required.

      # Nullify the symbol file.
      compile_command=`$ECHO "$compile_command" | $SED "s% @SYMFILE@%%"`
      finalize_command=`$ECHO "$finalize_command" | $SED "s% @SYMFILE@%%"`
    fi
}

# func_win32_libid arg
# return the library type of file 'arg'
#
# Need a lot of goo to handle *both* DLLs and import libs
# Has to be a shell function in order to 'eat' the argument
# that is supplied when $file_magic_command is called.
# Despite the name, also deal with 64 bit binaries.
func_win32_libid ()
{
  $opt_debug
  win32_libid_type="unknown"
  win32_fileres=`file -L $1 2>/dev/null`
  case $win32_fileres in
  *ar\ archive\ import\ library*) # definitely import
    win32_libid_type="x86 archive import"
    ;;
  *ar\ archive*) # could be an import, or static
    # Keep the egrep pattern in sync with the one in _LT_CHECK_MAGIC_METHOD.
    if eval $OBJDUMP -f $1 | $SED -e '10q' 2>/dev/null |
       $EGREP 'file format (pei*-i386(.*architecture: i386)?|pe-arm-wince|pe-x86-64)' >/dev/null; then
      func_to_tool_file "$1" func_convert_file_msys_to_w32
      win32_nmres=`eval $NM -f posix -A \"$func_to_tool_file_result\" |
	$SED -n -e '
	    1,100{
		/ I /{
		    s,.*,import,
		    p
		    q
		}
	    }'`
      case $win32_nmres in
      import*)  win32_libid_type="x86 archive import";;
      *)        win32_libid_type="x86 archive static";;
      esac
    fi
    ;;
  *DLL*)
    win32_libid_type="x86 DLL"
    ;;
  *executable*) # but shell scripts are "executable" too...
    case $win32_fileres in
    *MS\ Windows\ PE\ Intel*)
      win32_libid_type="x86 DLL"
      ;;
    esac
    ;;
  esac
  $ECHO "$win32_libid_type"
}

# func_cygming_dll_for_implib ARG
#
# Platform-specific function to extract the
# name of the DLL associated with the specified
# import library ARG.
# Invoked by eval'ing the libtool variable
#    $sharedlib_from_linklib_cmd
# Result is available in the variable
#    $sharedlib_from_linklib_result
func_cygming_dll_for_implib ()
{
  $opt_debug
  sharedlib_from_linklib_result=`$DLLTOOL --identify-strict --identify "$1"`
}

# func_cygming_dll_for_implib_fallback_core SECTION_NAME LIBNAMEs
#
# The is the core of a fallback implementation of a
# platform-specific function to extract the name of the
# DLL associated with the specified import library LIBNAME.
#
# SECTION_NAME is either .idata$6 or .idata$7, depending
# on the platform and compiler that created the implib.
#
# Echos the name of the DLL associated with the
# specified import library.
func_cygming_dll_for_implib_fallback_core ()
{
  $opt_debug
  match_literal=`$ECHO "$1" | $SED "$sed_make_literal_regex"`
  $OBJDUMP -s --section "$1" "$2" 2>/dev/null |
    $SED '/^Contents of section '"$match_literal"':/{
      # Place marker at beginning of archive member dllname section
      s/.*/====MARK====/
      p
      d
    }
    # These lines can sometimes be longer than 43 characters, but
    # are always uninteresting
    /:[	 ]*file format pe[i]\{,1\}-/d
    /^In archive [^:]*:/d
    # Ensure marker is printed
    /^====MARK====/p
    # Remove all lines with less than 43 characters
    /^.\{43\}/!d
    # From remaining lines, remove first 43 characters
    s/^.\{43\}//' |
    $SED -n '
      # Join marker and all lines until next marker into a single line
      /^====MARK====/ b para
      H
      $ b para
      b
      :para
      x
      s/\n//g
      # Remove the marker
      s/^====MARK====//
      # Remove trailing dots and whitespace
      s/[\. \t]*$//
      # Print
      /./p' |
    # we now have a list, one entry per line, of the stringified
    # contents of the appropriate section of all members of the
    # archive which possess that section. Heuristic: eliminate
    # all those which have a first or second character that is
    # a '.' (that is, objdump's representation of an unprintable
    # character.) This should work for all archives with less than
    # 0x302f exports -- but will fail for DLLs whose name actually
    # begins with a literal '.' or a single character followed by
    # a '.'.
    #
    # Of those that remain, print the first one.
    $SED -e '/^\./d;/^.\./d;q'
}

# func_cygming_gnu_implib_p ARG
# This predicate returns with zero status (TRUE) if
# ARG is a GNU/binutils-style import library. Returns
# with nonzero status (FALSE) otherwise.
func_cygming_gnu_implib_p ()
{
  $opt_debug
  func_to_tool_file "$1" func_convert_file_msys_to_w32
  func_cygming_gnu_implib_tmp=`$NM "$func_to_tool_file_result" | eval "$global_symbol_pipe" | $EGREP ' (_head_[A-Za-z0-9_]+_[ad]l*|[A-Za-z0-9_]+_[ad]l*_iname)$'`
  test -n "$func_cygming_gnu_implib_tmp"
}

# func_cygming_ms_implib_p ARG
# This predicate returns with zero status (TRUE) if
# ARG is an MS-style import library. Returns
# with nonzero status (FALSE) otherwise.
func_cygming_ms_implib_p ()
{
  $opt_debug
  func_to_tool_file "$1" func_convert_file_msys_to_w32
  func_cygming_ms_implib_tmp=`$NM "$func_to_tool_file_result" | eval "$global_symbol_pipe" | $GREP '_NULL_IMPORT_DESCRIPTOR'`
  test -n "$func_cygming_ms_implib_tmp"
}

# func_cygming_dll_for_implib_fallback ARG
# Platform-specific function to extract the
# name of the DLL associated with the specified
# import library ARG.
#
# This fallback implementation is for use when $DLLTOOL
# does not support the --identify-strict option.
# Invoked by eval'ing the libtool variable
#    $sharedlib_from_linklib_cmd
# Result is available in the variable
#    $sharedlib_from_linklib_result
func_cygming_dll_for_implib_fallback ()
{
  $opt_debug
  if func_cygming_gnu_implib_p "$1" ; then
    # binutils import library
    sharedlib_from_linklib_result=`func_cygming_dll_for_implib_fallback_core '.idata$7' "$1"`
  elif func_cygming_ms_implib_p "$1" ; then
    # ms-generated import library
    sharedlib_from_linklib_result=`func_cygming_dll_for_implib_fallback_core '.idata$6' "$1"`
  else
    # unknown
    sharedlib_from_linklib_result=""
  fi
}


# func_extract_an_archive dir oldlib
func_extract_an_archive ()
{
    $opt_debug
    f_ex_an_ar_dir="$1"; shift
    f_ex_an_ar_oldlib="$1"
    if test "$lock_old_archive_extraction" = yes; then
      lockfile=$f_ex_an_ar_oldlib.lock
      until $opt_dry_run || ln "$progpath" "$lockfile" 2>/dev/null; do
	func_echo "Waiting for $lockfile to be removed"
	sleep 2
      done
    fi
    func_show_eval "(cd \$f_ex_an_ar_dir && $AR x \"\$f_ex_an_ar_oldlib\")" \
		   'stat=$?; rm -f "$lockfile"; exit $stat'
    if test "$lock_old_archive_extraction" = yes; then
      $opt_dry_run || rm -f "$lockfile"
    fi
    if ($AR t "$f_ex_an_ar_oldlib" | sort | sort -uc >/dev/null 2>&1); then
     :
    else
      func_fatal_error "object name conflicts in archive: $f_ex_an_ar_dir/$f_ex_an_ar_oldlib"
    fi
}


# func_extract_archives gentop oldlib ...
func_extract_archives ()
{
    $opt_debug
    my_gentop="$1"; shift
    my_oldlibs=${1+"$@"}
    my_oldobjs=""
    my_xlib=""
    my_xabs=""
    my_xdir=""

    for my_xlib in $my_oldlibs; do
      # Extract the objects.
      case $my_xlib in
	[\\/]* | [A-Za-z]:[\\/]*) my_xabs="$my_xlib" ;;
	*) my_xabs=`pwd`"/$my_xlib" ;;
      esac
      func_basename "$my_xlib"
      my_xlib="$func_basename_result"
      my_xlib_u=$my_xlib
      while :; do
        case " $extracted_archives " in
	*" $my_xlib_u "*)
	  func_arith $extracted_serial + 1
	  extracted_serial=$func_arith_result
	  my_xlib_u=lt$extracted_serial-$my_xlib ;;
	*) break ;;
	esac
      done
      extracted_archives="$extracted_archives $my_xlib_u"
      my_xdir="$my_gentop/$my_xlib_u"

      func_mkdir_p "$my_xdir"

      case $host in
      *-darwin*)
	func_verbose "Extracting $my_xabs"
	# Do not bother doing anything if just a dry run
	$opt_dry_run || {
	  darwin_orig_dir=`pwd`
	  cd $my_xdir || exit $?
	  darwin_archive=$my_xabs
	  darwin_curdir=`pwd`
	  darwin_base_archive=`basename "$darwin_archive"`
	  darwin_arches=`$LIPO -info "$darwin_archive" 2>/dev/null | $GREP Architectures 2>/dev/null || true`
	  if test -n "$darwin_arches"; then
	    darwin_arches=`$ECHO "$darwin_arches" | $SED -e 's/.*are://'`
	    darwin_arch=
	    func_verbose "$darwin_base_archive has multiple architectures $darwin_arches"
	    for darwin_arch in  $darwin_arches ; do
	      func_mkdir_p "unfat-$$/${darwin_base_archive}-${darwin_arch}"
	      $LIPO -thin $darwin_arch -output "unfat-$$/${darwin_base_archive}-${darwin_arch}/${darwin_base_archive}" "${darwin_archive}"
	      cd "unfat-$$/${darwin_base_archive}-${darwin_arch}"
	      func_extract_an_archive "`pwd`" "${darwin_base_archive}"
	      cd "$darwin_curdir"
	      $RM "unfat-$$/${darwin_base_archive}-${darwin_arch}/${darwin_base_archive}"
	    done # $darwin_arches
            ## Okay now we've a bunch of thin objects, gotta fatten them up :)
	    darwin_filelist=`find unfat-$$ -type f -name \*.o -print -o -name \*.lo -print | $SED -e "$basename" | sort -u`
	    darwin_file=
	    darwin_files=
	    for darwin_file in $darwin_filelist; do
	      darwin_files=`find unfat-$$ -name $darwin_file -print | sort | $NL2SP`
	      $LIPO -create -output "$darwin_file" $darwin_files
	    done # $darwin_filelist
	    $RM -rf unfat-$$
	    cd "$darwin_orig_dir"
	  else
	    cd $darwin_orig_dir
	    func_extract_an_archive "$my_xdir" "$my_xabs"
	  fi # $darwin_arches
	} # !$opt_dry_run
	;;
      *)
        func_extract_an_archive "$my_xdir" "$my_xabs"
	;;
      esac
      my_oldobjs="$my_oldobjs "`find $my_xdir -name \*.$objext -print -o -name \*.lo -print | sort | $NL2SP`
    done

    func_extract_archives_result="$my_oldobjs"
}


# func_emit_wrapper [arg=no]
#
# Emit a libtool wrapper script on stdout.
# Don't directly open a file because we may want to
# incorporate the script contents within a cygwin/mingw
# wrapper executable.  Must ONLY be called from within
# func_mode_link because it depends on a number of variables
# set therein.
#
# ARG is the value that the WRAPPER_SCRIPT_BELONGS_IN_OBJDIR
# variable will take.  If 'yes', then the emitted script
# will assume that the directory in which it is stored is
# the $objdir directory.  This is a cygwin/mingw-specific
# behavior.
func_emit_wrapper ()
{
	func_emit_wrapper_arg1=${1-no}

	$ECHO "\
#! $SHELL

# $output - temporary wrapper script for $objdir/$outputname
# Generated by $PROGRAM (GNU $PACKAGE$TIMESTAMP) $VERSION
#
# The $output program cannot be directly executed until all the libtool
# libraries that it depends on are installed.
#
# This wrapper script should never be moved out of the build directory.
# If it is, it will not operate correctly.

# Sed substitution that helps us do robust quoting.  It backslashifies
# metacharacters that are still active within double-quoted strings.
sed_quote_subst='$sed_quote_subst'

# Be Bourne compatible
if test -n \"\${ZSH_VERSION+set}\" && (emulate sh) >/dev/null 2>&1; then
  emulate sh
  NULLCMD=:
  # Zsh 3.x and 4.x performs word splitting on \${1+\"\$@\"}, which
  # is contrary to our usage.  Disable this feature.
  alias -g '\${1+\"\$@\"}'='\"\$@\"'
  setopt NO_GLOB_SUBST
else
  case \`(set -o) 2>/dev/null\` in *posix*) set -o posix;; esac
fi
BIN_SH=xpg4; export BIN_SH # for Tru64
DUALCASE=1; export DUALCASE # for MKS sh

# The HP-UX ksh and POSIX shell print the target directory to stdout
# if CDPATH is set.
(unset CDPATH) >/dev/null 2>&1 && unset CDPATH

relink_command=\"$relink_command\"

# This environment variable determines our operation mode.
if test \"\$libtool_install_magic\" = \"$magic\"; then
  # install mode needs the following variables:
  generated_by_libtool_version='$macro_version'
  notinst_deplibs='$notinst_deplibs'
else
  # When we are sourced in execute mode, \$file and \$ECHO are already set.
  if test \"\$libtool_execute_magic\" != \"$magic\"; then
    file=\"\$0\""

    qECHO=`$ECHO "$ECHO" | $SED "$sed_quote_subst"`
    $ECHO "\

# A function that is used when there is no print builtin or printf.
func_fallback_echo ()
{
  eval 'cat <<_LTECHO_EOF
\$1
_LTECHO_EOF'
}
    ECHO=\"$qECHO\"
  fi

# Very basic option parsing. These options are (a) specific to
# the libtool wrapper, (b) are identical between the wrapper
# /script/ and the wrapper /executable/ which is used only on
# windows platforms, and (c) all begin with the string "--lt-"
# (application programs are unlikely to have options which match
# this pattern).
#
# There are only two supported options: --lt-debug and
# --lt-dump-script. There is, deliberately, no --lt-help.
#
# The first argument to this parsing function should be the
# script's $0 value, followed by "$@".
lt_option_debug=
func_parse_lt_options ()
{
  lt_script_arg0=\$0
  shift
  for lt_opt
  do
    case \"\$lt_opt\" in
    --lt-debug) lt_option_debug=1 ;;
    --lt-dump-script)
        lt_dump_D=\`\$ECHO \"X\$lt_script_arg0\" | $SED -e 's/^X//' -e 's%/[^/]*$%%'\`
        test \"X\$lt_dump_D\" = \"X\$lt_script_arg0\" && lt_dump_D=.
        lt_dump_F=\`\$ECHO \"X\$lt_script_arg0\" | $SED -e 's/^X//' -e 's%^.*/%%'\`
        cat \"\$lt_dump_D/\$lt_dump_F\"
        exit 0
      ;;
    --lt-*)
        \$ECHO \"Unrecognized --lt- option: '\$lt_opt'\" 1>&2
        exit 1
      ;;
    esac
  done

  # Print the debug banner immediately:
  if test -n \"\$lt_option_debug\"; then
    echo \"${outputname}:${output}:\${LINENO}: libtool wrapper (GNU $PACKAGE$TIMESTAMP) $VERSION\" 1>&2
  fi
}

# Used when --lt-debug. Prints its arguments to stdout
# (redirection is the responsibility of the caller)
func_lt_dump_args ()
{
  lt_dump_args_N=1;
  for lt_arg
  do
    \$ECHO \"${outputname}:${output}:\${LINENO}: newargv[\$lt_dump_args_N]: \$lt_arg\"
    lt_dump_args_N=\`expr \$lt_dump_args_N + 1\`
  done
}

# Core function for launching the target application
func_exec_program_core ()
{
"
  case $host in
  # Backslashes separate directories on plain windows
  *-*-mingw | *-*-os2* | *-cegcc*)
    $ECHO "\
      if test -n \"\$lt_option_debug\"; then
        \$ECHO \"${outputname}:${output}:\${LINENO}: newargv[0]: \$progdir\\\\\$program\" 1>&2
        func_lt_dump_args \${1+\"\$@\"} 1>&2
      fi
      exec \"\$progdir\\\\\$program\" \${1+\"\$@\"}
"
    ;;

  *)
    $ECHO "\
      if test -n \"\$lt_option_debug\"; then
        \$ECHO \"${outputname}:${output}:\${LINENO}: newargv[0]: \$progdir/\$program\" 1>&2
        func_lt_dump_args \${1+\"\$@\"} 1>&2
      fi
      exec \"\$progdir/\$program\" \${1+\"\$@\"}
"
    ;;
  esac
  $ECHO "\
      \$ECHO \"\$0: cannot exec \$program \$*\" 1>&2
      exit 1
}

# A function to encapsulate launching the target application
# Strips options in the --lt-* namespace from \$@ and
# launches target application with the remaining arguments.
func_exec_program ()
{
  case \" \$* \" in
  *\\ --lt-*)
    for lt_wr_arg
    do
      case \$lt_wr_arg in
      --lt-*) ;;
      *) set x \"\$@\" \"\$lt_wr_arg\"; shift;;
      esac
      shift
    done ;;
  esac
  func_exec_program_core \${1+\"\$@\"}
}

  # Parse options
  func_parse_lt_options \"\$0\" \${1+\"\$@\"}

  # Find the directory that this script lives in.
  thisdir=\`\$ECHO \"\$file\" | $SED 's%/[^/]*$%%'\`
  test \"x\$thisdir\" = \"x\$file\" && thisdir=.

  # Follow symbolic links until we get to the real thisdir.
  file=\`ls -ld \"\$file\" | $SED -n 's/.*-> //p'\`
  while test -n \"\$file\"; do
    destdir=\`\$ECHO \"\$file\" | $SED 's%/[^/]*\$%%'\`

    # If there was a directory component, then change thisdir.
    if test \"x\$destdir\" != \"x\$file\"; then
      case \"\$destdir\" in
      [\\\\/]* | [A-Za-z]:[\\\\/]*) thisdir=\"\$destdir\" ;;
      *) thisdir=\"\$thisdir/\$destdir\" ;;
      esac
    fi

    file=\`\$ECHO \"\$file\" | $SED 's%^.*/%%'\`
    file=\`ls -ld \"\$thisdir/\$file\" | $SED -n 's/.*-> //p'\`
  done

  # Usually 'no', except on cygwin/mingw when embedded into
  # the cwrapper.
  WRAPPER_SCRIPT_BELONGS_IN_OBJDIR=$func_emit_wrapper_arg1
  if test \"\$WRAPPER_SCRIPT_BELONGS_IN_OBJDIR\" = \"yes\"; then
    # special case for '.'
    if test \"\$thisdir\" = \".\"; then
      thisdir=\`pwd\`
    fi
    # remove .libs from thisdir
    case \"\$thisdir\" in
    *[\\\\/]$objdir ) thisdir=\`\$ECHO \"\$thisdir\" | $SED 's%[\\\\/][^\\\\/]*$%%'\` ;;
    $objdir )   thisdir=. ;;
    esac
  fi

  # Try to get the absolute directory name.
  absdir=\`cd \"\$thisdir\" && pwd\`
  test -n \"\$absdir\" && thisdir=\"\$absdir\"
"

	if test "$fast_install" = yes; then
	  $ECHO "\
  program=lt-'$outputname'$exeext
  progdir=\"\$thisdir/$objdir\"

  if test ! -f \"\$progdir/\$program\" ||
     { file=\`ls -1dt \"\$progdir/\$program\" \"\$progdir/../\$program\" 2>/dev/null | ${SED} 1q\`; \\
       test \"X\$file\" != \"X\$progdir/\$program\"; }; then

    file=\"\$\$-\$program\"

    if test ! -d \"\$progdir\"; then
      $MKDIR \"\$progdir\"
    else
      $RM \"\$progdir/\$file\"
    fi"

	  $ECHO "\

    # relink executable if necessary
    if test -n \"\$relink_command\"; then
      if relink_command_output=\`eval \$relink_command 2>&1\`; then :
      else
	$ECHO \"\$relink_command_output\" >&2
	$RM \"\$progdir/\$file\"
	exit 1
      fi
    fi

    $MV \"\$progdir/\$file\" \"\$progdir/\$program\" 2>/dev/null ||
    { $RM \"\$progdir/\$program\";
      $MV \"\$progdir/\$file\" \"\$progdir/\$program\"; }
    $RM \"\$progdir/\$file\"
  fi"
	else
	  $ECHO "\
  program='$outputname'
  progdir=\"\$thisdir/$objdir\"
"
	fi

	$ECHO "\

  if test -f \"\$progdir/\$program\"; then"

	# fixup the dll searchpath if we need to.
	#
	# Fix the DLL searchpath if we need to.  Do this before prepending
	# to shlibpath, because on Windows, both are PATH and uninstalled
	# libraries must come first.
	if test -n "$dllsearchpath"; then
	  $ECHO "\
    # Add the dll search path components to the executable PATH
    PATH=$dllsearchpath:\$PATH
"
	fi

	# Export our shlibpath_var if we have one.
	if test "$shlibpath_overrides_runpath" = yes && test -n "$shlibpath_var" && test -n "$temp_rpath"; then
	  $ECHO "\
    # Add our own library path to $shlibpath_var
    $shlibpath_var=\"$temp_rpath\$$shlibpath_var\"

    # Some systems cannot cope with colon-terminated $shlibpath_var
    # The second colon is a workaround for a bug in BeOS R4 sed
    $shlibpath_var=\`\$ECHO \"\$$shlibpath_var\" | $SED 's/::*\$//'\`

    export $shlibpath_var
"
	fi

	$ECHO "\
    if test \"\$libtool_execute_magic\" != \"$magic\"; then
      # Run the actual program with our arguments.
      func_exec_program \${1+\"\$@\"}
    fi
  else
    # The program doesn't exist.
    \$ECHO \"\$0: error: \\\`\$progdir/\$program' does not exist\" 1>&2
    \$ECHO \"This script is just a wrapper for \$program.\" 1>&2
    \$ECHO \"See the $PACKAGE documentation for more information.\" 1>&2
    exit 1
  fi
fi\
"
}


# func_emit_cwrapperexe_src
# emit the source code for a wrapper executable on stdout
# Must ONLY be called from within func_mode_link because
# it depends on a number of variable set therein.
func_emit_cwrapperexe_src ()
{
	cat <<EOF

/* $cwrappersource - temporary wrapper executable for $objdir/$outputname
   Generated by $PROGRAM (GNU $PACKAGE$TIMESTAMP) $VERSION

   The $output program cannot be directly executed until all the libtool
   libraries that it depends on are installed.

   This wrapper executable should never be moved out of the build directory.
   If it is, it will not operate correctly.
*/
EOF
	    cat <<"EOF"
#ifdef _MSC_VER
# define _CRT_SECURE_NO_DEPRECATE 1
#endif
#include <stdio.h>
#include <stdlib.h>
#ifdef _MSC_VER
# include <direct.h>
# include <process.h>
# include <io.h>
#else
# include <unistd.h>
# include <stdint.h>
# ifdef __CYGWIN__
#  include <io.h>
# endif
#endif
#include <malloc.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

/* declarations of non-ANSI functions */
#if defined(__MINGW32__)
# ifdef __STRICT_ANSI__
int _putenv (const char *);
# endif
#elif defined(__CYGWIN__)
# ifdef __STRICT_ANSI__
char *realpath (const char *, char *);
int putenv (char *);
int setenv (const char *, const char *, int);
# endif
/* #elif defined (other platforms) ... */
#endif

/* portability defines, excluding path handling macros */
#if defined(_MSC_VER)
# define setmode _setmode
# define stat    _stat
# define chmod   _chmod
# define getcwd  _getcwd
# define putenv  _putenv
# define S_IXUSR _S_IEXEC
# ifndef _INTPTR_T_DEFINED
#  define _INTPTR_T_DEFINED
#  define intptr_t int
# endif
#elif defined(__MINGW32__)
# define setmode _setmode
# define stat    _stat
# define chmod   _chmod
# define getcwd  _getcwd
# define putenv  _putenv
#elif defined(__CYGWIN__)
# define HAVE_SETENV
# define FOPEN_WB "wb"
/* #elif defined (other platforms) ... */
#endif

#if defined(PATH_MAX)
# define LT_PATHMAX PATH_MAX
#elif defined(MAXPATHLEN)
# define LT_PATHMAX MAXPATHLEN
#else
# define LT_PATHMAX 1024
#endif

#ifndef S_IXOTH
# define S_IXOTH 0
#endif
#ifndef S_IXGRP
# define S_IXGRP 0
#endif

/* path handling portability macros */
#ifndef DIR_SEPARATOR
# define DIR_SEPARATOR '/'
# define PATH_SEPARATOR ':'
#endif

#if defined (_WIN32) || defined (__MSDOS__) || defined (__DJGPP__) || \
  defined (__OS2__)
# define HAVE_DOS_BASED_FILE_SYSTEM
# define FOPEN_WB "wb"
# ifndef DIR_SEPARATOR_2
#  define DIR_SEPARATOR_2 '\\'
# endif
# ifndef PATH_SEPARATOR_2
#  define PATH_SEPARATOR_2 ';'
# endif
#endif

#ifndef DIR_SEPARATOR_2
# define IS_DIR_SEPARATOR(ch) ((ch) == DIR_SEPARATOR)
#else /* DIR_SEPARATOR_2 */
# define IS_DIR_SEPARATOR(ch) \
	(((ch) == DIR_SEPARATOR) || ((ch) == DIR_SEPARATOR_2))
#endif /* DIR_SEPARATOR_2 */

#ifndef PATH_SEPARATOR_2
# define IS_PATH_SEPARATOR(ch) ((ch) == PATH_SEPARATOR)
#else /* PATH_SEPARATOR_2 */
# define IS_PATH_SEPARATOR(ch) ((ch) == PATH_SEPARATOR_2)
#endif /* PATH_SEPARATOR_2 */

#ifndef FOPEN_WB
# define FOPEN_WB "w"
#endif
#ifndef _O_BINARY
# define _O_BINARY 0
#endif

#define XMALLOC(type, num)      ((type *) xmalloc ((num) * sizeof(type)))
#define XFREE(stale) do { \
  if (stale) { free ((void *) stale); stale = 0; } \
} while (0)

#if defined(LT_DEBUGWRAPPER)
static int lt_debug = 1;
#else
static int lt_debug = 0;
#endif

const char *program_name = "libtool-wrapper"; /* in case xstrdup fails */

void *xmalloc (size_t num);
char *xstrdup (const char *string);
const char *base_name (const char *name);
char *find_executable (const char *wrapper);
char *chase_symlinks (const char *pathspec);
int make_executable (const char *path);
int check_executable (const char *path);
char *strendzap (char *str, const char *pat);
void lt_debugprintf (const char *file, int line, const char *fmt, ...);
void lt_fatal (const char *file, int line, const char *message, ...);
static const char *nonnull (const char *s);
static const char *nonempty (const char *s);
void lt_setenv (const char *name, const char *value);
char *lt_extend_str (const char *orig_value, const char *add, int to_end);
void lt_update_exe_path (const char *name, const char *value);
void lt_update_lib_path (const char *name, const char *value);
char **prepare_spawn (char **argv);
void lt_dump_script (FILE *f);
EOF

	    cat <<EOF
volatile const char * MAGIC_EXE = "$magic_exe";
const char * LIB_PATH_VARNAME = "$shlibpath_var";
EOF

	    if test "$shlibpath_overrides_runpath" = yes && test -n "$shlibpath_var" && test -n "$temp_rpath"; then
              func_to_host_path "$temp_rpath"
	      cat <<EOF
const char * LIB_PATH_VALUE   = "$func_to_host_path_result";
EOF
	    else
	      cat <<"EOF"
const char * LIB_PATH_VALUE   = "";
EOF
	    fi

	    if test -n "$dllsearchpath"; then
              func_to_host_path "$dllsearchpath:"
	      cat <<EOF
const char * EXE_PATH_VARNAME = "PATH";
const char * EXE_PATH_VALUE   = "$func_to_host_path_result";
EOF
	    else
	      cat <<"EOF"
const char * EXE_PATH_VARNAME = "";
const char * EXE_PATH_VALUE   = "";
EOF
	    fi

	    if test "$fast_install" = yes; then
	      cat <<EOF
const char * TARGET_PROGRAM_NAME = "lt-$outputname"; /* hopefully, no .exe */
EOF
	    else
	      cat <<EOF
const char * TARGET_PROGRAM_NAME = "$outputname"; /* hopefully, no .exe */
EOF
	    fi


	    cat <<"EOF"

#define LTWRAPPER_OPTION_PREFIX         "--lt-"

static const char *ltwrapper_option_prefix = LTWRAPPER_OPTION_PREFIX;
static const char *dumpscript_opt       = LTWRAPPER_OPTION_PREFIX "dump-script";
static const char *debug_opt            = LTWRAPPER_OPTION_PREFIX "debug";

int
main (int argc, char *argv[])
{
  char **newargz;
  int  newargc;
  char *tmp_pathspec;
  char *actual_cwrapper_path;
  char *actual_cwrapper_name;
  char *target_name;
  char *lt_argv_zero;
  intptr_t rval = 127;

  int i;

  program_name = (char *) xstrdup (base_name (argv[0]));
  newargz = XMALLOC (char *, argc + 1);

  /* very simple arg parsing; don't want to rely on getopt
   * also, copy all non cwrapper options to newargz, except
   * argz[0], which is handled differently
   */
  newargc=0;
  for (i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], dumpscript_opt) == 0)
	{
EOF
	    case "$host" in
	      *mingw* | *cygwin* )
		# make stdout use "unix" line endings
		echo "          setmode(1,_O_BINARY);"
		;;
	      esac

	    cat <<"EOF"
	  lt_dump_script (stdout);
	  return 0;
	}
      if (strcmp (argv[i], debug_opt) == 0)
	{
          lt_debug = 1;
          continue;
	}
      if (strcmp (argv[i], ltwrapper_option_prefix) == 0)
        {
          /* however, if there is an option in the LTWRAPPER_OPTION_PREFIX
             namespace, but it is not one of the ones we know about and
             have already dealt with, above (inluding dump-script), then
             report an error. Otherwise, targets might begin to believe
             they are allowed to use options in the LTWRAPPER_OPTION_PREFIX
             namespace. The first time any user complains about this, we'll
             need to make LTWRAPPER_OPTION_PREFIX a configure-time option
             or a configure.ac-settable value.
           */
          lt_fatal (__FILE__, __LINE__,
		    "unrecognized %s option: '%s'",
                    ltwrapper_option_prefix, argv[i]);
        }
      /* otherwise ... */
      newargz[++newargc] = xstrdup (argv[i]);
    }
  newargz[++newargc] = NULL;

EOF
	    cat <<EOF
  /* The GNU banner must be the first non-error debug message */
  lt_debugprintf (__FILE__, __LINE__, "libtool wrapper (GNU $PACKAGE$TIMESTAMP) $VERSION\n");
EOF
	    cat <<"EOF"
  lt_debugprintf (__FILE__, __LINE__, "(main) argv[0]: %s\n", argv[0]);
  lt_debugprintf (__FILE__, __LINE__, "(main) program_name: %s\n", program_name);

  tmp_pathspec = find_executable (argv[0]);
  if (tmp_pathspec == NULL)
    lt_fatal (__FILE__, __LINE__, "couldn't find %s", argv[0]);
  lt_debugprintf (__FILE__, __LINE__,
                  "(main) found exe (before symlink chase) at: %s\n",
		  tmp_pathspec);

  actual_cwrapper_path = chase_symlinks (tmp_pathspec);
  lt_debugprintf (__FILE__, __LINE__,
                  "(main) found exe (after symlink chase) at: %s\n",
		  actual_cwrapper_path);
  XFREE (tmp_pathspec);

  actual_cwrapper_name = xstrdup (base_name (actual_cwrapper_path));
  strendzap (actual_cwrapper_path, actual_cwrapper_name);

  /* wrapper name transforms */
  strendzap (actual_cwrapper_name, ".exe");
  tmp_pathspec = lt_extend_str (actual_cwrapper_name, ".exe", 1);
  XFREE (actual_cwrapper_name);
  actual_cwrapper_name = tmp_pathspec;
  tmp_pathspec = 0;

  /* target_name transforms -- use actual target program name; might have lt- prefix */
  target_name = xstrdup (base_name (TARGET_PROGRAM_NAME));
  strendzap (target_name, ".exe");
  tmp_pathspec = lt_extend_str (target_name, ".exe", 1);
  XFREE (target_name);
  target_name = tmp_pathspec;
  tmp_pathspec = 0;

  lt_debugprintf (__FILE__, __LINE__,
		  "(main) libtool target name: %s\n",
		  target_name);
EOF

	    cat <<EOF
  newargz[0] =
    XMALLOC (char, (strlen (actual_cwrapper_path) +
		    strlen ("$objdir") + 1 + strlen (actual_cwrapper_name) + 1));
  strcpy (newargz[0], actual_cwrapper_path);
  strcat (newargz[0], "$objdir");
  strcat (newargz[0], "/");
EOF

	    cat <<"EOF"
  /* stop here, and copy so we don't have to do this twice */
  tmp_pathspec = xstrdup (newargz[0]);

  /* do NOT want the lt- prefix here, so use actual_cwrapper_name */
  strcat (newargz[0], actual_cwrapper_name);

  /* DO want the lt- prefix here if it exists, so use target_name */
  lt_argv_zero = lt_extend_str (tmp_pathspec, target_name, 1);
  XFREE (tmp_pathspec);
  tmp_pathspec = NULL;
EOF

	    case $host_os in
	      mingw*)
	    cat <<"EOF"
  {
    char* p;
    while ((p = strchr (newargz[0], '\\')) != NULL)
      {
	*p = '/';
      }
    while ((p = strchr (lt_argv_zero, '\\')) != NULL)
      {
	*p = '/';
      }
  }
EOF
	    ;;
	    esac

	    cat <<"EOF"
  XFREE (target_name);
  XFREE (actual_cwrapper_path);
  XFREE (actual_cwrapper_name);

  lt_setenv ("BIN_SH", "xpg4"); /* for Tru64 */
  lt_setenv ("DUALCASE", "1");  /* for MSK sh */
  /* Update the DLL searchpath.  EXE_PATH_VALUE ($dllsearchpath) must
     be prepended before (that is, appear after) LIB_PATH_VALUE ($temp_rpath)
     because on Windows, both *_VARNAMEs are PATH but uninstalled
     libraries must come first. */
  lt_update_exe_path (EXE_PATH_VARNAME, EXE_PATH_VALUE);
  lt_update_lib_path (LIB_PATH_VARNAME, LIB_PATH_VALUE);

  lt_debugprintf (__FILE__, __LINE__, "(main) lt_argv_zero: %s\n",
		  nonnull (lt_argv_zero));
  for (i = 0; i < newargc; i++)
    {
      lt_debugprintf (__FILE__, __LINE__, "(main) newargz[%d]: %s\n",
		      i, nonnull (newargz[i]));
    }

EOF

	    case $host_os in
	      mingw*)
		cat <<"EOF"
  /* execv doesn't actually work on mingw as expected on unix */
  newargz = prepare_spawn (newargz);
  rval = _spawnv (_P_WAIT, lt_argv_zero, (const char * const *) newargz);
  if (rval == -1)
    {
      /* failed to start process */
      lt_debugprintf (__FILE__, __LINE__,
		      "(main) failed to launch target \"%s\": %s\n",
		      lt_argv_zero, nonnull (strerror (errno)));
      return 127;
    }
  return rval;
EOF
		;;
	      *)
		cat <<"EOF"
  execv (lt_argv_zero, newargz);
  return rval; /* =127, but avoids unused variable warning */
EOF
		;;
	    esac

	    cat <<"EOF"
}

void *
xmalloc (size_t num)
{
  void *p = (void *) malloc (num);
  if (!p)
    lt_fatal (__FILE__, __LINE__, "memory exhausted");

  return p;
}

char *
xstrdup (const char *string)
{
  return string ? strcpy ((char *) xmalloc (strlen (string) + 1),
			  string) : NULL;
}

const char *
base_name (const char *name)
{
  const char *base;

#if defined (HAVE_DOS_BASED_FILE_SYSTEM)
  /* Skip over the disk name in MSDOS pathnames. */
  if (isalpha ((unsigned char) name[0]) && name[1] == ':')
    name += 2;
#endif

  for (base = name; *name; name++)
    if (IS_DIR_SEPARATOR (*name))
      base = name + 1;
  return base;
}

int
check_executable (const char *path)
{
  struct stat st;

  lt_debugprintf (__FILE__, __LINE__, "(check_executable): %s\n",
                  nonempty (path));
  if ((!path) || (!*path))
    return 0;

  if ((stat (path, &st) >= 0)
      && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
    return 1;
  else
    return 0;
}

int
make_executable (const char *path)
{
  int rval = 0;
  struct stat st;

  lt_debugprintf (__FILE__, __LINE__, "(make_executable): %s\n",
                  nonempty (path));
  if ((!path) || (!*path))
    return 0;

  if (stat (path, &st) >= 0)
    {
      rval = chmod (path, st.st_mode | S_IXOTH | S_IXGRP | S_IXUSR);
    }
  return rval;
}

/* Searches for the full path of the wrapper.  Returns
   newly allocated full path name if found, NULL otherwise
   Does not chase symlinks, even on platforms that support them.
*/
char *
find_executable (const char *wrapper)
{
  int has_slash = 0;
  const char *p;
  const char *p_next;
  /* static buffer for getcwd */
  char tmp[LT_PATHMAX + 1];
  int tmp_len;
  char *concat_name;

  lt_debugprintf (__FILE__, __LINE__, "(find_executable): %s\n",
                  nonempty (wrapper));

  if ((wrapper == NULL) || (*wrapper == '\0'))
    return NULL;

  /* Absolute path? */
#if defined (HAVE_DOS_BASED_FILE_SYSTEM)
  if (isalpha ((unsigned char) wrapper[0]) && wrapper[1] == ':')
    {
      concat_name = xstrdup (wrapper);
      if (check_executable (concat_name))
	return concat_name;
      XFREE (concat_name);
    }
  else
    {
#endif
      if (IS_DIR_SEPARATOR (wrapper[0]))
	{
	  concat_name = xstrdup (wrapper);
	  if (check_executable (concat_name))
	    return concat_name;
	  XFREE (concat_name);
	}
#if defined (HAVE_DOS_BASED_FILE_SYSTEM)
    }
#endif

  for (p = wrapper; *p; p++)
    if (*p == '/')
      {
	has_slash = 1;
	break;
      }
  if (!has_slash)
    {
      /* no slashes; search PATH */
      const char *path = getenv ("PATH");
      if (path != NULL)
	{
	  for (p = path; *p; p = p_next)
	    {
	      const char *q;
	      size_t p_len;
	      for (q = p; *q; q++)
		if (IS_PATH_SEPARATOR (*q))
		  break;
	      p_len = q - p;
	      p_next = (*q == '\0' ? q : q + 1);
	      if (p_len == 0)
		{
		  /* empty path: current directory */
		  if (getcwd (tmp, LT_PATHMAX) == NULL)
		    lt_fatal (__FILE__, __LINE__, "getcwd failed: %s",
                              nonnull (strerror (errno)));
		  tmp_len = strlen (tmp);
		  concat_name =
		    XMALLOC (char, tmp_len + 1 + strlen (wrapper) + 1);
		  memcpy (concat_name, tmp, tmp_len);
		  concat_name[tmp_len] = '/';
		  strcpy (concat_name + tmp_len + 1, wrapper);
		}
	      else
		{
		  concat_name =
		    XMALLOC (char, p_len + 1 + strlen (wrapper) + 1);
		  memcpy (concat_name, p, p_len);
		  concat_name[p_len] = '/';
		  strcpy (concat_name + p_len + 1, wrapper);
		}
	      if (check_executable (concat_name))
		return concat_name;
	      XFREE (concat_name);
	    }
	}
      /* not found in PATH; assume curdir */
    }
  /* Relative path | not found in path: prepend cwd */
  if (getcwd (tmp, LT_PATHMAX) == NULL)
    lt_fatal (__FILE__, __LINE__, "getcwd failed: %s",
              nonnull (strerror (errno)));
  tmp_len = strlen (tmp);
  concat_name = XMALLOC (char, tmp_len + 1 + strlen (wrapper) + 1);
  memcpy (concat_name, tmp, tmp_len);
  concat_name[tmp_len] = '/';
  strcpy (concat_name + tmp_len + 1, wrapper);

  if (check_executable (concat_name))
    return concat_name;
  XFREE (concat_name);
  return NULL;
}

char *
chase_symlinks (const char *pathspec)
{
#ifndef S_ISLNK
  return xstrdup (pathspec);
#else
  char buf[LT_PATHMAX];
  struct stat s;
  char *tmp_pathspec = xstrdup (pathspec);
  char *p;
  int has_symlinks = 0;
  while (strlen (tmp_pathspec) && !has_symlinks)
    {
      lt_debugprintf (__FILE__, __LINE__,
		      "checking path component for symlinks: %s\n",
		      tmp_pathspec);
      if (lstat (tmp_pathspec, &s) == 0)
	{
	  if (S_ISLNK (s.st_mode) != 0)
	    {
	      has_symlinks = 1;
	      break;
	    }

	  /* search backwards for last DIR_SEPARATOR */
	  p = tmp_pathspec + strlen (tmp_pathspec) - 1;
	  while ((p > tmp_pathspec) && (!IS_DIR_SEPARATOR (*p)))
	    p--;
	  if ((p == tmp_pathspec) && (!IS_DIR_SEPARATOR (*p)))
	    {
	      /* no more DIR_SEPARATORS left */
	      break;
	    }
	  *p = '\0';
	}
      else
	{
	  lt_fatal (__FILE__, __LINE__,
		    "error accessing file \"%s\": %s",
		    tmp_pathspec, nonnull (strerror (errno)));
	}
    }
  XFREE (tmp_pathspec);

  if (!has_symlinks)
    {
      return xstrdup (pathspec);
    }

  tmp_pathspec = realpath (pathspec, buf);
  if (tmp_pathspec == 0)
    {
      lt_fatal (__FILE__, __LINE__,
		"could not follow symlinks for %s", pathspec);
    }
  return xstrdup (tmp_pathspec);
#endif
}

char *
strendzap (char *str, const char *pat)
{
  size_t len, patlen;

  assert (str != NULL);
  assert (pat != NULL);

  len = strlen (str);
  patlen = strlen (pat);

  if (patlen <= len)
    {
      str += len - patlen;
      if (strcmp (str, pat) == 0)
	*str = '\0';
    }
  return str;
}

void
lt_debugprintf (const char *file, int line, const char *fmt, ...)
{
  va_list args;
  if (lt_debug)
    {
      (void) fprintf (stderr, "%s:%s:%d: ", program_name, file, line);
      va_start (args, fmt);
      (void) vfprintf (stderr, fmt, args);
      va_end (args);
    }
}

static void
lt_error_core (int exit_status, const char *file,
	       int line, const char *mode,
	       const char *message, va_list ap)
{
  fprintf (stderr, "%s:%s:%d: %s: ", program_name, file, line, mode);
  vfprintf (stderr, message, ap);
  fprintf (stderr, ".\n");

  if (exit_status >= 0)
    exit (exit_status);
}

void
lt_fatal (const char *file, int line, const char *message, ...)
{
  va_list ap;
  va_start (ap, message);
  lt_error_core (EXIT_FAILURE, file, line, "FATAL", message, ap);
  va_end (ap);
}

static const char *
nonnull (const char *s)
{
  return s ? s : "(null)";
}

static const char *
nonempty (const char *s)
{
  return (s && !*s) ? "(empty)" : nonnull (s);
}

void
lt_setenv (const char *name, const char *value)
{
  lt_debugprintf (__FILE__, __LINE__,
		  "(lt_setenv) setting '%s' to '%s'\n",
                  nonnull (name), nonnull (value));
  {
#ifdef HAVE_SETENV
    /* always make a copy, for consistency with !HAVE_SETENV */
    char *str = xstrdup (value);
    setenv (name, str, 1);
#else
    int len = strlen (name) + 1 + strlen (value) + 1;
    char *str = XMALLOC (char, len);
    sprintf (str, "%s=%s", name, value);
    if (putenv (str) != EXIT_SUCCESS)
      {
        XFREE (str);
      }
#endif
  }
}

char *
lt_extend_str (const char *orig_value, const char *add, int to_end)
{
  char *new_value;
  if (orig_value && *orig_value)
    {
      int orig_value_len = strlen (orig_value);
      int add_len = strlen (add);
      new_value = XMALLOC (char, add_len + orig_value_len + 1);
      if (to_end)
        {
          strcpy (new_value, orig_value);
          strcpy (new_value + orig_value_len, add);
        }
      else
        {
          strcpy (new_value, add);
          strcpy (new_value + add_len, orig_value);
        }
    }
  else
    {
      new_value = xstrdup (add);
    }
  return new_value;
}

void
lt_update_exe_path (const char *name, const char *value)
{
  lt_debugprintf (__FILE__, __LINE__,
		  "(lt_update_exe_path) modifying '%s' by prepending '%s'\n",
                  nonnull (name), nonnull (value));

  if (name && *name && value && *value)
    {
      char *new_value = lt_extend_str (getenv (name), value, 0);
      /* some systems can't cope with a ':'-terminated path #' */
      int len = strlen (new_value);
      while (((len = strlen (new_value)) > 0) && IS_PATH_SEPARATOR (new_value[len-1]))
        {
          new_value[len-1] = '\0';
        }
      lt_setenv (name, new_value);
      XFREE (new_value);
    }
}

void
lt_update_lib_path (const char *name, const char *value)
{
  lt_debugprintf (__FILE__, __LINE__,
		  "(lt_update_lib_path) modifying '%s' by prepending '%s'\n",
                  nonnull (name), nonnull (value));

  if (name && *name && value && *value)
    {
      char *new_value = lt_extend_str (getenv (name), value, 0);
      lt_setenv (name, new_value);
      XFREE (new_value);
    }
}

EOF
	    case $host_os in
	      mingw*)
		cat <<"EOF"

/* Prepares an argument vector before calling spawn().
   Note that spawn() does not by itself call the command interpreter
     (getenv ("COMSPEC") != NULL ? getenv ("COMSPEC") :
      ({ OSVERSIONINFO v; v.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
         GetVersionEx(&v);
         v.dwPlatformId == VER_PLATFORM_WIN32_NT;
      }) ? "cmd.exe" : "command.com").
   Instead it simply concatenates the arguments, separated by ' ', and calls
   CreateProcess().  We must quote the arguments since Win32 CreateProcess()
   interprets characters like ' ', '\t', '\\', '"' (but not '<' and '>') in a
   special way:
   - Space and tab are interpreted as delimiters. They are not treated as
     delimiters if they are surrounded by double quotes: "...".
   - Unescaped double quotes are removed from the input. Their only effect is
     that within double quotes, space and tab are treated like normal
     characters.
   - Backslashes not followed by double quotes are not special.
   - But 2*n+1 backslashes followed by a double quote become
     n backslashes followed by a double quote (n >= 0):
       \" -> "
       \\\" -> \"
       \\\\\" -> \\"
 */
#define SHELL_SPECIAL_CHARS "\"\\ \001\002\003\004\005\006\007\010\011\012\013\014\015\016\017\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037"
#define SHELL_SPACE_CHARS " \001\002\003\004\005\006\007\010\011\012\013\014\015\016\017\020\021\022\023\024\025\026\027\030\031\032\033\034\035\036\037"
char **
prepare_spawn (char **argv)
{
  size_t argc;
  char **new_argv;
  size_t i;

  /* Count number of arguments.  */
  for (argc = 0; argv[argc] != NULL; argc++)
    ;

  /* Allocate new argument vector.  */
  new_argv = XMALLOC (char *, argc + 1);

  /* Put quoted arguments into the new argument vector.  */
  for (i = 0; i < argc; i++)
    {
      const char *string = argv[i];

      if (string[0] == '\0')
	new_argv[i] = xstrdup ("\"\"");
      else if (strpbrk (string, SHELL_SPECIAL_CHARS) != NULL)
	{
	  int quote_around = (strpbrk (string, SHELL_SPACE_CHARS) != NULL);
	  size_t length;
	  unsigned int backslashes;
	  const char *s;
	  char *quoted_string;
	  char *p;

	  length = 0;
	  backslashes = 0;
	  if (quote_around)
	    length++;
	  for (s = string; *s != '\0'; s++)
	    {
	      char c = *s;
	      if (c == '"')
		length += backslashes + 1;
	      length++;
	      if (c == '\\')
		backslashes++;
	      else
		backslashes = 0;
	    }
	  if (quote_around)
	    length += backslashes + 1;

	  quoted_string = XMALLOC (char, length + 1);

	  p = quoted_string;
	  backslashes = 0;
	  if (quote_around)
	    *p++ = '"';
	  for (s = string; *s != '\0'; s++)
	    {
	      char c = *s;
	      if (c == '"')
		{
		  unsigned int j;
		  for (j = backslashes + 1; j > 0; j--)
		    *p++ = '\\';
		}
	      *p++ = c;
	      if (c == '\\')
		backslashes++;
	      else
		backslashes = 0;
	    }
	  if (quote_around)
	    {
	      unsigned int j;
	      for (j = backslashes; j > 0; j--)
		*p++ = '\\';
	      *p++ = '"';
	    }
	  *p = '\0';

	  new_argv[i] = quoted_string;
	}
      else
	new_argv[i] = (char *) string;
    }
  new_argv[argc] = NULL;

  return new_argv;
}
EOF
		;;
	    esac

            cat <<"EOF"
void lt_dump_script (FILE* f)
{
EOF
	    func_emit_wrapper yes |
	      $SED -n -e '
s/^\(.\{79\}\)\(..*\)/\1\
\2/
h
s/\([\\"]\)/\\\1/g
s/$/\\n/
s/\([^\n]*\).*/  fputs ("\1", f);/p
g
D'
            cat <<"EOF"
}
EOF
}
# end: func_emit_cwrapperexe_src

# func_win32_import_lib_p ARG
# True if ARG is an import lib, as indicated by $file_magic_cmd
func_win32_import_lib_p ()
{
    $opt_debug
    case `eval $file_magic_cmd \"\$1\" 2>/dev/null | $SED -e 10q` in
    *import*) : ;;
    *) false ;;
    esac
}

# func_mode_link arg...
func_mode_link ()
{
    $opt_debug
    case $host in
    *-*-cygwin* | *-*-mingw* | *-*-pw32* | *-*-os2* | *-cegcc*)
      # It is impossible to link a dll without this setting, and
      # we shouldn't force the makefile maintainer to figure out
      # which system we are compiling for in order to pass an extra
      # flag for every libtool invocation.
      # allow_undefined=no

      # FIXME: Unfortunately, there are problems with the above when trying
      # to make a dll which has undefined symbols, in which case not
      # even a static library is built.  For now, we need to specify
      # -no-undefined on the libtool link line when we can be certain
      # that all symbols are satisfied, otherwise we get a static library.
      allow_undefined=yes
      ;;
    *)
      allow_undefined=yes
      ;;
    esac
    libtool_args=$nonopt
    base_compile="$nonopt $@"
    compile_command=$nonopt
    finalize_command=$nonopt

    compile_rpath=
    finalize_rpath=
    compile_shlibpath=
    finalize_shlibpath=
    convenience=
    old_convenience=
    deplibs=
    old_deplibs=
    compiler_flags=
    linker_flags=
    dllsearchpath=
    lib_search_path=`pwd`
    inst_prefix_dir=
    new_inherited_linker_flags=

    avoid_version=no
    bindir=
    dlfiles=
    dlprefiles=
    dlself=no
    export_dynamic=no
    export_symbols=
    export_symbols_regex=
    generated=
    libobjs=
    ltlibs=
    module=no
    no_install=no
    objs=
    non_pic_objects=
    precious_files_regex=
    prefer_static_libs=no
    preload=no
    prev=
    prevarg=
    release=
    rpath=
    xrpath=
    perm_rpath=
    temp_rpath=
    thread_safe=no
    vinfo=
    vinfo_number=no
    weak_libs=
    single_module="${wl}-single_module"
    func_infer_tag $base_compile

    # We need to know -static, to get the right output filenames.
    for arg
    do
      case $arg in
      -shared)
	test "$build_libtool_libs" != yes && \
	  func_fatal_configuration "can not build a shared library"
	build_old_libs=no
	break
	;;
      -all-static | -static | -static-libtool-libs)
	case $arg in
	-all-static)
	  if test "$build_libtool_libs" = yes && test -z "$link_static_flag"; then
	    func_warning "complete static linking is impossible in this configuration"
	  fi
	  if test -n "$link_static_flag"; then
	    dlopen_self=$dlopen_self_static
	  fi
	  prefer_static_libs=yes
	  ;;
	-static)
	  if test -z "$pic_flag" && test -n "$link_static_flag"; then
	    dlopen_self=$dlopen_self_static
	  fi
	  prefer_static_libs=built
	  ;;
	-static-libtool-libs)
	  if test -z "$pic_flag" && test -n "$link_static_flag"; then
	    dlopen_self=$dlopen_self_static
	  fi
	  prefer_static_libs=yes
	  ;;
	esac
	build_libtool_libs=no
	build_old_libs=yes
	break
	;;
      esac
    done

    # See if our shared archives depend on static archives.
    test -n "$old_archive_from_new_cmds" && build_old_libs=yes

    # Go through the arguments, transforming them on the way.
    while test "$#" -gt 0; do
      arg="$1"
      shift
      func_quote_for_eval "$arg"
      qarg=$func_quote_for_eval_unquoted_result
      func_append libtool_args " $func_quote_for_eval_result"

      # If the previous option needs an argument, assign it.
      if test -n "$prev"; then
	case $prev in
	output)
	  func_append compile_command " @OUTPUT@"
	  func_append finalize_command " @OUTPUT@"
	  ;;
	esac

	case $prev in
	bindir)
	  bindir="$arg"
	  prev=
	  continue
	  ;;
	dlfiles|dlprefiles)
	  if test "$preload" = no; then
	    # Add the symbol object into the linking commands.
	    func_append compile_command " @SYMFILE@"
	    func_append finalize_command " @SYMFILE@"
	    preload=yes
	  fi
	  case $arg in
	  *.la | *.lo) ;;  # We handle these cases below.
	  force)
	    if test "$dlself" = no; then
	      dlself=needless
	      export_dynamic=yes
	    fi
	    prev=
	    continue
	    ;;
	  self)
	    if test "$prev" = dlprefiles; then
	      dlself=yes
	    elif test "$prev" = dlfiles && test "$dlopen_self" != yes; then
	      dlself=yes
	    else
	      dlself=needless
	      export_dynamic=yes
	    fi
	    prev=
	    continue
	    ;;
	  *)
	    if test "$prev" = dlfiles; then
	      func_append dlfiles " $arg"
	    else
	      func_append dlprefiles " $arg"
	    fi
	    prev=
	    continue
	    ;;
	  esac
	  ;;
	expsyms)
	  export_symbols="$arg"
	  test -f "$arg" \
	    || func_fatal_error "symbol file \`$arg' does not exist"
	  prev=
	  continue
	  ;;
	expsyms_regex)
	  export_symbols_regex="$arg"
	  prev=
	  continue
	  ;;
	framework)
	  case $host in
	    *-*-darwin*)
	      case "$deplibs " in
		*" $qarg.ltframework "*) ;;
		*) func_append deplibs " $qarg.ltframework" # this is fixed later
		   ;;
	      esac
	      ;;
	  esac
	  prev=
	  continue
	  ;;
	inst_prefix)
	  inst_prefix_dir="$arg"
	  prev=
	  continue
	  ;;
	objectlist)
	  if test -f "$arg"; then
	    save_arg=$arg
	    moreargs=
	    for fil in `cat "$save_arg"`
	    do
#	      func_append moreargs " $fil"
	      arg=$fil
	      # A libtool-controlled object.

	      # Check to see that this really is a libtool object.
	      if func_lalib_unsafe_p "$arg"; then
		pic_object=
		non_pic_object=

		# Read the .lo file
		func_source "$arg"

		if test -z "$pic_object" ||
		   test -z "$non_pic_object" ||
		   test "$pic_object" = none &&
		   test "$non_pic_object" = none; then
		  func_fatal_error "cannot find name of object for \`$arg'"
		fi

		# Extract subdirectory from the argument.
		func_dirname "$arg" "/" ""
		xdir="$func_dirname_result"

		if test "$pic_object" != none; then
		  # Prepend the subdirectory the object is found in.
		  pic_object="$xdir$pic_object"

		  if test "$prev" = dlfiles; then
		    if test "$build_libtool_libs" = yes && test "$dlopen_support" = yes; then
		      func_append dlfiles " $pic_object"
		      prev=
		      continue
		    else
		      # If libtool objects are unsupported, then we need to preload.
		      prev=dlprefiles
		    fi
		  fi

		  # CHECK ME:  I think I busted this.  -Ossama
		  if test "$prev" = dlprefiles; then
		    # Preload the old-style object.
		    func_append dlprefiles " $pic_object"
		    prev=
		  fi

		  # A PIC object.
		  func_append libobjs " $pic_object"
		  arg="$pic_object"
		fi

		# Non-PIC object.
		if test "$non_pic_object" != none; then
		  # Prepend the subdirectory the object is found in.
		  non_pic_object="$xdir$non_pic_object"

		  # A standard non-PIC object
		  func_append non_pic_objects " $non_pic_object"
		  if test -z "$pic_object" || test "$pic_object" = none ; then
		    arg="$non_pic_object"
		  fi
		else
		  # If the PIC object exists, use it instead.
		  # $xdir was prepended to $pic_object above.
		  non_pic_object="$pic_object"
		  func_append non_pic_objects " $non_pic_object"
		fi
	      else
		# Only an error if not doing a dry-run.
		if $opt_dry_run; then
		  # Extract subdirectory from the argument.
		  func_dirname "$arg" "/" ""
		  xdir="$func_dirname_result"

		  func_lo2o "$arg"
		  pic_object=$xdir$objdir/$func_lo2o_result
		  non_pic_object=$xdir$func_lo2o_result
		  func_append libobjs " $pic_object"
		  func_append non_pic_objects " $non_pic_object"
	        else
		  func_fatal_error "\`$arg' is not a valid libtool object"
		fi
	      fi
	    done
	  else
	    func_fatal_error "link input file \`$arg' does not exist"
	  fi
	  arg=$save_arg
	  prev=
	  continue
	  ;;
	precious_regex)
	  precious_files_regex="$arg"
	  prev=
	  continue
	  ;;
	release)
	  release="-$arg"
	  prev=
	  continue
	  ;;
	rpath | xrpath)
	  # We need an absolute path.
	  case $arg in
	  [\\/]* | [A-Za-z]:[\\/]*) ;;
	  *)
	    func_fatal_error "only absolute run-paths are allowed"
	    ;;
	  esac
	  if test "$prev" = rpath; then
	    case "$rpath " in
	    *" $arg "*) ;;
	    *) func_append rpath " $arg" ;;
	    esac
	  else
	    case "$xrpath " in
	    *" $arg "*) ;;
	    *) func_append xrpath " $arg" ;;
	    esac
	  fi
	  prev=
	  continue
	  ;;
	shrext)
	  shrext_cmds="$arg"
	  prev=
	  continue
	  ;;
	weak)
	  func_append weak_libs " $arg"
	  prev=
	  continue
	  ;;
	xcclinker)
	  func_append linker_flags " $qarg"
	  func_append compiler_flags " $qarg"
	  prev=
	  func_append compile_command " $qarg"
	  func_append finalize_command " $qarg"
	  continue
	  ;;
	xcompiler)
	  func_append compiler_flags " $qarg"
	  prev=
	  func_append compile_command " $qarg"
	  func_append finalize_command " $qarg"
	  continue
	  ;;
	xlinker)
	  func_append linker_flags " $qarg"
	  func_append compiler_flags " $wl$qarg"
	  prev=
	  func_append compile_command " $wl$qarg"
	  func_append finalize_command " $wl$qarg"
	  continue
	  ;;
	*)
	  eval "$prev=\"\$arg\""
	  prev=
	  continue
	  ;;
	esac
      fi # test -n "$prev"

      prevarg="$arg"

      case $arg in
      -all-static)
	if test -n "$link_static_flag"; then
	  # See comment for -static flag below, for more details.
	  func_append compile_command " $link_static_flag"
	  func_append finalize_command " $link_static_flag"
	fi
	continue
	;;

      -allow-undefined)
	# FIXME: remove this flag sometime in the future.
	func_fatal_error "\`-allow-undefined' must not be used because it is the default"
	;;

      -avoid-version)
	avoid_version=yes
	continue
	;;

      -bindir)
	prev=bindir
	continue
	;;

      -dlopen)
	prev=dlfiles
	continue
	;;

      -dlpreopen)
	prev=dlprefiles
	continue
	;;

      -export-dynamic)
	export_dynamic=yes
	continue
	;;

      -export-symbols | -export-symbols-regex)
	if test -n "$export_symbols" || test -n "$export_symbols_regex"; then
	  func_fatal_error "more than one -exported-symbols argument is not allowed"
	fi
	if test "X$arg" = "X-export-symbols"; then
	  prev=expsyms
	else
	  prev=expsyms_regex
	fi
	continue
	;;

      -framework)
	prev=framework
	continue
	;;

      -inst-prefix-dir)
	prev=inst_prefix
	continue
	;;

      # The native IRIX linker understands -LANG:*, -LIST:* and -LNO:*
      # so, if we see these flags be careful not to treat them like -L
      -L[A-Z][A-Z]*:*)
	case $with_gcc/$host in
	no/*-*-irix* | /*-*-irix*)
	  func_append compile_command " $arg"
	  func_append finalize_command " $arg"
	  ;;
	esac
	continue
	;;

      -L*)
	func_stripname "-L" '' "$arg"
	if test -z "$func_stripname_result"; then
	  if test "$#" -gt 0; then
	    func_fatal_error "require no space between \`-L' and \`$1'"
	  else
	    func_fatal_error "need path for \`-L' option"
	  fi
	fi
	func_resolve_sysroot "$func_stripname_result"
	dir=$func_resolve_sysroot_result
	# We need an absolute path.
	case $dir in
	[\\/]* | [A-Za-z]:[\\/]*) ;;
	*)
	  absdir=`cd "$dir" && pwd`
	  test -z "$absdir" && \
	    func_fatal_error "cannot determine absolute directory name of \`$dir'"
	  dir="$absdir"
	  ;;
	esac
	case "$deplibs " in
	*" -L$dir "* | *" $arg "*)
	  # Will only happen for absolute or sysroot arguments
	  ;;
	*)
	  # Preserve sysroot, but never include relative directories
	  case $dir in
	    [\\/]* | [A-Za-z]:[\\/]* | =*) func_append deplibs " $arg" ;;
	    *) func_append deplibs " -L$dir" ;;
	  esac
	  func_append lib_search_path " $dir"
	  ;;
	esac
	case $host in
	*-*-cygwin* | *-*-mingw* | *-*-pw32* | *-*-os2* | *-cegcc*)
	  testbindir=`$ECHO "$dir" | $SED 's*/lib$*/bin*'`
	  case :$dllsearchpath: in
	  *":$dir:"*) ;;
	  ::) dllsearchpath=$dir;;
	  *) func_append dllsearchpath ":$dir";;
	  esac
	  case :$dllsearchpath: in
	  *":$testbindir:"*) ;;
	  ::) dllsearchpath=$testbindir;;
	  *) func_append dllsearchpath ":$testbindir";;
	  esac
	  ;;
	esac
	continue
	;;

      -l*)
	if test "X$arg" = "X-lc" || test "X$arg" = "X-lm"; then
	  case $host in
	  *-*-cygwin* | *-*-mingw* | *-*-pw32* | *-*-beos* | *-cegcc* | *-*-haiku*)
	    # These systems don't actually have a C or math library (as such)
	    continue
	    ;;
	  *-*-os2*)
	    # These systems don't actually have a C library (as such)
	    test "X$arg" = "X-lc" && continue
	    ;;
	  *-*-openbsd* | *-*-freebsd* | *-*-dragonfly*)
	    # Do not include libc due to us having libc/libc_r.
	    test "X$arg" = "X-lc" && continue
	    ;;
	  *-*-rhapsody* | *-*-darwin1.[012])
	    # Rhapsody C and math libraries are in the System framework
	    func_append deplibs " System.ltframework"
	    continue
	    ;;
	  *-*-sco3.2v5* | *-*-sco5v6*)
	    # Causes problems with __ctype
	    test "X$arg" = "X-lc" && continue
	    ;;
	  *-*-sysv4.2uw2* | *-*-sysv5* | *-*-unixware* | *-*-OpenUNIX*)
	    # Compiler inserts libc in the correct place for threads to work
	    test "X$arg" = "X-lc" && continue
	    ;;
	  esac
	elif test "X$arg" = "X-lc_r"; then
	 case $host in
	 *-*-openbsd* | *-*-freebsd* | *-*-dragonfly*)
	   # Do not include libc_r directly, use -pthread flag.
	   continue
	   ;;
	 esac
	fi
	func_append deplibs " $arg"
	continue
	;;

      -module)
	module=yes
	continue
	;;

      # Tru64 UNIX uses -model [arg] to determine the layout of C++
      # classes, name mangling, and exception handling.
      # Darwin uses the -arch flag to determine output architecture.
      -model|-arch|-isysroot|--sysroot)
	func_append compiler_flags " $arg"
	func_append compile_command " $arg"
	func_append finalize_command " $arg"
	prev=xcompiler
	continue
	;;

      -mt|-mthreads|-kthread|-Kthread|-pthread|-pthreads|--thread-safe \
      |-threads|-fopenmp|-openmp|-mp|-xopenmp|-omp|-qsmp=*)
	func_append compiler_flags " $arg"
	func_append compile_command " $arg"
	func_append finalize_command " $arg"
	case "$new_inherited_linker_flags " in
	    *" $arg "*) ;;
	    * ) func_append new_inherited_linker_flags " $arg" ;;
	esac
	continue
	;;

      -multi_module)
	single_module="${wl}-multi_module"
	continue
	;;

      -no-fast-install)
	fast_install=no
	continue
	;;

      -no-install)
	case $host in
	*-*-cygwin* | *-*-mingw* | *-*-pw32* | *-*-os2* | *-*-darwin* | *-cegcc*)
	  # The PATH hackery in wrapper scripts is required on Windows
	  # and Darwin in order for the loader to find any dlls it needs.
	  func_warning "\`-no-install' is ignored for $host"
	  func_warning "assuming \`-no-fast-install' instead"
	  fast_install=no
	  ;;
	*) no_install=yes ;;
	esac
	continue
	;;

      -no-undefined)
	allow_undefined=no
	continue
	;;

      -objectlist)
	prev=objectlist
	continue
	;;

      -o) prev=output ;;

      -precious-files-regex)
	prev=precious_regex
	continue
	;;

      -release)
	prev=release
	continue
	;;

      -rpath)
	prev=rpath
	continue
	;;

      -R)
	prev=xrpath
	continue
	;;

      -R*)
	func_stripname '-R' '' "$arg"
	dir=$func_stripname_result
	# We need an absolute path.
	case $dir in
	[\\/]* | [A-Za-z]:[\\/]*) ;;
	=*)
	  func_stripname '=' '' "$dir"
	  dir=$lt_sysroot$func_stripname_result
	  ;;
	*)
	  func_fatal_error "only absolute run-paths are allowed"
	  ;;
	esac
	case "$xrpath " in
	*" $dir "*) ;;
	*) func_append xrpath " $dir" ;;
	esac
	continue
	;;

      -shared)
	# The effects of -shared are defined in a previous loop.
	continue
	;;

      -shrext)
	prev=shrext
	continue
	;;

      -static | -static-libtool-libs)
	# The effects of -static are defined in a previous loop.
	# We used to do the same as -all-static on platforms that
	# didn't have a PIC flag, but the assumption that the effects
	# would be equivalent was wrong.  It would break on at least
	# Digital Unix and AIX.
	continue
	;;

      -thread-safe)
	thread_safe=yes
	continue
	;;

      -version-info)
	prev=vinfo
	continue
	;;

      -version-number)
	prev=vinfo
	vinfo_number=yes
	continue
	;;

      -weak)
        prev=weak
	continue
	;;

      -Wc,*)
	func_stripname '-Wc,' '' "$arg"
	args=$func_stripname_result
	arg=
	save_ifs="$IFS"; IFS=','
	for flag in $args; do
	  IFS="$save_ifs"
          func_quote_for_eval "$flag"
	  func_append arg " $func_quote_for_eval_result"
	  func_append compiler_flags " $func_quote_for_eval_result"
	done
	IFS="$save_ifs"
	func_stripname ' ' '' "$arg"
	arg=$func_stripname_result
	;;

      -Wl,*)
	func_stripname '-Wl,' '' "$arg"
	args=$func_stripname_result
	arg=
	save_ifs="$IFS"; IFS=','
	for flag in $args; do
	  IFS="$save_ifs"
          func_quote_for_eval "$flag"
	  func_append arg " $wl$func_quote_for_eval_result"
	  func_append compiler_flags " $wl$func_quote_for_eval_result"
	  func_append linker_flags " $func_quote_for_eval_result"
	done
	IFS="$save_ifs"
	func_stripname ' ' '' "$arg"
	arg=$func_stripname_result
	;;

      -Xcompiler)
	prev=xcompiler
	continue
	;;

      -Xlinker)
	prev=xlinker
	continue
	;;

      -XCClinker)
	prev=xcclinker
	continue
	;;

      # -msg_* for osf cc
      -msg_*)
	func_quote_for_eval "$arg"
	arg="$func_quote_for_eval_result"
	;;

      # Flags to be passed through unchanged, with rationale:
      # -64, -mips[0-9]      enable 64-bit mode for the SGI compiler
      # -r[0-9][0-9]*        specify processor for the SGI compiler
      # -xarch=*, -xtarget=* enable 64-bit mode for the Sun compiler
      # +DA*, +DD*           enable 64-bit mode for the HP compiler
      # -q*                  compiler args for the IBM compiler
      # -m*, -t[45]*, -txscale* architecture-specific flags for GCC
      # -F/path              path to uninstalled frameworks, gcc on darwin
      # -p, -pg, --coverage, -fprofile-*  profiling flags for GCC
      # @file                GCC response files
      # -tp=*                Portland pgcc target processor selection
      # --sysroot=*          for sysroot support
      # -O*, -flto*, -fwhopr*, -fuse-linker-plugin GCC link-time optimization
      -64|-mips[0-9]|-r[0-9][0-9]*|-xarch=*|-xtarget=*|+DA*|+DD*|-q*|-m*| \
      -t[45]*|-txscale*|-p|-pg|--coverage|-fprofile-*|-F*|@*|-tp=*|--sysroot=*| \
      -O*|-flto*|-fwhopr*|-fuse-linker-plugin)
        func_quote_for_eval "$arg"
	arg="$func_quote_for_eval_result"
        func_append compile_command " $arg"
        func_append finalize_command " $arg"
        func_append compiler_flags " $arg"
        continue
        ;;

      # Some other compiler flag.
      -* | +*)
        func_quote_for_eval "$arg"
	arg="$func_quote_for_eval_result"
	;;

      *.$objext)
	# A standard object.
	func_append objs " $arg"
	;;

      *.lo)
	# A libtool-controlled object.

	# Check to see that this really is a libtool object.
	if func_lalib_unsafe_p "$arg"; then
	  pic_object=
	  non_pic_object=

	  # Read the .lo file
	  func_source "$arg"

	  if test -z "$pic_object" ||
	     test -z "$non_pic_object" ||
	     test "$pic_object" = none &&
	     test "$non_pic_object" = none; then
	    func_fatal_error "cannot find name of object for \`$arg'"
	  fi

	  # Extract subdirectory from the argument.
	  func_dirname "$arg" "/" ""
	  xdir="$func_dirname_result"

	  if test "$pic_object" != none; then
	    # Prepend the subdirectory the object is found in.
	    pic_object="$xdir$pic_object"

	    if test "$prev" = dlfiles; then
	      if test "$build_libtool_libs" = yes && test "$dlopen_support" = yes; then
		func_append dlfiles " $pic_object"
		prev=
		continue
	      else
		# If libtool objects are unsupported, then we need to preload.
		prev=dlprefiles
	      fi
	    fi

	    # CHECK ME:  I think I busted this.  -Ossama
	    if test "$prev" = dlprefiles; then
	      # Preload the old-style object.
	      func_append dlprefiles " $pic_object"
	      prev=
	    fi

	    # A PIC object.
	    func_append libobjs " $pic_object"
	    arg="$pic_object"
	  fi

	  # Non-PIC object.
	  if test "$non_pic_object" != none; then
	    # Prepend the subdirectory the object is found in.
	    non_pic_object="$xdir$non_pic_object"

	    # A standard non-PIC object
	    func_append non_pic_objects " $non_pic_object"
	    if test -z "$pic_object" || test "$pic_object" = none ; then
	      arg="$non_pic_object"
	    fi
	  else
	    # If the PIC object exists, use it instead.
	    # $xdir was prepended to $pic_object above.
	    non_pic_object="$pic_object"
	    func_append non_pic_objects " $non_pic_object"
	  fi
	else
	  # Only an error if not doing a dry-run.
	  if $opt_dry_run; then
	    # Extract subdirectory from the argument.
	    func_dirname "$arg" "/" ""
	    xdir="$func_dirname_result"

	    func_lo2o "$arg"
	    pic_object=$xdir$objdir/$func_lo2o_result
	    non_pic_object=$xdir$func_lo2o_result
	    func_append libobjs " $pic_object"
	    func_append non_pic_objects " $non_pic_object"
	  else
	    func_fatal_error "\`$arg' is not a valid libtool object"
	  fi
	fi
	;;

      *.$libext)
	# An archive.
	func_append deplibs " $arg"
	func_append old_deplibs " $arg"
	continue
	;;

      *.la)
	# A libtool-controlled library.

	func_resolve_sysroot "$arg"
	if test "$prev" = dlfiles; then
	  # This library was specified with -dlopen.
	  func_append dlfiles " $func_resolve_sysroot_result"
	  prev=
	elif test "$prev" = dlprefiles; then
	  # The library was specified with -dlpreopen.
	  func_append dlprefiles " $func_resolve_sysroot_result"
	  prev=
	else
	  func_append deplibs " $func_resolve_sysroot_result"
	fi
	continue
	;;

      # Some other compiler argument.
      *)
	# Unknown arguments in both finalize_command and compile_command need
	# to be aesthetically quoted because they are evaled later.
	func_quote_for_eval "$arg"
	arg="$func_quote_for_eval_result"
	;;
      esac # arg

      # Now actually substitute the argument into the commands.
      if test -n "$arg"; then
	func_append compile_command " $arg"
	func_append finalize_command " $arg"
      fi
    done # argument parsing loop

    test -n "$prev" && \
      func_fatal_help "the \`$prevarg' option requires an argument"

    if test "$export_dynamic" = yes && test -n "$export_dynamic_flag_spec"; then
      eval arg=\"$export_dynamic_flag_spec\"
      func_append compile_command " $arg"
      func_append finalize_command " $arg"
    fi

    oldlibs=
    # calculate the name of the file, without its directory
    func_basename "$output"
    outputname="$func_basename_result"
    libobjs_save="$libobjs"

    if test -n "$shlibpath_var"; then
      # get the directories listed in $shlibpath_var
      eval shlib_search_path=\`\$ECHO \"\${$shlibpath_var}\" \| \$SED \'s/:/ /g\'\`
    else
      shlib_search_path=
    fi
    eval sys_lib_search_path=\"$sys_lib_search_path_spec\"
    eval sys_lib_dlsearch_path=\"$sys_lib_dlsearch_path_spec\"

    func_dirname "$output" "/" ""
    output_objdir="$func_dirname_result$objdir"
    func_to_tool_file "$output_objdir/"
    tool_output_objdir=$func_to_tool_file_result
    # Create the object directory.
    func_mkdir_p "$output_objdir"

    # Determine the type of output
    case $output in
    "")
      func_fatal_help "you must specify an output file"
      ;;
    *.$libext) linkmode=oldlib ;;
    *.lo | *.$objext) linkmode=obj ;;
    *.la) linkmode=lib ;;
    *) linkmode=prog ;; # Anything else should be a program.
    esac

    specialdeplibs=

    libs=
    # Find all interdependent deplibs by searching for libraries
    # that are linked more than once (e.g. -la -lb -la)
    for deplib in $deplibs; do
      if $opt_preserve_dup_deps ; then
	case "$libs " in
	*" $deplib "*) func_append specialdeplibs " $deplib" ;;
	esac
      fi
      func_append libs " $deplib"
    done

    if test "$linkmode" = lib; then
      libs="$predeps $libs $compiler_lib_search_path $postdeps"

      # Compute libraries that are listed more than once in $predeps
      # $postdeps and mark them as special (i.e., whose duplicates are
      # not to be eliminated).
      pre_post_deps=
      if $opt_duplicate_compiler_generated_deps; then
	for pre_post_dep in $predeps $postdeps; do
	  case "$pre_post_deps " in
	  *" $pre_post_dep "*) func_append specialdeplibs " $pre_post_deps" ;;
	  esac
	  func_append pre_post_deps " $pre_post_dep"
	done
      fi
      pre_post_deps=
    fi

    deplibs=
    newdependency_libs=
    newlib_search_path=
    need_relink=no # whether we're linking any uninstalled libtool libraries
    notinst_deplibs= # not-installed libtool libraries
    notinst_path= # paths that contain not-installed libtool libraries

    case $linkmode in
    lib)
	passes="conv dlpreopen link"
	for file in $dlfiles $dlprefiles; do
	  case $file in
	  *.la) ;;
	  *)
	    func_fatal_help "libraries can \`-dlopen' only libtool libraries: $file"
	    ;;
	  esac
	done
	;;
    prog)
	compile_deplibs=
	finalize_deplibs=
	alldeplibs=no
	newdlfiles=
	newdlprefiles=
	passes="conv scan dlopen dlpreopen link"
	;;
    *)  passes="conv"
	;;
    esac

    for pass in $passes; do
      # The preopen pass in lib mode reverses $deplibs; put it back here
      # so that -L comes before libs that need it for instance...
      if test "$linkmode,$pass" = "lib,link"; then
	## FIXME: Find the place where the list is rebuilt in the wrong
	##        order, and fix it there properly
        tmp_deplibs=
	for deplib in $deplibs; do
	  tmp_deplibs="$deplib $tmp_deplibs"
	done
	deplibs="$tmp_deplibs"
      fi

      if test "$linkmode,$pass" = "lib,link" ||
	 test "$linkmode,$pass" = "prog,scan"; then
	libs="$deplibs"
	deplibs=
      fi
      if test "$linkmode" = prog; then
	case $pass in
	dlopen) libs="$dlfiles" ;;
	dlpreopen) libs="$dlprefiles" ;;
	link)
	  libs="$deplibs %DEPLIBS%"
	  test "X$link_all_deplibs" != Xno && libs="$libs $dependency_libs"
	  ;;
	esac
      fi
      if test "$linkmode,$pass" = "lib,dlpreopen"; then
	# Collect and forward deplibs of preopened libtool libs
	for lib in $dlprefiles; do
	  # Ignore non-libtool-libs
	  dependency_libs=
	  func_resolve_sysroot "$lib"
	  case $lib in
	  *.la)	func_source "$func_resolve_sysroot_result" ;;
	  esac

	  # Collect preopened libtool deplibs, except any this library
	  # has declared as weak libs
	  for deplib in $dependency_libs; do
	    func_basename "$deplib"
            deplib_base=$func_basename_result
	    case " $weak_libs " in
	    *" $deplib_base "*) ;;
	    *) func_append deplibs " $deplib" ;;
	    esac
	  done
	done
	libs="$dlprefiles"
      fi
      if test "$pass" = dlopen; then
	# Collect dlpreopened libraries
	save_deplibs="$deplibs"
	deplibs=
      fi

      for deplib in $libs; do
	lib=
	found=no
	case $deplib in
	-mt|-mthreads|-kthread|-Kthread|-pthread|-pthreads|--thread-safe \
        |-threads|-fopenmp|-openmp|-mp|-xopenmp|-omp|-qsmp=*)
	  if test "$linkmode,$pass" = "prog,link"; then
	    compile_deplibs="$deplib $compile_deplibs"
	    finalize_deplibs="$deplib $finalize_deplibs"
	  else
	    func_append compiler_flags " $deplib"
	    if test "$linkmode" = lib ; then
		case "$new_inherited_linker_flags " in
		    *" $deplib "*) ;;
		    * ) func_append new_inherited_linker_flags " $deplib" ;;
		esac
	    fi
	  fi
	  continue
	  ;;
	-l*)
	  if test "$linkmode" != lib && test "$linkmode" != prog; then
	    func_warning "\`-l' is ignored for archives/objects"
	    continue
	  fi
	  func_stripname '-l' '' "$deplib"
	  name=$func_stripname_result
	  if test "$linkmode" = lib; then
	    searchdirs="$newlib_search_path $lib_search_path $compiler_lib_search_dirs $sys_lib_search_path $shlib_search_path"
	  else
	    searchdirs="$newlib_search_path $lib_search_path $sys_lib_search_path $shlib_search_path"
	  fi
	  for searchdir in $searchdirs; do
	    for search_ext in .la $std_shrext .so .a; do
	      # Search the libtool library
	      lib="$searchdir/lib${name}${search_ext}"
	      if test -f "$lib"; then
		if test "$search_ext" = ".la"; then
		  found=yes
		else
		  found=no
		fi
		break 2
	      fi
	    done
	  done
	  if test "$found" != yes; then
	    # deplib doesn't seem to be a libtool library
	    if test "$linkmode,$pass" = "prog,link"; then
	      compile_deplibs="$deplib $compile_deplibs"
	      finalize_deplibs="$deplib $finalize_deplibs"
	    else
	      deplibs="$deplib $deplibs"
	      test "$linkmode" = lib && newdependency_libs="$deplib $newdependency_libs"
	    fi
	    continue
	  else # deplib is a libtool library
	    # If $allow_libtool_libs_with_static_runtimes && $deplib is a stdlib,
	    # We need to do some special things here, and not later.
	    if test "X$allow_libtool_libs_with_static_runtimes" = "Xyes" ; then
	      case " $predeps $postdeps " in
	      *" $deplib "*)
		if func_lalib_p "$lib"; then
		  library_names=
		  old_library=
		  func_source "$lib"
		  for l in $old_library $library_names; do
		    ll="$l"
		  done
		  if test "X$ll" = "X$old_library" ; then # only static version available
		    found=no
		    func_dirname "$lib" "" "."
		    ladir="$func_dirname_result"
		    lib=$ladir/$old_library
		    if test "$linkmode,$pass" = "prog,link"; then
		      compile_deplibs="$deplib $compile_deplibs"
		      finalize_deplibs="$deplib $finalize_deplibs"
		    else
		      deplibs="$deplib $deplibs"
		      test "$linkmode" = lib && newdependency_libs="$deplib $newdependency_libs"
		    fi
		    continue
		  fi
		fi
		;;
	      *) ;;
	      esac
	    fi
	  fi
	  ;; # -l
	*.ltframework)
	  if test "$linkmode,$pass" = "prog,link"; then
	    compile_deplibs="$deplib $compile_deplibs"
	    finalize_deplibs="$deplib $finalize_deplibs"
	  else
	    deplibs="$deplib $deplibs"
	    if test "$linkmode" = lib ; then
		case "$new_inherited_linker_flags " in
		    *" $deplib "*) ;;
		    * ) func_append new_inherited_linker_flags " $deplib" ;;
		esac
	    fi
	  fi
	  continue
	  ;;
	-L*)
	  case $linkmode in
	  lib)
	    deplibs="$deplib $deplibs"
	    test "$pass" = conv && continue
	    newdependency_libs="$deplib $newdependency_libs"
	    func_stripname '-L' '' "$deplib"
	    func_resolve_sysroot "$func_stripname_result"
	    func_append newlib_search_path " $func_resolve_sysroot_result"
	    ;;
	  prog)
	    if test "$pass" = conv; then
	      deplibs="$deplib $deplibs"
	      continue
	    fi
	    if test "$pass" = scan; then
	      deplibs="$deplib $deplibs"
	    else
	      compile_deplibs="$deplib $compile_deplibs"
	      finalize_deplibs="$deplib $finalize_deplibs"
	    fi
	    func_stripname '-L' '' "$deplib"
	    func_resolve_sysroot "$func_stripname_result"
	    func_append newlib_search_path " $func_resolve_sysroot_result"
	    ;;
	  *)
	    func_warning "\`-L' is ignored for archives/objects"
	    ;;
	  esac # linkmode
	  continue
	  ;; # -L
	-R*)
	  if test "$pass" = link; then
	    func_stripname '-R' '' "$deplib"
	    func_resolve_sysroot "$func_stripname_result"
	    dir=$func_resolve_sysroot_result
	    # Make sure the xrpath contains only unique directories.
	    case "$xrpath " in
	    *" $dir "*) ;;
	    *) func_append xrpath " $dir" ;;
	    esac
	  fi
	  deplibs="$deplib $deplibs"
	  continue
	  ;;
	*.la)
	  func_resolve_sysroot "$deplib"
	  lib=$func_resolve_sysroot_result
	  ;;
	*.$libext)
	  if test "$pass" = conv; then
	    deplibs="$deplib $deplibs"
	    continue
	  fi
	  case $linkmode in
	  lib)
	    # Linking convenience modules into shared libraries is allowed,
	    # but linking other static libraries is non-portable.
	    case " $dlpreconveniencelibs " in
	    *" $deplib "*) ;;
	    *)
	      valid_a_lib=no
	      case $deplibs_check_method in
		match_pattern*)
		  set dummy $deplibs_check_method; shift
		  match_pattern_regex=`expr "$deplibs_check_method" : "$1 \(.*\)"`
		  if eval "\$ECHO \"$deplib\"" 2>/dev/null | $SED 10q \
		    | $EGREP "$match_pattern_regex" > /dev/null; then
		    valid_a_lib=yes
		  fi
		;;
		pass_all)
		  valid_a_lib=yes
		;;
	      esac
	      if test "$valid_a_lib" != yes; then
		echo
		$ECHO "*** Warning: Trying to link with static lib archive $deplib."
		echo "*** I have the capability to make that library automatically link in when"
		echo "*** you link to this library.  But I can only do this if you have a"
		echo "*** shared version of the library, which you do not appear to have"
		echo "*** because the file extensions .$libext of this argument makes me believe"
		echo "*** that it is just a static archive that I should not use here."
	      else
		echo
		$ECHO "*** Warning: Linking the shared library $output against the"
		$ECHO "*** static library $deplib is not portable!"
		deplibs="$deplib $deplibs"
	      fi
	      ;;
	    esac
	    continue
	    ;;
	  prog)
	    if test "$pass" != link; then
	      deplibs="$deplib $deplibs"
	    else
	      compile_deplibs="$deplib $compile_deplibs"
	      finalize_deplibs="$deplib $finalize_deplibs"
	    fi
	    continue
	    ;;
	  esac # linkmode
	  ;; # *.$libext
	*.lo | *.$objext)
	  if test "$pass" = conv; then
	    deplibs="$deplib $deplibs"
	  elif test "$linkmode" = prog; then
	    if test "$pass" = dlpreopen || test "$dlopen_support" != yes || test "$build_libtool_libs" = no; then
	      # If there is no dlopen support or we're linking statically,
	      # we need to preload.
	      func_append newdlprefiles " $deplib"
	      compile_deplibs="$deplib $compile_deplibs"
	      finalize_deplibs="$deplib $finalize_deplibs"
	    else
	      func_append newdlfiles " $deplib"
	    fi
	  fi
	  continue
	  ;;
	%DEPLIBS%)
	  alldeplibs=yes
	  continue
	  ;;
	esac # case $deplib

	if test "$found" = yes || test -f "$lib"; then :
	else
	  func_fatal_error "cannot find the library \`$lib' or unhandled argument \`$deplib'"
	fi

	# Check to see that this really is a libtool archive.
	func_lalib_unsafe_p "$lib" \
	  || func_fatal_error "\`$lib' is not a valid libtool archive"

	func_dirname "$lib" "" "."
	ladir="$func_dirname_result"

	dlname=
	dlopen=
	dlpreopen=
	libdir=
	library_names=
	old_library=
	inherited_linker_flags=
	# If the library was installed with an old release of libtool,
	# it will not redefine variables installed, or shouldnotlink
	installed=yes
	shouldnotlink=no
	avoidtemprpath=


	# Read the .la file
	func_source "$lib"

	# Convert "-framework foo" to "foo.ltframework"
	if test -n "$inherited_linker_flags"; then
	  tmp_inherited_linker_flags=`$ECHO "$inherited_linker_flags" | $SED 's/-framework \([^ $]*\)/\1.ltframework/g'`
	  for tmp_inherited_linker_flag in $tmp_inherited_linker_flags; do
	    case " $new_inherited_linker_flags " in
	      *" $tmp_inherited_linker_flag "*) ;;
	      *) func_append new_inherited_linker_flags " $tmp_inherited_linker_flag";;
	    esac
	  done
	fi
	dependency_libs=`$ECHO " $dependency_libs" | $SED 's% \([^ $]*\).ltframework% -framework \1%g'`
	if test "$linkmode,$pass" = "lib,link" ||
	   test "$linkmode,$pass" = "prog,scan" ||
	   { test "$linkmode" != prog && test "$linkmode" != lib; }; then
	  test -n "$dlopen" && func_append dlfiles " $dlopen"
	  test -n "$dlpreopen" && func_append dlprefiles " $dlpreopen"
	fi

	if test "$pass" = conv; then
	  # Only check for convenience libraries
	  deplibs="$lib $deplibs"
	  if test -z "$libdir"; then
	    if test -z "$old_library"; then
	      func_fatal_error "cannot find name of link library for \`$lib'"
	    fi
	    # It is a libtool convenience library, so add in its objects.
	    func_append convenience " $ladir/$objdir/$old_library"
	    func_append old_convenience " $ladir/$objdir/$old_library"
	    tmp_libs=
	    for deplib in $dependency_libs; do
	      deplibs="$deplib $deplibs"
	      if $opt_preserve_dup_deps ; then
		case "$tmp_libs " in
		*" $deplib "*) func_append specialdeplibs " $deplib" ;;
		esac
	      fi
	      func_append tmp_libs " $deplib"
	    done
	  elif test "$linkmode" != prog && test "$linkmode" != lib; then
	    func_fatal_error "\`$lib' is not a convenience library"
	  fi
	  continue
	fi # $pass = conv


	# Get the name of the library we link against.
	linklib=
	if test -n "$old_library" &&
	   { test "$prefer_static_libs" = yes ||
	     test "$prefer_static_libs,$installed" = "built,no"; }; then
	  linklib=$old_library
	else
	  for l in $old_library $library_names; do
	    linklib="$l"
	  done
	fi
	if test -z "$linklib"; then
	  func_fatal_error "cannot find name of link library for \`$lib'"
	fi

	# This library was specified with -dlopen.
	if test "$pass" = dlopen; then
	  if test -z "$libdir"; then
	    func_fatal_error "cannot -dlopen a convenience library: \`$lib'"
	  fi
	  if test -z "$dlname" ||
	     test "$dlopen_support" != yes ||
	     test "$build_libtool_libs" = no; then
	    # If there is no dlname, no dlopen support or we're linking
	    # statically, we need to preload.  We also need to preload any
	    # dependent libraries so libltdl's deplib preloader doesn't
	    # bomb out in the load deplibs phase.
	    func_append dlprefiles " $lib $dependency_libs"
	  else
	    func_append newdlfiles " $lib"
	  fi
	  continue
	fi # $pass = dlopen

	# We need an absolute path.
	case $ladir in
	[\\/]* | [A-Za-z]:[\\/]*) abs_ladir="$ladir" ;;
	*)
	  abs_ladir=`cd "$ladir" && pwd`
	  if test -z "$abs_ladir"; then
	    func_warning "cannot determine absolute directory name of \`$ladir'"
	    func_warning "passing it literally to the linker, although it might fail"
	    abs_ladir="$ladir"
	  fi
	  ;;
	esac
	func_basename "$lib"
	laname="$func_basename_result"

	# Find the relevant object directory and library name.
	if test "X$installed" = Xyes; then
	  if test ! -f "$lt_sysroot$libdir/$linklib" && test -f "$abs_ladir/$linklib"; then
	    func_warning "library \`$lib' was moved."
	    dir="$ladir"
	    absdir="$abs_ladir"
	    libdir="$abs_ladir"
	  else
	    dir="$lt_sysroot$libdir"
	    absdir="$lt_sysroot$libdir"
	  fi
	  test "X$hardcode_automatic" = Xyes && avoidtemprpath=yes
	else
	  if test ! -f "$ladir/$objdir/$linklib" && test -f "$abs_ladir/$linklib"; then
	    dir="$ladir"
	    absdir="$abs_ladir"
	    # Remove this search path later
	    func_append notinst_path " $abs_ladir"
	  else
	    dir="$ladir/$objdir"
	    absdir="$abs_ladir/$objdir"
	    # Remove this search path later
	    func_append notinst_path " $abs_ladir"
	  fi
	fi # $installed = yes
	func_stripname 'lib' '.la' "$laname"
	name=$func_stripname_result

	# This library was specified with -dlpreopen.
	if test "$pass" = dlpreopen; then
	  if test -z "$libdir" && test "$linkmode" = prog; then
	    func_fatal_error "only libraries may -dlpreopen a convenience library: \`$lib'"
	  fi
	  case "$host" in
	    # special handling for platforms with PE-DLLs.
	    *cygwin* | *mingw* | *cegcc* )
	      # Linker will automatically link against shared library if both
	      # static and shared are present.  Therefore, ensure we extract
	      # symbols from the import library if a shared library is present
	      # (otherwise, the dlopen module name will be incorrect).  We do
	      # this by putting the import library name into $newdlprefiles.
	      # We recover the dlopen module name by 'saving' the la file
	      # name in a special purpose variable, and (later) extracting the
	      # dlname from the la file.
	      if test -n "$dlname"; then
	        func_tr_sh "$dir/$linklib"
	        eval "libfile_$func_tr_sh_result=\$abs_ladir/\$laname"
	        func_append newdlprefiles " $dir/$linklib"
	      else
	        func_append newdlprefiles " $dir/$old_library"
	        # Keep a list of preopened convenience libraries to check
	        # that they are being used correctly in the link pass.
	        test -z "$libdir" && \
	          func_append dlpreconveniencelibs " $dir/$old_library"
	      fi
	    ;;
	    * )
	      # Prefer using a static library (so that no silly _DYNAMIC symbols
	      # are required to link).
	      if test -n "$old_library"; then
	        func_append newdlprefiles " $dir/$old_library"
	        # Keep a list of preopened convenience libraries to check
	        # that they are being used correctly in the link pass.
	        test -z "$libdir" && \
	          func_append dlpreconveniencelibs " $dir/$old_library"
	      # Otherwise, use the dlname, so that lt_dlopen finds it.
	      elif test -n "$dlname"; then
	        func_append newdlprefiles " $dir/$dlname"
	      else
	        func_append newdlprefiles " $dir/$linklib"
	      fi
	    ;;
	  esac
	fi # $pass = dlpreopen

	if test -z "$libdir"; then
	  # Link the convenience library
	  if test "$linkmode" = lib; then
	    deplibs="$dir/$old_library $deplibs"
	  elif test "$linkmode,$pass" = "prog,link"; then
	    compile_deplibs="$dir/$old_library $compile_deplibs"
	    finalize_deplibs="$dir/$old_library $finalize_deplibs"
	  else
	    deplibs="$lib $deplibs" # used for prog,scan pass
	  fi
	  continue
	fi


	if test "$linkmode" = prog && test "$pass" != link; then
	  func_append newlib_search_path " $ladir"
	  deplibs="$lib $deplibs"

	  linkalldeplibs=no
	  if test "$link_all_deplibs" != no || test -z "$library_names" ||
	     test "$build_libtool_libs" = no; then
	    linkalldeplibs=yes
	  fi

	  tmp_libs=
	  for deplib in $dependency_libs; do
	    case $deplib in
	    -L*) func_stripname '-L' '' "$deplib"
	         func_resolve_sysroot "$func_stripname_result"
	         func_append newlib_search_path " $func_resolve_sysroot_result"
		 ;;
	    esac
	    # Need to link against all dependency_libs?
	    if test "$linkalldeplibs" = yes; then
	      deplibs="$deplib $deplibs"
	    else
	      # Need to hardcode shared library paths
	      # or/and link against static libraries
	      newdependency_libs="$deplib $newdependency_libs"
	    fi
	    if $opt_preserve_dup_deps ; then
	      case "$tmp_libs " in
	      *" $deplib "*) func_append specialdeplibs " $deplib" ;;
	      esac
	    fi
	    func_append tmp_libs " $deplib"
	  done # for deplib
	  continue
	fi # $linkmode = prog...

	if test "$linkmode,$pass" = "prog,link"; then
	  if test -n "$library_names" &&
	     { { test "$prefer_static_libs" = no ||
	         test "$prefer_static_libs,$installed" = "built,yes"; } ||
	       test -z "$old_library"; }; then
	    # We need to hardcode the library path
	    if test -n "$shlibpath_var" && test -z "$avoidtemprpath" ; then
	      # Make sure the rpath contains only unique directories.
	      case "$temp_rpath:" in
	      *"$absdir:"*) ;;
	      *) func_append temp_rpath "$absdir:" ;;
	      esac
	    fi

	    # Hardcode the library path.
	    # Skip directories that are in the system default run-time
	    # search path.
	    case " $sys_lib_dlsearch_path " in
	    *" $absdir "*) ;;
	    *)
	      case "$compile_rpath " in
	      *" $absdir "*) ;;
	      *) func_append compile_rpath " $absdir" ;;
	      esac
	      ;;
	    esac
	    case " $sys_lib_dlsearch_path " in
	    *" $libdir "*) ;;
	    *)
	      case "$finalize_rpath " in
	      *" $libdir "*) ;;
	      *) func_append finalize_rpath " $libdir" ;;
	      esac
	      ;;
	    esac
	  fi # $linkmode,$pass = prog,link...

	  if test "$alldeplibs" = yes &&
	     { test "$deplibs_check_method" = pass_all ||
	       { test "$build_libtool_libs" = yes &&
		 test -n "$library_names"; }; }; then
	    # We only need to search for static libraries
	    continue
	  fi
	fi

	link_static=no # Whether the deplib will be linked statically
	use_static_libs=$prefer_static_libs
	if test "$use_static_libs" = built && test "$installed" = yes; then
	  use_static_libs=no
	fi
	if test -n "$library_names" &&
	   { test "$use_static_libs" = no || test -z "$old_library"; }; then
	  case $host in
	  *cygwin* | *mingw* | *cegcc*)
	      # No point in relinking DLLs because paths are not encoded
	      func_append notinst_deplibs " $lib"
	      need_relink=no
	    ;;
	  *)
	    if test "$installed" = no; then
	      func_append notinst_deplibs " $lib"
	      need_relink=yes
	    fi
	    ;;
	  esac
	  # This is a shared library

	  # Warn about portability, can't link against -module's on some
	  # systems (darwin).  Don't bleat about dlopened modules though!
	  dlopenmodule=""
	  for dlpremoduletest in $dlprefiles; do
	    if test "X$dlpremoduletest" = "X$lib"; then
	      dlopenmodule="$dlpremoduletest"
	      break
	    fi
	  done
	  if test -z "$dlopenmodule" && test "$shouldnotlink" = yes && test "$pass" = link; then
	    echo
	    if test "$linkmode" = prog; then
	      $ECHO "*** Warning: Linking the executable $output against the loadable module"
	    else
	      $ECHO "*** Warning: Linking the shared library $output against the loadable module"
	    fi
	    $ECHO "*** $linklib is not portable!"
	  fi
	  if test "$linkmode" = lib &&
	     test "$hardcode_into_libs" = yes; then
	    # Hardcode the library path.
	    # Skip directories that are in the system default run-time
	    # search path.
	    case " $sys_lib_dlsearch_path " in
	    *" $absdir "*) ;;
	    *)
	      case "$compile_rpath " in
	      *" $absdir "*) ;;
	      *) func_append compile_rpath " $absdir" ;;
	      esac
	      ;;
	    esac
	    case " $sys_lib_dlsearch_path " in
	    *" $libdir "*) ;;
	    *)
	      case "$finalize_rpath " in
	      *" $libdir "*) ;;
	      *) func_append finalize_rpath " $libdir" ;;
	      esac
	      ;;
	    esac
	  fi

	  if test -n "$old_archive_from_expsyms_cmds"; then
	    # figure out the soname
	    set dummy $library_names
	    shift
	    realname="$1"
	    shift
	    libname=`eval "\\$ECHO \"$libname_spec\""`
	    # use dlname if we got it. it's perfectly good, no?
	    if test -n "$dlname"; then
	      soname="$dlname"
	    elif test -n "$soname_spec"; then
	      # bleh windows
	      case $host in
	      *cygwin* | mingw* | *cegcc*)
	        func_arith $current - $age
		major=$func_arith_result
		versuffix="-$major"
		;;
	      esac
	      eval soname=\"$soname_spec\"
	    else
	      soname="$realname"
	    fi

	    # Make a new name for the extract_expsyms_cmds to use
	    soroot="$soname"
	    func_basename "$soroot"
	    soname="$func_basename_result"
	    func_stripname 'lib' '.dll' "$soname"
	    newlib=libimp-$func_stripname_result.a

	    # If the library has no export list, then create one now
	    if test -f "$output_objdir/$soname-def"; then :
	    else
	      func_verbose "extracting exported symbol list from \`$soname'"
	      func_execute_cmds "$extract_expsyms_cmds" 'exit $?'
	    fi

	    # Create $newlib
	    if test -f "$output_objdir/$newlib"; then :; else
	      func_verbose "generating import library for \`$soname'"
	      func_execute_cmds "$old_archive_from_expsyms_cmds" 'exit $?'
	    fi
	    # make sure the library variables are pointing to the new library
	    dir=$output_objdir
	    linklib=$newlib
	  fi # test -n "$old_archive_from_expsyms_cmds"

	  if test "$linkmode" = prog || test "$opt_mode" != relink; then
	    add_shlibpath=
	    add_dir=
	    add=
	    lib_linked=yes
	    case $hardcode_action in
	    immediate | unsupported)
	      if test "$hardcode_direct" = no; then
		add="$dir/$linklib"
		case $host in
		  *-*-sco3.2v5.0.[024]*) add_dir="-L$dir" ;;
		  *-*-sysv4*uw2*) add_dir="-L$dir" ;;
		  *-*-sysv5OpenUNIX* | *-*-sysv5UnixWare7.[01].[10]* | \
		    *-*-unixware7*) add_dir="-L$dir" ;;
		  *-*-darwin* )
		    # if the lib is a (non-dlopened) module then we can not
		    # link against it, someone is ignoring the earlier warnings
		    if /usr/bin/file -L $add 2> /dev/null |
			 $GREP ": [^:]* bundle" >/dev/null ; then
		      if test "X$dlopenmodule" != "X$lib"; then
			$ECHO "*** Warning: lib $linklib is a module, not a shared library"
			if test -z "$old_library" ; then
			  echo
			  echo "*** And there doesn't seem to be a static archive available"
			  echo "*** The link will probably fail, sorry"
			else
			  add="$dir/$old_library"
			fi
		      elif test -n "$old_library"; then
			add="$dir/$old_library"
		      fi
		    fi
		esac
	      elif test "$hardcode_minus_L" = no; then
		case $host in
		*-*-sunos*) add_shlibpath="$dir" ;;
		esac
		add_dir="-L$dir"
		add="-l$name"
	      elif test "$hardcode_shlibpath_var" = no; then
		add_shlibpath="$dir"
		add="-l$name"
	      else
		lib_linked=no
	      fi
	      ;;
	    relink)
	      if test "$hardcode_direct" = yes &&
	         test "$hardcode_direct_absolute" = no; then
		add="$dir/$linklib"
	      elif test "$hardcode_minus_L" = yes; then
		add_dir="-L$absdir"
		# Try looking first in the location we're being installed to.
		if test -n "$inst_prefix_dir"; then
		  case $libdir in
		    [\\/]*)
		      func_append add_dir " -L$inst_prefix_dir$libdir"
		      ;;
		  esac
		fi
		add="-l$name"
	      elif test "$hardcode_shlibpath_var" = yes; then
		add_shlibpath="$dir"
		add="-l$name"
	      else
		lib_linked=no
	      fi
	      ;;
	    *) lib_linked=no ;;
	    esac

	    if test "$lib_linked" != yes; then
	      func_fatal_configuration "unsupported hardcode properties"
	    fi

	    if test -n "$add_shlibpath"; then
	      case :$compile_shlibpath: in
	      *":$add_shlibpath:"*) ;;
	      *) func_append compile_shlibpath "$add_shlibpath:" ;;
	      esac
	    fi
	    if test "$linkmode" = prog; then
	      test -n "$add_dir" && compile_deplibs="$add_dir $compile_deplibs"
	      test -n "$add" && compile_deplibs="$add $compile_deplibs"
	    else
	      test -n "$add_dir" && deplibs="$add_dir $deplibs"
	      test -n "$add" && deplibs="$add $deplibs"
	      if test "$hardcode_direct" != yes &&
		 test "$hardcode_minus_L" != yes &&
		 test "$hardcode_shlibpath_var" = yes; then
		case :$finalize_shlibpath: in
		*":$libdir:"*) ;;
		*) func_append finalize_shlibpath "$libdir:" ;;
		esac
	      fi
	    fi
	  fi

	  if test "$linkmode" = prog || test "$opt_mode" = relink; then
	    add_shlibpath=
	    add_dir=
	    add=
	    # Finalize command for both is simple: just hardcode it.
	    if test "$hardcode_direct" = yes &&
	       test "$hardcode_direct_absolute" = no; then
	      add="$libdir/$linklib"
	    elif test "$hardcode_minus_L" = yes; then
	      add_dir="-L$libdir"
	      add="-l$name"
	    elif test "$hardcode_shlibpath_var" = yes; then
	      case :$finalize_shlibpath: in
	      *":$libdir:"*) ;;
	      *) func_append finalize_shlibpath "$libdir:" ;;
	      esac
	      add="-l$name"
	    elif test "$hardcode_automatic" = yes; then
	      if test -n "$inst_prefix_dir" &&
		 test -f "$inst_prefix_dir$libdir/$linklib" ; then
		add="$inst_prefix_dir$libdir/$linklib"
	      else
		add="$libdir/$linklib"
	      fi
	    else
	      # We cannot seem to hardcode it, guess we'll fake it.
	      add_dir="-L$libdir"
	      # Try looking first in the location we're being installed to.
	      if test -n "$inst_prefix_dir"; then
		case $libdir in
		  [\\/]*)
		    func_append add_dir " -L$inst_prefix_dir$libdir"
		    ;;
		esac
	      fi
	      add="-l$name"
	    fi

	    if test "$linkmode" = prog; then
	      test -n "$add_dir" && finalize_deplibs="$add_dir $finalize_deplibs"
	      test -n "$add" && finalize_deplibs="$add $finalize_deplibs"
	    else
	      test -n "$add_dir" && deplibs="$add_dir $deplibs"
	      test -n "$add" && deplibs="$add $deplibs"
	    fi
	  fi
	elif test "$linkmode" = prog; then
	  # Here we assume that one of hardcode_direct or hardcode_minus_L
	  # is not unsupported.  This is valid on all known static and
	  # shared platforms.
	  if test "$hardcode_direct" != unsupported; then
	    test -n "$old_library" && linklib="$old_library"
	    compile_deplibs="$dir/$linklib $compile_deplibs"
	    finalize_deplibs="$dir/$linklib $finalize_deplibs"
	  else
	    compile_deplibs="-l$name -L$dir $compile_deplibs"
	    finalize_deplibs="-l$name -L$dir $finalize_deplibs"
	  fi
	elif test "$build_libtool_libs" = yes; then
	  # Not a shared library
	  if test "$deplibs_check_method" != pass_all; then
	    # We're trying link a shared library against a static one
	    # but the system doesn't support it.

	    # Just print a warning and add the library to dependency_libs so
	    # that the program can be linked against the static library.
	    echo
	    $ECHO "*** Warning: This system can not link to static lib archive $lib."
	    echo "*** I have the capability to make that library automatically link in when"
	    echo "*** you link to this library.  But I can only do this if you have a"
	    echo "*** shared version of the library, which you do not appear to have."
	    if test "$module" = yes; then
	      echo "*** But as you try to build a module library, libtool will still create "
	      echo "*** a static module, that should work as long as the dlopening application"
	      echo "*** is linked with the -dlopen flag to resolve symbols at runtime."
	      if test -z "$global_symbol_pipe"; then
		echo
		echo "*** However, this would only work if libtool was able to extract symbol"
		echo "*** lists from a program, using \`nm' or equivalent, but libtool could"
		echo "*** not find such a program.  So, this module is probably useless."
		echo "*** \`nm' from GNU binutils and a full rebuild may help."
	      fi
	      if test "$build_old_libs" = no; then
		build_libtool_libs=module
		build_old_libs=yes
	      else
		build_libtool_libs=no
	      fi
	    fi
	  else
	    deplibs="$dir/$old_library $deplibs"
	    link_static=yes
	  fi
	fi # link shared/static library?

	if test "$linkmode" = lib; then
	  if test -n "$dependency_libs" &&
	     { test "$hardcode_into_libs" != yes ||
	       test "$build_old_libs" = yes ||
	       test "$link_static" = yes; }; then
	    # Extract -R from dependency_libs
	    temp_deplibs=
	    for libdir in $dependency_libs; do
	      case $libdir in
	      -R*) func_stripname '-R' '' "$libdir"
	           temp_xrpath=$func_stripname_result
		   case " $xrpath " in
		   *" $temp_xrpath "*) ;;
		   *) func_append xrpath " $temp_xrpath";;
		   esac;;
	      *) func_append temp_deplibs " $libdir";;
	      esac
	    done
	    dependency_libs="$temp_deplibs"
	  fi

	  func_append newlib_search_path " $absdir"
	  # Link against this library
	  test "$link_static" = no && newdependency_libs="$abs_ladir/$laname $newdependency_libs"
	  # ... and its dependency_libs
	  tmp_libs=
	  for deplib in $dependency_libs; do
	    newdependency_libs="$deplib $newdependency_libs"
	    case $deplib in
              -L*) func_stripname '-L' '' "$deplib"
                   func_resolve_sysroot "$func_stripname_result";;
              *) func_resolve_sysroot "$deplib" ;;
            esac
	    if $opt_preserve_dup_deps ; then
	      case "$tmp_libs " in
	      *" $func_resolve_sysroot_result "*)
                func_append specialdeplibs " $func_resolve_sysroot_result" ;;
	      esac
	    fi
	    func_append tmp_libs " $func_resolve_sysroot_result"
	  done

	  if test "$link_all_deplibs" != no; then
	    # Add the search paths of all dependency libraries
	    for deplib in $dependency_libs; do
	      path=
	      case $deplib in
	      -L*) path="$deplib" ;;
	      *.la)
	        func_resolve_sysroot "$deplib"
	        deplib=$func_resolve_sysroot_result
	        func_dirname "$deplib" "" "."
		dir=$func_dirname_result
		# We need an absolute path.
		case $dir in
		[\\/]* | [A-Za-z]:[\\/]*) absdir="$dir" ;;
		*)
		  absdir=`cd "$dir" && pwd`
		  if test -z "$absdir"; then
		    func_warning "cannot determine absolute directory name of \`$dir'"
		    absdir="$dir"
		  fi
		  ;;
		esac
		if $GREP "^installed=no" $deplib > /dev/null; then
		case $host in
		*-*-darwin*)
		  depdepl=
		  eval deplibrary_names=`${SED} -n -e 's/^library_names=\(.*\)$/\1/p' $deplib`
		  if test -n "$deplibrary_names" ; then
		    for tmp in $deplibrary_names ; do
		      depdepl=$tmp
		    done
		    if test -f "$absdir/$objdir/$depdepl" ; then
		      depdepl="$absdir/$objdir/$depdepl"
		      darwin_install_name=`${OTOOL} -L $depdepl | awk '{if (NR == 2) {print $1;exit}}'`
                      if test -z "$darwin_install_name"; then
                          darwin_install_name=`${OTOOL64} -L $depdepl  | awk '{if (NR == 2) {print $1;exit}}'`
                      fi
		      func_append compiler_flags " ${wl}-dylib_file ${wl}${darwin_install_name}:${depdepl}"
		      func_append linker_flags " -dylib_file ${darwin_install_name}:${depdepl}"
		      path=
		    fi
		  fi
		  ;;
		*)
		  path="-L$absdir/$objdir"
		  ;;
		esac
		else
		  eval libdir=`${SED} -n -e 's/^libdir=\(.*\)$/\1/p' $deplib`
		  test -z "$libdir" && \
		    func_fatal_error "\`$deplib' is not a valid libtool archive"
		  test "$absdir" != "$libdir" && \
		    func_warning "\`$deplib' seems to be moved"

		  path="-L$absdir"
		fi
		;;
	      esac
	      case " $deplibs " in
	      *" $path "*) ;;
	      *) deplibs="$path $deplibs" ;;
	      esac
	    done
	  fi # link_all_deplibs != no
	fi # linkmode = lib
      done # for deplib in $libs
      if test "$pass" = link; then
	if test "$linkmode" = "prog"; then
	  compile_deplibs="$new_inherited_linker_flags $compile_deplibs"
	  finalize_deplibs="$new_inherited_linker_flags $finalize_deplibs"
	else
	  compiler_flags="$compiler_flags "`$ECHO " $new_inherited_linker_flags" | $SED 's% \([^ $]*\).ltframework% -framework \1%g'`
	fi
      fi
      dependency_libs="$newdependency_libs"
      if test "$pass" = dlpreopen; then
	# Link the dlpreopened libraries before other libraries
	for deplib in $save_deplibs; do
	  deplibs="$deplib $deplibs"
	done
      fi
      if test "$pass" != dlopen; then
	if test "$pass" != conv; then
	  # Make sure lib_search_path contains only unique directories.
	  lib_search_path=
	  for dir in $newlib_search_path; do
	    case "$lib_search_path " in
	    *" $dir "*) ;;
	    *) func_append lib_search_path " $dir" ;;
	    esac
	  done
	  newlib_search_path=
	fi

	if test "$linkmode,$pass" != "prog,link"; then
	  vars="deplibs"
	else
	  vars="compile_deplibs finalize_deplibs"
	fi
	for var in $vars dependency_libs; do
	  # Add libraries to $var in reverse order
	  eval tmp_libs=\"\$$var\"
	  new_libs=
	  for deplib in $tmp_libs; do
	    # FIXME: Pedantically, this is the right thing to do, so
	    #        that some nasty dependency loop isn't accidentally
	    #        broken:
	    #new_libs="$deplib $new_libs"
	    # Pragmatically, this seems to cause very few problems in
	    # practice:
	    case $deplib in
	    -L*) new_libs="$deplib $new_libs" ;;
	    -R*) ;;
	    *)
	      # And here is the reason: when a library appears more
	      # than once as an explicit dependence of a library, or
	      # is implicitly linked in more than once by the
	      # compiler, it is considered special, and multiple
	      # occurrences thereof are not removed.  Compare this
	      # with having the same library being listed as a
	      # dependency of multiple other libraries: in this case,
	      # we know (pedantically, we assume) the library does not
	      # need to be listed more than once, so we keep only the
	      # last copy.  This is not always right, but it is rare
	      # enough that we require users that really mean to play
	      # such unportable linking tricks to link the library
	      # using -Wl,-lname, so that libtool does not consider it
	      # for duplicate removal.
	      case " $specialdeplibs " in
	      *" $deplib "*) new_libs="$deplib $new_libs" ;;
	      *)
		case " $new_libs " in
		*" $deplib "*) ;;
		*) new_libs="$deplib $new_libs" ;;
		esac
		;;
	      esac
	      ;;
	    esac
	  done
	  tmp_libs=
	  for deplib in $new_libs; do
	    case $deplib in
	    -L*)
	      case " $tmp_libs " in
	      *" $deplib "*) ;;
	      *) func_append tmp_libs " $deplib" ;;
	      esac
	      ;;
	    *) func_append tmp_libs " $deplib" ;;
	    esac
	  done
	  eval $var=\"$tmp_libs\"
	done # for var
      fi
      # Last step: remove runtime libs from dependency_libs
      # (they stay in deplibs)
      tmp_libs=
      for i in $dependency_libs ; do
	case " $predeps $postdeps $compiler_lib_search_path " in
	*" $i "*)
	  i=""
	  ;;
	esac
	if test -n "$i" ; then
	  func_append tmp_libs " $i"
	fi
      done
      dependency_libs=$tmp_libs
    done # for pass
    if test "$linkmode" = prog; then
      dlfiles="$newdlfiles"
    fi
    if test "$linkmode" = prog || test "$linkmode" = lib; then
      dlprefiles="$newdlprefiles"
    fi

    case $linkmode in
    oldlib)
      if test -n "$dlfiles$dlprefiles" || test "$dlself" != no; then
	func_warning "\`-dlopen' is ignored for archives"
      fi

      case " $deplibs" in
      *\ -l* | *\ -L*)
	func_warning "\`-l' and \`-L' are ignored for archives" ;;
      esac

      test -n "$rpath" && \
	func_warning "\`-rpath' is ignored for archives"

      test -n "$xrpath" && \
	func_warning "\`-R' is ignored for archives"

      test -n "$vinfo" && \
	func_warning "\`-version-info/-version-number' is ignored for archives"

      test -n "$release" && \
	func_warning "\`-release' is ignored for archives"

      test -n "$export_symbols$export_symbols_regex" && \
	func_warning "\`-export-symbols' is ignored for archives"

      # Now set the variables for building old libraries.
      build_libtool_libs=no
      oldlibs="$output"
      func_append objs "$old_deplibs"
      ;;

    lib)
      # Make sure we only generate libraries of the form `libNAME.la'.
      case $outputname in
      lib*)
	func_stripname 'lib' '.la' "$outputname"
	name=$func_stripname_result
	eval shared_ext=\"$shrext_cmds\"
	eval libname=\"$libname_spec\"
	;;
      *)
	test "$module" = no && \
	  func_fatal_help "libtool library \`$output' must begin with \`lib'"

	if test "$need_lib_prefix" != no; then
	  # Add the "lib" prefix for modules if required
	  func_stripname '' '.la' "$outputname"
	  name=$func_stripname_result
	  eval shared_ext=\"$shrext_cmds\"
	  eval libname=\"$libname_spec\"
	else
	  func_stripname '' '.la' "$outputname"
	  libname=$func_stripname_result
	fi
	;;
      esac

      if test -n "$objs"; then
	if test "$deplibs_check_method" != pass_all; then
	  func_fatal_error "cannot build libtool library \`$output' from non-libtool objects on this host:$objs"
	else
	  echo
	  $ECHO "*** Warning: Linking the shared library $output against the non-libtool"
	  $ECHO "*** objects $objs is not portable!"
	  func_append libobjs " $objs"
	fi
      fi

      test "$dlself" != no && \
	func_warning "\`-dlopen self' is ignored for libtool libraries"

      set dummy $rpath
      shift
      test "$#" -gt 1 && \
	func_warning "ignoring multiple \`-rpath's for a libtool library"

      install_libdir="$1"

      oldlibs=
      if test -z "$rpath"; then
	if test "$build_libtool_libs" = yes; then
	  # Building a libtool convenience library.
	  # Some compilers have problems with a `.al' extension so
	  # convenience libraries should have the same extension an
	  # archive normally would.
	  oldlibs="$output_objdir/$libname.$libext $oldlibs"
	  build_libtool_libs=convenience
	  build_old_libs=yes
	fi

	test -n "$vinfo" && \
	  func_warning "\`-version-info/-version-number' is ignored for convenience libraries"

	test -n "$release" && \
	  func_warning "\`-release' is ignored for convenience libraries"
      else

	# Parse the version information argument.
	save_ifs="$IFS"; IFS=':'
	set dummy $vinfo 0 0 0
	shift
	IFS="$save_ifs"

	test -n "$7" && \
	  func_fatal_help "too many parameters to \`-version-info'"

	# convert absolute version numbers to libtool ages
	# this retains compatibility with .la files and attempts
	# to make the code below a bit more comprehensible

	case $vinfo_number in
	yes)
	  number_major="$1"
	  number_minor="$2"
	  number_revision="$3"
	  #
	  # There are really only two kinds -- those that
	  # use the current revision as the major version
	  # and those that subtract age and use age as
	  # a minor version.  But, then there is irix
	  # which has an extra 1 added just for fun
	  #
	  case $version_type in
	  # correct linux to gnu/linux during the next big refactor
	  darwin|linux|osf|windows|none)
	    func_arith $number_major + $number_minor
	    current=$func_arith_result
	    age="$number_minor"
	    revision="$number_revision"
	    ;;
	  freebsd-aout|freebsd-elf|qnx|sunos)
	    current="$number_major"
	    revision="$number_minor"
	    age="0"
	    ;;
	  irix|nonstopux)
	    func_arith $number_major + $number_minor
	    current=$func_arith_result
	    age="$number_minor"
	    revision="$number_minor"
	    lt_irix_increment=no
	    ;;
	  *)
	    func_fatal_configuration "$modename: unknown library version type \`$version_type'"
	    ;;
	  esac
	  ;;
	no)
	  current="$1"
	  revision="$2"
	  age="$3"
	  ;;
	esac

	# Check that each of the things are valid numbers.
	case $current in
	0|[1-9]|[1-9][0-9]|[1-9][0-9][0-9]|[1-9][0-9][0-9][0-9]|[1-9][0-9][0-9][0-9][0-9]) ;;
	*)
	  func_error "CURRENT \`$current' must be a nonnegative integer"
	  func_fatal_error "\`$vinfo' is not valid version information"
	  ;;
	esac

	case $revision in
	0|[1-9]|[1-9][0-9]|[1-9][0-9][0-9]|[1-9][0-9][0-9][0-9]|[1-9][0-9][0-9][0-9][0-9]) ;;
	*)
	  func_error "REVISION \`$revision' must be a nonnegative integer"
	  func_fatal_error "\`$vinfo' is not valid version information"
	  ;;
	esac

	case $age in
	0|[1-9]|[1-9][0-9]|[1-9][0-9][0-9]|[1-9][0-9][0-9][0-9]|[1-9][0-9][0-9][0-9][0-9]) ;;
	*)
	  func_error "AGE \`$age' must be a nonnegative integer"
	  func_fatal_error "\`$vinfo' is not valid version information"
	  ;;
	esac

	if test "$age" -gt "$current"; then
	  func_error "AGE \`$age' is greater than the current interface number \`$current'"
	  func_fatal_error "\`$vinfo' is not valid version information"
	fi

	# Calculate the version variables.
	major=
	versuffix=
	verstring=
	case $version_type in
	none) ;;

	darwin)
	  # Like Linux, but with the current version available in
	  # verstring for coding it into the library header
	  func_arith $current - $age
	  major=.$func_arith_result
	  versuffix="$major.$age.$revision"
	  # Darwin ld doesn't like 0 for these options...
	  func_arith $current + 1
	  minor_current=$func_arith_result
	  xlcverstring="${wl}-compatibility_version ${wl}$minor_current ${wl}-current_version ${wl}$minor_current.$revision"
	  verstring="-compatibility_version $minor_current -current_version $minor_current.$revision"
	  ;;

	freebsd-aout)
	  major=".$current"
	  versuffix=".$current.$revision";
	  ;;

	freebsd-elf)
	  major=".$current"
	  versuffix=".$current"
	  ;;

	irix | nonstopux)
	  if test "X$lt_irix_increment" = "Xno"; then
	    func_arith $current - $age
	  else
	    func_arith $current - $age + 1
	  fi
	  major=$func_arith_result

	  case $version_type in
	    nonstopux) verstring_prefix=nonstopux ;;
	    *)         verstring_prefix=sgi ;;
	  esac
	  verstring="$verstring_prefix$major.$revision"

	  # Add in all the interfaces that we are compatible with.
	  loop=$revision
	  while test "$loop" -ne 0; do
	    func_arith $revision - $loop
	    iface=$func_arith_result
	    func_arith $loop - 1
	    loop=$func_arith_result
	    verstring="$verstring_prefix$major.$iface:$verstring"
	  done

	  # Before this point, $major must not contain `.'.
	  major=.$major
	  versuffix="$major.$revision"
	  ;;

	linux) # correct to gnu/linux during the next big refactor
	  func_arith $current - $age
	  major=.$func_arith_result
	  versuffix="$major.$age.$revision"
	  ;;

	osf)
	  func_arith $current - $age
	  major=.$func_arith_result
	  versuffix=".$current.$age.$revision"
	  verstring="$current.$age.$revision"

	  # Add in all the interfaces that we are compatible with.
	  loop=$age
	  while test "$loop" -ne 0; do
	    func_arith $current - $loop
	    iface=$func_arith_result
	    func_arith $loop - 1
	    loop=$func_arith_result
	    verstring="$verstring:${iface}.0"
	  done

	  # Make executables depend on our current version.
	  func_append verstring ":${current}.0"
	  ;;

	qnx)
	  major=".$current"
	  versuffix=".$current"
	  ;;

	sunos)
	  major=".$current"
	  versuffix=".$current.$revision"
	  ;;

	windows)
	  # Use '-' rather than '.', since we only want one
	  # extension on DOS 8.3 filesystems.
	  func_arith $current - $age
	  major=$func_arith_result
	  versuffix="-$major"
	  ;;

	*)
	  func_fatal_configuration "unknown library version type \`$version_type'"
	  ;;
	esac

	# Clear the version info if we defaulted, and they specified a release.
	if test -z "$vinfo" && test -n "$release"; then
	  major=
	  case $version_type in
	  darwin)
	    # we can't check for "0.0" in archive_cmds due to quoting
	    # problems, so we reset it completely
	    verstring=
	    ;;
	  *)
	    verstring="0.0"
	    ;;
	  esac
	  if test "$need_version" = no; then
	    versuffix=
	  else
	    versuffix=".0.0"
	  fi
	fi

	# Remove version info from name if versioning should be avoided
	if test "$avoid_version" = yes && test "$need_version" = no; then
	  major=
	  versuffix=
	  verstring=""
	fi

	# Check to see if the archive will have undefined symbols.
	if test "$allow_undefined" = yes; then
	  if test "$allow_undefined_flag" = unsupported; then
	    func_warning "undefined symbols not allowed in $host shared libraries"
	    build_libtool_libs=no
	    build_old_libs=yes
	  fi
	else
	  # Don't allow undefined symbols.
	  allow_undefined_flag="$no_undefined_flag"
	fi

      fi

      func_generate_dlsyms "$libname" "$libname" "yes"
      func_append libobjs " $symfileobj"
      test "X$libobjs" = "X " && libobjs=

      if test "$opt_mode" != relink; then
	# Remove our outputs, but don't remove object files since they
	# may have been created when compiling PIC objects.
	removelist=
	tempremovelist=`$ECHO "$output_objdir/*"`
	for p in $tempremovelist; do
	  case $p in
	    *.$objext | *.gcno)
	       ;;
	    $output_objdir/$outputname | $output_objdir/$libname.* | $output_objdir/${libname}${release}.*)
	       if test "X$precious_files_regex" != "X"; then
		 if $ECHO "$p" | $EGREP -e "$precious_files_regex" >/dev/null 2>&1
		 then
		   continue
		 fi
	       fi
	       func_append removelist " $p"
	       ;;
	    *) ;;
	  esac
	done
	test -n "$removelist" && \
	  func_show_eval "${RM}r \$removelist"
      fi

      # Now set the variables for building old libraries.
      if test "$build_old_libs" = yes && test "$build_libtool_libs" != convenience ; then
	func_append oldlibs " $output_objdir/$libname.$libext"

	# Transform .lo files to .o files.
	oldobjs="$objs "`$ECHO "$libobjs" | $SP2NL | $SED "/\.${libext}$/d; $lo2o" | $NL2SP`
      fi

      # Eliminate all temporary directories.
      #for path in $notinst_path; do
      #	lib_search_path=`$ECHO "$lib_search_path " | $SED "s% $path % %g"`
      #	deplibs=`$ECHO "$deplibs " | $SED "s% -L$path % %g"`
      #	dependency_libs=`$ECHO "$dependency_libs " | $SED "s% -L$path % %g"`
      #done

      if test -n "$xrpath"; then
	# If the user specified any rpath flags, then add them.
	temp_xrpath=
	for libdir in $xrpath; do
	  func_replace_sysroot "$libdir"
	  func_append temp_xrpath " -R$func_replace_sysroot_result"
	  case "$finalize_rpath " in
	  *" $libdir "*) ;;
	  *) func_append finalize_rpath " $libdir" ;;
	  esac
	done
	if test "$hardcode_into_libs" != yes || test "$build_old_libs" = yes; then
	  dependency_libs="$temp_xrpath $dependency_libs"
	fi
      fi

      # Make sure dlfiles contains only unique files that won't be dlpreopened
      old_dlfiles="$dlfiles"
      dlfiles=
      for lib in $old_dlfiles; do
	case " $dlprefiles $dlfiles " in
	*" $lib "*) ;;
	*) func_append dlfiles " $lib" ;;
	esac
      done

      # Make sure dlprefiles contains only unique files
      old_dlprefiles="$dlprefiles"
      dlprefiles=
      for lib in $old_dlprefiles; do
	case "$dlprefiles " in
	*" $lib "*) ;;
	*) func_append dlprefiles " $lib" ;;
	esac
      done

      if test "$build_libtool_libs" = yes; then
	if test -n "$rpath"; then
	  case $host in
	  *-*-cygwin* | *-*-mingw* | *-*-pw32* | *-*-os2* | *-*-beos* | *-cegcc* | *-*-haiku*)
	    # these systems don't actually have a c library (as such)!
	    ;;
	  *-*-rhapsody* | *-*-darwin1.[012])
	    # Rhapsody C library is in the System framework
	    func_append deplibs " System.ltframework"
	    ;;
	  *-*-netbsd*)
	    # Don't link with libc until the a.out ld.so is fixed.
	    ;;
	  *-*-openbsd* | *-*-freebsd* | *-*-dragonfly*)
	    # Do not include libc due to us having libc/libc_r.
	    ;;
	  *-*-sco3.2v5* | *-*-sco5v6*)
	    # Causes problems with __ctype
	    ;;
	  *-*-sysv4.2uw2* | *-*-sysv5* | *-*-unixware* | *-*-OpenUNIX*)
	    # Compiler inserts libc in the correct place for threads to work
	    ;;
	  *)
	    # Add libc to deplibs on all other systems if necessary.
	    if test "$build_libtool_need_lc" = "yes"; then
	      func_append deplibs " -lc"
	    fi
	    ;;
	  esac
	fi

	# Transform deplibs into only deplibs that can be linked in shared.
	name_save=$name
	libname_save=$libname
	release_save=$release
	versuffix_save=$versuffix
	major_save=$major
	# I'm not sure if I'm treating the release correctly.  I think
	# release should show up in the -l (ie -lgmp5) so we don't want to
	# add it in twice.  Is that correct?
	release=""
	versuffix=""
	major=""
	newdeplibs=
	droppeddeps=no
	case $deplibs_check_method in
	pass_all)
	  # Don't check for shared/static.  Everything works.
	  # This might be a little naive.  We might want to check
	  # whether the library exists or not.  But this is on
	  # osf3 & osf4 and I'm not really sure... Just
	  # implementing what was already the behavior.
	  newdeplibs=$deplibs
	  ;;
	test_compile)
	  # This code stresses the "libraries are programs" paradigm to its
	  # limits. Maybe even breaks it.  We compile a program, linking it
	  # against the deplibs as a proxy for the library.  Then we can check
	  # whether they linked in statically or dynamically with ldd.
	  $opt_dry_run || $RM conftest.c
	  cat > conftest.c <<EOF
	  int main() { return 0; }
EOF
	  $opt_dry_run || $RM conftest
	  if $LTCC $LTCFLAGS -o conftest conftest.c $deplibs; then
	    ldd_output=`ldd conftest`
	    for i in $deplibs; do
	      case $i in
	      -l*)
		func_stripname -l '' "$i"
		name=$func_stripname_result
		if test "X$allow_libtool_libs_with_static_runtimes" = "Xyes" ; then
		  case " $predeps $postdeps " in
		  *" $i "*)
		    func_append newdeplibs " $i"
		    i=""
		    ;;
		  esac
		fi
		if test -n "$i" ; then
		  libname=`eval "\\$ECHO \"$libname_spec\""`
		  deplib_matches=`eval "\\$ECHO \"$library_names_spec\""`
		  set dummy $deplib_matches; shift
		  deplib_match=$1
		  if test `expr "$ldd_output" : ".*$deplib_match"` -ne 0 ; then
		    func_append newdeplibs " $i"
		  else
		    droppeddeps=yes
		    echo
		    $ECHO "*** Warning: dynamic linker does not accept needed library $i."
		    echo "*** I have the capability to make that library automatically link in when"
		    echo "*** you link to this library.  But I can only do this if you have a"
		    echo "*** shared version of the library, which I believe you do not have"
		    echo "*** because a test_compile did reveal that the linker did not use it for"
		    echo "*** its dynamic dependency list that programs get resolved with at runtime."
		  fi
		fi
		;;
	      *)
		func_append newdeplibs " $i"
		;;
	      esac
	    done
	  else
	    # Error occurred in the first compile.  Let's try to salvage
	    # the situation: Compile a separate program for each library.
	    for i in $deplibs; do
	      case $i in
	      -l*)
		func_stripname -l '' "$i"
		name=$func_stripname_result
		$opt_dry_run || $RM conftest
		if $LTCC $LTCFLAGS -o conftest conftest.c $i; then
		  ldd_output=`ldd conftest`
		  if test "X$allow_libtool_libs_with_static_runtimes" = "Xyes" ; then
		    case " $predeps $postdeps " in
		    *" $i "*)
		      func_append newdeplibs " $i"
		      i=""
		      ;;
		    esac
		  fi
		  if test -n "$i" ; then
		    libname=`eval "\\$ECHO \"$libname_spec\""`
		    deplib_matches=`eval "\\$ECHO \"$library_names_spec\""`
		    set dummy $deplib_matches; shift
		    deplib_match=$1
		    if test `expr "$ldd_output" : ".*$deplib_match"` -ne 0 ; then
		      func_append newdeplibs " $i"
		    else
		      droppeddeps=yes
		      echo
		      $ECHO "*** Warning: dynamic linker does not accept needed library $i."
		      echo "*** I have the capability to make that library automatically link in when"
		      echo "*** you link to this library.  But I can only do this if you have a"
		      echo "*** shared version of the library, which you do not appear to have"
		      echo "*** because a test_compile did reveal that the linker did not use this one"
		      echo "*** as a dynamic dependency that programs can get resolved with at runtime."
		    fi
		  fi
		else
		  droppeddeps=yes
		  echo
		  $ECHO "*** Warning!  Library $i is needed by this library but I was not able to"
		  echo "*** make it link in!  You will probably need to install it or some"
		  echo "*** library that it depends on before this library will be fully"
		  echo "*** functional.  Installing it before continuing would be even better."
		fi
		;;
	      *)
		func_append newdeplibs " $i"
		;;
	      esac
	    done
	  fi
	  ;;
	file_magic*)
	  set dummy $deplibs_check_method; shift
	  file_magic_regex=`expr "$deplibs_check_method" : "$1 \(.*\)"`
	  for a_deplib in $deplibs; do
	    case $a_deplib in
	    -l*)
	      func_stripname -l '' "$a_deplib"
	      name=$func_stripname_result
	      if test "X$allow_libtool_libs_with_static_runtimes" = "Xyes" ; then
		case " $predeps $postdeps " in
		*" $a_deplib "*)
		  func_append newdeplibs " $a_deplib"
		  a_deplib=""
		  ;;
		esac
	      fi
	      if test -n "$a_deplib" ; then
		libname=`eval "\\$ECHO \"$libname_spec\""`
		if test -n "$file_magic_glob"; then
		  libnameglob=`func_echo_all "$libname" | $SED -e $file_magic_glob`
		else
		  libnameglob=$libname
		fi
		test "$want_nocaseglob" = yes && nocaseglob=`shopt -p nocaseglob`
		for i in $lib_search_path $sys_lib_search_path $shlib_search_path; do
		  if test "$want_nocaseglob" = yes; then
		    shopt -s nocaseglob
		    potential_libs=`ls $i/$libnameglob[.-]* 2>/dev/null`
		    $nocaseglob
		  else
		    potential_libs=`ls $i/$libnameglob[.-]* 2>/dev/null`
		  fi
		  for potent_lib in $potential_libs; do
		      # Follow soft links.
		      if ls -lLd "$potent_lib" 2>/dev/null |
			 $GREP " -> " >/dev/null; then
			continue
		      fi
		      # The statement above tries to avoid entering an
		      # endless loop below, in case of cyclic links.
		      # We might still enter an endless loop, since a link
		      # loop can be closed while we follow links,
		      # but so what?
		      potlib="$potent_lib"
		      while test -h "$potlib" 2>/dev/null; do
			potliblink=`ls -ld $potlib | ${SED} 's/.* -> //'`
			case $potliblink in
			[\\/]* | [A-Za-z]:[\\/]*) potlib="$potliblink";;
			*) potlib=`$ECHO "$potlib" | $SED 's,[^/]*$,,'`"$potliblink";;
			esac
		      done
		      if eval $file_magic_cmd \"\$potlib\" 2>/dev/null |
			 $SED -e 10q |
			 $EGREP "$file_magic_regex" > /dev/null; then
			func_append newdeplibs " $a_deplib"
			a_deplib=""
			break 2
		      fi
		  done
		done
	      fi
	      if test -n "$a_deplib" ; then
		droppeddeps=yes
		echo
		$ECHO "*** Warning: linker path does not have real file for library $a_deplib."
		echo "*** I have the capability to make that library automatically link in when"
		echo "*** you link to this library.  But I can only do this if you have a"
		echo "*** shared version of the library, which you do not appear to have"
		echo "*** because I did check the linker path looking for a file starting"
		if test -z "$potlib" ; then
		  $ECHO "*** with $libname but no candidates were found. (...for file magic test)"
		else
		  $ECHO "*** with $libname and none of the candidates passed a file format test"
		  $ECHO "*** using a file magic. Last file checked: $potlib"
		fi
	      fi
	      ;;
	    *)
	      # Add a -L argument.
	      func_append newdeplibs " $a_deplib"
	      ;;
	    esac
	  done # Gone through all deplibs.
	  ;;
	match_pattern*)
	  set dummy $deplibs_check_method; shift
	  match_pattern_regex=`expr "$deplibs_check_method" : "$1 \(.*\)"`
	  for a_deplib in $deplibs; do
	    case $a_deplib in
	    -l*)
	      func_stripname -l '' "$a_deplib"
	      name=$func_stripname_result
	      if test "X$allow_libtool_libs_with_static_runtimes" = "Xyes" ; then
		case " $predeps $postdeps " in
		*" $a_deplib "*)
		  func_append newdeplibs " $a_deplib"
		  a_deplib=""
		  ;;
		esac
	      fi
	      if test -n "$a_deplib" ; then
		libname=`eval "\\$ECHO \"$libname_spec\""`
		for i in $lib_search_path $sys_lib_search_path $shlib_search_path; do
		  potential_libs=`ls $i/$libname[.-]* 2>/dev/null`
		  for potent_lib in $potential_libs; do
		    potlib="$potent_lib" # see symlink-check above in file_magic test
		    if eval "\$ECHO \"$potent_lib\"" 2>/dev/null | $SED 10q | \
		       $EGREP "$match_pattern_regex" > /dev/null; then
		      func_append newdeplibs " $a_deplib"
		      a_deplib=""
		      break 2
		    fi
		  done
		done
	      fi
	      if test -n "$a_deplib" ; then
		droppeddeps=yes
		echo
		$ECHO "*** Warning: linker path does not have real file for library $a_deplib."
		echo "*** I have the capability to make that library automatically link in when"
		echo "*** you link to this library.  But I can only do this if you have a"
		echo "*** shared version of the library, which you do not appear to have"
		echo "*** because I did check the linker path looking for a file starting"
		if test -z "$potlib" ; then
		  $ECHO "*** with $libname but no candidates were found. (...for regex pattern test)"
		else
		  $ECHO "*** with $libname and none of the candidates passed a file format test"
		  $ECHO "*** using a regex pattern. Last file checked: $potlib"
		fi
	      fi
	      ;;
	    *)
	      # Add a -L argument.
	      func_append newdeplibs " $a_deplib"
	      ;;
	    esac
	  done # Gone through all deplibs.
	  ;;
	none | unknown | *)
	  newdeplibs=""
	  tmp_deplibs=`$ECHO " $deplibs" | $SED 's/ -lc$//; s/ -[LR][^ ]*//g'`
	  if test "X$allow_libtool_libs_with_static_runtimes" = "Xyes" ; then
	    for i in $predeps $postdeps ; do
	      # can't use Xsed below, because $i might contain '/'
	      tmp_deplibs=`$ECHO " $tmp_deplibs" | $SED "s,$i,,"`
	    done
	  fi
	  case $tmp_deplibs in
	  *[!\	\ ]*)
	    echo
	    if test "X$deplibs_check_method" = "Xnone"; then
	      echo "*** Warning: inter-library dependencies are not supported in this platform."
	    else
	      echo "*** Warning: inter-library dependencies are not known to be supported."
	    fi
	    echo "*** All declared inter-library dependencies are being dropped."
	    droppeddeps=yes
	    ;;
	  esac
	  ;;
	esac
	versuffix=$versuffix_save
	major=$major_save
	release=$release_save
	libname=$libname_save
	name=$name_save

	case $host in
	*-*-rhapsody* | *-*-darwin1.[012])
	  # On Rhapsody replace the C library with the System framework
	  newdeplibs=`$ECHO " $newdeplibs" | $SED 's/ -lc / System.ltframework /'`
	  ;;
	esac

	if test "$droppeddeps" = yes; then
	  if test "$module" = yes; then
	    echo
	    echo "*** Warning: libtool could not satisfy all declared inter-library"
	    $ECHO "*** dependencies of module $libname.  Therefore, libtool will create"
	    echo "*** a static module, that should work as long as the dlopening"
	    echo "*** application is linked with the -dlopen flag."
	    if test -z "$global_symbol_pipe"; then
	      echo
	      echo "*** However, this would only work if libtool was able to extract symbol"
	      echo "*** lists from a program, using \`nm' or equivalent, but libtool could"
	      echo "*** not find such a program.  So, this module is probably useless."
	      echo "*** \`nm' from GNU binutils and a full rebuild may help."
	    fi
	    if test "$build_old_libs" = no; then
	      oldlibs="$output_objdir/$libname.$libext"
	      build_libtool_libs=module
	      build_old_libs=yes
	    else
	      build_libtool_libs=no
	    fi
	  else
	    echo "*** The inter-library dependencies that have been dropped here will be"
	    echo "*** automatically added whenever a program is linked with this library"
	    echo "*** or is declared to -dlopen it."

	    if test "$allow_undefined" = no; then
	      echo
	      echo "*** Since this library must not contain undefined symbols,"
	      echo "*** because either the platform does not support them or"
	      echo "*** it was explicitly requested with -no-undefined,"
	      echo "*** libtool will only create a static version of it."
	      if test "$build_old_libs" = no; then
		oldlibs="$output_objdir/$libname.$libext"
		build_libtool_libs=module
		build_old_libs=yes
	      else
		build_libtool_libs=no
	      fi
	    fi
	  fi
	fi
	# Done checking deplibs!
	deplibs=$newdeplibs
      fi
      # Time to change all our "foo.ltframework" stuff back to "-framework foo"
      case $host in
	*-*-darwin*)
	  newdeplibs=`$ECHO " $newdeplibs" | $SED 's% \([^ $]*\).ltframework% -framework \1%g'`
	  new_inherited_linker_flags=`$ECHO " $new_inherited_linker_flags" | $SED 's% \([^ $]*\).ltframework% -framework \1%g'`
	  deplibs=`$ECHO " $deplibs" | $SED 's% \([^ $]*\).ltframework% -framework \1%g'`
	  ;;
      esac

      # move library search paths that coincide with paths to not yet
      # installed libraries to the beginning of the library search list
      new_libs=
      for path in $notinst_path; do
	case " $new_libs " in
	*" -L$path/$objdir "*) ;;
	*)
	  case " $deplibs " in
	  *" -L$path/$objdir "*)
	    func_append new_libs " -L$path/$objdir" ;;
	  esac
	  ;;
	esac
      done
      for deplib in $deplibs; do
	case $deplib in
	-L*)
	  case " $new_libs " in
	  *" $deplib "*) ;;
	  *) func_append new_libs " $deplib" ;;
	  esac
	  ;;
	*) func_append new_libs " $deplib" ;;
	esac
      done
      deplibs="$new_libs"

      # All the library-specific variables (install_libdir is set above).
      library_names=
      old_library=
      dlname=

      # Test again, we may have decided not to build it any more
      if test "$build_libtool_libs" = yes; then
	# Remove ${wl} instances when linking with ld.
	# FIXME: should test the right _cmds variable.
	case $archive_cmds in
	  *\$LD\ *) wl= ;;
        esac
	if test "$hardcode_into_libs" = yes; then
	  # Hardcode the library paths
	  hardcode_libdirs=
	  dep_rpath=
	  rpath="$finalize_rpath"
	  test "$opt_mode" != relink && rpath="$compile_rpath$rpath"
	  for libdir in $rpath; do
	    if test -n "$hardcode_libdir_flag_spec"; then
	      if test -n "$hardcode_libdir_separator"; then
		func_replace_sysroot "$libdir"
		libdir=$func_replace_sysroot_result
		if test -z "$hardcode_libdirs"; then
		  hardcode_libdirs="$libdir"
		else
		  # Just accumulate the unique libdirs.
		  case $hardcode_libdir_separator$hardcode_libdirs$hardcode_libdir_separator in
		  *"$hardcode_libdir_separator$libdir$hardcode_libdir_separator"*)
		    ;;
		  *)
		    func_append hardcode_libdirs "$hardcode_libdir_separator$libdir"
		    ;;
		  esac
		fi
	      else
		eval flag=\"$hardcode_libdir_flag_spec\"
		func_append dep_rpath " $flag"
	      fi
	    elif test -n "$runpath_var"; then
	      case "$perm_rpath " in
	      *" $libdir "*) ;;
	      *) func_append perm_rpath " $libdir" ;;
	      esac
	    fi
	  done
	  # Substitute the hardcoded libdirs into the rpath.
	  if test -n "$hardcode_libdir_separator" &&
	     test -n "$hardcode_libdirs"; then
	    libdir="$hardcode_libdirs"
	    eval "dep_rpath=\"$hardcode_libdir_flag_spec\""
	  fi
	  if test -n "$runpath_var" && test -n "$perm_rpath"; then
	    # We should set the runpath_var.
	    rpath=
	    for dir in $perm_rpath; do
	      func_append rpath "$dir:"
	    done
	    eval "$runpath_var='$rpath\$$runpath_var'; export $runpath_var"
	  fi
	  test -n "$dep_rpath" && deplibs="$dep_rpath $deplibs"
	fi

	shlibpath="$finalize_shlibpath"
	test "$opt_mode" != relink && shlibpath="$compile_shlibpath$shlibpath"
	if test -n "$shlibpath"; then
	  eval "$shlibpath_var='$shlibpath\$$shlibpath_var'; export $shlibpath_var"
	fi

	# Get the real and link names of the library.
	eval shared_ext=\"$shrext_cmds\"
	eval library_names=\"$library_names_spec\"
	set dummy $library_names
	shift
	realname="$1"
	shift

	if test -n "$soname_spec"; then
	  eval soname=\"$soname_spec\"
	else
	  soname="$realname"
	fi
	if test -z "$dlname"; then
	  dlname=$soname
	fi

	lib="$output_objdir/$realname"
	linknames=
	for link
	do
	  func_append linknames " $link"
	done

	# Use standard objects if they are pic
	test -z "$pic_flag" && libobjs=`$ECHO "$libobjs" | $SP2NL | $SED "$lo2o" | $NL2SP`
	test "X$libobjs" = "X " && libobjs=

	delfiles=
	if test -n "$export_symbols" && test -n "$include_expsyms"; then
	  $opt_dry_run || cp "$export_symbols" "$output_objdir/$libname.uexp"
	  export_symbols="$output_objdir/$libname.uexp"
	  func_append delfiles " $export_symbols"
	fi

	orig_export_symbols=
	case $host_os in
	cygwin* | mingw* | cegcc*)
	  if test -n "$export_symbols" && test -z "$export_symbols_regex"; then
	    # exporting using user supplied symfile
	    if test "x`$SED 1q $export_symbols`" != xEXPORTS; then
	      # and it's NOT already a .def file. Must figure out
	      # which of the given symbols are data symbols and tag
	      # them as such. So, trigger use of export_symbols_cmds.
	      # export_symbols gets reassigned inside the "prepare
	      # the list of exported symbols" if statement, so the
	      # include_expsyms logic still works.
	      orig_export_symbols="$export_symbols"
	      export_symbols=
	      always_export_symbols=yes
	    fi
	  fi
	  ;;
	esac

	# Prepare the list of exported symbols
	if test -z "$export_symbols"; then
	  if test "$always_export_symbols" = yes || test -n "$export_symbols_regex"; then
	    func_verbose "generating symbol list for \`$libname.la'"
	    export_symbols="$output_objdir/$libname.exp"
	    $opt_dry_run || $RM $export_symbols
	    cmds=$export_symbols_cmds
	    save_ifs="$IFS"; IFS='~'
	    for cmd1 in $cmds; do
	      IFS="$save_ifs"
	      # Take the normal branch if the nm_file_list_spec branch
	      # doesn't work or if tool conversion is not needed.
	      case $nm_file_list_spec~$to_tool_file_cmd in
		*~func_convert_file_noop | *~func_convert_file_msys_to_w32 | ~*)
		  try_normal_branch=yes
		  eval cmd=\"$cmd1\"
		  func_len " $cmd"
		  len=$func_len_result
		  ;;
		*)
		  try_normal_branch=no
		  ;;
	      esac
	      if test "$try_normal_branch" = yes \
		 && { test "$len" -lt "$max_cmd_len" \
		      || test "$max_cmd_len" -le -1; }
	      then
		func_show_eval "$cmd" 'exit $?'
		skipped_export=false
	      elif test -n "$nm_file_list_spec"; then
		func_basename "$output"
		output_la=$func_basename_result
		save_libobjs=$libobjs
		save_output=$output
		output=${output_objdir}/${output_la}.nm
		func_to_tool_file "$output"
		libobjs=$nm_file_list_spec$func_to_tool_file_result
		func_append delfiles " $output"
		func_verbose "creating $NM input file list: $output"
		for obj in $save_libobjs; do
		  func_to_tool_file "$obj"
		  $ECHO "$func_to_tool_file_result"
		done > "$output"
		eval cmd=\"$cmd1\"
		func_show_eval "$cmd" 'exit $?'
		output=$save_output
		libobjs=$save_libobjs
		skipped_export=false
	      else
		# The command line is too long to execute in one step.
		func_verbose "using reloadable object file for export list..."
		skipped_export=:
		# Break out early, otherwise skipped_export may be
		# set to false by a later but shorter cmd.
		break
	      fi
	    done
	    IFS="$save_ifs"
	    if test -n "$export_symbols_regex" && test "X$skipped_export" != "X:"; then
	      func_show_eval '$EGREP -e "$export_symbols_regex" "$export_symbols" > "${export_symbols}T"'
	      func_show_eval '$MV "${export_symbols}T" "$export_symbols"'
	    fi
	  fi
	fi

	if test -n "$export_symbols" && test -n "$include_expsyms"; then
	  tmp_export_symbols="$export_symbols"
	  test -n "$orig_export_symbols" && tmp_export_symbols="$orig_export_symbols"
	  $opt_dry_run || eval '$ECHO "$include_expsyms" | $SP2NL >> "$tmp_export_symbols"'
	fi

	if test "X$skipped_export" != "X:" && test -n "$orig_export_symbols"; then
	  # The given exports_symbols file has to be filtered, so filter it.
	  func_verbose "filter symbol list for \`$libname.la' to tag DATA exports"
	  # FIXME: $output_objdir/$libname.filter potentially contains lots of
	  # 's' commands which not all seds can handle. GNU sed should be fine
	  # though. Also, the filter scales superlinearly with the number of
	  # global variables. join(1) would be nice here, but unfortunately
	  # isn't a blessed tool.
	  $opt_dry_run || $SED -e '/[ ,]DATA/!d;s,\(.*\)\([ \,].*\),s|^\1$|\1\2|,' < $export_symbols > $output_objdir/$libname.filter
	  func_append delfiles " $export_symbols $output_objdir/$libname.filter"
	  export_symbols=$output_objdir/$libname.def
	  $opt_dry_run || $SED -f $output_objdir/$libname.filter < $orig_export_symbols > $export_symbols
	fi

	tmp_deplibs=
	for test_deplib in $deplibs; do
	  case " $convenience " in
	  *" $test_deplib "*) ;;
	  *)
	    func_append tmp_deplibs " $test_deplib"
	    ;;
	  esac
	done
	deplibs="$tmp_deplibs"

	if test -n "$convenience"; then
	  if test -n "$whole_archive_flag_spec" &&
	    test "$compiler_needs_object" = yes &&
	    test -z "$libobjs"; then
	    # extract the archives, so we have objects to list.
	    # TODO: could optimize this to just extract one archive.
	    whole_archive_flag_spec=
	  fi
	  if test -n "$whole_archive_flag_spec"; then
	    save_libobjs=$libobjs
	    eval libobjs=\"\$libobjs $whole_archive_flag_spec\"
	    test "X$libobjs" = "X " && libobjs=
	  else
	    gentop="$output_objdir/${outputname}x"
	    func_append generated " $gentop"

	    func_extract_archives $gentop $convenience
	    func_append libobjs " $func_extract_archives_result"
	    test "X$libobjs" = "X " && libobjs=
	  fi
	fi

	if test "$thread_safe" = yes && test -n "$thread_safe_flag_spec"; then
	  eval flag=\"$thread_safe_flag_spec\"
	  func_append linker_flags " $flag"
	fi

	# Make a backup of the uninstalled library when relinking
	if test "$opt_mode" = relink; then
	  $opt_dry_run || eval '(cd $output_objdir && $RM ${realname}U && $MV $realname ${realname}U)' || exit $?
	fi

	# Do each of the archive commands.
	if test "$module" = yes && test -n "$module_cmds" ; then
	  if test -n "$export_symbols" && test -n "$module_expsym_cmds"; then
	    eval test_cmds=\"$module_expsym_cmds\"
	    cmds=$module_expsym_cmds
	  else
	    eval test_cmds=\"$module_cmds\"
	    cmds=$module_cmds
	  fi
	else
	  if test -n "$export_symbols" && test -n "$archive_expsym_cmds"; then
	    eval test_cmds=\"$archive_expsym_cmds\"
	    cmds=$archive_expsym_cmds
	  else
	    eval test_cmds=\"$archive_cmds\"
	    cmds=$archive_cmds
	  fi
	fi

	if test "X$skipped_export" != "X:" &&
	   func_len " $test_cmds" &&
	   len=$func_len_result &&
	   test "$len" -lt "$max_cmd_len" || test "$max_cmd_len" -le -1; then
	  :
	else
	  # The command line is too long to link in one step, link piecewise
	  # or, if using GNU ld and skipped_export is not :, use a linker
	  # script.

	  # Save the value of $output and $libobjs because we want to
	  # use them later.  If we have whole_archive_flag_spec, we
	  # want to use save_libobjs as it was before
	  # whole_archive_flag_spec was expanded, because we can't
	  # assume the linker understands whole_archive_flag_spec.
	  # This may have to be revisited, in case too many
	  # convenience libraries get linked in and end up exceeding
	  # the spec.
	  if test -z "$convenience" || test -z "$whole_archive_flag_spec"; then
	    save_libobjs=$libobjs
	  fi
	  save_output=$output
	  func_basename "$output"
	  output_la=$func_basename_result

	  # Clear the reloadable object creation command queue and
	  # initialize k to one.
	  test_cmds=
	  concat_cmds=
	  objlist=
	  last_robj=
	  k=1

	  if test -n "$save_libobjs" && test "X$skipped_export" != "X:" && test "$with_gnu_ld" = yes; then
	    output=${output_objdir}/${output_la}.lnkscript
	    func_verbose "creating GNU ld script: $output"
	    echo 'INPUT (' > $output
	    for obj in $save_libobjs
	    do
	      func_to_tool_file "$obj"
	      $ECHO "$func_to_tool_file_result" >> $output
	    done
	    echo ')' >> $output
	    func_append delfiles " $output"
	    func_to_tool_file "$output"
	    output=$func_to_tool_file_result
	  elif test -n "$save_libobjs" && test "X$skipped_export" != "X:" && test "X$file_list_spec" != X; then
	    output=${output_objdir}/${output_la}.lnk
	    func_verbose "creating linker input file list: $output"
	    : > $output
	    set x $save_libobjs
	    shift
	    firstobj=
	    if test "$compiler_needs_object" = yes; then
	      firstobj="$1 "
	      shift
	    fi
	    for obj
	    do
	      func_to_tool_file "$obj"
	      $ECHO "$func_to_tool_file_result" >> $output
	    done
	    func_append delfiles " $output"
	    func_to_tool_file "$output"
	    output=$firstobj\"$file_list_spec$func_to_tool_file_result\"
	  else
	    if test -n "$save_libobjs"; then
	      func_verbose "creating reloadable object files..."
	      output=$output_objdir/$output_la-${k}.$objext
	      eval test_cmds=\"$reload_cmds\"
	      func_len " $test_cmds"
	      len0=$func_len_result
	      len=$len0

	      # Loop over the list of objects to be linked.
	      for obj in $save_libobjs
	      do
		func_len " $obj"
		func_arith $len + $func_len_result
		len=$func_arith_result
		if test "X$objlist" = X ||
		   test "$len" -lt "$max_cmd_len"; then
		  func_append objlist " $obj"
		else
		  # The command $test_cmds is almost too long, add a
		  # command to the queue.
		  if test "$k" -eq 1 ; then
		    # The first file doesn't have a previous command to add.
		    reload_objs=$objlist
		    eval concat_cmds=\"$reload_cmds\"
		  else
		    # All subsequent reloadable object files will link in
		    # the last one created.
		    reload_objs="$objlist $last_robj"
		    eval concat_cmds=\"\$concat_cmds~$reload_cmds~\$RM $last_robj\"
		  fi
		  last_robj=$output_objdir/$output_la-${k}.$objext
		  func_arith $k + 1
		  k=$func_arith_result
		  output=$output_objdir/$output_la-${k}.$objext
		  objlist=" $obj"
		  func_len " $last_robj"
		  func_arith $len0 + $func_len_result
		  len=$func_arith_result
		fi
	      done
	      # Handle the remaining objects by creating one last
	      # reloadable object file.  All subsequent reloadable object
	      # files will link in the last one created.
	      test -z "$concat_cmds" || concat_cmds=$concat_cmds~
	      reload_objs="$objlist $last_robj"
	      eval concat_cmds=\"\${concat_cmds}$reload_cmds\"
	      if test -n "$last_robj"; then
	        eval concat_cmds=\"\${concat_cmds}~\$RM $last_robj\"
	      fi
	      func_append delfiles " $output"

	    else
	      output=
	    fi

	    if ${skipped_export-false}; then
	      func_verbose "generating symbol list for \`$libname.la'"
	      export_symbols="$output_objdir/$libname.exp"
	      $opt_dry_run || $RM $export_symbols
	      libobjs=$output
	      # Append the command to create the export file.
	      test -z "$concat_cmds" || concat_cmds=$concat_cmds~
	      eval concat_cmds=\"\$concat_cmds$export_symbols_cmds\"
	      if test -n "$last_robj"; then
		eval concat_cmds=\"\$concat_cmds~\$RM $last_robj\"
	      fi
	    fi

	    test -n "$save_libobjs" &&
	      func_verbose "creating a temporary reloadable object file: $output"

	    # Loop through the commands generated above and execute them.
	    save_ifs="$IFS"; IFS='~'
	    for cmd in $concat_cmds; do
	      IFS="$save_ifs"
	      $opt_silent || {
		  func_quote_for_expand "$cmd"
		  eval "func_echo $func_quote_for_expand_result"
	      }
	      $opt_dry_run || eval "$cmd" || {
		lt_exit=$?

		# Restore the uninstalled library and exit
		if test "$opt_mode" = relink; then
		  ( cd "$output_objdir" && \
		    $RM "${realname}T" && \
		    $MV "${realname}U" "$realname" )
		fi

		exit $lt_exit
	      }
	    done
	    IFS="$save_ifs"

	    if test -n "$export_symbols_regex" && ${skipped_export-false}; then
	      func_show_eval '$EGREP -e "$export_symbols_regex" "$export_symbols" > "${export_symbols}T"'
	      func_show_eval '$MV "${export_symbols}T" "$export_symbols"'
	    fi
	  fi

          if ${skipped_export-false}; then
	    if test -n "$export_symbols" && test -n "$include_expsyms"; then
	      tmp_export_symbols="$export_symbols"
	      test -n "$orig_export_symbols" && tmp_export_symbols="$orig_export_symbols"
	      $opt_dry_run || eval '$ECHO "$include_expsyms" | $SP2NL >> "$tmp_export_symbols"'
	    fi

	    if test -n "$orig_export_symbols"; then
	      # The given exports_symbols file has to be filtered, so filter it.
	      func_verbose "filter symbol list for \`$libname.la' to tag DATA exports"
	      # FIXME: $output_objdir/$libname.filter potentially contains lots of
	      # 's' commands which not all seds can handle. GNU sed should be fine
	      # though. Also, the filter scales superlinearly with the number of
	      # global variables. join(1) would be nice here, but unfortunately
	      # isn't a blessed tool.
	      $opt_dry_run || $SED -e '/[ ,]DATA/!d;s,\(.*\)\([ \,].*\),s|^\1$|\1\2|,' < $export_symbols > $output_objdir/$libname.filter
	      func_append delfiles " $export_symbols $output_objdir/$libname.filter"
	      export_symbols=$output_objdir/$libname.def
	      $opt_dry_run || $SED -f $output_objdir/$libname.filter < $orig_export_symbols > $export_symbols
	    fi
	  fi

	  libobjs=$output
	  # Restore the value of output.
	  output=$save_output

	  if test -n "$convenience" && test -n "$whole_archive_flag_spec"; then
	    eval libobjs=\"\$libobjs $whole_archive_flag_spec\"
	    test "X$libobjs" = "X " && libobjs=
	  fi
	  # Expand the library linking commands again to reset the
	  # value of $libobjs for piecewise linking.

	  # Do each of the archive commands.
	  if test "$module" = yes && test -n "$module_cmds" ; then
	    if test -n "$export_symbols" && test -n "$module_expsym_cmds"; then
	      cmds=$module_expsym_cmds
	    else
	      cmds=$module_cmds
	    fi
	  else
	    if test -n "$export_symbols" && test -n "$archive_expsym_cmds"; then
	      cmds=$archive_expsym_cmds
	    else
	      cmds=$archive_cmds
	    fi
	  fi
	fi

	if test -n "$delfiles"; then
	  # Append the command to remove temporary files to $cmds.
	  eval cmds=\"\$cmds~\$RM $delfiles\"
	fi

	# Add any objects from preloaded convenience libraries
	if test -n "$dlprefiles"; then
	  gentop="$output_objdir/${outputname}x"
	  func_append generated " $gentop"

	  func_extract_archives $gentop $dlprefiles
	  func_append libobjs " $func_extract_archives_result"
	  test "X$libobjs" = "X " && libobjs=
	fi

	save_ifs="$IFS"; IFS='~'
	for cmd in $cmds; do
	  IFS="$save_ifs"
	  eval cmd=\"$cmd\"
	  $opt_silent || {
	    func_quote_for_expand "$cmd"
	    eval "func_echo $func_quote_for_expand_result"
	  }
	  $opt_dry_run || eval "$cmd" || {
	    lt_exit=$?

	    # Restore the uninstalled library and exit
	    if test "$opt_mode" = relink; then
	      ( cd "$output_objdir" && \
	        $RM "${realname}T" && \
		$MV "${realname}U" "$realname" )
	    fi

	    exit $lt_exit
	  }
	done
	IFS="$save_ifs"

	# Restore the uninstalled library and exit
	if test "$opt_mode" = relink; then
	  $opt_dry_run || eval '(cd $output_objdir && $RM ${realname}T && $MV $realname ${realname}T && $MV ${realname}U $realname)' || exit $?

	  if test -n "$convenience"; then
	    if test -z "$whole_archive_flag_spec"; then
	      func_show_eval '${RM}r "$gentop"'
	    fi
	  fi

	  exit $EXIT_SUCCESS
	fi

	# Create links to the real library.
	for linkname in $linknames; do
	  if test "$realname" != "$linkname"; then
	    func_show_eval '(cd "$output_objdir" && $RM "$linkname" && $LN_S "$realname" "$linkname")' 'exit $?'
	  fi
	done

	# If -module or -export-dynamic was specified, set the dlname.
	if test "$module" = yes || test "$export_dynamic" = yes; then
	  # On all known operating systems, these are identical.
	  dlname="$soname"
	fi
      fi
      ;;

    obj)
      if test -n "$dlfiles$dlprefiles" || test "$dlself" != no; then
	func_warning "\`-dlopen' is ignored for objects"
      fi

      case " $deplibs" in
      *\ -l* | *\ -L*)
	func_warning "\`-l' and \`-L' are ignored for objects" ;;
      esac

      test -n "$rpath" && \
	func_warning "\`-rpath' is ignored for objects"

      test -n "$xrpath" && \
	func_warning "\`-R' is ignored for objects"

      test -n "$vinfo" && \
	func_warning "\`-version-info' is ignored for objects"

      test -n "$release" && \
	func_warning "\`-release' is ignored for objects"

      case $output in
      *.lo)
	test -n "$objs$old_deplibs" && \
	  func_fatal_error "cannot build library object \`$output' from non-libtool objects"

	libobj=$output
	func_lo2o "$libobj"
	obj=$func_lo2o_result
	;;
      *)
	libobj=
	obj="$output"
	;;
      esac

      # Delete the old objects.
      $opt_dry_run || $RM $obj $libobj

      # Objects from convenience libraries.  This assumes
      # single-version convenience libraries.  Whenever we create
      # different ones for PIC/non-PIC, this we'll have to duplicate
      # the extraction.
      reload_conv_objs=
      gentop=
      # reload_cmds runs $LD directly, so let us get rid of
      # -Wl from whole_archive_flag_spec and hope we can get by with
      # turning comma into space..
      wl=

      if test -n "$convenience"; then
	if test -n "$whole_archive_flag_spec"; then
	  eval tmp_whole_archive_flags=\"$whole_archive_flag_spec\"
	  reload_conv_objs=$reload_objs\ `$ECHO "$tmp_whole_archive_flags" | $SED 's|,| |g'`
	else
	  gentop="$output_objdir/${obj}x"
	  func_append generated " $gentop"

	  func_extract_archives $gentop $convenience
	  reload_conv_objs="$reload_objs $func_extract_archives_result"
	fi
      fi

      # If we're not building shared, we need to use non_pic_objs
      test "$build_libtool_libs" != yes && libobjs="$non_pic_objects"

      # Create the old-style object.
      reload_objs="$objs$old_deplibs "`$ECHO "$libobjs" | $SP2NL | $SED "/\.${libext}$/d; /\.lib$/d; $lo2o" | $NL2SP`" $reload_conv_objs" ### testsuite: skip nested quoting test

      output="$obj"
      func_execute_cmds "$reload_cmds" 'exit $?'

      # Exit if we aren't doing a library object file.
      if test -z "$libobj"; then
	if test -n "$gentop"; then
	  func_show_eval '${RM}r "$gentop"'
	fi

	exit $EXIT_SUCCESS
      fi

      if test "$build_libtool_libs" != yes; then
	if test -n "$gentop"; then
	  func_show_eval '${RM}r "$gentop"'
	fi

	# Create an invalid libtool object if no PIC, so that we don't
	# accidentally link it into a program.
	# $show "echo timestamp > $libobj"
	# $opt_dry_run || eval "echo timestamp > $libobj" || exit $?
	exit $EXIT_SUCCESS
      fi

      if test -n "$pic_flag" || test "$pic_mode" != default; then
	# Only do commands if we really have different PIC objects.
	reload_objs="$libobjs $reload_conv_objs"
	output="$libobj"
	func_execute_cmds "$reload_cmds" 'exit $?'
      fi

      if test -n "$gentop"; then
	func_show_eval '${RM}r "$gentop"'
      fi

      exit $EXIT_SUCCESS
      ;;

    prog)
      case $host in
	*cygwin*) func_stripname '' '.exe' "$output"
	          output=$func_stripname_result.exe;;
      esac
      test -n "$vinfo" && \
	func_warning "\`-version-info' is ignored for programs"

      test -n "$release" && \
	func_warning "\`-release' is ignored for programs"

      test "$preload" = yes \
        && test "$dlopen_support" = unknown \
	&& test "$dlopen_self" = unknown \
	&& test "$dlopen_self_static" = unknown && \
	  func_warning "\`LT_INIT([dlopen])' not used. Assuming no dlopen support."

      case $host in
      *-*-rhapsody* | *-*-darwin1.[012])
	# On Rhapsody replace the C library is the System framework
	compile_deplibs=`$ECHO " $compile_deplibs" | $SED 's/ -lc / System.ltframework /'`
	finalize_deplibs=`$ECHO " $finalize_deplibs" | $SED 's/ -lc / System.ltframework /'`
	;;
      esac

      case $host in
      *-*-darwin*)
	# Don't allow lazy linking, it breaks C++ global constructors
	# But is supposedly fixed on 10.4 or later (yay!).
	if test "$tagname" = CXX ; then
	  case ${MACOSX_DEPLOYMENT_TARGET-10.0} in
	    10.[0123])
	      func_append compile_command " ${wl}-bind_at_load"
	      func_append finalize_command " ${wl}-bind_at_load"
	    ;;
	  esac
	fi
	# Time to change all our "foo.ltframework" stuff back to "-framework foo"
	compile_deplibs=`$ECHO " $compile_deplibs" | $SED 's% \([^ $]*\).ltframework% -framework \1%g'`
	finalize_deplibs=`$ECHO " $finalize_deplibs" | $SED 's% \([^ $]*\).ltframework% -framework \1%g'`
	;;
      esac


      # move library search paths that coincide with paths to not yet
      # installed libraries to the beginning of the library search list
      new_libs=
      for path in $notinst_path; do
	case " $new_libs " in
	*" -L$path/$objdir "*) ;;
	*)
	  case " $compile_deplibs " in
	  *" -L$path/$objdir "*)
	    func_append new_libs " -L$path/$objdir" ;;
	  esac
	  ;;
	esac
      done
      for deplib in $compile_deplibs; do
	case $deplib in
	-L*)
	  case " $new_libs " in
	  *" $deplib "*) ;;
	  *) func_append new_libs " $deplib" ;;
	  esac
	  ;;
	*) func_append new_libs " $deplib" ;;
	esac
      done
      compile_deplibs="$new_libs"


      func_append compile_command " $compile_deplibs"
      func_append finalize_command " $finalize_deplibs"

      if test -n "$rpath$xrpath"; then
	# If the user specified any rpath flags, then add them.
	for libdir in $rpath $xrpath; do
	  # This is the magic to use -rpath.
	  case "$finalize_rpath " in
	  *" $libdir "*) ;;
	  *) func_append finalize_rpath " $libdir" ;;
	  esac
	done
      fi

      # Now hardcode the library paths
      rpath=
      hardcode_libdirs=
      for libdir in $compile_rpath $finalize_rpath; do
	if test -n "$hardcode_libdir_flag_spec"; then
	  if test -n "$hardcode_libdir_separator"; then
	    if test -z "$hardcode_libdirs"; then
	      hardcode_libdirs="$libdir"
	    else
	      # Just accumulate the unique libdirs.
	      case $hardcode_libdir_separator$hardcode_libdirs$hardcode_libdir_separator in
	      *"$hardcode_libdir_separator$libdir$hardcode_libdir_separator"*)
		;;
	      *)
		func_append hardcode_libdirs "$hardcode_libdir_separator$libdir"
		;;
	      esac
	    fi
	  else
	    eval flag=\"$hardcode_libdir_flag_spec\"
	    func_append rpath " $flag"
	  fi
	elif test -n "$runpath_var"; then
	  case "$perm_rpath " in
	  *" $libdir "*) ;;
	  *) func_append perm_rpath " $libdir" ;;
	  esac
	fi
	case $host in
	*-*-cygwin* | *-*-mingw* | *-*-pw32* | *-*-os2* | *-cegcc*)
	  testbindir=`${ECHO} "$libdir" | ${SED} -e 's*/lib$*/bin*'`
	  case :$dllsearchpath: in
	  *":$libdir:"*) ;;
	  ::) dllsearchpath=$libdir;;
	  *) func_append dllsearchpath ":$libdir";;
	  esac
	  case :$dllsearchpath: in
	  *":$testbindir:"*) ;;
	  ::) dllsearchpath=$testbindir;;
	  *) func_append dllsearchpath ":$testbindir";;
	  esac
	  ;;
	esac
      done
      # Substitute the hardcoded libdirs into the rpath.
      if test -n "$hardcode_libdir_separator" &&
	 test -n "$hardcode_libdirs"; then
	libdir="$hardcode_libdirs"
	eval rpath=\" $hardcode_libdir_flag_spec\"
      fi
      compile_rpath="$rpath"

      rpath=
      hardcode_libdirs=
      for libdir in $finalize_rpath; do
	if test -n "$hardcode_libdir_flag_spec"; then
	  if test -n "$hardcode_libdir_separator"; then
	    if test -z "$hardcode_libdirs"; then
	      hardcode_libdirs="$libdir"
	    else
	      # Just accumulate the unique libdirs.
	      case $hardcode_libdir_separator$hardcode_libdirs$hardcode_libdir_separator in
	      *"$hardcode_libdir_separator$libdir$hardcode_libdir_separator"*)
		;;
	      *)
		func_append hardcode_libdirs "$hardcode_libdir_separator$libdir"
		;;
	      esac
	    fi
	  else
	    eval flag=\"$hardcode_libdir_flag_spec\"
	    func_append rpath " $flag"
	  fi
	elif test -n "$runpath_var"; then
	  case "$finalize_perm_rpath " in
	  *" $libdir "*) ;;
	  *) func_append finalize_perm_rpath " $libdir" ;;
	  esac
	fi
      done
      # Substitute the hardcoded libdirs into the rpath.
      if test -n "$hardcode_libdir_separator" &&
	 test -n "$hardcode_libdirs"; then
	libdir="$hardcode_libdirs"
	eval rpath=\" $hardcode_libdir_flag_spec\"
      fi
      finalize_rpath="$rpath"

      if test -n "$libobjs" && test "$build_old_libs" = yes; then
	# Transform all the library objects into standard objects.
	compile_command=`$ECHO "$compile_command" | $SP2NL | $SED "$lo2o" | $NL2SP`
	finalize_command=`$ECHO "$finalize_command" | $SP2NL | $SED "$lo2o" | $NL2SP`
      fi

      func_generate_dlsyms "$outputname" "@PROGRAM@" "no"

      # template prelinking step
      if test -n "$prelink_cmds"; then
	func_execute_cmds "$prelink_cmds" 'exit $?'
      fi

      wrappers_required=yes
      case $host in
      *cegcc* | *mingw32ce*)
        # Disable wrappers for cegcc and mingw32ce hosts, we are cross compiling anyway.
        wrappers_required=no
        ;;
      *cygwin* | *mingw* )
        if test "$build_libtool_libs" != yes; then
          wrappers_required=no
        fi
        ;;
      *)
        if test "$need_relink" = no || test "$build_libtool_libs" != yes; then
          wrappers_required=no
        fi
        ;;
      esac
      if test "$wrappers_required" = no; then
	# Replace the output file specification.
	compile_command=`$ECHO "$compile_command" | $SED 's%@OUTPUT@%'"$output"'%g'`
	link_command="$compile_command$compile_rpath"

	# We have no uninstalled library dependencies, so finalize right now.
	exit_status=0
	func_show_eval "$link_command" 'exit_status=$?'

	if test -n "$postlink_cmds"; then
	  func_to_tool_file "$output"
	  postlink_cmds=`func_echo_all "$postlink_cmds" | $SED -e 's%@OUTPUT@%'"$output"'%g' -e 's%@TOOL_OUTPUT@%'"$func_to_tool_file_result"'%g'`
	  func_execute_cmds "$postlink_cmds" 'exit $?'
	fi

	# Delete the generated files.
	if test -f "$output_objdir/${outputname}S.${objext}"; then
	  func_show_eval '$RM "$output_objdir/${outputname}S.${objext}"'
	fi

	exit $exit_status
      fi

      if test -n "$compile_shlibpath$finalize_shlibpath"; then
	compile_command="$shlibpath_var=\"$compile_shlibpath$finalize_shlibpath\$$shlibpath_var\" $compile_command"
      fi
      if test -n "$finalize_shlibpath"; then
	finalize_command="$shlibpath_var=\"$finalize_shlibpath\$$shlibpath_var\" $finalize_command"
      fi

      compile_var=
      finalize_var=
      if test -n "$runpath_var"; then
	if test -n "$perm_rpath"; then
	  # We should set the runpath_var.
	  rpath=
	  for dir in $perm_rpath; do
	    func_append rpath "$dir:"
	  done
	  compile_var="$runpath_var=\"$rpath\$$runpath_var\" "
	fi
	if test -n "$finalize_perm_rpath"; then
	  # We should set the runpath_var.
	  rpath=
	  for dir in $finalize_perm_rpath; do
	    func_append rpath "$dir:"
	  done
	  finalize_var="$runpath_var=\"$rpath\$$runpath_var\" "
	fi
      fi

      if test "$no_install" = yes; then
	# We don't need to create a wrapper script.
	link_command="$compile_var$compile_command$compile_rpath"
	# Replace the output file specification.
	link_command=`$ECHO "$link_command" | $SED 's%@OUTPUT@%'"$output"'%g'`
	# Delete the old output file.
	$opt_dry_run || $RM $output
	# Link the executable and exit
	func_show_eval "$link_command" 'exit $?'

	if test -n "$postlink_cmds"; then
	  func_to_tool_file "$output"
	  postlink_cmds=`func_echo_all "$postlink_cmds" | $SED -e 's%@OUTPUT@%'"$output"'%g' -e 's%@TOOL_OUTPUT@%'"$func_to_tool_file_result"'%g'`
	  func_execute_cmds "$postlink_cmds" 'exit $?'
	fi

	exit $EXIT_SUCCESS
      fi

      if test "$hardcode_action" = relink; then
	# Fast installation is not supported
	link_command="$compile_var$compile_command$compile_rpath"
	relink_command="$finalize_var$finalize_command$finalize_rpath"

	func_warning "this platform does not like uninstalled shared libraries"
	func_warning "\`$output' will be relinked during installation"
      else
	if test "$fast_install" != no; then
	  link_command="$finalize_var$compile_command$finalize_rpath"
	  if test "$fast_install" = yes; then
	    relink_command=`$ECHO "$compile_var$compile_command$compile_rpath" | $SED 's%@OUTPUT@%\$progdir/\$file%g'`
	  else
	    # fast_install is set to needless
	    relink_command=
	  fi
	else
	  link_command="$compile_var$compile_command$compile_rpath"
	  relink_command="$finalize_var$finalize_command$finalize_rpath"
	fi
      fi

      # Replace the output file specification.
      link_command=`$ECHO "$link_command" | $SED 's%@OUTPUT@%'"$output_objdir/$outputname"'%g'`

      # Delete the old output files.
      $opt_dry_run || $RM $output $output_objdir/$outputname $output_objdir/lt-$outputname

      func_show_eval "$link_command" 'exit $?'

      if test -n "$postlink_cmds"; then
	func_to_tool_file "$output_objdir/$outputname"
	postlink_cmds=`func_echo_all "$postlink_cmds" | $SED -e 's%@OUTPUT@%'"$output_objdir/$outputname"'%g' -e 's%@TOOL_OUTPUT@%'"$func_to_tool_file_result"'%g'`
	func_execute_cmds "$postlink_cmds" 'exit $?'
      fi

      # Now create the wrapper script.
      func_verbose "creating $output"

      # Quote the relink command for shipping.
      if test -n "$relink_command"; then
	# Preserve any variables that may affect compiler behavior
	for var in $variables_saved_for_relink; do
	  if eval test -z \"\${$var+set}\"; then
	    relink_command="{ test -z \"\${$var+set}\" || $lt_unset $var || { $var=; export $var; }; }; $relink_command"
	  elif eval var_value=\$$var; test -z "$var_value"; then
	    relink_command="$var=; export $var; $relink_command"
	  else
	    func_quote_for_eval "$var_value"
	    relink_command="$var=$func_quote_for_eval_result; export $var; $relink_command"
	  fi
	done
	relink_command="(cd `pwd`; $relink_command)"
	relink_command=`$ECHO "$relink_command" | $SED "$sed_quote_subst"`
      fi

      # Only actually do things if not in dry run mode.
      $opt_dry_run || {
	# win32 will think the script is a binary if it has
	# a .exe suffix, so we strip it off here.
	case $output in
	  *.exe) func_stripname '' '.exe' "$output"
	         output=$func_stripname_result ;;
	esac
	# test for cygwin because mv fails w/o .exe extensions
	case $host in
	  *cygwin*)
	    exeext=.exe
	    func_stripname '' '.exe' "$outputname"
	    outputname=$func_stripname_result ;;
	  *) exeext= ;;
	esac
	case $host in
	  *cygwin* | *mingw* )
	    func_dirname_and_basename "$output" "" "."
	    output_name=$func_basename_result
	    output_path=$func_dirname_result
	    cwrappersource="$output_path/$objdir/lt-$output_name.c"
	    cwrapper="$output_path/$output_name.exe"
	    $RM $cwrappersource $cwrapper
	    trap "$RM $cwrappersource $cwrapper; exit $EXIT_FAILURE" 1 2 15

	    func_emit_cwrapperexe_src > $cwrappersource

	    # The wrapper executable is built using the $host compiler,
	    # because it contains $host paths and files. If cross-
	    # compiling, it, like the target executable, must be
	    # executed on the $host or under an emulation environment.
	    $opt_dry_run || {
	      $LTCC $LTCFLAGS -o $cwrapper $cwrappersource
	      $STRIP $cwrapper
	    }

	    # Now, create the wrapper script for func_source use:
	    func_ltwrapper_scriptname $cwrapper
	    $RM $func_ltwrapper_scriptname_result
	    trap "$RM $func_ltwrapper_scriptname_result; exit $EXIT_FAILURE" 1 2 15
	    $opt_dry_run || {
	      # note: this script will not be executed, so do not chmod.
	      if test "x$build" = "x$host" ; then
		$cwrapper --lt-dump-script > $func_ltwrapper_scriptname_result
	      else
		func_emit_wrapper no > $func_ltwrapper_scriptname_result
	      fi
	    }
	  ;;
	  * )
	    $RM $output
	    trap "$RM $output; exit $EXIT_FAILURE" 1 2 15

	    func_emit_wrapper no > $output
	    chmod +x $output
	  ;;
	esac
      }
      exit $EXIT_SUCCESS
      ;;
    esac

    # See if we need to build an old-fashioned archive.
    for oldlib in $oldlibs; do

      if test "$build_libtool_libs" = convenience; then
	oldobjs="$libobjs_save $symfileobj"
	addlibs="$convenience"
	build_libtool_libs=no
      else
	if test "$build_libtool_libs" = module; then
	  oldobjs="$libobjs_save"
	  build_libtool_libs=no
	else
	  oldobjs="$old_deplibs $non_pic_objects"
	  if test "$preload" = yes && test -f "$symfileobj"; then
	    func_append oldobjs " $symfileobj"
	  fi
	fi
	addlibs="$old_convenience"
      fi

      if test -n "$addlibs"; then
	gentop="$output_objdir/${outputname}x"
	func_append generated " $gentop"

	func_extract_archives $gentop $addlibs
	func_append oldobjs " $func_extract_archives_result"
      fi

      # Do each command in the archive commands.
      if test -n "$old_archive_from_new_cmds" && test "$build_libtool_libs" = yes; then
	cmds=$old_archive_from_new_cmds
      else

	# Add any objects from preloaded convenience libraries
	if test -n "$dlprefiles"; then
	  gentop="$output_objdir/${outputname}x"
	  func_append generated " $gentop"

	  func_extract_archives $gentop $dlprefiles
	  func_append oldobjs " $func_extract_archives_result"
	fi

	# POSIX demands no paths to be encoded in archives.  We have
	# to avoid creating archives with duplicate basenames if we
	# might have to extract them afterwards, e.g., when creating a
	# static archive out of a convenience library, or when linking
	# the entirety of a libtool archive into another (currently
	# not supported by libtool).
	if (for obj in $oldobjs
	    do
	      func_basename "$obj"
	      $ECHO "$func_basename_result"
	    done | sort | sort -uc >/dev/null 2>&1); then
	  :
	else
	  echo "copying selected object files to avoid basename conflicts..."
	  gentop="$output_objdir/${outputname}x"
	  func_append generated " $gentop"
	  func_mkdir_p "$gentop"
	  save_oldobjs=$oldobjs
	  oldobjs=
	  counter=1
	  for obj in $save_oldobjs
	  do
	    func_basename "$obj"
	    objbase="$func_basename_result"
	    case " $oldobjs " in
	    " ") oldobjs=$obj ;;
	    *[\ /]"$objbase "*)
	      while :; do
		# Make sure we don't pick an alternate name that also
		# overlaps.
		newobj=lt$counter-$objbase
		func_arith $counter + 1
		counter=$func_arith_result
		case " $oldobjs " in
		*[\ /]"$newobj "*) ;;
		*) if test ! -f "$gentop/$newobj"; then break; fi ;;
		esac
	      done
	      func_show_eval "ln $obj $gentop/$newobj || cp $obj $gentop/$newobj"
	      func_append oldobjs " $gentop/$newobj"
	      ;;
	    *) func_append oldobjs " $obj" ;;
	    esac
	  done
	fi
	func_to_tool_file "$oldlib" func_convert_file_msys_to_w32
	tool_oldlib=$func_to_tool_file_result
	eval cmds=\"$old_archive_cmds\"

	func_len " $cmds"
	len=$func_len_result
	if test "$len" -lt "$max_cmd_len" || test "$max_cmd_len" -le -1; then
	  cmds=$old_archive_cmds
	elif test -n "$archiver_list_spec"; then
	  func_verbose "using command file archive linking..."
	  for obj in $oldobjs
	  do
	    func_to_tool_file "$obj"
	    $ECHO "$func_to_tool_file_result"
	  done > $output_objdir/$libname.libcmd
	  func_to_tool_file "$output_objdir/$libname.libcmd"
	  oldobjs=" $archiver_list_spec$func_to_tool_file_result"
	  cmds=$old_archive_cmds
	else
	  # the command line is too long to link in one step, link in parts
	  func_verbose "using piecewise archive linking..."
	  save_RANLIB=$RANLIB
	  RANLIB=:
	  objlist=
	  concat_cmds=
	  save_oldobjs=$oldobjs
	  oldobjs=
	  # Is there a better way of finding the last object in the list?
	  for obj in $save_oldobjs
	  do
	    last_oldobj=$obj
	  done
	  eval test_cmds=\"$old_archive_cmds\"
	  func_len " $test_cmds"
	  len0=$func_len_result
	  len=$len0
	  for obj in $save_oldobjs
	  do
	    func_len " $obj"
	    func_arith $len + $func_len_result
	    len=$func_arith_result
	    func_append objlist " $obj"
	    if test "$len" -lt "$max_cmd_len"; then
	      :
	    else
	      # the above command should be used before it gets too long
	      oldobjs=$objlist
	      if test "$obj" = "$last_oldobj" ; then
		RANLIB=$save_RANLIB
	      fi
	      test -z "$concat_cmds" || concat_cmds=$concat_cmds~
	      eval concat_cmds=\"\${concat_cmds}$old_archive_cmds\"
	      objlist=
	      len=$len0
	    fi
	  done
	  RANLIB=$save_RANLIB
	  oldobjs=$objlist
	  if test "X$oldobjs" = "X" ; then
	    eval cmds=\"\$concat_cmds\"
	  else
	    eval cmds=\"\$concat_cmds~\$old_archive_cmds\"
	  fi
	fi
      fi
      func_execute_cmds "$cmds" 'exit $?'
    done

    test -n "$generated" && \
      func_show_eval "${RM}r$generated"

    # Now create the libtool archive.
    case $output in
    *.la)
      old_library=
      test "$build_old_libs" = yes && old_library="$libname.$libext"
      func_verbose "creating $output"

      # Preserve any variables that may affect compiler behavior
      for var in $variables_saved_for_relink; do
	if eval test -z \"\${$var+set}\"; then
	  relink_command="{ test -z \"\${$var+set}\" || $lt_unset $var || { $var=; export $var; }; }; $relink_command"
	elif eval var_value=\$$var; test -z "$var_value"; then
	  relink_command="$var=; export $var; $relink_command"
	else
	  func_quote_for_eval "$var_value"
	  relink_command="$var=$func_quote_for_eval_result; export $var; $relink_command"
	fi
      done
      # Quote the link command for shipping.
      relink_command="(cd `pwd`; $SHELL $progpath $preserve_args --mode=relink $libtool_args @inst_prefix_dir@)"
      relink_command=`$ECHO "$relink_command" | $SED "$sed_quote_subst"`
      if test "$hardcode_automatic" = yes ; then
	relink_command=
      fi

      # Only create the output if not a dry run.
      $opt_dry_run || {
	for installed in no yes; do
	  if test "$installed" = yes; then
	    if test -z "$install_libdir"; then
	      break
	    fi
	    output="$output_objdir/$outputname"i
	    # Replace all uninstalled libtool libraries with the installed ones
	    newdependency_libs=
	    for deplib in $dependency_libs; do
	      case $deplib in
	      *.la)
		func_basename "$deplib"
		name="$func_basename_result"
		func_resolve_sysroot "$deplib"
		eval libdir=`${SED} -n -e 's/^libdir=\(.*\)$/\1/p' $func_resolve_sysroot_result`
		test -z "$libdir" && \
		  func_fatal_error "\`$deplib' is not a valid libtool archive"
		func_append newdependency_libs " ${lt_sysroot:+=}$libdir/$name"
		;;
	      -L*)
		func_stripname -L '' "$deplib"
		func_replace_sysroot "$func_stripname_result"
		func_append newdependency_libs " -L$func_replace_sysroot_result"
		;;
	      -R*)
		func_stripname -R '' "$deplib"
		func_replace_sysroot "$func_stripname_result"
		func_append newdependency_libs " -R$func_replace_sysroot_result"
		;;
	      *) func_append newdependency_libs " $deplib" ;;
	      esac
	    done
	    dependency_libs="$newdependency_libs"
	    newdlfiles=

	    for lib in $dlfiles; do
	      case $lib in
	      *.la)
	        func_basename "$lib"
		name="$func_basename_result"
		eval libdir=`${SED} -n -e 's/^libdir=\(.*\)$/\1/p' $lib`
		test -z "$libdir" && \
		  func_fatal_error "\`$lib' is not a valid libtool archive"
		func_append newdlfiles " ${lt_sysroot:+=}$libdir/$name"
		;;
	      *) func_append newdlfiles " $lib" ;;
	      esac
	    done
	    dlfiles="$newdlfiles"
	    newdlprefiles=
	    for lib in $dlprefiles; do
	      case $lib in
	      *.la)
		# Only pass preopened files to the pseudo-archive (for
		# eventual linking with the app. that links it) if we
		# didn't already link the preopened objects directly into
		# the library:
		func_basename "$lib"
		name="$func_basename_result"
		eval libdir=`${SED} -n -e 's/^libdir=\(.*\)$/\1/p' $lib`
		test -z "$libdir" && \
		  func_fatal_error "\`$lib' is not a valid libtool archive"
		func_append newdlprefiles " ${lt_sysroot:+=}$libdir/$name"
		;;
	      esac
	    done
	    dlprefiles="$newdlprefiles"
	  else
	    newdlfiles=
	    for lib in $dlfiles; do
	      case $lib in
		[\\/]* | [A-Za-z]:[\\/]*) abs="$lib" ;;
		*) abs=`pwd`"/$lib" ;;
	      esac
	      func_append newdlfiles " $abs"
	    done
	    dlfiles="$newdlfiles"
	    newdlprefiles=
	    for lib in $dlprefiles; do
	      case $lib in
		[\\/]* | [A-Za-z]:[\\/]*) abs="$lib" ;;
		*) abs=`pwd`"/$lib" ;;
	      esac
	      func_append newdlprefiles " $abs"
	    done
	    dlprefiles="$newdlprefiles"
	  fi
	  $RM $output
	  # place dlname in correct position for cygwin
	  # In fact, it would be nice if we could use this code for all target
	  # systems that can't hard-code library paths into their executables
	  # and that have no shared library path variable independent of PATH,
	  # but it turns out we can't easily determine that from inspecting
	  # libtool variables, so we have to hard-code the OSs to which it
	  # applies here; at the moment, that means platforms that use the PE
	  # object format with DLL files.  See the long comment at the top of
	  # tests/bindir.at for full details.
	  tdlname=$dlname
	  case $host,$output,$installed,$module,$dlname in
	    *cygwin*,*lai,yes,no,*.dll | *mingw*,*lai,yes,no,*.dll | *cegcc*,*lai,yes,no,*.dll)
	      # If a -bindir argument was supplied, place the dll there.
	      if test "x$bindir" != x ;
	      then
		func_relative_path "$install_libdir" "$bindir"
		tdlname=$func_relative_path_result$dlname
	      else
		# Otherwise fall back on heuristic.
		tdlname=../bin/$dlname
	      fi
	      ;;
	  esac
	  $ECHO > $output "\
# $outputname - a libtool library file
# Generated by $PROGRAM (GNU $PACKAGE$TIMESTAMP) $VERSION
#
# Please DO NOT delete this file!
# It is necessary for linking the library.

# The name that we can dlopen(3).
dlname='$tdlname'

# Names of this library.
library_names='$library_names'

# The name of the static archive.
old_library='$old_library'

# Linker flags that can not go in dependency_libs.
inherited_linker_flags='$new_inherited_linker_flags'

# Libraries that this one depends upon.
dependency_libs='$dependency_libs'

# Names of additional weak libraries provided by this library
weak_library_names='$weak_libs'

# Version information for $libname.
current=$current
age=$age
revision=$revision

# Is this an already installed library?
installed=$installed

# Should we warn about portability when linking against -modules?
shouldnotlink=$module

# Files to dlopen/dlpreopen
dlopen='$dlfiles'
dlpreopen='$dlprefiles'

# Directory that this library needs to be installed in:
libdir='$install_libdir'"
	  if test "$installed" = no && test "$need_relink" = yes; then
	    $ECHO >> $output "\
relink_command=\"$relink_command\""
	  fi
	done
      }

      # Do a symbolic link so that the libtool archive can be found in
      # LD_LIBRARY_PATH before the program is installed.
      func_show_eval '( cd "$output_objdir" && $RM "$outputname" && $LN_S "../$outputname" "$outputname" )' 'exit $?'
      ;;
    esac
    exit $EXIT_SUCCESS
}

{ test "$opt_mode" = link || test "$opt_mode" = relink; } &&
    func_mode_link ${1+"$@"}


# func_mode_uninstall arg...
func_mode_uninstall ()
{
    $opt_debug
    RM="$nonopt"
    files=
    rmforce=
    exit_status=0

    # This variable tells wrapper scripts just to set variables rather
    # than running their programs.
    libtool_install_magic="$magic"

    for arg
    do
      case $arg in
      -f) func_append RM " $arg"; rmforce=yes ;;
      -*) func_append RM " $arg" ;;
      *) func_append files " $arg" ;;
      esac
    done

    test -z "$RM" && \
      func_fatal_help "you must specify an RM program"

    rmdirs=

    for file in $files; do
      func_dirname "$file" "" "."
      dir="$func_dirname_result"
      if test "X$dir" = X.; then
	odir="$objdir"
      else
	odir="$dir/$objdir"
      fi
      func_basename "$file"
      name="$func_basename_result"
      test "$opt_mode" = uninstall && odir="$dir"

      # Remember odir for removal later, being careful to avoid duplicates
      if test "$opt_mode" = clean; then
	case " $rmdirs " in
	  *" $odir "*) ;;
	  *) func_append rmdirs " $odir" ;;
	esac
      fi

      # Don't error if the file doesn't exist and rm -f was used.
      if { test -L "$file"; } >/dev/null 2>&1 ||
	 { test -h "$file"; } >/dev/null 2>&1 ||
	 test -f "$file"; then
	:
      elif test -d "$file"; then
	exit_status=1
	continue
      elif test "$rmforce" = yes; then
	continue
      fi

      rmfiles="$file"

      case $name in
      *.la)
	# Possibly a libtool archive, so verify it.
	if func_lalib_p "$file"; then
	  func_source $dir/$name

	  # Delete the libtool libraries and symlinks.
	  for n in $library_names; do
	    func_append rmfiles " $odir/$n"
	  done
	  test -n "$old_library" && func_append rmfiles " $odir/$old_library"

	  case "$opt_mode" in
	  clean)
	    case " $library_names " in
	    *" $dlname "*) ;;
	    *) test -n "$dlname" && func_append rmfiles " $odir/$dlname" ;;
	    esac
	    test -n "$libdir" && func_append rmfiles " $odir/$name $odir/${name}i"
	    ;;
	  uninstall)
	    if test -n "$library_names"; then
	      # Do each command in the postuninstall commands.
	      func_execute_cmds "$postuninstall_cmds" 'test "$rmforce" = yes || exit_status=1'
	    fi

	    if test -n "$old_library"; then
	      # Do each command in the old_postuninstall commands.
	      func_execute_cmds "$old_postuninstall_cmds" 'test "$rmforce" = yes || exit_status=1'
	    fi
	    # FIXME: should reinstall the best remaining shared library.
	    ;;
	  esac
	fi
	;;

      *.lo)
	# Possibly a libtool object, so verify it.
	if func_lalib_p "$file"; then

	  # Read the .lo file
	  func_source $dir/$name

	  # Add PIC object to the list of files to remove.
	  if test -n "$pic_object" &&
	     test "$pic_object" != none; then
	    func_append rmfiles " $dir/$pic_object"
	  fi

	  # Add non-PIC object to the list of files to remove.
	  if test -n "$non_pic_object" &&
	     test "$non_pic_object" != none; then
	    func_append rmfiles " $dir/$non_pic_object"
	  fi
	fi
	;;

      *)
	if test "$opt_mode" = clean ; then
	  noexename=$name
	  case $file in
	  *.exe)
	    func_stripname '' '.exe' "$file"
	    file=$func_stripname_result
	    func_stripname '' '.exe' "$name"
	    noexename=$func_stripname_result
	    # $file with .exe has already been added to rmfiles,
	    # add $file without .exe
	    func_append rmfiles " $file"
	    ;;
	  esac
	  # Do a test to see if this is a libtool program.
	  if func_ltwrapper_p "$file"; then
	    if func_ltwrapper_executable_p "$file"; then
	      func_ltwrapper_scriptname "$file"
	      relink_command=
	      func_source $func_ltwrapper_scriptname_result
	      func_append rmfiles " $func_ltwrapper_scriptname_result"
	    else
	      relink_command=
	      func_source $dir/$noexename
	    fi

	    # note $name still contains .exe if it was in $file originally
	    # as does the version of $file that was added into $rmfiles
	    func_append rmfiles " $odir/$name $odir/${name}S.${objext}"
	    if test "$fast_install" = yes && test -n "$relink_command"; then
	      func_append rmfiles " $odir/lt-$name"
	    fi
	    if test "X$noexename" != "X$name" ; then
	      func_append rmfiles " $odir/lt-${noexename}.c"
	    fi
	  fi
	fi
	;;
      esac
      func_show_eval "$RM $rmfiles" 'exit_status=1'
    done

    # Try to remove the ${objdir}s in the directories where we deleted files
    for dir in $rmdirs; do
      if test -d "$dir"; then
	func_show_eval "rmdir $dir >/dev/null 2>&1"
      fi
    done

    exit $exit_status
}

{ test "$opt_mode" = uninstall || test "$opt_mode" = clean; } &&
    func_mode_uninstall ${1+"$@"}

test -z "$opt_mode" && {
  help="$generic_help"
  func_fatal_help "you must specify a MODE"
}

test -z "$exec_cmd" && \
  func_fatal_help "invalid operation mode \`$opt_mode'"

if test -n "$exec_cmd"; then
  eval exec "$exec_cmd"
  exit $EXIT_FAILURE
fi

exit $exit_status


# The TAGs below are defined such that we never get into a situation
# in which we disable both kinds of libraries.  Given conflicting
# choices, we go for a static library, that is the most portable,
# since we can't tell whether shared libraries were disabled because
# the user asked for that or because the platform doesn't support
# them.  This is particularly important on AIX, because we don't
# support having both static and shared libraries enabled at the same
# time on that platform, so we default to a shared-only configuration.
# If a disable-shared tag is given, we'll fallback to a static-only
# configuration.  But we'll never go from static-only to shared-only.

# ### BEGIN LIBTOOL TAG CONFIG: disable-shared
build_libtool_libs=no
build_old_libs=yes
# ### END LIBTOOL TAG CONFIG: disable-shared

# ### BEGIN LIBTOOL TAG CONFIG: disable-static
build_old_libs=`case $build_libtool_libs in yes) echo no;; *) echo yes;; esac`
# ### END LIBTOOL TAG CONFIG: disable-static

# Local Variables:
# mode:shell-script
# sh-indentation:2
# End:
# vi:sw=2

