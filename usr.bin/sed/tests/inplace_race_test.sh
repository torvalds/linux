#!/bin/sh

#-
# Copyright (c) 2011 Jilles Tjoelker
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

: "${SED=sed}"

# This test really needs an SMP system. On an UP system, it will
# usually pass even if the race condition exists.
if command -v cpuset >/dev/null; then
	case `cpuset -g -p $$` in
	*,*) ;;
	*)
		echo '1..0 # Skipped: not an SMP system'
		exit 0 ;;
	esac
fi

echo "1..1"

data=abababab
data=$data$data$data$data
data=$data$data$data$data
data=$data$data$data$data
data=$data$data$data$data
data="BEGIN
$data
END"
for i in 0 1 2 3 4 5 6 7 8 9; do
	echo "$data" >file$i
done
len=${#data}

i=0
while [ $i -lt 100 ]; do
	${SED} -i.prev "s/$i/ab/" file[0-9]
	i=$((i+1))
done &
sedproc=$!

while :; do
	set -- file[0-9]
	if [ $# -ne 10 ]; then
		echo "not ok 1 inplace_race"
		exit 3
	fi
done &
checkproc=$!

wait $sedproc
kill $checkproc 2>/dev/null
wait $checkproc >/dev/null 2>&1
if [ $? -ne 3 ]; then
	echo "ok 1 inplace_race"
fi
