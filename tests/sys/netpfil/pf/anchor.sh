# $FreeBSD$

. $(atf_get_srcdir)/utils.subr

atf_test_case "pr183198" "cleanup"
pr183198_head()
{
	atf_set descr 'Test tables referenced by rules in anchors'
	atf_set require.user root
}

pr183198_body()
{
	pft_init

	epair=$(vnet_mkepair)
	vnet_mkjail alcatraz ${epair}b
	jexec alcatraz pfctl -e

	# Forward with pf enabled
	pft_set_rules alcatraz  \
		"table <test> { 10.0.0.1, 10.0.0.2, 10.0.0.3 }" \
		"block in" \
		"anchor \"epair\" on ${epair}b { \n\
			pass in from <test> \n\
		}"

	atf_check -s exit:0 -o ignore jexec alcatraz pfctl -sr -a '*'
	atf_check -s exit:0 -o ignore jexec alcatraz pfctl -t test -T show
}

pr183198_cleanup()
{
	pft_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case "pr183198"
}
