#!/bin/sh

# mergemaster mtree database generator

# This script is intended to be used as part of the release building
# process to generate the /var/db/mergemaster.mtree file relevant to
# the source tree used to create the release so that users can make
# use of mergemaster's -U option to update their files after updating
# to -stable.

# Copyright 2009 Douglas Barton
# dougb@FreeBSD.org

# $FreeBSD$

PATH=/bin:/usr/bin:/usr/sbin

display_usage () {
  VERSION_NUMBER=`grep "[$]FreeBSD:" $0 | cut -d ' ' -f 4`
  echo "${0##*/} version ${VERSION_NUMBER}"
  echo "Usage: ${0##*/} [-m /path] [-t /path] [-A arch] [-F <make args>] [-D /path]"
  echo "Options:"
  echo "  -m /path/directory  Specify location of source to do the make in"
  echo "  -t /path/directory  Specify temp root directory"
  echo "  -A architecture  Alternative architecture name to pass to make"
  echo "  -F <arguments for make> Specify what to put on the make command line"
  echo '  -D /path/directory  Specify the destination directory to install files to'
  echo ''
}

# Set the default path for the temporary root environment
#
TEMPROOT=`TMPDIR=/var/tmp mktemp -d -t temproot`

# Assign the location of the mtree database
#
MTREEDB=${MTREEDB:-/var/db}
MTREEFILE="${MTREEDB}/mergemaster.mtree"

# Check the command line options
#
while getopts "m:t:A:F:D:h" COMMAND_LINE_ARGUMENT ; do
  case "${COMMAND_LINE_ARGUMENT}" in
  m)
    SOURCEDIR=${OPTARG}
    ;;
  t)
    TEMPROOT=${OPTARG}
    ;;
  A)
    ARCHSTRING='TARGET_ARCH='${OPTARG}
    ;;
  F)
    MM_MAKE_ARGS="${OPTARG}"
    ;;
  D)
    DESTDIR=${OPTARG}
    ;;
  h)
    display_usage
    exit 0
    ;;
  *)
    echo ''
    display_usage
    exit 1
    ;;
  esac
done

# Assign the source directory
#
SOURCEDIR=${SOURCEDIR:-/usr/src}
if [ ! -f ${SOURCEDIR}/Makefile.inc1 -a \
   -f ${SOURCEDIR}/../Makefile.inc1 ]; then
  echo " *** The source directory you specified (${SOURCEDIR})"
  echo "     will be reset to ${SOURCEDIR}/.."
  echo ''
  sleep 3
  SOURCEDIR=${SOURCEDIR}/..
fi

# Setup make to use system files from SOURCEDIR
MM_MAKE="make ${ARCHSTRING} ${MM_MAKE_ARGS} -m ${SOURCEDIR}/share/mk -DDB_FROM_SRC"

delete_temproot () {
  rm -rf "${TEMPROOT}" 2>/dev/null
  chflags -R 0 "${TEMPROOT}" 2>/dev/null
  rm -rf "${TEMPROOT}" || exit 1
}

[ -d "${TEMPROOT}" ] && delete_temproot

echo "*** Creating the temporary root environment in ${TEMPROOT}"

if mkdir -p "${TEMPROOT}"; then
  echo " *** ${TEMPROOT} ready for use"
fi

if [ ! -d "${TEMPROOT}" ]; then
  echo ''
  echo "  *** FATAL ERROR: Cannot create ${TEMPROOT}"
  echo ''
  exit 1
fi

echo " *** Creating and populating directory structure in ${TEMPROOT}"
echo ''

{ cd ${SOURCEDIR} || { echo "*** Cannot cd to ${SOURCEDIR}" ; exit 1;}
  case "${DESTDIR}" in
  '') ;;
  *)
    ${MM_MAKE} DESTDIR=${DESTDIR} distrib-dirs
    ;;
  esac
  ${MM_MAKE} DESTDIR=${TEMPROOT} distrib-dirs &&
  ${MM_MAKE} _obj SUBDIR_OVERRIDE=etc &&
  ${MM_MAKE} everything SUBDIR_OVERRIDE=etc &&
  ${MM_MAKE} DESTDIR=${TEMPROOT} distribution;} ||
  { echo '';
    echo "  *** FATAL ERROR: Cannot 'cd' to ${SOURCEDIR} and install files to";
    echo "      the temproot environment";
    echo '';
    exit 1;}

# We really don't want to have to deal with files like login.conf.db, pwd.db,
# or spwd.db.  Instead, we want to compare the text versions, and run *_mkdb.
# Prompt the user to do so below, as needed.
#
rm -f ${TEMPROOT}/etc/*.db ${TEMPROOT}/etc/passwd

# We only need to compare things like freebsd.cf once
find ${TEMPROOT}/usr/obj -type f -delete 2>/dev/null

# Delete stuff we do not need to keep the mtree database small,
# and to make the actual comparison faster.
find ${TEMPROOT}/usr -type l -delete 2>/dev/null
find ${TEMPROOT} -type f -size 0 -delete 2>/dev/null
find -d ${TEMPROOT} -type d -empty -delete 2>/dev/null

# Build the mtree database in a temporary location.
MTREENEW=`mktemp -t mergemaster.mtree`
mtree -nci -p ${TEMPROOT} -k size,md5digest > ${MTREENEW} 2>/dev/null

if [ -s "${MTREENEW}" ]; then
  echo "*** Saving mtree database for future upgrades"
  test -e "${DESTDIR}${MTREEFILE}" && unlink ${DESTDIR}${MTREEFILE}
  mv ${MTREENEW} ${DESTDIR}${MTREEFILE}
fi

delete_temproot

exit 0
