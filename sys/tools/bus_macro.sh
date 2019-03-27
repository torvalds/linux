#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2004-2005 Poul-Henning Kamp.
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
#
# Generate the convenience macros at the bottom of sys/bus.h
#

macro () {

	n=${1}
	shift
	echo -n "#define bus_${n}(r"
	for i
	do
		echo -n ", ${i}"
	done
	echo ") \\"
	echo -n "	bus_space_${n}((r)->r_bustag, (r)->r_bushandle"
	for i
	do
		echo -n ", (${i})"
	done
	echo ")"
}

macro barrier o l f

for w in 1 2 4 8
do
	# macro copy_region_$w so dh do c
	# macro copy_region_stream_$w ?
	# macro peek_$w
	for s in "" stream_
	do
		macro read_$s$w o
		macro read_multi_$s$w o d c
		macro read_region_$s$w o d c
		macro set_multi_$s$w o v c
		macro set_region_$s$w o v c
		macro write_$s$w o v
		macro write_multi_$s$w o d c
		macro write_region_$s$w o d c
	done
done
