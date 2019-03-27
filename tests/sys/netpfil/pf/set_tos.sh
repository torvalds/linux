# $FreeBSD$

. $(atf_get_srcdir)/utils.subr

atf_test_case "v4" "cleanup"
v4_head()
{
	atf_set descr 'set-tos test'
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

	jexec alcatraz pfctl -e

	# No change is done if not requested
	pft_set_rules alcatraz "scrub out proto icmp"
	atf_check -s exit:1 -o ignore $(atf_get_srcdir)/pft_ping.py \
		--sendif ${epair_send}a \
		--to 198.51.100.3 \
		--recvif ${epair_recv}a \
		--expect-tos 42

	# The requested ToS is set
	pft_set_rules alcatraz "scrub out proto icmp set-tos 42"
	atf_check -s exit:0 $(atf_get_srcdir)/pft_ping.py \
		--sendif ${epair_send}a \
		--to 198.51.100.3 \
		--recvif ${epair_recv}a \
		--expect-tos 42

	# ToS is not changed if the scrub rule does not match
	pft_set_rules alcatraz "scrub out proto tcp set-tos 42"
	atf_check -s exit:1 -o ignore $(atf_get_srcdir)/pft_ping.py \
		--sendif ${epair_send}a \
		--to 198.51.100.3 \
		--recvif ${epair_recv}a \
		--expect-tos 42

	# Multiple scrub rules match as expected
	pft_set_rules alcatraz "scrub out proto tcp set-tos 13" \
		"scrub out proto icmp set-tos 14"
	atf_check -s exit:0 $(atf_get_srcdir)/pft_ping.py \
		--sendif ${epair_send}a \
		--to 198.51.100.3 \
		--recvif ${epair_recv}a \
		--expect-tos 14

	# And this works even if the packet already has ToS values set
	atf_check -s exit:0 $(atf_get_srcdir)/pft_ping.py \
		--sendif ${epair_send}a \
		--to 198.51.100.3 \
		--recvif ${epair_recv}a \
		--send-tos 42 \
		--expect-tos 14

	# ToS values are unmolested if the packets do not match a scrub rule
	pft_set_rules alcatraz "scrub out proto tcp set-tos 13"
	atf_check -s exit:0 $(atf_get_srcdir)/pft_ping.py \
		--sendif ${epair_send}a \
		--to 198.51.100.3 \
		--recvif ${epair_recv}a \
		--send-tos 42 \
		--expect-tos 42
}

v4_cleanup()
{
	pft_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case "v4"
}
