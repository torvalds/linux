# $FreeBSD$

. $(atf_get_srcdir)/utils.subr

atf_test_case "v4" "cleanup"
v4_head()
{
	atf_set descr 'Basic route-to test'
	atf_set require.user root
}

v4_body()
{
	pft_init

	epair_send=$(vnet_mkepair)
	ifconfig ${epair_send}a 192.0.2.1/24 up
	epair_route=$(vnet_mkepair)
	ifconfig ${epair_route}a 203.0.113.1/24 up

	vnet_mkjail alcatraz ${epair_send}b ${epair_route}b
	jexec alcatraz ifconfig ${epair_send}b 192.0.2.2/24 up
	jexec alcatraz ifconfig ${epair_route}b 203.0.113.2/24 up
	jexec alcatraz route add -net 198.51.100.0/24 192.0.2.1
	jexec alcatraz pfctl -e

	# Attempt to provoke PR 228782
	pft_set_rules alcatraz "block all" "pass user 2" \
		"pass out route-to (${epair_route}b 203.0.113.1) from 192.0.2.2 to 198.51.100.1 no state"
	jexec alcatraz nc -w 3 -s 192.0.2.2 198.51.100.1 22

	# atf wants us to not return an error, but our netcat will fail
	true
}

v4_cleanup()
{
	pft_cleanup
}

atf_test_case "v6" "cleanup"
v6_head()
{
	atf_set descr 'Basic route-to test (IPv6)'
	atf_set require.user root
}

v6_body()
{
	pft_init

	epair_send=$(vnet_mkepair)
	ifconfig ${epair_send}a inet6 2001:db8:42::1/64 up no_dad -ifdisabled
	epair_route=$(vnet_mkepair)
	ifconfig ${epair_route}a inet6 2001:db8:43::1/64 up no_dad -ifdisabled

	vnet_mkjail alcatraz ${epair_send}b ${epair_route}b
	jexec alcatraz ifconfig ${epair_send}b inet6 2001:db8:42::2/64 up no_dad
	jexec alcatraz ifconfig ${epair_route}b inet6 2001:db8:43::2/64 up no_dad
	jexec alcatraz route add -6 2001:db8:666::/64 2001:db8:42::2
	jexec alcatraz pfctl -e

	# Attempt to provoke PR 228782
	pft_set_rules alcatraz "block all" "pass user 2" \
		"pass out route-to (${epair_route}b 2001:db8:43::1) from 2001:db8:42::2 to 2001:db8:666::1 no state"
	jexec alcatraz nc -6 -w 3 -s 2001:db8:42::2 2001:db8:666::1 22

	# atf wants us to not return an error, but our netcat will fail
	true
}

v6_cleanup()
{
	pft_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case "v4"
	atf_add_test_case "v6"
}
