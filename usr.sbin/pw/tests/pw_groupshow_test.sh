# $FreeBSD$

# Import helper functions
. $(atf_get_srcdir)/helper_functions.shin

atf_test_case group_show_all
group_show_all_body() {
	populate_etc_skel
	atf_check -o not-empty ${PW} groupshow -a
}

atf_test_case group_show_gid
group_show_gid_body() {
	populate_etc_skel
	atf_check -o not-empty ${PW} groupshow -g 0
}

atf_test_case group_show_name
group_show_name_body() {
	populate_etc_skel
	atf_check -o not-empty ${PW} groupshow wheel
}

atf_test_case group_show_nonexistent_gid
group_show_nonexistent_gid_body() {
	populate_etc_skel

	nonexistent_gid=4242
	no_such_name_msg="pw: unknown gid \`$nonexistent_gid'\n"

	atf_check -e "inline:$no_such_name_msg" -s exit:65 ${PW} groupshow \
		$nonexistent_gid
	atf_check -e "inline:$no_such_name_msg" -s exit:65 ${PW} groupshow \
		-g $nonexistent_gid
}

atf_test_case group_show_nonexistent_name
group_show_nonexistent_name_body() {
	populate_etc_skel

	nonexistent_name=bogus
	no_such_name_msg="pw: unknown group \`$nonexistent_name'\n"

	atf_check -e "inline:$no_such_name_msg" -s exit:65 ${PW} groupshow \
		$nonexistent_name
	atf_check -e "inline:$no_such_name_msg" -s exit:65 ${PW} groupshow \
		-n $nonexistent_name
}

atf_init_test_cases() {
	atf_add_test_case group_show_all
	atf_add_test_case group_show_gid
	atf_add_test_case group_show_name
	atf_add_test_case group_show_nonexistent_gid
	atf_add_test_case group_show_nonexistent_name
}
