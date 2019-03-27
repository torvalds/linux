#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2001-2006,2012 Douglas Barton, dougb@FreeBSD.org
# All rights reserved.
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

# This script is called by cron to store bits of randomness which are
# then used to seed /dev/random on boot.

# Originally developed by Doug Barton, dougb@FreeBSD.org

PATH=/bin:/usr/bin

# If there is a global system configuration file, suck it in.
#
if [ -r /etc/defaults/rc.conf ]; then
	. /etc/defaults/rc.conf
	source_rc_confs 2>/dev/null
elif [ -r /etc/rc.conf ]; then
	. /etc/rc.conf 2>/dev/null
fi

[ $(/sbin/sysctl -n security.jail.jailed) = 0 ] || exit 0

case ${entropy_dir} in
[Nn][Oo])
	exit 0
	;;
*)
	entropy_dir=${entropy_dir:-/var/db/entropy}
	;;
esac

entropy_save_sz=${entropy_save_sz:-4096}
entropy_save_num=${entropy_save_num:-8}

if [ ! -d "${entropy_dir}" ]; then
	install -d -o operator -g operator -m 0700 "${entropy_dir}" || {
		logger -is -t "$0" The entropy directory "${entropy_dir}" does \
		    not exist, and cannot be created. Therefore no entropy can \
		    be saved.; exit 1; }
fi

cd "${entropy_dir}" || {
	logger -is -t "$0" Cannot cd to the entropy directory: "${entropy_dir}". \
	    Entropy file rotation is aborted.; exit 1; }

for f in saved-entropy.*; do
	case "${f}" in saved-entropy.\*) continue ;; esac	# No files match
	[ ${f#saved-entropy\.} -ge ${entropy_save_num} ] && unlink ${f}
done

umask 377

n=$(( ${entropy_save_num} - 1 ))
while [ ${n} -ge 1 ]; do
	if [ -f "saved-entropy.${n}" ]; then
		mv "saved-entropy.${n}" "saved-entropy.$(( ${n} + 1 ))"
	elif [ -e "saved-entropy.${n}" -o -L "saved-entropy.${n}" ]; then
		logger -is -t "$0" \
	"${entropy_dir}/saved-entropy.${n}" is not a regular file, and so \
	    it will not be rotated. Entropy file rotation is aborted.
		exit 1
	fi
	n=$(( ${n} - 1 ))
done

dd if=/dev/random of=saved-entropy.1 bs=${entropy_save_sz} count=1 2>/dev/null

exit 0
