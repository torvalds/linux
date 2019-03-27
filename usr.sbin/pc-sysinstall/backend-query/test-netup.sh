#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2010 iXsystems, Inc.  All rights reserved.
# Copyright (c) 2011 The FreeBSD Foundation
# All rights reserved.
#
# Portions of this software were developed by Bjoern Zeeb
# under sponsorship from the FreeBSD Foundation.#
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


# Script which tries to ping "home" to see if Internet connectivity is
# available.
#############################################################################

rm ${TMPDIR}/.testftp >/dev/null 2>/dev/null

ping -c 2 www.pcbsd.org >/dev/null 2>/dev/null
if [ "$?" = "0" ]
then
  echo "ftp: Up"
  exit 0
fi

ping6 -c 2 www.pcbsd.org >/dev/null 2>/dev/null
if [ "$?" = "0" ]
then
  echo "ftp: Up"
  exit 0
fi

ping -c 2 www.freebsd.org >/dev/null 2>/dev/null
if [ "$?" = "0" ]
then
  echo "ftp: Up"
  exit 0
fi

ping6 -c 2 www.freebsd.org >/dev/null 2>/dev/null
if [ "$?" = "0" ]
then
  echo "ftp: Up"
  exit 0
fi

echo "ftp: Down"
exit 1
