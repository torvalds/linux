#!/bin/sh
#-
# Copyright (c) 2010, "Bjoern A. Zeeb" <bz@FreeBSD.org>
# Copyright (c) 2011, Sandvine Incorporated ULC.
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

#
# Test ipfw fwd for IPv4 and IPv6 using VIMAGE, testing that as well.
# For no test the packet header contents must be changed but always
# keeping the original destination.
#

case `id -u` in
0)	;;
*)	echo "ERROR: Must be run as superuser." >&2
	exit 2
esac

epair_base()
{
	local ep
	
	ep=`ifconfig epair create`
	expr ${ep} : '\(.*\).'
}

debug_err()
{
	local _p
	_p="$1"

	case "${DEBUG}" in
	"")	;;
	*)
		echo " ~~ start of debug ~~"
		echo " ~~ left:"
		jexec ${ljid} /sbin/ipfw show
		echo " ~~ middle:"
		jexec ${mjid} /sbin/ipfw show
		echo " ~~ right:"
		jexec ${rjid} /sbin/ipfw show
		echo " ~~ result file:"
		cat ${_p}.1
		echo " ~~ log file:"
		cat ${_p}
		echo " ~~ end of debug ~~"
		;;
	esac
}

check_cleanup_result_file()
{
	local _p
	_p="$1"

	if test ! -s ${_p}.1; then
		echo "FAIL (output file empty)."
		debug_err ${_p}
	else
		read line < ${_p}.1
		# Netcat adds 'X's in udp mode.
		l="/${line#*/}"
		if test "${l}" = "${_p}"; then
			echo "PASS."
		else
			echo "FAIL (expected: '${_p}' got '${l}')."
			debug_err ${_p}
		fi
	fi

	rm -f ${_p}.1
	rm -f ${_p}
}

# Transparent proxy scenario (local address).
run_test_tp()
{
	local _descr
	local _sip _dip _fip _fport _dport _p
	local _nc_af _nc_p
	local _lport
	descr="$1"
	_sip="$2"
	_dip="$3"
	_fip="$4"
	_fport="$5"
	_dport="$6"
	_p="$7"
	_nc_af="$8"

	_lport=${_dport}
	case "${_fport}" in
	"")	_lport="${_dport}" ;;
	*)	_lport="${_fport#,}" ;;
	esac

	case "${_p}" in
	udp)	_nc_p="-u" ;;
	esac

	OUT=`mktemp -t "ipfwfwd$$-XXXXXX"`
	echo -n "${descr} (${OUT}).."
	(
	jexec ${ljid} /sbin/ipfw -f flush
	jexec ${ljid} /sbin/ipfw -f zero
	jexec ${mjid} /sbin/ipfw -f flush
	jexec ${mjid} /sbin/ipfw -f zero
	jexec ${rjid} /sbin/ipfw -f flush
	jexec ${rjid} /sbin/ipfw -f zero
	jexec ${mjid} /sbin/ipfw add 100 fwd ${_fip}${_fport} ${_p} from ${_sip} to ${_dip}

	jexec ${mjid} /bin/sh -c "nc -w 10 ${_nc_af} -n ${_nc_p} -l ${_fip} ${_lport} > ${OUT}.1 &"
	jexec ${rjid} /bin/sh -c "echo '${OUT}' | nc -w 1 -v ${_nc_af} -n ${_nc_p} ${_dip} ${_dport}"
	) > ${OUT} 2>&1
	check_cleanup_result_file "${OUT}"
}

# Transparent redirect scenario (non-local address).
run_test_nh()
{
	local _descr
	local _sip _dip _fip _fport _dport _p
	local _nc_af _nc_p
	local _lport
	descr="$1"
	_sip="$2"
	_dip="$3"
	_fip="$4"
	_fport="$5"
	_dport="$6"
	_p="$7"
	_nc_af="$8"

	_lport=${_dport}
	case "${_fport}" in
	"")	_lport="${_dport}" ;;
	*)	_lport="${_fport#,}" ;;
	esac

	case "${_p}" in
	udp)	_nc_p="-u" ;;
	esac

	OUT=`mktemp -t "ipfwfwd$$-XXXXXX"`
	echo -n "${descr} (${OUT}).."
	(
	jexec ${ljid} /sbin/ipfw -f flush
	jexec ${ljid} /sbin/ipfw -f zero
	jexec ${mjid} /sbin/ipfw -f flush
	jexec ${mjid} /sbin/ipfw -f zero
	jexec ${rjid} /sbin/ipfw -f flush
	jexec ${rjid} /sbin/ipfw -f zero
	jexec ${mjid} /sbin/ipfw add 100 fwd ${_fip} ${_p} from ${_sip} to ${_dip}

	jexec ${ljid} /bin/sh -c "nc -w 10 ${_nc_af} -n ${_nc_p} -l ${_dip} ${_lport} > ${OUT}.1 &"
	jexec ${rjid} /bin/sh -c "echo '${OUT}' | nc -w 1 -v ${_nc_af} -n ${_nc_p} ${_dip} ${_dport}"
	) > ${OUT} 2>&1
	check_cleanup_result_file "${OUT}"
}

echo "==> Setting up test network"
kldload -q ipfw > /dev/null 2>&1

# Start left (sender) jail.
ljid=`jail -i -c -n lef$$ host.hostname=left.example.net vnet persist`

# Start middle (ipfw) jail.
mjid=`jail -i -c -n mid$$ host.hostname=center.example.net vnet persist`

# Start right (non-local ip redirects go to here) jail.
rjid=`jail -i -c -n right$$ host.hostname=right.example.net vnet persist`

echo "left ${ljid}   middle ${mjid}    right ${rjid}"

# Create networking.
#
# jail:		left            middle           right
# ifaces:	lmep:a ---- lmep:b  mrep:a ---- mrep:b
#

jexec ${mjid} sysctl net.inet.ip.forwarding=1
jexec ${mjid} sysctl net.inet6.ip6.forwarding=1
jexec ${mjid} sysctl net.inet6.ip6.accept_rtadv=0

lmep=$(epair_base)
ifconfig ${lmep}a vnet ${ljid}
ifconfig ${lmep}b vnet ${mjid}

jexec ${ljid} ifconfig lo0 inet 127.0.0.1/8
jexec ${ljid} ifconfig lo0 inet 192.0.2.5/32 alias		# Test 9-10
jexec ${ljid} ifconfig lo0 inet6 2001:db8:1::1/128 alias	# Test 11-12
jexec ${ljid} ifconfig ${lmep}a inet  192.0.2.1/30 up
jexec ${ljid} ifconfig ${lmep}a inet6 2001:db8::1/64 alias

jexec ${ljid} route add default 192.0.2.2
jexec ${ljid} route add -inet6 default 2001:db8::2

jexec ${mjid} ifconfig lo0 inet 127.0.0.1/8
jexec ${mjid} ifconfig lo0 inet 192.0.2.255/32 alias		# Test 1-4
jexec ${mjid} ifconfig lo0 inet6 2001:db8:ffff::1/128 alias	# Test 5-8
jexec ${mjid} ifconfig ${lmep}b inet  192.0.2.2/30 up
jexec ${mjid} ifconfig ${lmep}b inet6 2001:db8::2/64 alias
jexec ${mjid} route add default 192.0.2.1

mrep=$(epair_base)
ifconfig ${mrep}a vnet ${mjid}
ifconfig ${mrep}b vnet ${rjid}

jexec ${mjid} ifconfig ${mrep}a inet  192.0.2.5/30 up
jexec ${mjid} ifconfig ${mrep}a inet6 2001:db8:1::1/64 alias

jexec ${rjid} ifconfig lo0 inet 127.0.0.1/8
jexec ${rjid} ifconfig ${mrep}b inet  192.0.2.6/30 up
jexec ${rjid} ifconfig ${mrep}b inet6 2001:db8:1::2/64 alias

jexec ${rjid} route add default 192.0.2.5
jexec ${rjid} route add -inet6 default 2001:db8:1::1

# ------------------------------------------------------------------------------
# Tests
#
# The jails are not chrooted to they all share the same base filesystem.
# This means we can put results into /tmp and just collect them from here.
#
echo "==> Running tests"

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
i=1
run_test_tp "TEST ${i} IPv4 UDP redirect local to other local address, same port" \
	192.0.2.6 192.0.2.5 192.0.2.255 "" 12345 udp "-4"

i=$((i + 1))
run_test_tp "TEST ${i} IPv4 UDP redirect local to other local address, different port" \
	192.0.2.6 192.0.2.5 192.0.2.255 ",65534" 12345 udp "-4"

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
i=$((i + 1))
run_test_tp "TEST ${i} IPv4 TCP redirect local to other local address, same port" \
	192.0.2.6 192.0.2.5 192.0.2.255 "" 12345 tcp "-4"

i=$((i + 1))
run_test_tp "TEST ${i} IPv4 TCP redirect local to other local address, different port" \
	192.0.2.6 192.0.2.5 192.0.2.255 ",65534" 12345 tcp "-4"

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
i=$((i + 1))
run_test_tp "TEST ${i} IPv4 UDP redirect foreign to local address, same port" \
	192.0.2.6 192.0.2.1 192.0.2.255 "" 12345 udp "-4"

i=$((i + 1))
run_test_tp "TEST ${i} IPv4 UDP redirect foreign to local address, different port" \
	192.0.2.6 192.0.2.1 192.0.2.255 ",65534" 12345 udp "-4"

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
i=$((i + 1))
run_test_tp "TEST ${i} IPv4 TCP redirect foreign to local address, same port" \
	192.0.2.6 192.0.2.1 192.0.2.255 "" 12345 tcp "-4"

i=$((i + 1))
run_test_tp "TEST ${i} IPv4 TCP redirect foreign to local address, different port" \
	192.0.2.6 192.0.2.1 192.0.2.255 ",65534" 12345 tcp "-4"

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
i=$((i + 1))
run_test_tp "TEST ${i} IPv6 UDP redirect local to other local address, same port" \
	2001:db8:1::2 2001:db8::1 2001:db8:ffff::1 "" 12345 udp "-6"

i=$((i + 1))
run_test_tp "TEST ${i} IPv6 UDP redirect local to other local address, different port" \
	2001:db8:1::2 2001:db8::1 2001:db8:ffff::1 ",65534" 12345 udp "-6"

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
i=$((i + 1))
run_test_tp "TEST ${i} IPv6 TCP redirect local to other local address, same port" \
	2001:db8:1::2 2001:db8::1 2001:db8:ffff::1 "" 12345 tcp "-6"

i=$((i + 1))
run_test_tp "TEST ${i} IPv6 TCP redirect local to other local address, different port" \
	2001:db8:1::2 2001:db8::1 2001:db8:ffff::1 ",65534" 12345 tcp "-6"

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
i=$((i + 1))
run_test_tp "TEST ${i} IPv6 UDP redirect foreign to local address, same port" \
	2001:db8:1::2 2001:db8::1 2001:db8:ffff::1 "" 12345 udp "-6"

i=$((i + 1))
run_test_tp "TEST ${i} IPv6 UDP redirect foreign to local address, different port" \
	2001:db8:1::2 2001:db8::1 2001:db8:ffff::1 ",65534" 12345 udp "-6"

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
i=$((i + 1))
run_test_tp "TEST ${i} IPv6 TCP redirect foreign to local address, same port" \
	2001:db8:1::2 2001:db8::1 2001:db8:ffff::1 "" 12345 tcp "-6"

i=$((i + 1))
run_test_tp "TEST ${i} IPv6 TCP redirect foreign to local address, different port" \
	2001:db8:1::2 2001:db8::1 2001:db8:ffff::1 ",65534" 12345 tcp "-6"

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
i=$((i + 1))
run_test_nh "TEST ${i} IPv4 UDP redirect to foreign address" \
	192.0.2.6 192.0.2.5 192.0.2.1 "" 12345 udp "-4"

i=$((i + 1))
run_test_nh "TEST ${i} IPv4 TCP redirect to foreign address" \
	192.0.2.6 192.0.2.5 192.0.2.1 "" 12345 tcp "-4"

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
i=$((i + 1))
run_test_nh "TEST ${i} IPv6 UDP redirect to foreign address" \
	2001:db8:1::2 2001:db8:1::1 2001:db8::1 "" 12345 udp "-6"

i=$((i + 1))
run_test_nh "TEST ${i} IPv6 TCP redirect to foreign address" \
	2001:db8:1::2 2001:db8:1::1 2001:db8::1 "" 12345 tcp "-6"

################################################################################
#
# Cleanup
#
echo "==> Cleaning up in 3 seconds"
# Let VIMAGE network stacks settle to avoid panics while still "experimental".
sleep 3

jail -r ${rjid}
jail -r ${mjid}
jail -r ${ljid}

for jid in ${rjid} ${mjid} ${ljid}; do
	while : ; do
		x=`jls -as -j ${jid} jid 2>/dev/null`
		case "${x}" in
		jid=*)	echo "Waiting for jail ${jid} to stop." >&2
			sleep 1
			continue
			;;
		esac
		break
	done
done

ifconfig ${lmep}a destroy
ifconfig ${mrep}a destroy

# end
