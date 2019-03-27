#!/bin/sh
#
# Copyright (c) 2007-2008 Sean C. Farley <scf@FreeBSD.org>
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer,
#    without modification, immediately at the beginning of the file.
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
# $FreeBSD$


# Initialization.
testndx=0


# Testing function.
run_test()
{
	lasttest="${@}"
	result=`./envctl -t "${@}"`

	if [ ${?} -ne 0 ]
	then
		echo "Test program failed" >&2
		exit 1
	fi

	return
}


# Perform test on results.
check_result()
{
	testndx=$((testndx + 1))

	echo "${result}" | sed 's/[ \t]*$//' | grep -q "^${@}$"
	if [ ${?} -eq 0 ]
	then
		echo "ok ${testndx}"
	else
		echo "not ok ${testndx} - '${lasttest}'"
	fi

	return
}


#
# Regression tests
#

# How NULL will be returned by program.
readonly NULL="\*NULL\*"

# Setup environment for tests.
readonly BAR="bar"
readonly NEWBAR="newbar"
export FOO=${BAR}


# Gets from environ.
run_test -g FOO
check_result "${FOO}"

run_test -c 3 -g FOO
check_result "${NULL}"

run_test -g FOOBAR
check_result "${NULL}"

run_test -c 3 -g FOOBAR
check_result "${NULL}"

run_test -G
check_result "${NULL}"


# Clear environ.
run_test -c 1 -g FOO
check_result "${NULL}"

run_test -c 2 -g FOO
check_result "${NULL}"

run_test -c 3 -g FOO
check_result "${NULL}"

run_test -c 4 -g FOO
check_result "${NULL}"


# Clear environ and verify values do not return after a set.
run_test -c 1 -g FOO -s FOO2 ${NEWBAR} 1 -g FOO -g FOO2
check_result "${NULL} 0 0 ${NULL} ${NEWBAR}"

run_test -c 2 -g FOO -s FOO2 ${NEWBAR} 1 -g FOO -g FOO2
check_result "${NULL} 0 0 ${NULL} ${NEWBAR}"

run_test -c 3 -g FOO -s FOO2 ${NEWBAR} 1 -g FOO -g FOO2
check_result "${NULL} 0 0 ${NULL} ${NEWBAR}"

run_test -c 4 -g FOO -s FOO2 ${NEWBAR} 1 -g FOO -g FOO2
check_result "${NULL} 0 0 ${NULL} ${NEWBAR}"


# Sets.
run_test -s FOO ${NEWBAR} 0 -g FOO
check_result "0 0 ${BAR}"

run_test -s FOO ${NEWBAR} 1 -g FOO
check_result "0 0 ${NEWBAR}"

run_test -c 3 -s FOO ${NEWBAR} 0 -g FOO
check_result "0 0 ${NEWBAR}"

run_test -c 3 -s FOO ${NEWBAR} 1 -g FOO
check_result "0 0 ${NEWBAR}"

run_test -s "FOO=" ${NEWBAR} 1 -g FOO
check_result "-1 22 ${BAR}"

run_test -s "=FOO" ${NEWBAR} 1
check_result "-1 22"

run_test -s "=" ${NEWBAR} 1
check_result "-1 22"

run_test -s "" ${NEWBAR} 1
check_result "-1 22"

run_test -S ${NEWBAR} 1
check_result "-1 22"

run_test -s FOO ${NEWBAR} 1 -s FOO ${BAR} 1 -g FOO
check_result "0 0 0 0 ${BAR}"

run_test -c 3 -s FOO ${NEWBAR} 1 -s FOO ${BAR} 1 -g FOO
check_result "0 0 0 0 ${BAR}"

run_test -s FOO ${NEWBAR} 1 -s FOO ${BAR} 1 -s FOO ${NEWBAR} 1 -g FOO
check_result "0 0 0 0 0 0 ${NEWBAR}"

run_test -s FOO ${NEWBAR} 1 -s FOO ${BAR} 1 -s FOO ${NEWBAR} 1 -s FOO ${BAR} 1\
	-g FOO
check_result "0 0 0 0 0 0 0 0 ${BAR}"

run_test -c 3 -s FOO ${BAR} 1 -g FOO -c 3 -s FOO ${NEWBAR} 1 -g FOO
check_result "0 0 ${BAR} 0 0 ${NEWBAR}"


# Unsets.
run_test -u FOO -g FOO
check_result "0 0 ${NULL}"

run_test -c 3 -u FOO -g FOO
check_result "0 0 ${NULL}"

run_test -U
check_result "-1 22"

run_test -u ""
check_result "-1 22"

run_test -u "=${BAR}"
check_result "-1 22"

run_test -c 3 -s FOO ${NEWBAR} 1 -g FOO -u FOO -g FOO
check_result "0 0 ${NEWBAR} 0 0 ${NULL}"

run_test -c 3 -u FOO -s FOO ${BAR} 1 -g FOO -u FOO -g FOO -c 3 -u FOO\
	-s FOO ${NEWBAR} 1 -g FOO
check_result "0 0 0 0 ${BAR} 0 0 ${NULL} 0 0 0 0 ${NEWBAR}"


# Puts.
run_test -p FOO=${NEWBAR} -g FOO
check_result "0 0 ${NEWBAR}"

run_test -c 3 -p FOO=${NEWBAR} -g FOO
check_result "0 0 ${NEWBAR}"

run_test -p FOO -g FOO
check_result "-1 22 ${BAR}"

run_test -p FOO=${BAR} -p FOO=${NEWBAR} -g FOO
check_result "0 0 0 0 ${NEWBAR}"

run_test -p FOO=${BAR} -s FOO ${NEWBAR} 1 -g FOO
check_result "0 0 0 0 ${NEWBAR}"

run_test -s FOO ${NEWBAR} 1 -p FOO=${BAR} -g FOO
check_result "0 0 0 0 ${BAR}"

run_test -p FOO=${BAR} -u FOO
check_result "0 0 0 0"

run_test -p FOO=${BAR} -s FOO ${NEWBAR} 1 -u FOO
check_result "0 0 0 0 0 0"

run_test -s FOO ${NEWBAR} 1 -p FOO=${BAR} -u FOO
check_result "0 0 0 0 0 0"

run_test -s FOO ${NEWBAR} 1 -p FOO=${BAR} -c 3 -g FOO -p FOO=${NEWBAR} -g FOO
check_result "0 0 0 0 ${NULL} 0 0 ${NEWBAR}"

run_test -c 3 -p FOO=${BAR} -g FOO -c 3 -p FOO=${NEWBAR} -g FOO
check_result "0 0 ${BAR} 0 0 ${NEWBAR}"


# environ replacements.
run_test -r -g FOO -s FOO ${BAR} 1 -g FOO -u FOO -g FOO
check_result "${BAR} 0 0 ${BAR} 0 0 ${NULL}"

run_test -r -g FOO -u FOO -g FOO -s FOO ${BAR} 1 -g FOO
check_result "${BAR} 0 0 ${NULL} 0 0 ${BAR}"


# corruption (blanking) of environ members.
export BLANK_ME=
export AFTER_BLANK=blanked
run_test -b BLANK_ME -p MORE=vars -g FOO -g BLANK_ME -g AFTER_BLANK
check_result "0 0 ${FOO} ${NULL} ${AFTER_BLANK}"

run_test -b BLANK_ME -u FOO -g FOO -g AFTER_BLANK
check_result "0 0 ${NULL} ${AFTER_BLANK}"

export BLANK_ME2=
export AFTER_BLANKS=blankD
run_test -b BLANK_ME -b AFTER_BLANK -b BLANK_ME2 -g FOO -g AFTER_BLANKS
check_result "${FOO} ${AFTER_BLANKS}"
