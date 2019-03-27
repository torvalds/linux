# $FreeBSD$

# Import helper functions
. $(atf_get_srcdir)/helper_functions.shin

atf_test_case group_add_gid_too_large
group_add_gid_too_large_body() {
	populate_etc_skel
	atf_check -s exit:64 -e inline:"pw: Bad id '9999999999999': too large\n" \
		${PW} groupadd -n test1 -g 9999999999999
}

atf_test_case group_add_already_exists
group_add_already_exists_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} groupadd foo
	atf_check -s exit:65 \
		-e inline:"pw: group name \`foo' already exists\n" \
		${PW} groupadd foo
}

atf_init_test_cases() {
	atf_add_test_case group_add_gid_too_large
	atf_add_test_case group_add_already_exists
}
