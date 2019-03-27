#!/bin/sh

# mergemaster

# Compare files created by /usr/src/etc/Makefile (or the directory
# the user specifies) with the currently installed copies.

# Copyright (c) 1998-2012 Douglas Barton, All rights reserved
# Please see detailed copyright below

# $FreeBSD$

PATH=/bin:/usr/bin:/usr/sbin

display_usage () {
  VERSION_NUMBER=`grep "[$]FreeBSD:" $0 | cut -d ' ' -f 4`
  echo "mergemaster version ${VERSION_NUMBER}"
  echo 'Usage: mergemaster [-scrvhpCP] [-a|[-iFU]] [--run-updates=always|never]'
  echo '    [-m /path] [-t /path] [-d] [-u N] [-w N] [-A arch] [-D /path]'
  echo "Options:"
  echo "  -s  Strict comparison (diff every pair of files)"
  echo "  -c  Use context diff instead of unified diff"
  echo "  -r  Re-run on a previously cleaned directory (skip temproot creation)"
  echo "  -v  Be more verbose about the process, include additional checks"
  echo "  -a  Leave all files that differ to merge by hand"
  echo "  -h  Display more complete help"
  echo '  -i  Automatically install files that do not exist in destination directory'
  echo '  -p  Pre-buildworld mode, only compares crucial files'
  echo '  -F  Install files that differ only by revision control Id ($FreeBSD)'
  echo '  -C  Compare local rc.conf variables to the defaults'
  echo '  -P  Preserve files that are overwritten'
  echo "  -U  Attempt to auto upgrade files that have not been user modified"
  echo '      ***DANGEROUS***'
  echo '  --run-updates=  Specify always or never to run newalises, pwd_mkdb, etc.'
  echo ''
  echo "  -m /path/directory  Specify location of source to do the make in"
  echo "  -t /path/directory  Specify temp root directory"
  echo "  -d  Add date and time to directory name (e.g., /var/tmp/temproot.`date +%m%d.%H.%M`)"
  echo "  -u N  Specify a numeric umask"
  echo "  -w N  Specify a screen width in columns to sdiff"
  echo "  -A architecture  Alternative architecture name to pass to make"
  echo '  -D /path/directory  Specify the destination directory to install files to'
  echo ''
}

display_help () {
  echo "* To specify a directory other than /var/tmp/temproot for the"
  echo "  temporary root environment, use -t /path/to/temp/root"
  echo "* The -w option takes a number as an argument for the column width"
  echo "  of the screen.  The default is 80."
  echo '* The -a option causes mergemaster to run without prompting.'
}

# Loop allowing the user to use sdiff to merge files and display the merged
# file.
merge_loop () {
  case "${VERBOSE}" in
  '') ;;
  *)
      echo "   *** Type h at the sdiff prompt (%) to get usage help"
      ;;
  esac
  echo ''
  MERGE_AGAIN=yes
  while [ "${MERGE_AGAIN}" = "yes" ]; do
    # Prime file.merged so we don't blat the owner/group id's
    cp -p "${COMPFILE}" "${COMPFILE}.merged"
    sdiff -o "${COMPFILE}.merged" --text --suppress-common-lines \
      --width=${SCREEN_WIDTH:-80} "${DESTDIR}${COMPFILE#.}" "${COMPFILE}"
    INSTALL_MERGED=V
    while [ "${INSTALL_MERGED}" = "v" -o "${INSTALL_MERGED}" = "V" ]; do
      echo ''
      echo "  Use 'i' to install merged file"
      echo "  Use 'r' to re-do the merge"
      echo "  Use 'v' to view the merged file"
      echo "  Default is to leave the temporary file to deal with by hand"
      echo ''
      echo -n "    *** How should I deal with the merged file? [Leave it for later] "
      read INSTALL_MERGED

      case "${INSTALL_MERGED}" in
      [iI])
        mv "${COMPFILE}.merged" "${COMPFILE}"
        echo ''
        if mm_install "${COMPFILE}"; then
          echo "     *** Merged version of ${COMPFILE} installed successfully"
        else
          echo "     *** Problem installing ${COMPFILE}, it will remain to merge by hand later"
        fi
        unset MERGE_AGAIN
        ;;
      [rR])
        rm "${COMPFILE}.merged"
        ;;
      [vV])
        ${PAGER} "${COMPFILE}.merged"
        ;;
      '')
        echo "   *** ${COMPFILE} will remain for your consideration"
        unset MERGE_AGAIN
        ;;
      *)
        echo "invalid choice: ${INSTALL_MERGED}"
        INSTALL_MERGED=V
        ;;
      esac
    done
  done
}

# Loop showing user differences between files, allow merge, skip or install
# options
diff_loop () {

  HANDLE_COMPFILE=v

  while [ "${HANDLE_COMPFILE}" = "v" -o "${HANDLE_COMPFILE}" = "V" -o \
    "${HANDLE_COMPFILE}" = "NOT V" ]; do
    if [ -f "${DESTDIR}${COMPFILE#.}" -a -f "${COMPFILE}" ]; then
      if [ -n "${AUTO_UPGRADE}" -a -n "${CHANGED}" ]; then
        case "${CHANGED}" in
        *:${DESTDIR}${COMPFILE#.}:*) ;;		# File has been modified
        *)
          echo ''
          echo "  *** ${COMPFILE} has not been user modified."
          echo ''

          if mm_install "${COMPFILE}"; then
            echo "   *** ${COMPFILE} upgraded successfully"
            echo ''
            # Make the list print one file per line
            AUTO_UPGRADED_FILES="${AUTO_UPGRADED_FILES}      ${DESTDIR}${COMPFILE#.}
"
          else
            echo "   *** Problem upgrading ${COMPFILE}, it will remain to merge by hand"
          fi
          return
          ;;
        esac
      fi
      if [ "${HANDLE_COMPFILE}" = "v" -o "${HANDLE_COMPFILE}" = "V" ]; then
	echo ''
	echo '   ======================================================================   '
	echo ''
        (
          echo "  *** Displaying differences between ${COMPFILE} and installed version:"
          echo ''
          diff ${DIFF_FLAG} ${DIFF_OPTIONS} "${DESTDIR}${COMPFILE#.}" "${COMPFILE}"
        ) | ${PAGER}
        echo ''
      fi
    else
      echo ''
      echo "  *** There is no installed version of ${COMPFILE}"
      echo ''
      case "${AUTO_INSTALL}" in
      [Yy][Ee][Ss])
        echo ''
        if mm_install "${COMPFILE}"; then
          echo "   *** ${COMPFILE} installed successfully"
          echo ''
          # Make the list print one file per line
          AUTO_INSTALLED_FILES="${AUTO_INSTALLED_FILES}      ${DESTDIR}${COMPFILE#.}
"
        else
          echo "   *** Problem installing ${COMPFILE}, it will remain to merge by hand"
        fi
        return
        ;;
      *)
        NO_INSTALLED=yes
        ;;
      esac
    fi

    echo "  Use 'd' to delete the temporary ${COMPFILE}"
    echo "  Use 'i' to install the temporary ${COMPFILE}"
    case "${NO_INSTALLED}" in
    '')
      echo "  Use 'm' to merge the temporary and installed versions"
      echo "  Use 'v' to view the diff results again"
      ;;
    esac
    echo ''
    echo "  Default is to leave the temporary file to deal with by hand"
    echo ''
    echo -n "How should I deal with this? [Leave it for later] "
    read HANDLE_COMPFILE

    case "${HANDLE_COMPFILE}" in
    [dD])
      rm "${COMPFILE}"
      echo ''
      echo "   *** Deleting ${COMPFILE}"
      ;;
    [iI])
      echo ''
      if mm_install "${COMPFILE}"; then
        echo "   *** ${COMPFILE} installed successfully"
      else
        echo "   *** Problem installing ${COMPFILE}, it will remain to merge by hand"
      fi
      ;;
    [mM])
      case "${NO_INSTALLED}" in
      '')
        # interact with user to merge files
        merge_loop
        ;;
      *)
        echo ''
        echo "   *** There is no installed version of ${COMPFILE}"
        echo ''
        HANDLE_COMPFILE="NOT V"
        ;;
      esac # End of "No installed version of file but user selected merge" test
      ;;
    [vV])
      continue
      ;;
    '')
      echo ''
      echo "   *** ${COMPFILE} will remain for your consideration"
      ;;
    *)
      # invalid choice, show menu again.
      echo "invalid choice: ${HANDLE_COMPFILE}"
      echo ''
      HANDLE_COMPFILE="NOT V"
      continue
      ;;
    esac  # End of "How to handle files that are different"
  done
  unset NO_INSTALLED
  echo ''
  case "${VERBOSE}" in
  '') ;;
  *)
    sleep 3
    ;;
  esac
}

press_to_continue () {
  local DISCARD
  echo -n ' *** Press the [Enter] or [Return] key to continue '
  read DISCARD
}

# Set the default path for the temporary root environment
#
TEMPROOT='/var/tmp/temproot'

# Read /etc/mergemaster.rc first so the one in $HOME can override
#
if [ -r /etc/mergemaster.rc ]; then
  . /etc/mergemaster.rc
fi

# Read .mergemasterrc before command line so CLI can override
#
if [ -r "$HOME/.mergemasterrc" ]; then
  . "$HOME/.mergemasterrc"
fi

for var in "$@" ; do
  case "$var" in
  --run-updates*)
    RUN_UPDATES=`echo ${var#--run-updates=} | tr [:upper:] [:lower:]`
    ;;
  *)
    newopts="$newopts $var"
    ;;
  esac
done

set -- $newopts
unset var newopts

# Check the command line options
#
while getopts ":ascrvhipCPm:t:du:w:D:A:FU" COMMAND_LINE_ARGUMENT ; do
  case "${COMMAND_LINE_ARGUMENT}" in
  A)
    ARCHSTRING='TARGET_ARCH='${OPTARG}
    ;;
  F)
    FREEBSD_ID=yes
    ;;
  U)
    AUTO_UPGRADE=yes
    ;;
  s)
    STRICT=yes
    unset DIFF_OPTIONS
    ;;
  c)
    DIFF_FLAG='-c'
    ;;
  r)
    RERUN=yes
    ;;
  v)
    case "${AUTO_RUN}" in
    '') VERBOSE=yes ;;
    esac
    ;;
  a)
    AUTO_RUN=yes
    unset VERBOSE
    ;;
  h)
    display_usage
    display_help
    exit 0
    ;;
  i)
    AUTO_INSTALL=yes
    ;;
  C)
    COMP_CONFS=yes
    ;;
  P)
    PRESERVE_FILES=yes
    ;;
  p)
    PRE_WORLD=yes
    unset COMP_CONFS
    unset AUTO_RUN
    ;;
  m)
    SOURCEDIR=${OPTARG}
    ;;
  t)
    TEMPROOT=${OPTARG}
    ;;
  d)
    TEMPROOT=${TEMPROOT}.`date +%m%d.%H.%M`
    ;;
  u)
    NEW_UMASK=${OPTARG}
    ;;
  w)
    SCREEN_WIDTH=${OPTARG}
    ;;
  D)
    DESTDIR=${OPTARG}
    ;;
  *)
    display_usage
    exit 1
    ;;
  esac
done

if [ -n "$AUTO_RUN" ]; then
  if [ -n "$FREEBSD_ID" -o -n "$AUTO_UPGRADE" -o -n "$AUTO_INSTALL" ]; then
    echo ''
    echo "*** You have included the -a option along with one or more options"
    echo '    that indicate that you wish mergemaster to actually make updates'
    echo '    (-F, -U, or -i), however these options are not compatible.'
    echo '    Please read mergemaster(8) for more information.'
    echo ''
    exit 1
  fi
fi

# Assign the location of the mtree database
#
MTREEDB=${MTREEDB:-${DESTDIR}/var/db}
MTREEFILE="${MTREEDB}/mergemaster.mtree"

# Don't force the user to set this in the mergemaster rc file
if [ -n "${PRESERVE_FILES}" -a -z "${PRESERVE_FILES_DIR}" ]; then
  PRESERVE_FILES_DIR=/var/tmp/mergemaster/preserved-files-`date +%y%m%d-%H%M%S`
  mkdir -p ${PRESERVE_FILES_DIR}
fi

# Check for the mtree database in DESTDIR
case "${AUTO_UPGRADE}" in
'') ;;	# If the option is not set no need to run the test or warn the user
*)
  if [ ! -s "${MTREEFILE}" ]; then
    echo ''
    echo "*** Unable to find mtree database (${MTREEFILE})."
    echo "    Skipping auto-upgrade on this run."
    echo "    It will be created for the next run when this one is complete."
    echo ''
    case "${AUTO_RUN}" in
    '')
      press_to_continue
      ;;
    esac
    unset AUTO_UPGRADE
  fi
  ;;
esac

if [ -e "${DESTDIR}/etc/fstab" ]; then
  if grep -q nodev ${DESTDIR}/etc/fstab; then
    echo ''
    echo "*** You have the deprecated 'nodev' option in ${DESTDIR}/etc/fstab."
    echo "    This can prevent the filesystem from being mounted on reboot."
    echo "    Please update your fstab before continuing."
    echo "    See fstab(5) for more information."
    echo ''
    exit 1
  fi
fi

echo ''

# If the user has a pager defined, make sure we can run it
#
case "${DONT_CHECK_PAGER}" in
'')
check_pager () {
  while ! type "${PAGER%% *}" >/dev/null; do
    echo " *** Your PAGER environment variable specifies '${PAGER}', but"
    echo "     due to the limited PATH that I use for security reasons,"
    echo "     I cannot execute it.  So, what would you like to do?"
    echo ''
    echo "  Use 'e' to exit mergemaster and fix your PAGER variable"
    echo "  Use 'l' to set PAGER to 'less' for this run"
    echo "  Use 'm' to use plain old 'more' as your PAGER for this run"
    echo ''
    echo "  or you may type an absolute path to PAGER for this run"
    echo ''
    echo "  Default is to use 'less' "
    echo ''
    echo -n "What should I do? [Use 'less'] "
    read FIXPAGER

    case "${FIXPAGER}" in
    [eE])
       exit 0
       ;;
    [lL]|'')
       PAGER=less
       ;;
    [mM])
       PAGER=more
       ;;
    /*)
       PAGER="$FIXPAGER"
       ;;
    *)
       echo ''
       echo "invalid choice: ${FIXPAGER}"
    esac
    echo ''
  done
}
  if [ -n "${PAGER}" ]; then
    check_pager
  fi
  ;;
esac

# If user has a pager defined, or got assigned one above, use it.
# If not, use less.
#
PAGER=${PAGER:-less}

if [ -n "${VERBOSE}" -a ! "${PAGER}" = "less" ]; then
  echo " *** You have ${PAGER} defined as your pager so we will use that"
  echo ''
  sleep 3
fi

# Assign the diff flag once so we will not have to keep testing it
#
DIFF_FLAG=${DIFF_FLAG:--u}

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
if [ ! -f ${SOURCEDIR}/Makefile.inc1 ]; then
    echo     "*** ${SOURCEDIR} was not found."
    if [ -f ./Makefile.inc1 ]; then
	echo "    Found Makefile.inc1 in the current directory."
	echo -n "    Would you like to set SOURCEDIR to $(pwd)? [no and exit] "
	read SRCDOT
	case "${SRCDOT}" in
	    [yY]*)
		echo "    *** Setting SOURCEDIR to $(pwd)"
		SOURCEDIR=$(pwd)
		;;
	    *)
		echo "    **** No suitable ${SOURCEDIR} found, exiting"
		exit 1
		;;
	esac
    else
	echo "    **** No suitable ${SOURCEDIR} found, exiting"
	exit 1
    fi
fi
SOURCEDIR=$(realpath "$SOURCEDIR")

# Setup make to use system files from SOURCEDIR
MM_MAKE="make ${ARCHSTRING} -m ${SOURCEDIR}/share/mk -DNO_FILEMON"

# Check DESTDIR against the mergemaster mtree database to see what
# files the user changed from the reference files.
#
if [ -n "${AUTO_UPGRADE}" -a -s "${MTREEFILE}" ]; then
	# Force FreeBSD 9 compatible output when available.
	if mtree -F freebsd9 -c -p /var/empty/ > /dev/null 2>&1; then
		MTREE_FLAVOR="-F freebsd9"
	else
		MTREE_FLAVOR=
	fi
	CHANGED=:
	for file in `mtree -eqL ${MTREE_FLAVOR} -f ${MTREEFILE} -p ${DESTDIR}/ \
		2>/dev/null | awk '($2 == "changed") {print $1}'`; do
		if [ -f "${DESTDIR}/$file" ]; then
			CHANGED="${CHANGED}${DESTDIR}/${file}:"
		fi
	done
	[ "$CHANGED" = ':' ] && unset CHANGED
fi

# Check the width of the user's terminal
#
if [ -t 0 ]; then
  w=`tput columns`
  case "${w}" in
  0|'') ;; # No-op, since the input is not valid
  *)
    case "${SCREEN_WIDTH}" in
    '') SCREEN_WIDTH="${w}" ;;
    "${w}") ;; # No-op, since they are the same
    *)
      echo -n "*** You entered ${SCREEN_WIDTH} as your screen width, but stty "
      echo "thinks it is ${w}."
      echo ''
      echo -n "What would you like to use? [${w}] "
      read SCREEN_WIDTH
      case "${SCREEN_WIDTH}" in
      '') SCREEN_WIDTH="${w}" ;;
      esac
      ;;
    esac
  esac
fi

# Define what $Id tag to look for to aid portability.
#
ID_TAG=FreeBSD

delete_temproot () {
  rm -rf "${TEMPROOT}" 2>/dev/null
  chflags -R 0 "${TEMPROOT}" 2>/dev/null
  rm -rf "${TEMPROOT}" || { echo "*** Unable to delete ${TEMPROOT}";  exit 1; }
}

case "${RERUN}" in
'')
  # Set up the loop to test for the existence of the
  # temp root directory.
  #
  TEST_TEMP_ROOT=yes
  while [ "${TEST_TEMP_ROOT}" = "yes" ]; do
    if [ -d "${TEMPROOT}" ]; then
      echo "*** The directory specified for the temporary root environment,"
      echo "    ${TEMPROOT}, exists.  This can be a security risk if untrusted"
      echo "    users have access to the system."
      echo ''
      case "${AUTO_RUN}" in
      '')
        echo "  Use 'd' to delete the old ${TEMPROOT} and continue"
        echo "  Use 't' to select a new temporary root directory"
        echo "  Use 'e' to exit mergemaster"
        echo ''
        echo "  Default is to use ${TEMPROOT} as is"
        echo ''
        echo -n "How should I deal with this? [Use the existing ${TEMPROOT}] "
        read DELORNOT

        case "${DELORNOT}" in
        [dD])
          echo ''
          echo "   *** Deleting the old ${TEMPROOT}"
          echo ''
          delete_temproot
          unset TEST_TEMP_ROOT
          ;;
        [tT])
          echo "   *** Enter new directory name for temporary root environment"
          read TEMPROOT
          ;;
        [eE])
          exit 0
          ;;
        '')
          echo ''
          echo "   *** Leaving ${TEMPROOT} intact"
          echo ''
          unset TEST_TEMP_ROOT
          ;;
        *)
          echo ''
          echo "invalid choice: ${DELORNOT}"
          echo ''
          ;;
        esac
        ;;
      *)
        # If this is an auto-run, try a hopefully safe alternative then
        # re-test anyway.
        TEMPROOT=/var/tmp/temproot.`date +%m%d.%H.%M.%S`
        ;;
      esac
    else
      unset TEST_TEMP_ROOT
    fi
  done

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

  case "${VERBOSE}" in
  '') ;;
  *)
    press_to_continue
    ;;
  esac

  case "${PRE_WORLD}" in
  '')
    { cd ${SOURCEDIR} &&
      case "${DESTDIR}" in
      '') ;;
      *)
        ${MM_MAKE} DESTDIR=${DESTDIR} distrib-dirs >/dev/null
        ;;
      esac
      ${MM_MAKE} DESTDIR=${TEMPROOT} distrib-dirs >/dev/null &&
      ${MM_MAKE} _obj SUBDIR_OVERRIDE=etc >/dev/null &&
      ${MM_MAKE} everything SUBDIR_OVERRIDE=etc >/dev/null &&
      ${MM_MAKE} DESTDIR=${TEMPROOT} distribution >/dev/null;} ||
    { echo '';
     echo "  *** FATAL ERROR: Cannot 'cd' to ${SOURCEDIR} and install files to";
      echo "      the temproot environment";
      echo '';
      exit 1;}
    ;;
  *)
    # Only set up files that are crucial to {build|install}world
    { mkdir -p ${TEMPROOT}/etc &&
      cp -p ${SOURCEDIR}/etc/master.passwd ${TEMPROOT}/etc &&
      install -p -o root -g wheel -m 0644 ${SOURCEDIR}/etc/group ${TEMPROOT}/etc;} ||
    { echo '';
      echo '  *** FATAL ERROR: Cannot copy files to the temproot environment';
      echo '';
      exit 1;}
    ;;
  esac

  # Doing the inventory and removing files that we don't want to compare only
  # makes sense if we are not doing a rerun, since we have no way of knowing
  # what happened to the files during previous incarnations.
  case "${VERBOSE}" in
  '') ;;
  *)
    echo ''
    echo ' *** The following files exist only in the installed version of'
    echo "     ${DESTDIR}/etc.  In the vast majority of cases these files"
    echo '     are necessary parts of the system and should not be deleted.'
    echo '     However because these files are not updated by this process you'
    echo '     might want to verify their status before rebooting your system.'
    echo ''
    press_to_continue
    diff -qr ${DESTDIR}/etc ${TEMPROOT}/etc | grep "^Only in ${DESTDIR}/etc" | ${PAGER}
    echo ''
    press_to_continue
    ;;
  esac

  case "${IGNORE_MOTD}" in
  '') ;;
  *)
     echo ''
     echo "*** You have the IGNORE_MOTD option set in your mergemaster rc file."
     echo "    This option is deprecated in favor of the IGNORE_FILES option."
     echo "    Please update your rc file accordingly."
     echo ''
     exit 1
     ;;
  esac

  # Avoid comparing the following user specified files
  for file in ${IGNORE_FILES}; do
    test -e ${TEMPROOT}/${file} && unlink ${TEMPROOT}/${file}
  done

  # We really don't want to have to deal with files like login.conf.db, pwd.db,
  # or spwd.db.  Instead, we want to compare the text versions, and run *_mkdb.
  # Prompt the user to do so below, as needed.
  #
  rm -f ${TEMPROOT}/etc/*.db ${TEMPROOT}/etc/passwd \
      ${TEMPROOT}/var/db/services.db

  # We only need to compare things like freebsd.cf once
  find ${TEMPROOT}/usr/obj -type f -delete 2>/dev/null

  # Delete stuff we do not need to keep the mtree database small,
  # and to make the actual comparison faster.
  find ${TEMPROOT}/usr -type l -delete 2>/dev/null
  find ${TEMPROOT} -type f -size 0 -delete 2>/dev/null
  find -d ${TEMPROOT} -type d -empty -mindepth 1 -delete 2>/dev/null

  # Build the mtree database in a temporary location.
  case "${PRE_WORLD}" in
  '') MTREENEW=`mktemp -t mergemaster.mtree`
      mtree -nci -p ${TEMPROOT} -k size,md5digest > ${MTREENEW} 2>/dev/null
      ;;
  *) # We don't want to mess with the mtree database on a pre-world run or
     # when re-scanning a previously-built tree.
     ;;
  esac
  ;; # End of the "RERUN" test
esac

# Get ready to start comparing files

# Check umask if not specified on the command line,
# and we are not doing an autorun
#
if [ -z "${NEW_UMASK}" -a -z "${AUTO_RUN}" ]; then
  USER_UMASK=`umask`
  case "${USER_UMASK}" in
  0022|022) ;;
  *)
    echo ''
    echo " *** Your umask is currently set to ${USER_UMASK}.  By default, this script"
    echo "     installs all files with the same user, group and modes that"
    echo "     they are created with by ${SOURCEDIR}/etc/Makefile, compared to"
    echo "     a umask of 022.  This umask allows world read permission when"
    echo "     the file's default permissions have it."
    echo ''
    echo "     No world permissions can sometimes cause problems.  A umask of"
    echo "     022 will restore the default behavior, but is not mandatory."
    echo "     /etc/master.passwd is a special case.  Its file permissions"
    echo "     will be 600 (rw-------) if installed."
    echo ''
    echo -n "What umask should I use? [${USER_UMASK}] "
    read NEW_UMASK

    NEW_UMASK="${NEW_UMASK:-$USER_UMASK}"
    ;;
  esac
  echo ''
fi

CONFIRMED_UMASK=${NEW_UMASK:-0022}

#
# Warn users who still have old rc files
#
for file in atm devfs diskless1 diskless2 network network6 pccard \
  serial syscons sysctl alpha amd64 i386 sparc64; do
  if [ -f "${DESTDIR}/etc/rc.${file}" ]; then
    OLD_RC_PRESENT=1
    break
  fi
done

case "${OLD_RC_PRESENT}" in
1)
  echo ''
  echo " *** There are elements of the old rc system in ${DESTDIR}/etc/."
  echo ''
  echo '     While these scripts will not hurt anything, they are not'
  echo '     functional on an up to date system, and can be removed.'
  echo ''

  case "${AUTO_RUN}" in
  '')
    echo -n 'Move these files to /var/tmp/mergemaster/old_rc? [yes] '
    read MOVE_OLD_RC

    case "${MOVE_OLD_RC}" in
    [nN]*) ;;
    *)
      mkdir -p /var/tmp/mergemaster/old_rc
        for file in atm devfs diskless1 diskless2 network network6 pccard \
          serial syscons sysctl alpha amd64 i386 sparc64; do
          if [ -f "${DESTDIR}/etc/rc.${file}" ]; then
            mv ${DESTDIR}/etc/rc.${file} /var/tmp/mergemaster/old_rc/
          fi
        done
      echo '  The files have been moved'
      press_to_continue
      ;;
    esac
    ;;
  *) ;;
  esac
esac

# Use the umask/mode information to install the files
# Create directories as needed
#
install_error () {
  echo "*** FATAL ERROR: Unable to install ${1} to ${2}"
  echo ''
  exit 1
}

do_install_and_rm () {
  case "${PRESERVE_FILES}" in
  [Yy][Ee][Ss])
    if [ -f "${3}/${2##*/}" ]; then
      mkdir -p ${PRESERVE_FILES_DIR}/${2%/*}
      cp ${3}/${2##*/} ${PRESERVE_FILES_DIR}/${2%/*}
    fi
    ;;
  esac

  if [ ! -d "${3}/${2##*/}" ]; then
    if install -m ${1} ${2} ${3}; then
      unlink ${2}
    else
      install_error ${2} ${3}
    fi
  else
    install_error ${2} ${3}
  fi
}

# 4095 = "obase=10;ibase=8;07777" | bc
find_mode () {
  local OCTAL
  OCTAL=$(( ~$(echo "obase=10; ibase=8; ${CONFIRMED_UMASK}" | bc) & 4095 &
    $(echo "obase=10; ibase=8; $(stat -f "%OMp%OLp" ${1})" | bc) ))
  printf "%04o\n" ${OCTAL}
}

mm_install () {
  local INSTALL_DIR
  INSTALL_DIR=${1#.}
  INSTALL_DIR=${INSTALL_DIR%/*}

  case "${INSTALL_DIR}" in
  '')
    INSTALL_DIR=/
    ;;
  esac

  if [ -n "${DESTDIR}${INSTALL_DIR}" -a ! -d "${DESTDIR}${INSTALL_DIR}" ]; then
    DIR_MODE=`find_mode "${TEMPROOT}/${INSTALL_DIR}"`
    install -d -o root -g wheel -m "${DIR_MODE}" "${DESTDIR}${INSTALL_DIR}" ||
      install_error $1 ${DESTDIR}${INSTALL_DIR}
  fi

  FILE_MODE=`find_mode "${1}"`

  if [ ! -x "${1}" ]; then
    case "${1#.}" in
    /etc/mail/aliases)
      NEED_NEWALIASES=yes
      ;;
    /etc/login.conf)
      NEED_CAP_MKDB=yes
      ;;
    /etc/services)
      NEED_SERVICES_MKDB=yes
      ;;
    /etc/master.passwd)
      do_install_and_rm 600 "${1}" "${DESTDIR}${INSTALL_DIR}"
      NEED_PWD_MKDB=yes
      DONT_INSTALL=yes
      ;;
    /.cshrc | /.profile)
      local st_nlink

      # install will unlink the file before it installs the new one,
      # so we have to restore/create the link afterwards.
      #
      st_nlink=0		# In case the file does not yet exist
      eval $(stat -s ${DESTDIR}${COMPFILE#.} 2>/dev/null)

      do_install_and_rm "${FILE_MODE}" "${1}" "${DESTDIR}${INSTALL_DIR}"

      if [ -n "${AUTO_INSTALL}" -a $st_nlink -gt 1 ]; then
        HANDLE_LINK=l
      else
        case "${LINK_EXPLAINED}" in
        '')
          echo "   *** Historically BSD derived systems have had a"
          echo "       hard link from /.cshrc and /.profile to"
          echo "       their namesakes in /root.  Please indicate"
          echo "       your preference below for bringing your"
          echo "       installed files up to date."
          echo ''
          LINK_EXPLAINED=yes
          ;;
        esac

        echo "   Use 'd' to delete the temporary ${COMPFILE}"
        echo "   Use 'l' to delete the existing ${DESTDIR}/root/${COMPFILE##*/} and create the link"
        echo ''
        echo "   Default is to leave the temporary file to deal with by hand"
        echo ''
        echo -n "  How should I handle ${COMPFILE}? [Leave it to install later] "
        read HANDLE_LINK
      fi

      case "${HANDLE_LINK}" in
      [dD]*)
        rm "${COMPFILE}"
        echo ''
        echo "   *** Deleting ${COMPFILE}"
        ;;
      [lL]*)
        echo ''
        unlink ${DESTDIR}/root/${COMPFILE##*/}
        if ln ${DESTDIR}${COMPFILE#.} ${DESTDIR}/root/${COMPFILE##*/}; then
          echo "   *** Link from ${DESTDIR}${COMPFILE#.} to ${DESTDIR}/root/${COMPFILE##*/} installed successfully"
        else
          echo "   *** Error linking ${DESTDIR}${COMPFILE#.} to ${DESTDIR}/root/${COMPFILE##*/}"
          echo "   *** ${COMPFILE} will remain for your consideration"
        fi
        ;;
      *)
        echo "   *** ${COMPFILE} will remain for your consideration"
        ;;
      esac
      return
      ;;
    esac

    case "${DONT_INSTALL}" in
    '')
      do_install_and_rm "${FILE_MODE}" "${1}" "${DESTDIR}${INSTALL_DIR}"
      ;;
    *)
      unset DONT_INSTALL
      ;;
    esac
  else	# File matched -x
    do_install_and_rm "${FILE_MODE}" "${1}" "${DESTDIR}${INSTALL_DIR}"
  fi
  return $?
}

if [ ! -d "${TEMPROOT}" ]; then
	echo "*** FATAL ERROR: The temproot directory (${TEMPROOT})"
	echo '                 has disappeared!'
	echo ''
	exit 1
fi

echo ''
echo "*** Beginning comparison"
echo ''

# Pre-world does not populate /etc/rc.d.
# It is very possible that a previous run would have deleted files in
# ${TEMPROOT}/etc/rc.d, thus creating a lot of false positives.
if [ -z "${PRE_WORLD}" -a -z "${RERUN}" ]; then
  echo "   *** Checking ${DESTDIR}/etc/rc.d for stale files"
  echo ''
  cd "${DESTDIR}/etc/rc.d" &&
  for file in *; do
    if [ ! -e "${TEMPROOT}/etc/rc.d/${file}" ]; then
      STALE_RC_FILES="${STALE_RC_FILES} ${file}"
    fi
  done
  case "${STALE_RC_FILES}" in
  ''|' *')
    echo '   *** No stale files found'
    ;;
  *)
    echo "   *** The following files exist in ${DESTDIR}/etc/rc.d but not in"
    echo "       ${TEMPROOT}/etc/rc.d/:"
    echo ''
    echo "${STALE_RC_FILES}"
    echo ''
    echo '       The presence of stale files in this directory can cause the'
    echo '       dreaded unpredictable results, and therefore it is highly'
    echo '       recommended that you delete them.'
    case "${AUTO_RUN}" in
    '')
      echo ''
      echo -n '   *** Delete them now? [n] '
      read DELETE_STALE_RC_FILES
      case "${DELETE_STALE_RC_FILES}" in
      [yY])
        echo '      *** Deleting ... '
        rm ${STALE_RC_FILES}
        echo '                       done.'
        ;;
      *)
        echo '      *** Files will not be deleted'
        ;;
      esac
      sleep 2
      ;;
    *)
      if [ -n "${DELETE_STALE_RC_FILES}" ]; then
        echo '      *** Deleting ... '
        rm ${STALE_RC_FILES}
        echo '                       done.'
      fi
    esac
    ;;
  esac
  echo ''
fi

cd "${TEMPROOT}"

if [ -r "${MM_PRE_COMPARE_SCRIPT}" ]; then
  . "${MM_PRE_COMPARE_SCRIPT}"
fi

# Things that were files/directories/links in one version can sometimes
# change to something else in a newer version.  So we need to explicitly
# test for this, and warn the user if what we find does not match.
#
for COMPFILE in `find . | sort` ; do
  if [ -e "${DESTDIR}${COMPFILE#.}" ]; then
    INSTALLED_TYPE=`stat -f '%HT' ${DESTDIR}${COMPFILE#.}`
  else
    continue
  fi
  TEMPROOT_TYPE=`stat -f '%HT' $COMPFILE`

  if [ ! "$TEMPROOT_TYPE" = "$INSTALLED_TYPE" ]; then
    [ "$COMPFILE" = '.' ] && continue
    TEMPROOT_TYPE=`echo $TEMPROOT_TYPE | tr [:upper:] [:lower:]`
    INSTALLED_TYPE=`echo $INSTALLED_TYPE | tr [:upper:] [:lower:]`

    echo "*** The installed file ${DESTDIR}${COMPFILE#.} has the type \"$INSTALLED_TYPE\""
    echo "    but the new version has the type \"$TEMPROOT_TYPE\""
    echo ''
    echo "    How would you like to handle this?"
    echo ''
    echo "    Use 'r' to remove ${DESTDIR}${COMPFILE#.}"
    case "$TEMPROOT_TYPE" in
    'symbolic link')
	TARGET=`readlink $COMPFILE`
	echo "    and create a link to $TARGET in its place" ;;
    *)	echo "    You will be able to install it as a \"$TEMPROOT_TYPE\"" ;;
    esac
    echo ''
    echo "    Use 'i' to ignore this"
    echo ''
    echo -n "    How to proceed? [i] "
    read ANSWER
    case "$ANSWER" in
    [rR])	case "${PRESERVE_FILES}" in
		[Yy][Ee][Ss])
		mv ${DESTDIR}${COMPFILE#.} ${PRESERVE_FILES_DIR}/ || exit 1 ;;
		*) rm -rf ${DESTDIR}${COMPFILE#.} ;;
		esac
		case "$TEMPROOT_TYPE" in
		'symbolic link') ln -sf $TARGET ${DESTDIR}${COMPFILE#.} ;;
		esac ;;
    *)	echo ''
        echo "*** See the man page about adding ${COMPFILE#.} to the list of IGNORE_FILES"
        press_to_continue ;;
    esac
    echo ''
  fi
done

for COMPFILE in `find . -type f | sort`; do

  # First, check to see if the file exists in DESTDIR.  If not, the
  # diff_loop function knows how to handle it.
  #
  if [ ! -e "${DESTDIR}${COMPFILE#.}" ]; then
    case "${AUTO_RUN}" in
      '')
        diff_loop
        ;;
      *)
        case "${AUTO_INSTALL}" in
        '')
          # If this is an auto run, make it official
          echo "   *** ${COMPFILE} will remain for your consideration"
          ;;
        *)
          diff_loop
          ;;
        esac
        ;;
    esac # Auto run test
    continue
  fi

  case "${STRICT}" in
  '' | [Nn][Oo])
    # Compare $Id's first so if the file hasn't been modified
    # local changes will be ignored.
    # If the files have the same $Id, delete the one in temproot so the
    # user will have less to wade through if files are left to merge by hand.
    #
    ID1=`grep "[$]${ID_TAG}:" ${DESTDIR}${COMPFILE#.} 2>/dev/null`
    ID2=`grep "[$]${ID_TAG}:" ${COMPFILE} 2>/dev/null` || ID2=none

    case "${ID2}" in
    "${ID1}")
      echo " *** Temp ${COMPFILE} and installed have the same Id, deleting"
      rm "${COMPFILE}"
      ;;
    esac
    ;;
  esac

  # If the file is still here either because the $Ids are different, the
  # file doesn't have an $Id, or we're using STRICT mode; look at the diff.
  #
  if [ -f "${COMPFILE}" ]; then

    # Do an absolute diff first to see if the files are actually different.
    # If they're not different, delete the one in temproot.
    #
    if diff -q ${DIFF_OPTIONS} "${DESTDIR}${COMPFILE#.}" "${COMPFILE}" > \
      /dev/null 2>&1; then
      echo " *** Temp ${COMPFILE} and installed are the same, deleting"
      rm "${COMPFILE}"
    else
      # Ok, the files are different, so show the user where they differ.
      # Use user's choice of diff methods; and user's pager if they have one.
      # Use less if not.
      # Use unified diffs by default.  Context diffs give me a headache. :)
      #
      # If the user chose the -F option, test for that before proceeding
      #
      if [ -n "$FREEBSD_ID" ]; then
        if diff -q -I'[$]FreeBSD.*[$]' "${DESTDIR}${COMPFILE#.}" "${COMPFILE}" > \
            /dev/null 2>&1; then
          if mm_install "${COMPFILE}"; then
            echo "*** Updated revision control Id for ${DESTDIR}${COMPFILE#.}"
          else
            echo "*** Problem installing ${COMPFILE}, it will remain to merge by hand later"
          fi
          continue
        fi
      fi
      case "${AUTO_RUN}" in
      '')
        # prompt user to install/delete/merge changes
        diff_loop
        ;;
      *)
        # If this is an auto run, make it official
        echo "   *** ${COMPFILE} will remain for your consideration"
        ;;
      esac # Auto run test
    fi # Yes, the files are different
  fi # Yes, the file still remains to be checked
done # This is for the for way up there at the beginning of the comparison

echo ''
echo "*** Comparison complete"

if [ -s "${MTREENEW}" ]; then
  echo "*** Saving mtree database for future upgrades"
  test -e "${MTREEFILE}" && unlink ${MTREEFILE}
  mv ${MTREENEW} ${MTREEFILE}
fi

echo ''

TEST_FOR_FILES=`find ${TEMPROOT} -type f -size +0 2>/dev/null`
if [ -n "${TEST_FOR_FILES}" ]; then
  echo "*** Files that remain for you to merge by hand:"
  find "${TEMPROOT}" -type f -size +0 | sort
  echo ''

  case "${AUTO_RUN}" in
  '')
    echo -n "Do you wish to delete what is left of ${TEMPROOT}? [no] "
    read DEL_TEMPROOT
    case "${DEL_TEMPROOT}" in
    [yY]*)
      delete_temproot
      ;;
    *)
      echo " *** ${TEMPROOT} will remain"
      ;;
    esac
    ;;
  *) ;;
  esac
else
  echo "*** ${TEMPROOT} is empty, deleting"
  delete_temproot
fi

case "${AUTO_INSTALLED_FILES}" in
'') ;;
*)
  case "${AUTO_RUN}" in
  '')
    (
      echo ''
      echo '*** You chose the automatic install option for files that did not'
      echo '    exist on your system.  The following were installed for you:'
      echo "${AUTO_INSTALLED_FILES}"
    ) | ${PAGER}
    ;;
  *)
    echo ''
    echo '*** You chose the automatic install option for files that did not'
    echo '    exist on your system.  The following were installed for you:'
    echo "${AUTO_INSTALLED_FILES}"
    ;;
  esac
  ;;
esac

case "${AUTO_UPGRADED_FILES}" in
'') ;;
*)
  case "${AUTO_RUN}" in
  '')
    (
      echo ''
      echo '*** You chose the automatic upgrade option for files that you did'
      echo '    not alter on your system.  The following were upgraded for you:'
      echo "${AUTO_UPGRADED_FILES}"
    ) | ${PAGER}
    ;;
  *)
    echo ''
    echo '*** You chose the automatic upgrade option for files that you did'
    echo '    not alter on your system.  The following were upgraded for you:'
    echo "${AUTO_UPGRADED_FILES}"
    ;;
  esac
  ;;
esac

run_it_now () {
  [ -n "$AUTO_RUN" ] && return

  local answer

  echo ''
  while : ; do
    if [ "$RUN_UPDATES" = always ]; then
      answer=y
    elif [ "$RUN_UPDATES" = never ]; then
      answer=n
    else
      echo -n '    Would you like to run it now? y or n [n] '
      read answer
    fi

    case "$answer" in
    y)
      echo "    Running ${1}"
      echo ''
      eval "${1}"
      return
      ;;
    ''|n)
      if [ ! "$RUN_UPDATES" = never ]; then
        echo ''
        echo "       *** Cancelled"
        echo ''
      fi
      echo "    Make sure to run ${1} yourself"
      return
      ;;
    *)
      echo ''
      echo "       *** Sorry, I do not understand your answer (${answer})"
      echo ''
    esac
  done
}

case "${NEED_NEWALIASES}" in
'') ;;
*)
  echo ''
  if [ -n "${DESTDIR}" ]; then
    echo "*** You installed a new aliases file into ${DESTDIR}/etc/mail, but"
    echo "    the newaliases command is limited to the directories configured"
    echo "    in sendmail.cf.  Make sure to create your aliases database by"
    echo "    hand when your sendmail configuration is done."
  else
    echo "*** You installed a new aliases file, so make sure that you run"
    echo "    '/usr/bin/newaliases' to rebuild your aliases database"
    run_it_now '/usr/bin/newaliases'
  fi
  ;;
esac

case "${NEED_CAP_MKDB}" in
'') ;;
*)
  echo ''
  echo "*** You installed a login.conf file, so make sure that you run"
  echo "    '/usr/bin/cap_mkdb ${DESTDIR}/etc/login.conf'"
  echo "     to rebuild your login.conf database"
  run_it_now "/usr/bin/cap_mkdb ${DESTDIR}/etc/login.conf"
  ;;
esac

case "${NEED_SERVICES_MKDB}" in
'') ;;
*)
  echo ''
  echo "*** You installed a services file, so make sure that you run"
  echo "    '/usr/sbin/services_mkdb -q -o ${DESTDIR}/var/db/services.db ${DESTDIR}/etc/services'"
  echo "     to rebuild your services database"
  run_it_now "/usr/sbin/services_mkdb -q -o ${DESTDIR}/var/db/services.db ${DESTDIR}/etc/services"
  ;;
esac

case "${NEED_PWD_MKDB}" in
'') ;;
*)
  echo ''
  echo "*** You installed a new master.passwd file, so make sure that you run"
  if [ -n "${DESTDIR}" ]; then
    echo "    '/usr/sbin/pwd_mkdb -d ${DESTDIR}/etc -p ${DESTDIR}/etc/master.passwd'"
    echo "    to rebuild your password files"
    run_it_now "/usr/sbin/pwd_mkdb -d ${DESTDIR}/etc -p ${DESTDIR}/etc/master.passwd"
  else
    echo "    '/usr/sbin/pwd_mkdb -p /etc/master.passwd'"
    echo "     to rebuild your password files"
    run_it_now '/usr/sbin/pwd_mkdb -p /etc/master.passwd'
  fi
  ;;
esac

if [ -e "${DESTDIR}/etc/localtime" -a ! -L "${DESTDIR}/etc/localtime" -a -z "${PRE_WORLD}" ]; then	# Ignore if TZ == UTC
  echo ''
  [ -n "${DESTDIR}" ] && tzs_args="-C ${DESTDIR}"
  if [ -f "${DESTDIR}/var/db/zoneinfo" ]; then
    echo "*** Reinstalling `cat ${DESTDIR}/var/db/zoneinfo` as ${DESTDIR}/etc/localtime"
    tzsetup $tzs_args -r
  else
    echo "*** There is no ${DESTDIR}/var/db/zoneinfo file to update ${DESTDIR}/etc/localtime."
    echo '    You should run tzsetup'
    run_it_now "tzsetup $tzs_args"
  fi
fi

echo ''

if [ -r "${MM_EXIT_SCRIPT}" ]; then
  . "${MM_EXIT_SCRIPT}"
fi

case "${COMP_CONFS}" in
'') ;;
*)
  . ${DESTDIR}/etc/defaults/rc.conf

  (echo ''
  echo "*** Comparing conf files: ${rc_conf_files}"

  for CONF_FILE in ${rc_conf_files}; do
    if [ -r "${DESTDIR}${CONF_FILE}" ]; then
      echo ''
      echo "*** From ${DESTDIR}${CONF_FILE}"
      echo "*** From ${DESTDIR}/etc/defaults/rc.conf"

      for RC_CONF_VAR in `grep -i ^[a-z] ${DESTDIR}${CONF_FILE} |
        cut -d '=' -f 1`; do
        echo ''
        grep -w ^${RC_CONF_VAR} ${DESTDIR}${CONF_FILE}
        grep -w ^${RC_CONF_VAR} ${DESTDIR}/etc/defaults/rc.conf ||
          echo ' * No default variable with this name'
      done
    fi
  done) | ${PAGER}
  echo ''
  ;;
esac

if [ -n "${PRESERVE_FILES}" ]; then
  find -d $PRESERVE_FILES_DIR -type d -empty -delete 2>/dev/null
  rmdir $PRESERVE_FILES_DIR 2>/dev/null
fi

exit 0

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#  Copyright (c) 1998-2012 Douglas Barton
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
#  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
#  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
#  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
#  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
#  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
#  SUCH DAMAGE.
