# $FreeBSD$

atf_test_case "space" cleanup
space_head()
{
	atf_set descr "Test macros with spaces"
}

space_body()
{
	echo \"this is\" = \"a variable\" > pf.conf
	cat pf.conf
	atf_check -o ignore -e ignore -s exit:1 pfctl -nvf pf.conf

	echo this = \"a variable\" > pf.conf
	cat pf.conf
	atf_check -o ignore -s exit:0 pfctl -nvf pf.conf
}

space_cleanup()
{
	rm -f pf.conf
}

atf_init_test_cases()
{
	atf_add_test_case "space"
}
