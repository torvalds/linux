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

# Functions which perform mounting / unmounting and switching of 
# optical / usb media

. ${BACKEND}/functions.sh
. ${BACKEND}/functions-parse.sh

# Displays an optical failure message
opt_fail()
{
  # If we got here, we must not have a DVD/USB we can find :(
  get_value_from_cfg installInteractive
  if [ "${VAL}" = "yes" ]
  then
    # We are running interactive, and didn't find a DVD, prompt user again
    echo_log "DISK ERROR: Unable to find installation disk!"
    echo_log "Please insert the installation disk and press enter."
    read tmp
  else
   exit_err "ERROR: Unable to locate installation DVD/USB"
  fi
};

# Performs the extraction of data to disk
opt_mount()
{
  FOUND="0"

  # Ensure we have a directory where its supposed to be
  if [ ! -d "${CDMNT}" ]
  then
    mkdir -p ${CDMNT}
  fi


  # Start by checking if we already have a cd mounted at CDMNT
  mount | grep -q "${CDMNT} " 2>/dev/null
  if [ $? -eq 0 ]
  then
    if [ -e "${CDMNT}/${INSFILE}" ]
    then
      echo "MOUNTED" >${TMPDIR}/cdmnt
      echo_log "FOUND DVD: MOUNTED"
      FOUND="1"
      return
    fi

    # failed to find optical disk
    opt_fail
    return
  fi

  # Setup our loop to search for installation media
  while
  z=1
  do

    # Loop though and look for an installation disk
    for i in `ls -1 /dev/cd* 2>/dev/null`
    do
      # Find the CD Device
      /sbin/mount_cd9660 $i ${CDMNT}

      # Check the package type to see if we have our install data
      if [ -e "${CDMNT}/${INSFILE}" ]
      then
        echo "${i}" >${TMPDIR}/cdmnt
        echo_log "FOUND DVD: ${i}"
        FOUND="1"
        break
      fi
      /sbin/umount ${CDMNT} >/dev/null 2>/dev/null
    done

    # If no DVD found, try USB
    if [ "$FOUND" != "1" ]
    then
      # Loop though and look for an installation disk
      for i in `ls -1 /dev/da* 2>/dev/null`
      do
        # Check if we can mount this device UFS
        /sbin/mount -r $i ${CDMNT}

        # Check the package type to see if we have our install data
        if [ -e "${CDMNT}/${INSFILE}" ]
        then
          echo "${i}" >${TMPDIR}/cdmnt
          echo_log "FOUND USB: ${i}"
          FOUND="1"
          break
        fi
        /sbin/umount ${CDMNT} >/dev/null 2>/dev/null

        # Also check if it is a FAT mount
        /sbin/mount -r -t msdosfs $i ${CDMNT}

        # Check the package type to see if we have our install data
        if [ -e "${CDMNT}/${INSFILE}" ]
        then
          echo "${i}" >${TMPDIR}/cdmnt
          echo_log "FOUND USB: ${i}"
          FOUND="1"
          break
        fi
        /sbin/umount ${CDMNT} >/dev/null 2>/dev/null
      done
    fi # End of USB Check


    if [ "$FOUND" = "1" ]
    then
      break
    fi
   
    # Failed to find a disk, take action now
    opt_fail

  done

};

# Function to unmount optical media
opt_umount()
{
  /sbin/umount ${CDMNT} >/dev/null 2>/dev/null
};
