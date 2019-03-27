#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2012 Eitan Adler
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer
#    in this position and unchanged.
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

usage() {
	echo "usage: ssh-copy-id [-lv] [-i keyfile] [-o option] [-p port] [user@]hostname" >&2
	exit 1
}

sendkey() {
	local h="$1"
	local k="$2"
	printf "%s\n" "$k" | ssh $port -S none $options "$user$h" /bin/sh -c \'' \
		set -e; \
		umask 077; \
		keyfile=$HOME/.ssh/authorized_keys ; \
		mkdir -p -- "$HOME/.ssh/" ; \
		while read alg key comment ; do \
			[ -n "$key" ] || continue; \
			if ! grep -sqwF "$key" "$keyfile"; then \
				printf "$alg $key $comment\n" >> "$keyfile" ; \
			fi ; \
		done ; \
		if [ -x /sbin/restorecon ]; then \
			/sbin/restorecon -F "$HOME/.ssh/" "$keyfile" >/dev/null 2>&1 || true ; \
		fi \
	'\' 
}

agentKeys() {
	keys="$(ssh-add -L | grep -v 'The agent has no identities.')$nl$keys"
}

keys=""
host=""
hasarg=""
user=""
port=""
nl="
"
options=""

IFS=$nl

while getopts 'i:lo:p:v' arg; do
	case $arg in
	i)	
		hasarg="x"
		if [ -r "${OPTARG}.pub" ]; then
			keys="$(cat -- "${OPTARG}.pub")$nl$keys"
		elif [ -r "$OPTARG" ]; then
			keys="$(cat -- "$OPTARG")$nl$keys"
		else
			echo "File $OPTARG not found" >&2
			exit 1
		fi
		;;
	l)	
		hasarg="x"
		agentKeys
		;;
	p)	
		port=-p$nl$OPTARG
		;;
	o)	
		options=$options$nl-o$nl$OPTARG
		;;
	v)
		options="$options$nl-v"
		;;
	*)	
		usage
		;;
	esac
done >&2

shift $((OPTIND-1))

if [ -z "$hasarg" ]; then
	agentKeys
fi
if [ -z "$keys" ] || [ "$keys" = "$nl" ]; then
	echo "no keys found" >&2
	exit 1
fi
if [ "$#" -eq 0 ]; then
	usage
fi

for host in "$@"; do
	sendkey "$host" "$keys"
done
