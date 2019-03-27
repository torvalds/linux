# $FreeBSD$

. $(atf_get_srcdir)/utils.subr

atf_test_case "names" "cleanup"
names_head()
{
	atf_set descr 'Test overlapping names'
	atf_set require.user root
}

names_body()
{
	pft_init

	epair=$(vnet_mkepair)

	vnet_mkjail alcatraz ${epair}b
	ifconfig ${epair}a name foo
	jexec alcatraz ifconfig ${epair}b name foo

	jail -r alcatraz
	ifconfig foo destroy
}

names_cleanup()
{
	pft_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case "names"
}
