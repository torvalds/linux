#!/bin/sh
#####################################################################
#       Author: Kris Moore
#      License: BSD 
#  Description: pc-sysinstall provides a backend for performing 
#  system installations, as well as calls which a front-end can use
#  to retrive information about the system
#####################################################################
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright 2010 iXsystems
# All rights reserved
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted providing that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#####################################################################

# User-editable configuration variables

# Set this to the program location
if [ -z "${PROGDIR}" ]
then
  PROGDIR="/usr/share/pc-sysinstall"
  export PROGDIR
fi

# Set this to the components location
COMPDIR="${PROGDIR}/components"
export COMPDIR

CONFDIR="${PROGDIR}/conf"
export CONFDIR

# Set this to the packages location
PKGDIR="${CONFDIR}"
export PKGDIR

# End of user-editable configuration
#####################################################################

# Set our QUERYDIR
QUERYDIR="${PROGDIR}/backend-query" 
export QUERYDIR

# Set our BACKEND
BACKEND="${PROGDIR}/backend" 
export BACKEND

PARTMANAGERDIR="${PROGDIR}/backend-partmanager"
export PARTMANAGERDIR

# Start by sourcing our conf file
if [ -e "${PROGDIR}/conf/pc-sysinstall.conf" ]
then
  . ${PROGDIR}/conf/pc-sysinstall.conf
else
  echo "ERROR: Could not find ${PROGDIR}/conf/pc-sysinstall.conf"
  exit 1
fi

# Now source our functions.sh 
if [ -e "${PROGDIR}/backend/functions.sh" ]
then
  . ${PROGDIR}/backend/functions.sh
else
  echo "ERROR: Could not find ${PROGDIR}/backend/functions.sh"
  exit 1
fi


# Check if we are called without any flags and display help
if [ -z "${1}" ]
then
  # Display the help index
  display_help
  exit 0
fi

case $1 in
  # The -c flag has been given, time to parse the script
  -c)
    if [ -z "${2}" ]
    then
      display_help
    else
      ${BACKEND}/parseconfig.sh ${2}
      exit $?
    fi
  ;;

  # The user requsted help
  help)
    if [ -z "${2}" ]
    then
      display_help
    else
      display_command_help ${2}
    fi
  ;;

  # Install an image file to a device
  install-image) ${BACKEND}/installimage.sh "${2}" "${3}"
  ;;

  # Parse an auto-install directive, and begin the installation
  start-autoinstall) ${BACKEND}/startautoinstall.sh ${2}
  ;;

  # The user is wanting to create a new partition
  create-part) ${PARTMANAGERDIR}/create-part.sh "${2}" "${3}" "${4}" "${5}"
  ;;

  # The user is wanting to delete an existing partition
  delete-part) ${PARTMANAGERDIR}/delete-part.sh "${2}"
  ;;

  # The user is wanting to check if we are on a laptop or desktop
  detect-laptop) ${QUERYDIR}/detect-laptop.sh
  ;;

  # The user is wanting to see what nics are available on the system
  detect-nics) ${QUERYDIR}/detect-nics.sh
  ;;
  
  # The user is wanting to check if we are in emulation
  detect-emulation) ${QUERYDIR}/detect-emulation.sh
  ;;

  # The user is wanting to query a disk's information
  disk-info) ${QUERYDIR}/disk-info.sh ${2}
  ;;

  # The user is wanting to query which disks are available
  disk-list) ${QUERYDIR}/disk-list.sh $*
  ;;
  
  # The user is wanting to query a disk's partitions
  disk-part) ${QUERYDIR}/disk-part.sh ${2}
  ;;

  # Function allows the setting of networking by a calling front-end
  enable-net) ${QUERYDIR}/enable-net.sh "${2}" "${3}" "${4}" "${5}" "${6}" "${7}"
  ;;

  # Function which lists components available
  list-components) ${QUERYDIR}/list-components.sh
  ;;

  # Function which lists pc-sysinstall configuration
  list-config) ${QUERYDIR}/list-config.sh
  ;;

  # Function which lists available FTP mirrors
  list-mirrors) ${QUERYDIR}/list-mirrors.sh "${2}"
  ;;

  # Function which lists available packages
  list-packages) ${QUERYDIR}/list-packages.sh "${2}" "${3}"
  ;;

  # Function which lists available backups on a rsync/ssh server
  list-rsync-backups) ${QUERYDIR}/list-rsync-backups.sh "${2}" "${3}" "${4}"
  ;;

  # Function which lists timezones available
  list-tzones) ${QUERYDIR}/list-tzones.sh
  ;;

  # Requested a list of languages this install will support
  query-langs) ${QUERYDIR}/query-langs.sh
  ;;

  # Function which creates a error report, and mails it to the specified address
  send-logs) ${QUERYDIR}/send-logs.sh ${2}
  ;;

  # Function to get package index
  get-packages) ${QUERYDIR}/get-packages.sh "${2}"
  ;;

  # Function to set FTP mirror
  set-mirror) ${QUERYDIR}/set-mirror.sh "${2}"
  ;;

  # Function which allows setting up of SSH keys
  setup-ssh-keys) ${QUERYDIR}/setup-ssh-keys.sh "${2}" "${3}" "${4}"
  ;;
  
  # Function which lists the real memory of the system in MB
  sys-mem) ${QUERYDIR}/sys-mem.sh
  ;;

  # Run script which determines if we are booted from install media, or on disk
  test-live) ${QUERYDIR}/test-live.sh
  ;;
  
  # The user is wanting to test if the network is up and working
  test-netup) ${QUERYDIR}/test-netup.sh
  ;;

  # The user is wanting to get a list of partitions available to be updated / repaired
  update-part-list) ${QUERYDIR}/update-part-list.sh
  ;;

  # Requested a list of keyboard layouts that xorg supports
  xkeyboard-layouts) ${QUERYDIR}/xkeyboard-layouts.sh
  ;;
  
  # Requested a list of keyboard models that xorg supports
  xkeyboard-models) ${QUERYDIR}/xkeyboard-models.sh
  ;;
  
  # Requested a list of keyboard variants that xorg supports
  xkeyboard-variants) ${QUERYDIR}/xkeyboard-variants.sh
  ;;
           
  *) echo "Unknown Command: ${1}" 
     exit 1 ;;
esac

# Exit with success if we made it to the end
exit $?
