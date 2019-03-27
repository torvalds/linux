#
# Copyright (c) 2017 Ngie Cooper <ngie@FreeBSD.org>
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
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#

MAX_TRIES=20
PROG_PID=
PROG_PATH=$(atf_get_srcdir)/while1

SP='[[:space:]]'

start_program()
{
	echo "Starting program in background"
	PROG_COMM=while1
	PROG_PATH=$(atf_get_srcdir)/$PROG_COMM

	$PROG_PATH $* &
	PROG_PID=$!
	try=0
	while [ $try -lt $MAX_TRIES ] && ! kill -0 $PROG_PID; do
		sleep 0.5
		: $(( try += 1 ))
	done
	if [ $try -ge $MAX_TRIES ]; then
		atf_fail "Polled for program start $MAX_TRIES tries and failed"
	fi
}

atf_test_case binary_info
binary_info_head()
{
	atf_set "descr" "Checks -b support"
}
binary_info_body()
{
	start_program bogus-arg

	line_format="$SP*%s$SP+%s$SP+%s$SP+%s$SP*"
	header_re=$(printf "$line_format" "PID" "COMM" "OSREL" "PATH")
	line_re=$(printf "$line_format" $PROG_PID $PROG_COMM "[[:digit:]]+" "$PROG_PATH")

	atf_check -o save:procstat.out procstat binary $PROG_PID
	atf_check -o match:"$header_re" head -n 1 procstat.out
	atf_check -o match:"$line_re" tail -n 1 procstat.out

	atf_check -o save:procstat.out procstat -b $PROG_PID
	atf_check -o match:"$header_re" head -n 1 procstat.out
	atf_check -o match:"$line_re" tail -n 1 procstat.out
}

atf_test_case command_line_arguments
command_line_arguments_head()
{
	atf_set "descr" "Checks -c support"
}
command_line_arguments_body()
{
	atf_skip "https://bugs.freebsd.org/233587"

	arguments="my arguments"

	start_program $arguments

	line_format="$SP*%s$SP+%s$SP+%s$SP*"
	header_re=$(printf "$line_format" "PID" "COMM" "ARGS")
	line_re=$(printf "$line_format" $PROG_PID "$PROG_COMM" "$PROG_PATH $arguments")

	atf_check -o save:procstat.out procstat arguments $PROG_PID
	atf_check -o match:"$header_re" head -n 1 procstat.out
	atf_check -o match:"$line_re" tail -n 1 procstat.out

	atf_check -o save:procstat.out procstat -c $PROG_PID
	atf_check -o match:"$header_re" head -n 1 procstat.out
	atf_check -o match:"$line_re" tail -n 1 procstat.out
}

atf_test_case environment
environment_head()
{
	atf_set "descr" "Checks -e support"
}
environment_body()
{
	atf_skip "https://bugs.freebsd.org/233588"

	var="MY_VARIABLE=foo"
	eval "export $var"

	start_program my arguments

	line_format="$SP*%s$SP+%s$SP+%s$SP*"
	header_re=$(printf "$line_format" "PID" "COMM" "ENVIRONMENT")
	line_re=$(printf "$line_format" $PROG_PID $PROG_COMM ".*$var.*")

	atf_check -o save:procstat.out procstat environment $PROG_PID
	atf_check -o match:"$header_re" head -n 1 procstat.out
	atf_check -o match:"$line_re" tail -n 1 procstat.out

	atf_check -o save:procstat.out procstat -e $PROG_PID
	atf_check -o match:"$header_re" head -n 1 procstat.out
	atf_check -o match:"$line_re" tail -n 1 procstat.out
}

atf_test_case file_descriptor
file_descriptor_head()
{
	atf_set "descr" "Checks -f support"
}
file_descriptor_body()
{
	start_program my arguments

	line_format="$SP*%s$SP+%s$SP+%s$SP+%s$SP+%s$SP+%s$SP+%s$SP+%s$SP+%s$SP%s$SP*"
	header_re=$(printf "$line_format" "PID" "COMM" "FD" "T" "V" "FLAGS" "REF" "OFFSET" "PRO" "NAME")
	# XXX: write a more sensible feature test
	line_re=$(printf "$line_format" $PROG_PID $PROG_COMM ".+" ".+" ".+" ".+" ".+" ".+" ".+" ".+")

	atf_check -o save:procstat.out procstat files $PROG_PID
	atf_check -o match:"$header_re" head -n 1 procstat.out
	atf_check -o match:"$line_re" awk 'NR > 1' procstat.out

	atf_check -o save:procstat.out procstat -f $PROG_PID
	atf_check -o match:"$header_re" head -n 1 procstat.out
	atf_check -o match:"$line_re" awk 'NR > 1' procstat.out
}

atf_test_case kernel_stacks
kernel_stacks_head()
{
	atf_set "descr" "Captures kernel stacks for all visible threads"
}
kernel_stacks_body()
{
	atf_check -o save:procstat.out procstat -a kstack
	atf_check -o not-empty awk '{if ($3 == "procstat") print}' procstat.out

	atf_check -o save:procstat.out procstat -kka
	atf_check -o not-empty awk '{if ($3 == "procstat") print}' procstat.out
}

atf_init_test_cases()
{
	atf_add_test_case binary_info
	atf_add_test_case command_line_arguments
	atf_add_test_case environment
	atf_add_test_case file_descriptor
	atf_add_test_case kernel_stacks
}
