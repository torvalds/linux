#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2010 iXsystems, Inc.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

# functions.sh
# Library of functions which pc-sysinstall may call upon

# Function which displays the help-index file
display_help()
{
  if [ -e "${PROGDIR}/doc/help-index" ]
  then
    cat ${PROGDIR}/doc/help-index
  else
    echo "Error: ${PROGDIR}/doc/help-index not found"
    exit 1
  fi
};

# Function which displays the help for a specified command
display_command_help()
{
  if [ -z "$1" ]
  then
    echo "Error: No command specified to display help for"
    exit 1
  fi
  
  if [ -e "${PROGDIR}/doc/help-${1}" ]
  then
    cat ${PROGDIR}/doc/help-${1}
  else
    echo "Error: ${PROGDIR}/doc/help-${1} not found"
    exit 1
  fi
};

# Function to convert bytes to megabytes
convert_byte_to_megabyte()
{
  if [ -z "${1}" ]
  then
    echo "Error: No bytes specified!"
    exit 1
  fi

  expr -e ${1} / 1048576
};

# Function to convert blocks to megabytes
convert_blocks_to_megabyte()
{
  if [ -z "${1}" ] ; then
    echo "Error: No blocks specified!"
    exit 1
  fi

  expr -e ${1} / 2048
};

# Takes $1 and strips the whitespace out of it, returns VAL
strip_white_space()
{
  if [ -z "${1}" ]
  then
    echo "Error: No value setup to strip whitespace from!"

    exit 1
  fi

  export VAL=`echo "$1" | tr -d ' '`
};

# Displays an error message and exits with error 1
exit_err()
{
  # Echo the message for the users benefit
  echo "EXITERROR: $1"

  # Save this error to the log file
  echo "EXITERROR: ${1}" >>$LOGOUT

  # Check if we need to unmount any file-systems after this failure
  unmount_all_filesystems_failure

  echo "For more details see log file: $LOGOUT"

  exit 1
};

# Run-command, don't halt if command exits with non-0
rc_nohalt()
{
  CMD="$1"

  if [ -z "${CMD}" ]
  then
    exit_err "Error: missing argument in rc_nohalt()"
  fi

  echo "Running: ${CMD}" >>${LOGOUT}
  ${CMD} >>${LOGOUT} 2>>${LOGOUT}

};

# Run-command, halt if command exits with non-0
rc_halt()
{
  CMD="$1"

  if [ -z "${CMD}" ]
  then
    exit_err "Error: missing argument in rc_halt()"
  fi

  echo "Running: ${CMD}" >>${LOGOUT}
  eval ${CMD} >>${LOGOUT} 2>>${LOGOUT}
  STATUS="$?"
  if [ "${STATUS}" != "0" ]
  then
    exit_err "Error ${STATUS}: ${CMD}"
  fi
};

# Run-command w/echo to screen, halt if command exits with non-0
rc_halt_echo()
{
  CMD="$1"

  if [ -z "${CMD}" ]
  then
    exit_err "Error: missing argument in rc_halt_echo()"
  fi

  echo "Running: ${CMD}" >>${LOGOUT}
  ${CMD} 2>&1 | tee -a ${LOGOUT} 
  STATUS="$?"
  if [ "$STATUS" != "0" ]
  then
    exit_err "Error ${STATUS}: $CMD"
  fi

};

# Run-command w/echo, don't halt if command exits with non-0
rc_nohalt_echo()
{
  CMD="$1"

  if [ -z "${CMD}" ]
  then
    exit_err "Error: missing argument in rc_nohalt_echo()"
  fi

  echo "Running: ${CMD}" >>${LOGOUT}
  ${CMD} 2>&1 | tee -a ${LOGOUT} 

};

# Echo to the screen and to the log
echo_log()
{
  STR="$1"

  if [ -z "${STR}" ]
  then
    exit_err "Error: missing argument in echo_log()"
  fi

  echo "${STR}" | tee -a ${LOGOUT} 
};

# Make sure we have a numeric
is_num()
{
  expr $1 + 1 2>/dev/null
  return $?
}

# Function which uses "fetch" to download a file, and display a progress report
fetch_file()
{

  FETCHFILE="$1"
  FETCHOUTFILE="$2"
  EXITFAILED="$3"

  EXITFILE="${TMPDIR}/.fetchExit"

  rm ${FETCHOUTFILE} 2>/dev/null >/dev/null

  SIZE=$(( `fetch -s "${FETCHFILE}"` / 1024 ))
  echo "FETCH: ${FETCHFILE}"
  echo "FETCH: ${FETCHOUTFILE}" >>${LOGOUT}

  ( fetch -o ${FETCHOUTFILE} "${FETCHFILE}" >/dev/null 2>/dev/null ; echo "$?" > ${EXITFILE} ) &
  PID="$!"
  while
  z=1
  do

    if [ -e "${FETCHOUTFILE}" ]
    then
      DSIZE=`du -k ${FETCHOUTFILE} | tr -d '\t' | cut -d '/' -f 1`
      if [ $(is_num "$DSIZE") ] ; then
      if [ $SIZE -lt $DSIZE ] ; then DSIZE="$SIZE"; fi 
    	echo "SIZE: ${SIZE} DOWNLOADED: ${DSIZE}"
    	echo "SIZE: ${SIZE} DOWNLOADED: ${DSIZE}" >>${LOGOUT}
      fi
    fi

    # Check if the download is finished
    ps -p ${PID} >/dev/null 2>/dev/null
    if [ $? -ne 0 ]
    then
      break;
    fi

    sleep 2
  done

  echo "FETCHDONE"

  EXIT="`cat ${EXITFILE}`"
  if [ "${EXIT}" != "0" -a "$EXITFAILED" = "1" ]
  then
    exit_err "Error: Failed to download ${FETCHFILE}"
  fi

  return $EXIT

};

# Function to return a the zpool name for this device
get_zpool_name()
{
  DEVICE="$1"

  # Set the base name we use for zpools
  BASENAME="tank"

  if [ ! -d "${TMPDIR}/.zpools" ] ; then
    mkdir -p ${TMPDIR}/.zpools
  fi

  if [ -e "${TMPDIR}/.zpools/${DEVICE}" ] ; then
    cat ${TMPDIR}/.zpools/${DEVICE}
    return 0
  else
    # Need to generate a zpool name for this device
    NUM=`ls ${TMPDIR}/.zpools/ | wc -l | sed 's| ||g'`

    # Is it used in another zpool?
    while :
    do
      NEWNAME="${BASENAME}${NUM}"
      zpool list | grep -qw "${NEWNAME}"
      local chk1=$?
      zpool import | grep -qw "${NEWNAME}"
      local chk2=$?
      if [ $chk1 -eq 1 -a $chk2 -eq 1 ] ; then break ; fi 
      NUM=$((NUM+1))
    done

    # Now save the new tank name
    mkdir -p ${TMPDIR}/.zpools/`dirname $DEVICE`
    echo "$NEWNAME" >${TMPDIR}/.zpools/${DEVICE} 
    echo "${NEWNAME}"
    return 0
  fi
};

iscompressed()
{
  local FILE
  local RES

  FILE="$1"
  RES=1

  if echo "${FILE}" | \
    grep -qiE '\.(Z|lzo|lzw|lzma|gz|bz2|xz|zip)$' 2>&1
  then
    RES=0
  fi

  return ${RES}
}

get_compression_type()
{
  local FILE
  local SUFFIX

  FILE="$1"
  SUFFIX=`echo "${FILE}" | sed -E 's|^(.+)\.(.+)$|\2|'`

  VAL=""
  SUFFIX=`echo "${SUFFIX}" | tr A-Z a-z`
  case "${SUFFIX}" in
    z) VAL="lzw" ;;
    lzo) VAL="lzo" ;;
    lzw) VAL="lzw" ;;
    lzma) VAL="lzma" ;;
    gz) VAL="gzip" ;;
    bz2) VAL="bzip2" ;;
    xz) VAL="xz" ;;
    zip) VAL="zip" ;;
  esac

  export VAL
}

write_image()
{
  local DEVICE_FILE

  IMAGE_FILE="$1"
  DEVICE_FILE="$2"

  if [ -z "${IMAGE_FILE}" ]
  then
    exit_err "ERROR: Image file not specified!"
  fi
 
  if [ -z "${DEVICE_FILE}" ]
  then
    exit_err "ERROR: Device file not specified!"
  fi
 
  if [ ! -f "${IMAGE_FILE}" ]
  then
    exit_err "ERROR: '${IMAGE_FILE}' does not exist!"
  fi

  DEVICE_FILE="${DEVICE_FILE#/dev/}"
  DEVICE_FILE="/dev/${DEVICE_FILE}"
 
  if [ ! -c "${DEVICE_FILE}" ]
  then
    exit_err "ERROR: '${DEVICE_FILE}' is not a character device!"
  fi

  if iscompressed "${IMAGE_FILE}"
  then
	local COMPRESSION

    get_compression_type "${IMAGE_FILE}"
	COMPRESSION="${VAL}"

    case "${COMPRESSION}" in
      lzw)
        rc_halt "uncompress ${IMAGE_FILE} -c | dd of=${DEVICE_FILE}"
        IMAGE_FILE="${IMAGE_FILE%.Z}"
        ;;

      lzo)
        rc_halt "lzop -d $IMAGE_{FILE} -c | dd of=${DEVICE_FILE}"
        IMAGE_FILE="${IMAGE_FILE%.lzo}"
        ;;

      lzma)
        rc_halt "lzma -d ${IMAGE_FILE} -c | dd of=${DEVICE_FILE}"
        IMAGE_FILE="${IMAGE_FILE%.lzma}"
        ;;

      gzip)
        rc_halt "gunzip ${IMAGE_FILE} -c | dd of=${DEVICE_FILE}"
        IMAGE_FILE="${IMAGE_FILE%.gz}"
        ;;

      bzip2)
        rc_halt "bunzip2 ${IMAGE_FILE} -c | dd of=${DEVICE_FILE}"
        IMAGE_FILE="${IMAGE_FILE%.bz2}"
        ;;

      xz)
        rc_halt "xz -d ${IMAGE_FILE} -c | dd of=${DEVICE_FILE}"
        IMAGE_FILE="${IMAGE_FILE%.xz}"
        ;;

      zip)
        rc_halt "unzip ${IMAGE_FILE} -c | dd of=${DEVICE_FILE}"
        IMAGE_FILE="${IMAGE_FILE%.zip}"
        ;;

      *) 
        exit_err "ERROR: ${COMPRESSION} compression is not supported"
        ;;
    esac

  else
    rc_halt "dd if=${IMAGE_FILE} of=${DEVICE_FILE}"

  fi
};

# Setup and install on a new disk / partition
install_fresh()
{
  # Lets start setting up the disk slices now
  setup_disk_slice
  
  if [ -z "${ROOTIMAGE}" ]
  then

    # Disk setup complete, now lets parse WORKINGSLICES and setup the bsdlabels
    setup_disk_label
  
    # Now we've setup the bsdlabels, lets go ahead and run newfs / zfs 
    # to setup the filesystems
    setup_filesystems

    # Lets mount the partitions now
    mount_all_filesystems

    # We are ready to begin extraction, lets start now
    init_extraction 

    # Check if we have any optional modules to load 
    install_components

    # Check if we have any packages to install
    install_packages

    # Do any localization in configuration
    run_localize
  
    # Save any networking config on the installed system
    save_networking_install

    # Now add any users
    setup_users

    # Do any last cleanup / setup before unmounting
    run_final_cleanup

    # Now run any commands specified
    run_commands

    # Unmount and finish up
    unmount_all_filesystems
  fi

  echo_log "Installation finished!"
};

# Extract the system to a pre-mounted directory
install_extractonly()
{
  # We are ready to begin extraction, lets start now
  init_extraction 

  # Check if we have any optional modules to load 
  install_components

  # Check if we have any packages to install
  install_packages

  # Do any localization in configuration
  run_localize

  # Save any networking config on the installed system
  save_networking_install

  # Now add any users
  setup_users

  # Now run any commands specified
  run_commands
  
  # Set a hostname on the install system
  setup_hostname
      
  # Set the root_pw if it is specified
  set_root_pw

  echo_log "Installation finished!"
};

install_image()
{
  # We are ready to begin extraction, lets start now
  init_extraction 

  echo_log "Installation finished!"
};

install_upgrade()
{
  # We're going to do an upgrade, skip all the disk setup 
  # and start by mounting the target drive/slices
  mount_upgrade
  
  # Start the extraction process
  init_extraction

  # Do any localization in configuration
  run_localize

  # Now run any commands specified
  run_commands
  
  # Merge any old configuration files
  merge_old_configs

  # Check if we have any optional modules to load 
  install_components

  # Check if we have any packages to install
  install_packages

  # All finished, unmount the file-systems
  unmount_upgrade

  echo_log "Upgrade finished!"
};
