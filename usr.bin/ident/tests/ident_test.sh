# $FreeBSD$

atf_test_case ident
ident_body() {
	atf_check -o file:$(atf_get_srcdir)/test.out \
		ident < $(atf_get_srcdir)/test.in
	atf_check -o match:'Foo.*' -s exit:1 \
		-e inline:"ident warning: no id keywords in $(atf_get_srcdir)/testnoid\n" \
		ident $(atf_get_srcdir)/test.in $(atf_get_srcdir)/testnoid
	atf_check -o match:'Foo.*' -s exit:1 \
		ident -q $(atf_get_srcdir)/test.in $(atf_get_srcdir)/testnoid
}
atf_init_test_cases()
{
	atf_add_test_case ident
}
