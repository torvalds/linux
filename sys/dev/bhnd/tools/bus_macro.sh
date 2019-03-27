#!/bin/sh
#
# Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
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
# Generate the bhnd resource macros at the bottom of dev/bhnd/bhnd.h
#
# Derived from PHK's tools/bus_macros.sh
#

macro () {
	n=${1}
	bus_n=$(echo $n | tr "[:lower:]" "[:upper:]")

	shift
	echo -n "#define bhnd_bus_${n}(r"
	for i
	do
		echo -n ", ${i}"
	done
	echo ") \\"
	echo "    (((r)->direct) ? \\"
	echo -n "	bus_${n}((r)->res"
	for i
	do
		echo -n ", (${i})"
	done
	echo ") : \\"
	echo "	BHND_BUS_${bus_n}( \\"
	echo "	    device_get_parent(rman_get_device((r)->res)),	\\"
	echo -n "	    rman_get_device((r)->res), (r)"
	for i
	do
		echo -n ", (${i})"
	done
	echo "))"

}

macro barrier o l f

for w in 1 2 4 #8
do
	# macro copy_region_$w so dh do c
	# macro copy_region_stream_$w ?
	# macro peek_$w
	for s in "" stream_
	do
		macro read_$s$w o
		macro read_multi_$s$w o d c
		macro read_region_$s$w o d c
		macro write_$s$w o v
		macro write_multi_$s$w o d c
		macro write_region_$s$w o d c
	done
	
	# set_(multi_)?_stream is not supported on ARM/ARM64, and so for
	# simplicity, we don't support their use with bhnd resources.
	# 
	# if that changes, these can be merged back into the stream-eanbled
	# loop above.
	for s in ""
	do
		macro set_multi_$s$w o v c
		macro set_region_$s$w o v c
	done
done
