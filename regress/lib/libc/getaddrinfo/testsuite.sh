#	$OpenBSD: testsuite.sh,v 1.1 2002/07/05 15:54:30 itojun Exp $
#	$NetBSD: testsuite.sh,v 1.3 2002/07/05 15:49:11 itojun Exp $

#
# Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2001, and 2002 WIDE Project.
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
# 3. Neither the name of the project nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

TEST=./gaitest
#TEST='./test -v'
#IF=`ifconfig -a | grep -v '^	' | sed -e 's/:.*//' | head -1 | awk '{print $1}'`

echo '== basic ones'
$TEST ::1 http
$TEST 127.0.0.1 http
$TEST localhost http
$TEST ::1 tftp
$TEST 127.0.0.1 tftp
$TEST localhost tftp
$TEST ::1 echo
$TEST 127.0.0.1 echo
$TEST localhost echo
echo

echo '== specific address family'
$TEST -4 localhost http
$TEST -6 localhost http
echo

echo '== empty hostname'
$TEST '' http
$TEST '' echo
$TEST '' tftp
$TEST '' 80
$TEST -P '' http
$TEST -P '' echo
$TEST -P '' tftp
$TEST -P '' 80
$TEST -S '' 80
$TEST -D '' 80
echo

echo '== empty servname'
$TEST ::1 ''
$TEST 127.0.0.1 ''
$TEST localhost ''
$TEST '' ''
echo

echo '== sock_raw'
$TEST -R -p 0 localhost ''
$TEST -R -p 59 localhost ''
$TEST -R -p 59 localhost 80
$TEST -R -p 59 localhost www
$TEST -R -p 59 ::1 ''
echo

echo '== unsupported family'
$TEST -f 99 localhost ''
echo

echo '== the following items are specified in jinmei scopeaddr format doc.'
$TEST fe80::1%lo0 http
#$TEST fe80::1%$IF http
echo
