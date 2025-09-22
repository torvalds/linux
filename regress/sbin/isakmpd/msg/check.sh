#!/bin/sh
#	$OpenBSD: check.sh,v 1.1 2005/04/08 17:12:50 cloder Exp $
#	$EOM: check.sh,v 1.4 1998/07/17 21:33:13 niklas Exp $

#
# Copyright (c) 1998 Niklas Hallqvist.  All rights reserved.
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

#
# This code was written under funding by Ericsson.
#

PROGNAME=$0
NC=/usr/bin/nc
HOST=localhost
ISAKMP_PORT=500

set -- `getopt p: $*`
if [ $? != 0 ]; then
  echo 'usage: $PROGNAME [-p port] host' >&2
  exit 2
fi
for i; do
  case "$i" in
  -p)
    ISAKMP_PORT=$2; shift; shift;;
  --)
    shift; break;;
  esac
done

if [ $# -gt 0 ]; then
  HOST=$1
fi

send () {
  ${NC} -u -w 1 ${HOST} ${ISAKMP_PORT}
}

# Short message
printf "SHORT!" |send

# (Most probably) invalid cookie
printf "INVALID COOKIES!\0\x10\0\0\0\0\0\0\0\0\0\x1c" |send

# Invalid next payload type
printf "01234567\0\0\0\0\0\0\0\0!\x10\0\0\0\0\0\0\0\0\0\x1c" |send

# Invalid major version
printf "01234567\0\0\0\0\0\0\0\0\0\x20\0\0\0\0\0\0\0\0\0\x1c" |send

# Invalid minor version
printf "01234567\0\0\0\0\0\0\0\0\0\x11\0\0\0\0\0\0\0\0\0\x1c" |send

# Invalid exchange type
printf "01234567\0\0\0\0\0\0\0\0\0\x10!\0\0\0\0\0\0\0\0\x1c" |send

# Invalid flags
printf "01234567\0\0\0\0\0\0\0\0\0\x10\2\x80\0\0\0\0\0\0\0\x1c" |send

# Invalid message ID
printf "01234567\0\0\0\0\0\0\0\0\0\x10\2\0BAD!\0\0\0\x1c" |send

# Short length
printf "01234567\0\0\0\0\0\0\0\0\0\x10\2\0\0\0\0\0\0\0\0\x1b" |send

# Long length
printf "01234567\0\0\0\0\0\0\0\0\0\x10\2\0\0\0\0\0\0\0\0\x1d" |send
