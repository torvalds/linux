# $FreeBSD$

# Import helper functions
. $(atf_get_srcdir)/helper_functions.shin


# Test to make sure we do not accidentially delete wheel when trying to delete
# an unknown group
atf_test_case group_do_not_delete_wheel_if_group_unknown
group_do_not_delete_wheel_if_group_unknown_head() {
        atf_set "descr" "Make sure we do not consider gid 0 an unknown group"
}
group_do_not_delete_wheel_if_group_unknown_body() {
        populate_etc_skel
        atf_check -s exit:0 -o inline:"wheel:*:0:root\n" -x ${PW} groupshow wheel
        atf_check -e inline:"pw: Bad id 'I_do_not_exist': invalid\n" -s exit:64 -x \
		${PW} groupdel -g I_do_not_exist
        atf_check -s exit:0 -o inline:"wheel:*:0:root\n" -x ${PW} groupshow wheel
}


atf_init_test_cases() {
        atf_add_test_case group_do_not_delete_wheel_if_group_unknown
}
