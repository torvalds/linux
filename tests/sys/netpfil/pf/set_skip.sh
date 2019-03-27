# $FreeBSD$

. $(atf_get_srcdir)/utils.subr

atf_test_case "set_skip_group" "cleanup"
set_skip_group_head()
{
	atf_set descr 'Basic set skip test'
	atf_set require.user root
}

set_skip_group_body()
{
	# See PR 229241
	pft_init

	vnet_mkjail alcatraz
	jexec alcatraz ifconfig lo0 127.0.0.1/8 up
	jexec alcatraz ifconfig lo0 group foo
	jexec alcatraz pfctl -e
	pft_set_rules alcatraz "set skip on foo" \
		"block in proto icmp"

	jexec alcatraz ifconfig
	atf_check -s exit:0 -o ignore jexec alcatraz ping -c 1 127.0.0.1
}

set_skip_group_cleanup()
{
	pft_cleanup
}

atf_test_case "set_skip_group_lo" "cleanup"
set_skip_group_lo_head()
{
	atf_set descr 'Basic set skip test, lo'
	atf_set require.user root
}

set_skip_group_lo_body()
{
	# See PR 229241
	pft_init

	vnet_mkjail alcatraz
	jexec alcatraz ifconfig lo0 127.0.0.1/8 up
	jexec alcatraz pfctl -e
	pft_set_rules alcatraz "set skip on lo" \
		"block on lo0"

	atf_check -s exit:0 -o ignore jexec alcatraz ping -c 1 127.0.0.1
	pft_set_rules noflush alcatraz "set skip on lo" \
		"block on lo0"
	atf_check -s exit:0 -o ignore jexec alcatraz ping -c 1 127.0.0.1
	jexec alcatraz pfctl -s rules
}

set_skip_group_lo_cleanup()
{
	pft_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case "set_skip_group"
	atf_add_test_case "set_skip_group_lo"
}
