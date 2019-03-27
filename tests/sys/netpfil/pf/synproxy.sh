# $FreeBSD$

. $(atf_get_srcdir)/utils.subr

atf_test_case "synproxy" "cleanup"
synproxy_head()
{
	atf_set descr 'Basic synproxy test'
	atf_set require.user root
}

synproxy_body()
{
	pft_init

	epair=$(vnet_mkepair)
	ifconfig ${epair}a 192.0.2.1/24 up
	route add -net 198.51.100.0/24 192.0.2.2

	link=$(vnet_mkepair)

	vnet_mkjail alcatraz ${epair}b ${link}a
	jexec alcatraz ifconfig ${epair}b 192.0.2.2/24 up
	jexec alcatraz ifconfig ${link}a 198.51.100.1/24 up
	jexec alcatraz sysctl net.inet.ip.forwarding=1

	vnet_mkjail singsing ${link}b
	jexec singsing ifconfig ${link}b 198.51.100.2/24 up
	jexec singsing route add default 198.51.100.1

	jexec singsing /usr/sbin/inetd $(atf_get_srcdir)/echo_inetd.conf

	jexec alcatraz pfctl -e
	pft_set_rules alcatraz "set fail-policy return" \
		"scrub in all fragment reassemble" \
		"pass out quick on ${epair}b all no state allow-opts" \
		"pass in quick on ${epair}b proto tcp from any to any port 7 synproxy state" \
		"pass in quick on ${epair}b all no state"

	# Sanity check, can we ping singing
	atf_check -s exit:0 -o ignore ping -c 1 198.51.100.2

	# Check that we can talk to the singsing jail, after synproxying
	reply=$(echo ping | nc -N 198.51.100.2 7)
	if [ "${reply}" != "ping" ];
	then
		atf_fail "echo failed"
	fi
}

synproxy_cleanup()
{
	pft_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case "synproxy"
}
