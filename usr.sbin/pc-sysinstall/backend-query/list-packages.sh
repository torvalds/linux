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

# Script which lists the available packages for this release
###########################################################################

. ${PROGDIR}/backend/functions.sh
. ${PROGDIR}/backend/functions-packages.sh

PACKAGE_CATEGORY="${1}"
PACKAGE_NAME="${2}"
NARGS=0

if [ ! -f "${PKGDIR}/INDEX" ]
then
  echo "Error: please fetch package index with get-packages!"
  exit 1
fi

if [ ! -f "${PKGDIR}/INDEX.parsed" ]
then
  parse_package_index
fi

if [ -n "${PACKAGE_CATEGORY}" ]
then
  NARGS=$((NARGS+1))
fi

if [ -n "${PACKAGE_NAME}" ]
then
  NARGS=$((NARGS+1))
fi

if [ "${NARGS}" -eq "0" ]
then
  show_packages

elif [ "${NARGS}" -eq "1" ]
then
	
  if [ "${PACKAGE_CATEGORY}" = "@INDEX@" ]
  then
    if [ -f "${PKGDIR}/INDEX" ]
    then
      echo "${PKGDIR}/INDEX"
      exit 0
    else
      exit 1
    fi
		
  else
    show_packages_by_category "${PACKAGE_CATEGORY}"
  fi

elif [ "${NARGS}" -eq "2" ]
then
  show_package_by_name "${PACKAGE_CATEGORY}" "${PACKAGE_NAME}"

else
  show_packages
fi
