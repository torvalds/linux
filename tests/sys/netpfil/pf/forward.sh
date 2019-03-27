# $FreeBSD$

. $(atf_get_srcdir)/utils.subr

atf_test_case "v4" "cleanup"
v4_head()
{
	atf_set descr 'Basic forwarding test'
	atf_set require.user root

	# We need scapy to be installed for out test scripts to work
	atf_set require.progs scapy
}

v4_body()
{
	pft_init

	epair_send=$(vnet_mkepair)
	ifconfig ${epair_send}a 192.0.2.1/24 up

	epair_recv=$(vnet_mkepair)
	ifconfig ${epair_recv}a up

	vnet_mkjail alcatraz ${epair_send}b ${epair_recv}b
	jexec alcatraz ifconfig ${epair_send}b 192.0.2.2/24 up
	jexec alcatraz ifconfig ${epair_recv}b 198.51.100.2/24 up
	jexec alcatraz sysctl net.inet.ip.forwarding=1
	jexec alcatraz arp -s 198.51.100.3 00:01:02:03:04:05
	route add -net 198.51.100.0/24 192.0.2.2

	# Sanity check, can we forward ICMP echo requests without pf?
	atf_check -s exit:0 $(atf_get_srcdir)/pft_ping.py \
		--sendif ${epair_send}a \
		--to 198.51.100.3 \
		--recvif ${epair_recv}a

	jexec alcatraz pfctl -e

	# Forward with pf enabled
	pft_set_rules alcatraz "block in"
	atf_check -s exit:1 $(atf_get_srcdir)/pft_ping.py \
		--sendif ${epair_send}a \
		--to 198.51.100.3 \
		--recvif ${epair_recv}a

	pft_set_rules alcatraz "block out"
	atf_check -s exit:1 $(atf_get_srcdir)/pft_ping.py \
		--sendif ${epair_send}a \
		--to 198.51.100.3 \
		--recv ${epair_recv}a

	# Allow ICMP
	pft_set_rules alcatraz "block in" "pass in proto icmp"
	atf_check -s exit:0 $(atf_get_srcdir)/pft_ping.py \
		--sendif ${epair_send}a \
		--to 198.51.100.3 \
		--recvif ${epair_recv}a
}

v4_cleanup()
{
	pft_cleanup
}

atf_test_case "v6" "cleanup"
v6_head()
{
	atf_set descr 'Basic IPv6 forwarding test'
	atf_set require.user root
	atf_set require.progs scapy
}

v6_body()
{
	pft_init

	epair_send=$(vnet_mkepair)
	epair_recv=$(vnet_mkepair)

	ifconfig ${epair_send}a inet6 2001:db8:42::1/64 up no_dad -ifdisabled
	ifconfig ${epair_recv}a up

	vnet_mkjail alcatraz ${epair_send}b ${epair_recv}b

	jexec alcatraz ifconfig ${epair_send}b inet6 2001:db8:42::2/64 up no_dad
	jexec alcatraz ifconfig ${epair_recv}b inet6 2001:db8:43::2/64 up no_dad
	jexec alcatraz sysctl net.inet6.ip6.forwarding=1
	jexec alcatraz ndp -s 2001:db8:43::3 00:01:02:03:04:05
	route add -6 2001:db8:43::/64 2001:db8:42::2

	# Sanity check, can we forward ICMP echo requests without pf?
	atf_check -s exit:0 $(atf_get_srcdir)/pft_ping.py \
		--ip6 \
		--sendif ${epair_send}a \
		--to 2001:db8:43::3 \
		--recvif ${epair_recv}a

	jexec alcatraz pfctl -e

	# Block incoming echo request packets
	pft_set_rules alcatraz \
		"block in inet6 proto icmp6 icmp6-type echoreq"
	atf_check -s exit:1 $(atf_get_srcdir)/pft_ping.py \
		--ip6 \
		--sendif ${epair_send}a \
		--to 2001:db8:43::3 \
		--recvif ${epair_recv}a

	# Block outgoing echo request packets
	pft_set_rules alcatraz \
		"block out inet6 proto icmp6 icmp6-type echoreq"
	atf_check -s exit:1 -e ignore $(atf_get_srcdir)/pft_ping.py \
		--ip6 \
		--sendif ${epair_send}a \
		--to 2001:db8:43::3 \
		--recvif ${epair_recv}a

	# Allow ICMPv6 but nothing else
	pft_set_rules alcatraz \
		"block out" \
		"pass out inet6 proto icmp6"
	atf_check -s exit:0 $(atf_get_srcdir)/pft_ping.py \
		--ip6 \
		--sendif ${epair_send}a \
		--to 2001:db8:43::3 \
		--recvif ${epair_recv}a

	# Allowing ICMPv4 does not allow ICMPv6
	pft_set_rules alcatraz \
		"block out inet6 proto icmp6 icmp6-type echoreq" \
		"pass in proto icmp"
	atf_check -s exit:1 $(atf_get_srcdir)/pft_ping.py \
		--ip6 \
		--sendif ${epair_send}a \
		--to 2001:db8:43::3 \
		--recvif ${epair_recv}a
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
