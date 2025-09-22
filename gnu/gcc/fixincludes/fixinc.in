#!/bin/sh
#
# Install modified versions of certain ANSI-incompatible system header
# files which are fixed to work correctly with ANSI C and placed in a
# directory that GCC will search.
#
# See README-fixinc for more information.
#
#  fixincludes copyright (c) 1998, 1999, 2000, 2002
#  The Free Software Foundation, Inc.
#
# fixincludes is free software.
# 
# You may redistribute it and/or modify it under the terms of the
# GNU General Public License, as published by the Free Software
# Foundation; either version 2, or (at your option) any later version.
# 
# fixincludes is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with fixincludes.  See the file "COPYING".  If not,
# write to:  The Free Software Foundation, Inc.,
#            51 Franklin Street, Fifth Floor,
#            Boston,  MA  02110-1301, USA.
#
# # # # # # # # # # # # # # # # # # # # #

# Usage: fixinc.sh output-dir input-dir
#
# Directory in which to store the results.
# Fail if no arg to specify a directory for the output.
if [ "x$1" = "x" ]
then
  echo fixincludes: no output directory specified
  exit 1
fi

LIB=${1}
shift

# Make sure it exists.
if [ ! -d $LIB ]; then
  mkdir $LIB || {
    echo fixincludes:  output dir '`'$LIB"' cannot be created"
    exit 1
  }
else
  ( cd $LIB && touch DONE && rm DONE ) || {
    echo fixincludes:  output dir '`'$LIB"' is an invalid directory"
    exit 1
  }
fi

if test -z "$VERBOSE"
then
  VERBOSE=2
  export VERBOSE
else
  case "$VERBOSE" in
  [0-9] ) : ;;
  * )  VERBOSE=3 ;;
  esac
fi

# Define what target system we're fixing.
#
if test -r ./Makefile; then
  target_canonical="`sed -n -e 's,^target[ 	]*=[ 	]*\(.*\)$,\1,p' < Makefile`"
fi

# If not from the Makefile, then try config.guess
#
if test -z "${target_canonical}" ; then
  if test -x ./config.guess ; then
    target_canonical="`config.guess`" ; fi
  test -z "${target_canonical}" && target_canonical=unknown
fi
export target_canonical

# # # # # # # # # # # # # # # # # # # # #
#
# Define PWDCMD as a command to use to get the working dir
# in the form that we want.
PWDCMD=${PWDCMD-pwd}

case "`$PWDCMD`" in
//*)
    # On an Apollo, discard everything before `/usr'.
    PWDCMD="eval pwd | sed -e 's,.*/usr/,/usr/,'"
    ;;
esac

# Original directory.
ORIGDIR=`${PWDCMD}`
export ORIGDIR
FIXINCL=`${PWDCMD}`/fixincl
if [ ! -x $FIXINCL ] ; then
  echo "Cannot find fixincl" >&2
  exit 1
fi
export FIXINCL

# Make LIB absolute only if needed to avoid problems with the amd.
case $LIB in
/*)
    ;;
*)
    cd $LIB; LIB=`${PWDCMD}`
    ;;
esac

if test $VERBOSE -gt 0
then echo Fixing headers into ${LIB} for ${target_canonical} target ; fi

# Determine whether this system has symbolic links.
if test -n "$DJDIR"; then
  LINKS=false
elif ln -s X $LIB/ShouldNotExist 2>/dev/null; then
  rm -f $LIB/ShouldNotExist
  LINKS=true
elif ln -s X /tmp/ShouldNotExist 2>/dev/null; then
  rm -f /tmp/ShouldNotExist
  LINKS=true
else
  LINKS=false
fi

# # # # # # # # # # # # # # # # # # # # #
#
#  In the file macro_list are listed all the predefined
#  macros that are not in the C89 reserved namespace (the reserved
#  namespace is all identifiers beginnning with two underscores or one
#  underscore followed by a capital letter).  A regular expression to find
#  any of those macros in a header file is written to MN_NAME_PAT.
#
#  Note dependency on ASCII. \012 = newline.
#  tr ' ' '\n' is, alas, not portable.

if test -s ${MACRO_LIST}
then
  if test $VERBOSE -gt 0; then
    echo "Forbidden identifiers: `tr '\012' ' ' < ${MACRO_LIST}`"
  fi
  MN_NAME_PAT="`sed 's/^/\\\\</; s/$/\\\\>/; $!s/$/|/' \
      < ${MACRO_LIST} | tr -d '\012'`"
  export MN_NAME_PAT
else
  if test $VERBOSE -gt 0
  then echo "No forbidden identifiers defined by this target" ; fi
fi

# # # # # # # # # # # # # # # # # # # # #
#
#  Search each input directory for broken header files.
#  This loop ends near the end of the file.
#
if test $# -eq 0
then
    INPUTLIST="/usr/include"
else
    INPUTLIST="$@"
fi

for INPUT in ${INPUTLIST} ; do

cd ${ORIGDIR}

#  Make sure a directory exists before changing into it,
#  otherwise Solaris2 will fail-exit the script.
#
if [ ! -d ${INPUT} ]; then
  continue
fi
cd ${INPUT}

INPUT=`${PWDCMD}`
export INPUT

#
# # # # # # # # # # # # # # # # # # # # #
#
if test $VERBOSE -gt 1
then echo Finding directories and links to directories ; fi

# Find all directories and all symlinks that point to directories.
# Put the list in $all_dirs.
# Each time we find a symlink, add it to newdirs
# so that we do another find within the dir the link points to.
# Note that $all_dirs may have duplicates in it;
# later parts of this file are supposed to ignore them.
dirs="."
levels=2
all_dirs=""
search_dirs=""

while [ -n "$dirs" ] && [ $levels -gt 0 ]
do
  levels=`expr $levels - 1`
  newdirs=
  for d in $dirs
  do
    if test $VERBOSE -gt 1
    then echo " Searching $INPUT/$d" ; fi

    # Find all directories under $d, relative to $d, excluding $d itself.
    # (The /. is needed after $d in case $d is a symlink.)
    all_dirs="$all_dirs `find $d/. -type d -print | \
               sed -e '/\/\.$/d' -e 's@/./@/@g'`"
    # Find all links to directories.
    # Using `-exec test -d' in find fails on some systems,
    # and trying to run test via sh fails on others,
    # so this is the simplest alternative left.
    # First find all the links, then test each one.
    theselinks=
    $LINKS && \
      theselinks=`find $d/. -type l -print | sed -e 's@/./@/@g'`
    for d1 in $theselinks --dummy--
    do
      # If the link points to a directory,
      # add that dir to $newdirs
      if [ -d $d1 ]
      then
        all_dirs="$all_dirs $d1"
        if [ "`ls -ld $d1 | sed -n 's/.*-> //p'`" != "." ]
        then
          newdirs="$newdirs $d1"
          search_dirs="$search_dirs $d1"
        fi
      fi
    done
  done

  dirs="$newdirs"
done

# # # # # # # # # # # # # # # # # # # # #
#
dirs=
if test $VERBOSE -gt 2
then echo "All directories (including links to directories):"
     echo $all_dirs
fi

for file in $all_dirs; do
  rm -rf $LIB/$file
  if [ ! -d $LIB/$file ]
  then mkdir $LIB/$file
  fi
done
mkdir $LIB/root

# # # # # # # # # # # # # # # # # # # # #
#
# treetops gets an alternating list
# of old directories to copy
# and the new directories to copy to.
treetops=". ${LIB}"

if $LINKS; then
  if test $VERBOSE -gt 1
  then echo 'Making symbolic directory links' ; fi
  cwd=`${PWDCMD}`

  for sym_link in $search_dirs; do
    cd ${INPUT}
    dest=`ls -ld ${sym_link} | sed -n 's/.*-> //p'`

    # In case $dest is relative, get to ${sym_link}'s dir first.
    #
    cd ./`echo ${sym_link} | sed 's;/[^/]*$;;'`

    # Check that the target directory exists.
    # Redirections changed to avoid bug in sh on Ultrix.
    #
    (cd $dest) > /dev/null 2>&1
    if [ $? = 0 ]; then
      cd $dest

      # full_dest_dir gets the dir that the link actually leads to.
      #
      full_dest_dir=`${PWDCMD}`

      # Canonicalize ${INPUT} now to minimize the time an
      # automounter has to change the result of ${PWDCMD}.
      #
      cinput=`cd ${INPUT}; ${PWDCMD}`

      # If a link points to ., make a similar link to .
      #
      if [ ${full_dest_dir} = ${cinput} ]; then
        if test $VERBOSE -gt 2
        then echo ${sym_link} '->' . ': Making self link' ; fi
        rm -fr ${LIB}/${sym_link} > /dev/null 2>&1
        ln -s . ${LIB}/${sym_link} > /dev/null 2>&1

      # If link leads back into ${INPUT},
      # make a similar link here.
      #
      elif expr ${full_dest_dir} : "${cinput}/.*" > /dev/null; then
        # Y gets the actual target dir name, relative to ${INPUT}.
        y=`echo ${full_dest_dir} | sed -n "s&${cinput}/&&p"`
        # DOTS is the relative path from ${LIB}/${sym_link} back to ${LIB}.
        dots=`echo "${sym_link}" |
          sed -e 's@^./@@' -e 's@/./@/@g' -e 's@[^/][^/]*@..@g' -e 's@..$@@'`
        if test $VERBOSE -gt 2
        then echo ${sym_link} '->' $dots$y ': Making local link' ; fi
        rm -fr ${LIB}/${sym_link} > /dev/null 2>&1
        ln -s $dots$y ${LIB}/${sym_link} > /dev/null 2>&1

      else
        # If the link is to a dir $target outside ${INPUT},
        # repoint the link at ${INPUT}/root$target
        # and process $target into ${INPUT}/root$target
        # treat this directory as if it actually contained the files.
        #
        if test $VERBOSE -gt 2
        then echo ${sym_link} '->' root${full_dest_dir} ': Making rooted link'
        fi
        if [ -d $LIB/root${full_dest_dir} ]
        then true
        else
          dirname=root${full_dest_dir}/
          dirmade=.
          cd $LIB
          while [ x$dirname != x ]; do
            component=`echo $dirname | sed -e 's|/.*$||'`
            mkdir $component >/dev/null 2>&1
            cd $component
            dirmade=$dirmade/$component
            dirname=`echo $dirname | sed -e 's|[^/]*/||'`
          done
        fi

        # Duplicate directory structure created in ${LIB}/${sym_link} in new
        # root area.
        #
        for file2 in $all_dirs; do
          case $file2 in
            ${sym_link}/*)
              dupdir=${LIB}/root${full_dest_dir}/`echo $file2 |
                      sed -n "s|^${sym_link}/||p"`
              if test $VERBOSE -gt 2
              then echo "Duplicating ${sym_link}'s ${dupdir}" ; fi
              if [ -d ${dupdir} ]
              then true
              else
                mkdir ${dupdir}
              fi
              ;;
            *)
              ;;
          esac
        done

        # Get the path from ${LIB} to ${sym_link}, accounting for symlinks.
        #
        parent=`echo "${sym_link}" | sed -e 's@/[^/]*$@@'`
        libabs=`cd ${LIB}; ${PWDCMD}`
        file2=`cd ${LIB}; cd $parent; ${PWDCMD} | sed -e "s@^${libabs}@@"`

        # DOTS is the relative path from ${LIB}/${sym_link} back to ${LIB}.
        #
        dots=`echo "$file2" | sed -e 's@/[^/]*@../@g'`
        rm -fr ${LIB}/${sym_link} > /dev/null 2>&1
        ln -s ${dots}root${full_dest_dir} ${LIB}/${sym_link} > /dev/null 2>&1
        treetops="$treetops ${sym_link} ${LIB}/root${full_dest_dir}"
      fi
    fi
  done
fi

# # # # # # # # # # # # # # # # # # # # #
#
required=
set x $treetops
shift
while [ $# != 0 ]; do
  # $1 is an old directory to copy, and $2 is the new directory to copy to.
  #
  SRCDIR=`cd ${INPUT} ; cd $1 ; ${PWDCMD}`
  export SRCDIR

  FIND_BASE=$1
  export FIND_BASE
  shift

  DESTDIR=`cd $1;${PWDCMD}`
  export DESTDIR
  shift

  # The same dir can appear more than once in treetops.
  # There's no need to scan it more than once.
  #
  if [ -f ${DESTDIR}/DONE ]
  then continue ; fi

  touch ${DESTDIR}/DONE
  if test $VERBOSE -gt 1
  then echo Fixing directory ${SRCDIR} into ${DESTDIR} ; fi

  # Check files which are symlinks as well as those which are files.
  #
  cd ${INPUT}
  required="$required `if $LINKS; then
    find ${FIND_BASE}/. -name '*.h' \( -type f -o -type l \) -print
  else
    find ${FIND_BASE}/. -name '*.h' -type f -print
  fi | \
    sed -e 's;/\./;/;g' -e 's;//*;/;g' | \
    ${FIXINCL}`"
done

## Make sure that any include files referenced using double quotes
## exist in the fixed directory.  This comes last since otherwise
## we might end up deleting some of these files "because they don't
## need any change."
set x `echo $required`
shift
while [ $# != 0 ]; do
  newreq=
  while [ $# != 0 ]; do
    # $1 is the directory to copy from,
    # $2 is the unfixed file,
    # $3 is the fixed file name.
    #
    cd ${INPUT}
    cd $1
    if [ -f $2 ] ; then
      if [ -r $2 ] && [ ! -r $3 ]; then
        cp $2 $3 >/dev/null 2>&1 || echo "Can't copy $2" >&2
        chmod +w $3 2>/dev/null
        chmod a+r $3 2>/dev/null
        if test $VERBOSE -gt 2
        then echo Copied $2 ; fi
        for include in `egrep '^[ 	]*#[ 	]*include[ 	]*"[^/]' $3 |
             sed -e 's/^[ 	]*#[ 	]*include[ 	]*"\([^"]*\)".*$/\1/'`
        do
	  dir=`echo $2 | sed -e s'|/[^/]*$||'`
	  dir2=`echo $3 | sed -e s'|/[^/]*$||'`
	  newreq="$newreq $1 $dir/$include $dir2/$include"
        done
      fi
    fi
    shift; shift; shift
  done
  set x $newreq
  shift
done

if test $VERBOSE -gt 2
then echo 'Cleaning up DONE files.' ; fi
cd $LIB
# Look for files case-insensitively, for the benefit of
# DOS/Windows filesystems.
find . -name '[Dd][Oo][Nn][Ee]' -exec rm -f '{}' ';'

if test $VERBOSE -gt 1
then echo 'Cleaning up unneeded directories:' ; fi
cd $LIB
all_dirs=`find . -type d \! -name '.' -print | sort -r`
for file in $all_dirs; do
  if rmdir $LIB/$file > /dev/null
  then
    test $VERBOSE -gt 3 && echo "  removed $file"
  fi
done 2> /dev/null

# On systems which don't support symlinks, `find' may barf
# if called with "-type l" predicate.  So only use that if
# we know we should look for symlinks.
if $LINKS; then
  test $VERBOSE -gt 2 && echo "Removing unused symlinks"

  all_dirs=`find . -type l -print`
  for file in $all_dirs
  do
    if test ! -d $file
    then
      rm -f $file
      test $VERBOSE -gt 3 && echo "  removed $file"
      rmdir `dirname $file` > /dev/null && \
           test $VERBOSE -gt 3 && \
           echo "  removed `dirname $file`"
    fi
  done 2> /dev/null
fi

if test $VERBOSE -gt 0
then echo fixincludes is done ; fi

# # # # # # # # # # # # # # # # # # # # #
#
# End of for INPUT directories
#
done
#
# # # # # # # # # # # # # # # # # # # # #
