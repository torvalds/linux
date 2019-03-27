# $FreeBSD$

atf_test_case rlf

rlf_head()
{
	atf_set "descr" "testing reverse line feed"
}
rlf_body()
{
	atf_check \
		-o inline:"a b\n" \
		-e empty \
		-s exit:0 \
		col < $(atf_get_srcdir)/rlf.in

	atf_check \
		-o inline:"a	b\n" \
		-e empty \
		-s exit:0 \
		col < $(atf_get_srcdir)/rlf2.in

	atf_check \
		-o inline:"a       b\n" \
		-e empty \
		-s exit:0 \
		col -x < $(atf_get_srcdir)/rlf2.in
}

atf_init_test_cases()
{
	atf_add_test_case rlf
}
