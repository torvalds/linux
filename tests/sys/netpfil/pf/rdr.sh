# $FreeBSD$

. $(atf_get_srcdir)/utils.subr

atf_test_case "basic" "cleanup"
basic_head()
{
	atf_set descr 'Basic rdr test'
	atf_set require.user root
}

basic_body()
{
	pft_init

	epair=$(vnet_mkepair)

	vnet_mkjail alcatraz ${epair}b

	ifconfig ${epair}a 192.0.2.2/24 up
	route add -net 198.51.100.0/24 192.0.2.1

	jexec alcatraz ifconfig ${epair}b 192.0.2.1/24 up
	jexec alcatraz sysctl net.inet.ip.forwarding=1

	# Enable pf!
	jexec alcatraz pfctl -e
	pft_set_rules alcatraz \
		"rdr pass on ${epair}b proto tcp from any to 198.51.100.0/24 port 1234 -> 192.0.2.1 port 4321"

	echo "foo" | jexec alcatraz nc -N -l 4321 &
	sleep 1

	result=$(nc -N -w 3 198.51.100.2 1234)
	if [ "$result" != "foo" ]; then
		atf_fail "Redirect failed"
	fi
}

basic_cleanup()
{
	pft_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case "basic"
}
