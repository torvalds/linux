# $FreeBSD$

# Import helper functions
. $(atf_get_srcdir)/helper_functions.shin

atf_test_case user_show_all
user_show_all_body() {
	populate_etc_skel
	atf_check -o not-empty ${PW} usershow -a
}

atf_test_case user_show_name
user_show_name_body() {
	populate_etc_skel
	atf_check -o not-empty ${PW} usershow root
}

atf_test_case user_show_nonexistent_name
user_show_nonexistent_name_body() {
	populate_etc_skel

	nonexistent_user=bogus
	no_such_user_msg="pw: no such user \`$nonexistent_user'\n"

	atf_check -e "inline:$no_such_user_msg" -s exit:67 ${PW} usershow \
		$nonexistent_user
	atf_check -e "inline:$no_such_user_msg" -s exit:67 ${PW} usershow \
		-n $nonexistent_user
}

atf_test_case user_show_nonexistent_uid
user_show_nonexistent_uid_body() {
	populate_etc_skel

	nonexistent_uid=4242
	no_such_uid_msg="pw: no such uid \`$nonexistent_uid'\n"

	atf_check -e "inline:$no_such_uid_msg" -s exit:67 ${PW} usershow \
		$nonexistent_uid
	atf_check -e "inline:$no_such_uid_msg" -s exit:67 ${PW} usershow \
		-u $nonexistent_uid
}

atf_test_case user_show_uid
user_show_uid_body() {
	populate_etc_skel
	atf_check -o not-empty ${PW} usershow -u 0
}

atf_init_test_cases() {
	atf_add_test_case user_show_all
	atf_add_test_case user_show_name
	atf_add_test_case user_show_nonexistent_name
	atf_add_test_case user_show_nonexistent_uid
	atf_add_test_case user_show_uid
}
