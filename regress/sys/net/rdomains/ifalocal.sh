#	$OpenBSD: ifalocal.sh,v 1.2 2023/10/19 18:36:41 anton Exp $

# Copyright (c) 2015 Vincent Gross <vgross@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


iface_exists()
{
	ifconfig_out=`ifconfig "$1" 2>&1`
	[ "${ifconfig_out}" != "$1: no such interface" ]
}

rtable_exists()
{
	route_show_out=`route -T "$1" -n show -inet 2>&1`
	[ "$route_show_out" != "route: routing table $1: No such file or directory" ]
}

local_route_count()
{
	matches=`route -T $3 -n show | awk -v "ifp=$2" -v "dest=$1" \
		'$1 == dest && $3 == "UHLl" && $8 == ifp { matches++ } END { print 0 + matches }'`
	if [ "$matches" -ne "$4" ]; then
		fmt >&2 << EOM
expected $4 routes to $1 with ifp $2 in rtable $3, found $matches
EOM
		return 1
	fi
	return 0
}


cleanup()
{
	route -T 6 flush
	ifconfig vether6 destroy
	route -T 5 flush
	ifconfig vether5 destroy
	route -T 4 flush
	ifconfig vether4 destroy
	route -T 3 flush
	ifconfig vether3 destroy
	ifconfig lo5 destroy
	ifconfig lo3 destroy
}

cleanup_and_die()
{
	echo "$1" >&2
	cleanup
	exit 1
}

test_rc=true

fail_test()
{
	test_rc=false
}

if false; then
if iface_exists vether3 || iface_exists vether4 ||
    iface_exists vether5 || iface_exists vether6 ; then
	cat >&2 << EOM
this test needs to create interfaces vether0, vether1, vether2 and vether3,
please destroy them before running this test.
EOM
	exit 1
fi
fi

ifconfig vether3 create rdomain 3 || cleanup_and_die "Error while creating vether3"
ifconfig vether4 create rdomain 3 || cleanup_and_die "Error while creating vether4"
ifconfig vether5 create rdomain 5 || cleanup_and_die "Error while creating vether5"
ifconfig vether6 create rdomain 5 || cleanup_and_die "Error while creating vether6"

rtable_exists 3 || cleanup_and_die "Error: rtable 3 does not exist but should"
rtable_exists 5 || cleanup_and_die "Error: rtable 5 does not exist but should"

ifconfig vether3 10.0.0.1/24 up
route -T 4 add -iface -ifp vether3 10.0.0.0/28 10.0.0.1
rtable_exists 4 || cleanup_and_die "Error: rtable 4 does not exist but should"

local_route_count 10.0.0.1 vether3 3 1 || test_fail
local_route_count 10.0.0.1 vether3 4 0 || test_fail

route -T 4 exec ifconfig vether4 10.0.0.2/24 up
local_route_count 10.0.0.2 vether4 3 1 || test_fail
local_route_count 10.0.0.2 vether4 4 0 || test_fail
local_route_count 10.0.0.1 vether3 3 1 || test_fail
local_route_count 10.0.0.1 vether3 4 0 || test_fail

ifconfig vether5 10.0.0.1/24 up
route -T 6 add -iface -ifp vether5 10.0.0.0/28 10.0.0.1
rtable_exists 6 || cleanup_and_die "Error: rtable 6 does not exist but should"

local_route_count 10.0.0.1 vether5 5 1 || test_fail
local_route_count 10.0.0.1 vether5 6 0 || test_fail

route -T 6 exec ifconfig vether6 10.0.0.2/24 up
local_route_count 10.0.0.2 vether6 5 1 || test_fail
local_route_count 10.0.0.2 vether6 6 0 || test_fail
local_route_count 10.0.0.1 vether5 5 1 || test_fail
local_route_count 10.0.0.1 vether5 6 0 || test_fail

cleanup

$test_rc
