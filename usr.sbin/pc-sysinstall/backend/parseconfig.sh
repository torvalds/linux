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

# Main install configuration parsing script
#

# Source our functions scripts
. ${BACKEND}/functions.sh
. ${BACKEND}/functions-bsdlabel.sh
. ${BACKEND}/functions-cleanup.sh
. ${BACKEND}/functions-disk.sh
. ${BACKEND}/functions-extractimage.sh
. ${BACKEND}/functions-installcomponents.sh
. ${BACKEND}/functions-installpackages.sh
. ${BACKEND}/functions-localize.sh
. ${BACKEND}/functions-mountdisk.sh
. ${BACKEND}/functions-networking.sh
. ${BACKEND}/functions-newfs.sh
. ${BACKEND}/functions-packages.sh
. ${BACKEND}/functions-parse.sh
. ${BACKEND}/functions-runcommands.sh
. ${BACKEND}/functions-ftp.sh
. ${BACKEND}/functions-unmount.sh
. ${BACKEND}/functions-upgrade.sh
. ${BACKEND}/functions-users.sh

# Check that the config file exists
if [ ! -e "${1}" ]
then
  echo "ERROR: Install configuration $1 does not exist!"
  exit 1
fi

# Set our config file variable
CFGF="$1"

# Resolve any relative pathing
CFGF="`realpath ${CFGF}`"
export CFGF

# Start by doing a sanity check, which will catch any obvious mistakes in the config
file_sanity_check "installMode installType installMedium packageType"

# We passed the Sanity check, lets grab some of the universal config settings and store them
check_value installMode "fresh upgrade extract"
check_value installType "PCBSD FreeBSD"
check_value installMedium "dvd usb ftp rsync image local"
check_value packageType "uzip tar rsync split dist"
if_check_value_exists mirrorbal "load prefer round-robin split"

# We passed all sanity checks! Yay, lets start the install
echo "File Sanity Check -> OK"

# Lets load the various universal settings now
get_value_from_cfg installMode
export INSTALLMODE="${VAL}"

get_value_from_cfg installType
export INSTALLTYPE="${VAL}"

get_value_from_cfg installMedium
export INSTALLMEDIUM="${VAL}"

get_value_from_cfg packageType
export PACKAGETYPE="${VAL}"

# Check if we are doing any networking setup
start_networking

# If we are not doing an upgrade, lets go ahead and setup the disk
case "${INSTALLMODE}" in
  fresh)
    if [ "${INSTALLMEDIUM}" = "image" ]
    then
      install_image
    else
      install_fresh
    fi
    ;;

  extract)
    # Extracting only, make sure we have a valid target directory
    get_value_from_cfg installLocation
    export FSMNT="${VAL}"
    if [ -z "$FSMNT" ] ; then exit_err "Missing installLocation=" ; fi
    if [ ! -d "$FSMNT" ] ; then exit_err "No such directory: $FSMNT" ; fi

    install_extractonly
    ;;

  upgrade)
    install_upgrade
    ;;

  *)
    exit 1
    ;;
esac

exit 0
