#
#  Copyright (c) 2014 Spectra Logic Corporation
#  All rights reserved.
# 
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions, and the following disclaimer,
#     without modification.
#  2. Redistributions in binary form must reproduce at minimum a disclaimer
#     substantially similar to the "NO WARRANTY" disclaimer below
#     ("Disclaimer") and any redistribution must be conditioned upon
#     including a substantially similar Disclaimer requirement for further
#     binary redistribution.
# 
#  NO WARRANTY
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
#  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
#  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
#  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGES.
# 
#  Authors: Alan Somers         (Spectra Logic Corporation)
#
# $FreeBSD$

atf_test_case static_ipv4_loopback_route_for_each_fib cleanup
static_ipv4_loopback_route_for_each_fib_head()
{
	atf_set "descr" "Every FIB should have a static IPv4 loopback route"
}
static_ipv4_loopback_route_for_each_fib_body()
{
	local nfibs fib
	nfibs=`sysctl -n net.fibs`

	# Check for an IPv4 loopback route
	for fib in `seq 0 $((${nfibs} - 1))`; do
		atf_check -o match:"interface: lo0" -s exit:0 \
			setfib -F ${fib} route -4 get 127.0.0.1
	done
}

atf_test_case static_ipv6_loopback_route_for_each_fib cleanup
static_ipv6_loopback_route_for_each_fib_head()
{
	atf_set "descr" "Every FIB should have a static IPv6 loopback route"
}
static_ipv6_loopback_route_for_each_fib_body()
{
	local nfibs fib
	nfibs=`sysctl -n net.fibs`

	if [ "`sysctl -in kern.features.inet6`" != "1" ]; then
		atf_skip "This test requires IPv6 support"
	fi

	# Check for an IPv6 loopback route
	for fib in `seq 0 $((${nfibs} - 1))`; do
		atf_check -o match:"interface: lo0" -s exit:0 \
			setfib -F ${fib} route -6 get ::1
	done
}

atf_init_test_cases()
{
	atf_add_test_case static_ipv4_loopback_route_for_each_fib
	atf_add_test_case static_ipv6_loopback_route_for_each_fib
}

