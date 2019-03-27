#
# Copyright (c) 2017 Dell EMC
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

POSITIVE_EXP_EXPR_RE='match:[1-9][0-9]*'

POSIX_CONSTANTS="ARG_MAX PAGESIZE"
POSIX_PATH_CONSTANTS="NAME_MAX PATH_MAX"
SUPPORTED_32BIT_PROGRAM_ENVS="POSIX_V6_ILP32_OFFBIG"
SUPPORTED_64BIT_PROGRAM_ENVS="POSIX_V6_LP64_OFF64"
UNAVAILABLE_PROGRAM_ENVS="I_AM_BOGUS"
UNSUPPORTED_32BIT_PROGRAM_ENVS="POSIX_V6_LP64_OFF64"
UNSUPPORTED_64BIT_PROGRAM_ENVS="POSIX_V6_ILP32_OFFBIG"

XOPEN_CONSTANTS=

# XXX: hardcoded sysexits
EX_USAGE=64
EX_UNAVAILABLE=69

set_program_environments()
{
	atf_check -o save:arch_type.out $(atf_get_srcdir)/arch_type
	arch_type=$(cat arch_type.out)
	case "$arch_type" in
	ILP32|LP32)
		SUPPORTED_PROGRAM_ENVS="$SUPPORTED_PROGRAM_ENVS $SUPPORTED_32BIT_PROGRAM_ENVS"
		UNSUPPORTED_PROGRAM_ENVS="$UNSUPPORTED_PROGRAM_ENVS $UNSUPPORTED_32BIT_PROGRAM_ENVS"
		;;
	LP64)
		SUPPORTED_PROGRAM_ENVS="$SUPPORTED_PROGRAM_ENVS $SUPPORTED_64BIT_PROGRAM_ENVS"
		UNSUPPORTED_PROGRAM_ENVS="$UNSUPPORTED_PROGRAM_ENVS $UNSUPPORTED_64BIT_PROGRAM_ENVS"
		;;
	*)
		atf_fail "arch_type output unexpected: $arch_type"
		;;
	esac
}

atf_test_case no_programming_environment
no_programming_environment_head()
{
	atf_set	"descr" "Test some POSIX constants as a positive functional test"
}

no_programming_environment_body()
{
	for var in $POSIX_CONSTANTS; do
		atf_check -o "$POSITIVE_EXP_EXPR_RE" getconf $var
	done
	for var in $POSIX_PATH_CONSTANTS; do
		atf_check -o "$POSITIVE_EXP_EXPR_RE" getconf $var .
	done
}

atf_test_case programming_environment
programming_environment_head()
{
	atf_set	"descr" "Test some constants with specific programming environments"
}

programming_environment_body()
{
	set_program_environments

	for prog_env in ${SUPPORTED_PROGRAM_ENVS}; do
		for var in $POSIX_CONSTANTS; do
			atf_check -o "$POSITIVE_EXP_EXPR_RE" \
			    getconf -v $prog_env $var
		done
	done
}

atf_test_case programming_environment_unsupported
programming_environment_unsupported_head()
{
	atf_set	"descr" "Test for unsupported environments"
}

programming_environment_unsupported_body()
{
	set_program_environments

	for prog_env in ${UNSUPPORTED_PROGRAM_ENVS}; do
		for var in $POSIX_CONSTANTS; do
			atf_check -e not-empty -s exit:$EX_UNAVAILABLE \
			    getconf -v $prog_env $var
		done
	done
}

atf_init_test_cases()
{
	atf_add_test_case no_programming_environment
	atf_add_test_case programming_environment
	atf_add_test_case programming_environment_unsupported
}
