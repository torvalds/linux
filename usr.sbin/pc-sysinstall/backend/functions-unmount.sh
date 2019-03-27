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

# Functions which unmount all mounted disk filesystems

# Unmount all mounted partitions under specified dir
umount_all_dir()
{
  _udir="$1"
  _umntdirs=`mount | sort -r | grep "on $_udir" | cut -d ' ' -f 3`
  for _ud in $_umntdirs
  do
    umount -f ${_ud} 
  done
}

# Script that adds our gmirror devices for syncing
start_gmirror_sync()
{

  cd ${MIRRORCFGDIR}
  for DISK in `ls ${MIRRORCFGDIR}`
  do
    MIRRORDISK="`cat ${DISK} | cut -d ':' -f 1`"
    MIRRORBAL="`cat ${DISK} | cut -d ':' -f 2`"
    MIRRORNAME="`cat ${DISK} | cut -d ':' -f 3`"
   
    # Start the mirroring service
    rc_nohalt "gmirror forget ${MIRRORNAME}"
    rc_halt "gmirror insert ${MIRRORNAME} ${MIRRORDISK}"

  done

};

# Unmounts all our mounted file-systems
unmount_all_filesystems()
{
  # Copy the logfile to disk before we unmount
  cp ${LOGOUT} ${FSMNT}/root/pc-sysinstall.log
  cd /

  # Start by unmounting any ZFS partitions
  zfs_cleanup_unmount

  # Lets read our partition list, and unmount each
  ##################################################################
  for PART in `ls ${PARTDIR}`
  do
    PARTDEV=`echo $PART | sed 's|-|/|g'`    
    PARTFS="`cat ${PARTDIR}/${PART} | cut -d '#' -f 1`"
    PARTMNT="`cat ${PARTDIR}/${PART} | cut -d '#' -f 2`"
    PARTENC="`cat ${PARTDIR}/${PART} | cut -d '#' -f 3`"
    PARTLABEL="`cat ${PARTDIR}/${PART} | cut -d '#' -f 4`"

    if [ "${PARTENC}" = "ON" ]
    then
      EXT=".eli"
    else
      EXT=""
    fi

    if [ "${PARTFS}" = "SWAP" ]
    then
      rc_nohalt "swapoff ${PARTDEV}${EXT}"
    fi

    # Check if we've found "/", and unmount that last
    if [ "$PARTMNT" != "/" -a "${PARTMNT}" != "none" -a "${PARTFS}" != "ZFS" ]
    then
      rc_halt "umount -f ${PARTDEV}${EXT}"

      # Re-check if we are missing a label for this device and create it again if so
      if [ ! -e "/dev/label/${PARTLABEL}" ]
      then
        case ${PARTFS} in
          UFS) glabel label ${PARTLABEL} ${PARTDEV}${EXT} ;;
          UFS+S) glabel label ${PARTLABEL} ${PARTDEV}${EXT} ;;
          UFS+SUJ) glabel label ${PARTLABEL} ${PARTDEV}${EXT} ;;
          UFS+J) glabel label ${PARTLABEL} ${PARTDEV}${EXT}.journal ;;
          *) ;;
        esac 
      fi
    fi

    # Check if we've found "/" and make sure the label exists
    if [ "$PARTMNT" = "/" -a "${PARTFS}" != "ZFS" ]
    then
      if [ ! -e "/dev/label/${PARTLABEL}" ]
      then
        case ${PARTFS} in
          UFS) ROOTRELABEL="glabel label ${PARTLABEL} ${PARTDEV}${EXT}" ;;
          UFS+S) ROOTRELABEL="glabel label ${PARTLABEL} ${PARTDEV}${EXT}" ;;
          UFS+SUJ) ROOTRELABEL="glabel label ${PARTLABEL} ${PARTDEV}${EXT}" ;;
          UFS+J) ROOTRELABEL="glabel label ${PARTLABEL} ${PARTDEV}${EXT}.journal" ;;
          *) ;;
        esac 
      fi
    fi
  done

  # Last lets the /mnt partition
  #########################################################
  rc_nohalt "umount -f ${FSMNT}"

   # If are using a ZFS on "/" set it to legacy
  if [ ! -z "${FOUNDZFSROOT}" ]
  then
    rc_halt "zfs set mountpoint=legacy ${FOUNDZFSROOT}"
  fi

  # If we need to relabel "/" do it now
  if [ ! -z "${ROOTRELABEL}" ]
  then
    ${ROOTRELABEL}
  fi

  # Unmount our CDMNT
  rc_nohalt "umount -f ${CDMNT}" >/dev/null 2>/dev/null

  # Check if we need to run any gmirror syncing
  ls ${MIRRORCFGDIR}/* >/dev/null 2>/dev/null
  if [ $? -eq 0 ]
  then
    # Lets start syncing now
    start_gmirror_sync
  fi

};

# Unmounts any filesystems after a failure
unmount_all_filesystems_failure()
{
  cd /

  # if we did a fresh install, start unmounting
  if [ "${INSTALLMODE}" = "fresh" ]
  then

    # Lets read our partition list, and unmount each
    ##################################################################
    if [ -d "${PARTDIR}" ]
    then
    for PART in `ls ${PARTDIR}`
    do
      PARTDEV=`echo $PART | sed 's|-|/|g'` 
      PARTFS="`cat ${PARTDIR}/${PART} | cut -d '#' -f 1`"
      PARTMNT="`cat ${PARTDIR}/${PART} | cut -d '#' -f 2`"
      PARTENC="`cat ${PARTDIR}/${PART} | cut -d '#' -f 3`"

      if [ "${PARTFS}" = "SWAP" ]
      then
        if [ "${PARTENC}" = "ON" ]
        then
          swapoff ${PARTDEV}.eli >/dev/null 2>/dev/null
        else
          swapoff ${PARTDEV} >/dev/null 2>/dev/null
        fi
      fi

      # Check if we've found "/" again, don't need to mount it twice
      if [ "$PARTMNT" != "/" -a "${PARTMNT}" != "none" -a "${PARTFS}" != "ZFS" ]
      then
        umount -f ${PARTDEV} >/dev/null 2>/dev/null
        umount -f ${FSMNT}${PARTMNT} >/dev/null 2>/dev/null
      fi
    done

    # Last lets the /mnt partition
    #########################################################
    umount -f ${FSMNT} >/dev/null 2>/dev/null

   fi
  else
    # We are doing a upgrade, try unmounting any of these filesystems
    chroot ${FSMNT} /sbin/umount -a >/dev/null 2>/dev/null
    umount -f ${FSMNT}/usr >/dev/null 2>/dev/null
    umount -f ${FSMNT}/dev >/dev/null 2>/dev/null
    umount -f ${FSMNT} >/dev/null 2>/dev/null 
    sh ${TMPDIR}/.upgrade-unmount >/dev/null 2>/dev/null
  fi
   
  # Unmount our CDMNT
  umount ${CDMNT} >/dev/null 2>/dev/null

};
