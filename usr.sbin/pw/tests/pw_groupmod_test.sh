# $FreeBSD$

# Import helper functions
. $(atf_get_srcdir)/helper_functions.shin


# Test adding & removing a user from a group
atf_test_case groupmod_user
groupmod_user_body() {
	populate_etc_skel
	atf_check -s exit:0 ${PW} addgroup test
	atf_check -s exit:0 ${PW} groupmod test -m root
	atf_check -s exit:0 -o match:"^test:\*:1001:root$" \
		grep "^test:\*:.*:root$" $HOME/group
	atf_check -s exit:0 ${PW} groupmod test -d root
	atf_check -s exit:0 -o match:"^test:\*:1001:$" \
		grep "^test:\*:.*:$" $HOME/group
}


# Test adding and removing a user that does not exist
atf_test_case groupmod_invalid_user
groupmod_invalid_user_body() {
	populate_etc_skel
	atf_check -s exit:0 ${PW} addgroup test
	atf_check -s exit:67 -e match:"does not exist" ${PW} groupmod test -m foo
	atf_check -s exit:0  ${PW} groupmod test -d foo
}

atf_test_case groupmod_bug_193704
groupmod_bug_193704_head() {
	atf_set "descr" "Regression test for the #193704 bug"
}
groupmod_bug_193704_body() {
	populate_etc_skel
	atf_check -s exit:0 -x ${PW} groupadd test
	atf_check -s exit:0 -x ${PW} groupmod test -l newgroupname
	atf_check -s exit:65 -e match:"^pw: unknown group" -x ${PW} groupshow test
}

atf_test_case usermod_bug_185666
usermod_bug_185666_head() {
	atf_set "descr" "Regression test for the #185666 bug"
}

usermod_bug_185666_body() {
	populate_etc_skel
	atf_check -s exit:0 -x ${PW} useradd testuser
	atf_check -s exit:0 -x ${PW} groupadd testgroup
	atf_check -s exit:0 -x ${PW} groupadd testgroup2
	atf_check -s exit:0 -x ${PW} usermod testuser -G testgroup
	atf_check -o inline:"testuser:*:1001:\n" -x ${PW} groupshow testuser
	atf_check -o inline:"testgroup:*:1002:testuser\n" -x ${PW} groupshow testgroup
	atf_check -o inline:"testgroup2:*:1003:\n" -x ${PW} groupshow testgroup2
	atf_check -s exit:0 -x ${PW} usermod testuser -G testgroup2
	atf_check -o inline:"testuser:*:1001:\n" -x ${PW} groupshow testuser
	atf_check -o inline:"testgroup:*:1002:\n" -x ${PW} groupshow testgroup
	atf_check -o inline:"testgroup2:*:1003:testuser\n" -x ${PW} groupshow testgroup2
}

atf_test_case do_not_duplicate_group_on_gid_change
do_not_duplicate_group_on_gid_change_head() {
	atf_set "descr" "Do not duplicate group on gid change"
}

do_not_duplicate_group_on_gid_change_body() {
	populate_etc_skel
	atf_check -s exit:0 -x ${PW} groupadd testgroup
	atf_check -s exit:0 -x ${PW} groupmod testgroup -g 12345
	# use grep to see if the entry has not be duplicated
	atf_check -o inline:"testgroup:*:12345:\n" -s exit:0 -x grep "^testgroup" ${HOME}/group
}

atf_test_case groupmod_rename
groupmod_rename_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} groupadd foo
	atf_check -s exit:0 ${PW} groupmod foo -l bar
	atf_check -s exit:0 -o match:"^bar:.*" \
		grep "^bar:.*" ${HOME}/group
}

atf_test_case groupmod_members
groupmod_members_body() {
	populate_etc_skel

	for i in user1 user2 user3 user4; do
		atf_check -s exit:0 ${PW} useradd $i
	done

	atf_check -s exit:0 ${PW} groupadd foo -M "user1, user2"
	atf_check -o inline:"foo:*:1005:user1,user2\n" -s exit:0 \
		${PW} groupshow foo
	atf_check -s exit:0 ${PW} groupmod foo -m "user3, user4"
	atf_check -o inline:"foo:*:1005:user1,user2,user3,user4\n" -s exit:0 \
		${PW} groupshow foo
	atf_check -s exit:0 ${PW} groupmod foo -M "user1, user4"
	atf_check -o inline:"foo:*:1005:user1,user4\n" -s exit:0 \
		${PW} groupshow foo
	# what about duplicates
	atf_check -s exit:0 ${PW} groupmod foo -m "user1, user2, user3, user4"
	atf_check -o inline:"foo:*:1005:user1,user4,user2,user3\n" -s exit:0 \
		${PW} groupshow foo
	atf_check -s exit:0 ${PW} groupmod foo -d "user1, user3"
	atf_check -o inline:"foo:*:1005:user4,user2\n" -s exit:0 \
		${PW} groupshow foo
}

atf_init_test_cases() {
	atf_add_test_case groupmod_user
	atf_add_test_case groupmod_invalid_user
	atf_add_test_case groupmod_bug_193704
	atf_add_test_case usermod_bug_185666
	atf_add_test_case do_not_duplicate_group_on_gid_change
	atf_add_test_case groupmod_rename
	atf_add_test_case groupmod_members
}
