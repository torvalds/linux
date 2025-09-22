#!/bin/sh
#===-- tdtags - TableGen tags wrapper ---------------------------*- sh -*-===#
# vim:set sts=2 sw=2 et:
#===----------------------------------------------------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===#
#
# This is a wrapper script to simplify generating ctags(1)-compatible index
# files for target .td files. Run tdtags -H for more documentation.
#
# For portability, this script is intended to conform to IEEE Std 1003.1-2008.
#
#===----------------------------------------------------------------------===#

SELF=${0##*/}

usage() {
cat <<END
Usage: $SELF [ <options> ] tdfile
   or: $SELF [ <options> ] -x recipe [arg ...]
OPTIONS
  -H          Display further help.
  -a          Append the tags to an existing tags file.
  -f <file>   Write tags to the specified file (defaults to 'tags').
  -I <dir>    Add the directory to the search path for tblgen include files.
  -x <recipe> Generate tags file(s) for a common use case:
  -q          Suppress $TBLGEN error messages.
  -v          Be verbose; report progress.
END
  usage_recipes
}

usage_recipes() {
cat <<END
     all      - Generate an index in each directory that contains .td files
                in the LLVM source tree.
     here     - Generate an index for all .td files in the current directory.
     recurse  - Generate an index in each directory that contains .td files
                in and under the current directory.
     target [<target> ...]
              - Generate a tags file for each specified LLVM code generator
                target, or if none are specified, all targets.
END
}

help() {
cat <<END
NAME
  $SELF - generate ctags(1)-compatible index files for tblgen .td source

SYNOPSIS
  $SELF [ options ] -x recipe [arg ...]
  $SELF [ options ] [file ...]

DESCRIPTION
  With the '-x' option, $SELF produces one or more tags files for a
  particular common use case. See the RECIPES section below for details.

  Without the '-x' option, $SELF provides a ctags(1)-like interface to
  $TBLGEN.

OPTIONS
  -a          Append newly generated tags to those already in an existing
              tags file. Without ths option, any and all existing tags are
              replaced. NOTE: When building a mixed tags file, using ${SELF}
              for tblgen tags and ctags(1) for other languages, it is best
              to run ${SELF} first without '-a', and ctags(1) second with '-a',
              because ctags(1) handling is more capable.
  -f <file>   Use the name <file> for the tags file, rather than the default
              "tags". If the <file> is "-", then the tag index is written to
              standard output.
  -H          Display this document.
  -I <dir>    Add the directory <dir> to the search path for 'include'
              statements in tblgen source.
  -x          Run a canned recipe, rather than operate on specified files.
              When '-x' is present, the first non-option argument is the
              name of a recipe, and any further arguments are arguments to
              that recipe. With no arguments, lists the available recipes.
  -q          Suppress $TBLGEN error messages. Not all .td files are well-
              formed outside a specific context, so recipes will sometimes
              produce error messages for certain .td files. These errors
              do not affect the indices produced for valid files.
  -v          Be verbose; report progress.

RECIPES
  $SELF -x all
              Produce a tags file in every directory in the LLVM source tree
              that contains any .td files.
  $SELF -x here
              Produce a tags file from .td files in the current directory.
  $SELF -x recurse
              Produce a tags file in every directory that contains any .td
              files, in and under the current directory.
  $SELF -x target [<target> ...]
              Produce a tags file for each named code generator target, or
              if none are named, for all code generator targets.
END
}

# Temporary file management.
#
# Since SUS sh(1) has no arrays, this script makes extensive use of
# temporary files. The follow are 'global' and used to carry information
# across functions:
#   $TMP:D    Include directories.
#   $TMP:I    Included files.
#   $TMP:T    Top-level files, that are not included by another.
#   $TMP:W    Directories in which to generate tags (Worklist).
# For portability to OS X, names must not differ only in case.
#
TMP=${TMPDIR:-/tmp}/$SELF:$$
trap "rm -f $TMP*" 0
trap exit 1 2 13 15
>$TMP:D

td_dump()
{
  if [ $OPT_VERBOSE -gt 1 ]
  then
    printf '===== %s =====\n' "$1"
    cat <"$1"
  fi
}

# Escape the arguments, taken as a whole.
e() {
  printf '%s' "$*" |
    sed -e "s/'/'\\\\''/g" -e "1s/^/'/" -e "\$s/\$/'/"
}

# Determine whether the given directory contains at least one .td file.
dir_has_td() {
  for i in $1/*.td
  do
    [ -f "$i" ] && return 0
  done
  return 1
}

# Partition the supplied list of files, plus any files included from them,
# into two groups:
#   $TMP:T    Top-level files, that are not included by another.
#   $TMP:I    Included files.
# Add standard directories to the include paths in $TMP:D if this would
# benefit the any of the included files.
td_prep() {
  >$TMP:E
  >$TMP:J
  for i in *.td
  do
    [ "x$i" = 'x*.td' ] && return 1
    if [ -f "$i" ]
    then
      printf '%s\n' "$i" >>$TMP:E
      sed -n -e 's/include[[:space:]]"\(.*\)".*/\1/p' <"$i" >>$TMP:J
    else
      printf >&2 '%s: "%s" not found.\n' "$SELF" "$i"
      exit 7
    fi
  done
  sort -u <$TMP:E >$TMP:X
  sort -u <$TMP:J >$TMP:I
  # A file that exists but is not included is toplevel.
  comm -23 $TMP:X $TMP:I >$TMP:T
  td_dump $TMP:T
  td_dump $TMP:I
  # Check include files.
  while read i
  do
    [ -f "$i" ] && continue
    while read d
    do
      [ -f "$d/$i" ] && break
    done <$TMP:D
    if [ -z "$d" ]
    then
      # See whether this include file can be found in a common location.
      for d in $LLVM_SRC_ROOT/include \
               $LLVM_SRC_ROOT/tools/clang/include
      do
        if [ -f "$d/$i" ]
        then
          printf '%s\n' "$d" >>$TMP:D
          break
        fi
      done
    fi
  done <$TMP:I
  td_dump $TMP:D
}

# Generate tags for the list of files in $TMP:T.
td_tag() {
  # Collect include directories.
  inc=
  while read d
  do
    inc="${inc}${inc:+ }$(e "-I=$d")"
  done <$TMP:D

  if [ $OPT_VERBOSE -ne 0 ]
  then
    printf >&2 'In "%s",\n' "$PWD"
  fi

  # Generate tags for each file.
  n=0
  while read i
  do
    if [ $OPT_VERBOSE -ne 0 ]
    then
      printf >&2 '  generating tags from "%s"\n' "$i"
    fi
    n=$((n + 1))
    t=$(printf '%s:A:%05u' "$TMP" $n)
    eval $TBLGEN --gen-ctags $inc "$i" >$t 2>$TMP:F
    [ $OPT_NOTBLGENERR -eq 1 ] || cat $TMP:F
  done <$TMP:T

  # Add existing tags if requested.
  if [ $OPT_APPEND -eq 1 -a -f "$OPT_TAGSFILE" ]
  then
    if [ $OPT_VERBOSE -ne 0 ]
    then
      printf >&2 '  and existing tags from "%s"\n' "$OPT_TAGSFILE"
    fi
    n=$((n + 1))
    t=$(printf '%s:A:%05u' "$TMP" $n)
    sed -e '/^!_TAG_/d' <"$OPT_TAGSFILE" | sort -u >$t
  fi

  # Merge tags.
  if [ $n = 1 ]
  then
    mv -f "$t" $TMP:M
  else
    sort -m -u $TMP:A:* >$TMP:M
  fi

  # Emit tags.
  if [ x${OPT_TAGSFILE}x = x-x ]
  then
    cat $TMP:M
  else
    if [ $OPT_VERBOSE -ne 0 ]
    then
      printf >&2 '  into "%s".\n' "$OPT_TAGSFILE"
    fi
    mv -f $TMP:M "$OPT_TAGSFILE"
  fi
}

# Generate tags for the current directory.
td_here() {
  td_prep
  [ -s $TMP:T ] || return 1
  td_tag
}

# Generate tags for the current directory, and report an error if there are
# no .td files present.
do_here()
{
  if ! td_here
  then
    printf >&2 '%s: Nothing to do here.\n' "$SELF"
    exit 1
  fi
}

# Generate tags for all .td files under the current directory.
do_recurse()
{
  td_find "$PWD"
  td_dirs
}

# Generate tags for all .td files in LLVM.
do_all()
{
  td_find "$LLVM_SRC_ROOT"
  td_dirs
}

# Generate tags for each directory in the worklist $TMP:W.
td_dirs()
{
  while read d
  do
    (cd "$d" && td_here)
  done <$TMP:W
}

# Find directories containing .td files within the specified directory,
# and record them in the worklist $TMP:W.
td_find()
{
  find -L "$1" -type f -name '*.td' |
    sed -e 's:/[^/]*$::' |
    sort -u >$TMP:W
  td_dump $TMP:W
}

# Generate tags for the specified code generator targets, or
# if there are no arguments, all targets.
do_targets() {
  cd $LLVM_SRC_ROOT/lib/Target
  if [ -z "$*" ]
  then
    td_find "$PWD"
  else
    # Check that every specified argument is a target directory;
    # if not, list all target directories.
    for d
    do
      if [ -d "$d" ] && dir_has_td "$d"
      then
        printf '%s/%s\n' "$PWD" "$d"
      else
        printf >&2 '%s: "%s" is not a target. Targets are:\n' "$SELF" "$d"
        for d in *
        do
          [ -d "$d" ] || continue
          dir_has_td "$d" && printf >&2 '  %s\n' "$d"
        done
        exit 2
      fi
    done >$TMP:W
  fi
  td_dirs
}

# Change to the directory at the top of the enclosing LLVM source tree,
# if possible.
llvm_src_root() {
  while [ "$PWD" != / ]
  do
    # Use this directory if multiple notable subdirectories are present.
    [ -d include/llvm -a -d lib/Target ] && return 0
    cd ..
  done
  return 1
}

# Ensure sort(1) behaves consistently.
LC_ALL=C
export LC_ALL

# Globals.
TBLGEN=llvm-tblgen
LLVM_SRC_ROOT=

# Command options.
OPT_TAGSFILE=tags
OPT_RECIPES=0
OPT_APPEND=0
OPT_VERBOSE=0
OPT_NOTBLGENERR=0

while getopts 'af:hxqvHI:' opt
do
  case $opt in
  a)
    OPT_APPEND=1
    ;;
  f)
    OPT_TAGSFILE="$OPTARG"
    ;;
  x)
    OPT_RECIPES=1
    ;;
  q)
    OPT_NOTBLGENERR=1
    ;;
  v)
    OPT_VERBOSE=$((OPT_VERBOSE + 1))
    ;;
  I)
    printf '%s\n' "$OPTARG" >>$TMP:D
    ;;
  [hH])
    help
    exit 0
    ;;
  *)
    usage >&2
    exit 4
    ;;
  esac
done
shift $((OPTIND - 1))

# Handle the case where tdtags is a simple ctags(1)-like wrapper for tblgen.
if [ $OPT_RECIPES -eq 0 ]
then
  if [ -z "$*" ]
  then
    help >&2
    exit 5
  fi
  for i
  do
    printf '%s\n' "$i"
  done >$TMP:T
  td_tag
  exit $?
fi

# Find the directory at the top of the enclosing LLVM source tree.
if ! LLVM_SRC_ROOT=$(llvm_src_root && pwd)
then
  printf >&2 '%s: Run from within the LLVM source tree.\n' "$SELF"
  exit 3
fi

# Select canned actions.
RECIPE="$1"
case "$RECIPE" in
all)
  shift
  do_all
  ;;
.|cwd|here)
  shift
  do_here
  ;;
recurse)
  shift
  do_recurse
  ;;
target)
  shift
  do_targets "$@"
  ;;
*)
  if [ -n "$RECIPE" ]
  then
    shift
    printf >&2 '%s: Unknown recipe "-x %s". ' "$SELF" "$RECIPE"
  fi
  printf >&2 'Recipes:\n'
  usage_recipes >&2
  printf >&2 'Run "%s -H" for help.\n' "$SELF"
  exit 6
  ;;
esac

exit $?
