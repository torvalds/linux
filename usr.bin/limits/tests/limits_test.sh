#
# Copyright 2015 EMC Corp.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#

# Make sure time(1) is consistent with the FreeBSD time command and not the
# shell interpretation of time(1)
TIME=/usr/bin/time

atf_test_case cputime_hard_flag
cputime_hard_flag_body()
{

	atf_check -e empty -o match:'cputime[[:space:]]+3 secs' -s exit:0 \
	    limits -H -t 3 limits -H
	atf_check -e empty -o match:'cputime[[:space:]]+3 secs' -s exit:0 \
	    limits -H -t 3 limits -S
	atf_check -e match:'real[[:space:]]+[34]\.[0-9][0-9]' -o empty -s signal:sigkill \
	    limits -H -t 3 $TIME -p sh -c 'while : ; do : ; done'
}

SIGXCPU=24 # atf_check doesn't know sigxcpu

atf_test_case cputime_soft_flag
cputime_soft_flag_body()
{

	atf_check -e empty -o match:'cputime-max[[:space:]]+infinity secs' -s exit:0 \
	    limits -S -t 3 limits -H
	atf_check -e empty -o match:'cputime-cur[[:space:]]+3 secs' -s exit:0 \
	    limits -S -t 3 limits -S
	atf_check -e match:'real[[:space:]]+[34]\.[0-9][0-9]' -o empty -s signal:$SIGXCPU \
	    limits -S -t 3 $TIME -p sh -c 'while : ; do : ; done'
}

atf_init_test_cases()
{

	atf_add_test_case cputime_hard_flag
	atf_add_test_case cputime_soft_flag
}
