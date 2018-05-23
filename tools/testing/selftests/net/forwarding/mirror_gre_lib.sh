# SPDX-License-Identifier: GPL-2.0

do_test_span_gre_dir_ips()
{
	local expect=$1; shift
	local tundev=$1; shift
	local direction=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift

	icmp_capture_install h3-$tundev
	mirror_test v$h1 $ip1 $ip2 h3-$tundev 100 $expect
	mirror_test v$h2 $ip2 $ip1 h3-$tundev 100 $expect
	icmp_capture_uninstall h3-$tundev
}

quick_test_span_gre_dir_ips()
{
	do_test_span_gre_dir_ips 10 "$@"
}

fail_test_span_gre_dir_ips()
{
	do_test_span_gre_dir_ips 0 "$@"
}

test_span_gre_dir_ips()
{
	local tundev=$1; shift
	local direction=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift

	quick_test_span_gre_dir_ips "$tundev" "$direction" "$ip1" "$ip2"

	icmp_capture_install h3-$tundev "type $forward_type"
	mirror_test v$h1 $ip1 $ip2 h3-$tundev 100 10
	icmp_capture_uninstall h3-$tundev

	icmp_capture_install h3-$tundev "type $backward_type"
	mirror_test v$h2 $ip2 $ip1 h3-$tundev 100 10
	icmp_capture_uninstall h3-$tundev
}

full_test_span_gre_dir_ips()
{
	local tundev=$1; shift
	local direction=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift
	local what=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift

	RET=0

	mirror_install $swp1 $direction $tundev "matchall $tcflags"
	test_span_gre_dir_ips "$tundev" "$direction" "$forward_type" \
			      "$backward_type" "$ip1" "$ip2"
	mirror_uninstall $swp1 $direction

	log_test "$direction $what ($tcflags)"
}

quick_test_span_gre_dir()
{
	quick_test_span_gre_dir_ips "$@" 192.0.2.1 192.0.2.2
}

fail_test_span_gre_dir()
{
	fail_test_span_gre_dir_ips "$@" 192.0.2.1 192.0.2.2
}

test_span_gre_dir()
{
	test_span_gre_dir_ips "$@" 192.0.2.1 192.0.2.2
}

full_test_span_gre_dir()
{
	full_test_span_gre_dir_ips "$@" 192.0.2.1 192.0.2.2
}
