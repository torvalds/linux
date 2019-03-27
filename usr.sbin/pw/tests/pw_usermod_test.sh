# $FreeBSD$

# Import helper functions
. $(atf_get_srcdir)/helper_functions.shin

# Test modifying a user
atf_test_case user_mod
user_mod_body() {
	populate_etc_skel

	atf_check -s exit:67 -e match:"no such user" ${PW} usermod test
	atf_check -s exit:0 ${PW} useradd test
	atf_check -s exit:0 ${PW} usermod test
	atf_check -s exit:0 -o match:"^test:.*" \
		grep "^test:.*" $HOME/master.passwd
}

# Test modifying a user with option -N
atf_test_case user_mod_noupdate
user_mod_noupdate_body() {
	populate_etc_skel

	atf_check -s exit:67 -e match:"no such user" ${PW} usermod test -N
	atf_check -s exit:0 ${PW} useradd test
	atf_check -s exit:0 -o match:"^test:.*" ${PW} usermod test -N
	atf_check -s exit:0 -o match:"^test:.*" \
		grep "^test:.*" $HOME/master.passwd
}

# Test modifying a user with comments
atf_test_case user_mod_comments
user_mod_comments_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd test -c "Test User,home,123,456"
	atf_check -s exit:0 ${PW} usermod test -c "Test User,work,123,456"
	atf_check -s exit:0 -o match:"^test:.*:Test User,work,123,456:" \
		grep "^test:.*:Test User,work,123,456:" $HOME/master.passwd
}

# Test modifying a user with comments with option -N
atf_test_case user_mod_comments_noupdate
user_mod_comments_noupdate_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd test -c "Test User,home,123,456"
	atf_check -s exit:0 -o match:"^test:.*:Test User,work,123,456:" \
		${PW} usermod test -c "Test User,work,123,456" -N
	atf_check -s exit:0 -o match:"^test:.*:Test User,home,123,456:" \
		grep "^test:.*:Test User,home,123,456:" $HOME/master.passwd
}

# Test modifying a user with invalid comments
atf_test_case user_mod_comments_invalid
user_mod_comments_invalid_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd test
	atf_check -s exit:65 -e match:"invalid character" \
		${PW} usermod test -c "Test User,work,123:456,456"
	atf_check -s exit:1 -o empty \
		grep "^test:.*:Test User,work,123:456,456:" $HOME/master.passwd
	atf_check -s exit:0 -o match:"^test:\*" \
		grep "^test:\*" $HOME/master.passwd
}

# Test modifying a user with invalid comments with option -N
atf_test_case user_mod_comments_invalid_noupdate
user_mod_comments_invalid_noupdate_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd test
	atf_check -s exit:65 -e match:"invalid character" \
		${PW} usermod test -c "Test User,work,123:456,456" -N
	atf_check -s exit:1 -o empty \
		grep "^test:.*:Test User,work,123:456,456:" $HOME/master.passwd
	atf_check -s exit:0 -o match:"^test:\*" \
		grep "^test:\*" $HOME/master.passwd
}

# Test modifying a user name with -l
atf_test_case user_mod_name
user_mod_name_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:0 ${PW} usermod foo -l "bar"
	atf_check -s exit:0 -o match:"^bar:.*" \
		grep "^bar:.*" $HOME/master.passwd
}

# Test modifying a user name with -l with option -N
atf_test_case user_mod_name_noupdate
user_mod_name_noupdate_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:0 -o match:"^bar:.*" ${PW} usermod foo -l "bar" -N
	atf_check -s exit:0 -o match:"^foo:.*" \
		grep "^foo:.*" $HOME/master.passwd
}

atf_test_case user_mod_rename_multigroups
user_mod_rename_multigroups_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} groupadd test1
	atf_check -s exit:0 ${PW} groupadd test2
	atf_check -s exit:0 ${PW} useradd foo -G test1,test2
	atf_check -o match:"foo" -s exit:0 ${PW} groupshow test1
	atf_check -o match:"foo" -s exit:0 ${PW} groupshow test2
	atf_check -s exit:0 ${PW} usermod foo -l bar
	atf_check -o match:"bar" -s exit:0 ${PW} groupshow test1
	atf_check -o match:"bar" -s exit:0 ${PW} groupshow test2
}

atf_test_case user_mod_nogroups
user_mod_nogroups_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} groupadd test1
	atf_check -s exit:0 ${PW} groupadd test2
	atf_check -s exit:0 ${PW} groupadd test3
	atf_check -s exit:0 ${PW} groupadd test4
	atf_check -s exit:0 ${PW} useradd foo -G test1,test2
	atf_check -o match:"foo" -s exit:0 ${PW} groupshow test1
	atf_check -o match:"foo" -s exit:0 ${PW} groupshow test2
	atf_check -s exit:0 ${PW} usermod foo -G test3,test4
	atf_check -s exit:0 -o inline:"test3\ntest4\n" \
		awk -F\: '$4 == "foo" { print $1 }' ${HOME}/group
	atf_check -s exit:0 ${PW} usermod foo -G ""
	atf_check -s exit:0 -o empty \
		awk -F\: '$4 == "foo" { print $1 }' ${HOME}/group
}

atf_test_case user_mod_rename
user_mod_rename_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:0 ${PW} usermod foo -l bar
	atf_check -s exit:0 -o match:"^bar:.*" \
		grep "^bar:.*" ${HOME}/master.passwd
}

atf_test_case user_mod_rename_too_long
user_mod_rename_too_long_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:64 -e match:"too long" ${PW} usermod foo \
		-l name_very_very_very_very_very_long
}

atf_test_case user_mod_h
user_mod_h_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:0 ${PW} usermod foo -h 0 <<- EOF
	$(echo a)
	EOF
	passhash=`awk -F ':' '/^foo:/ {print $2}' $HOME/master.passwd`
	atf_check -s exit:0 -o inline:$passhash \
		$(atf_get_srcdir)/crypt $passhash "a"
	atf_check -s exit:0 ${PW} usermod foo -h - <<- EOF
	$(echo b)
	EOF
	atf_check -s exit:0 -o match:"^foo:\*:.*" \
		grep "^foo" ${HOME}/master.passwd
	atf_check -e inline:"pw: Bad file descriptor 'a': invalid\n" \
		-s exit:64 ${PW} usermod foo -h a <<- EOF
	$(echo a)
	EOF
}

atf_test_case user_mod_H
user_mod_H_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:0 ${PW} usermod foo -H 0 <<- EOF
	$(echo a)
	EOF
	atf_check -s exit:0 -o match:"^foo:a:.*" \
		grep "^foo" ${HOME}/master.passwd
	atf_check -s exit:64 -e inline:"pw: -H expects a file descriptor\n" \
		${PW} usermod foo -H -
}

atf_test_case user_mod_renamehome
user_mod_renamehome_body() {
	populate_root_etc_skel

	mkdir -p ${HOME}/home
	atf_check -s exit:0 ${RPW} useradd foo -m
	test -d ${HOME}/home/foo || atf_fail "Directory not created"
	atf_check -s exit:0 ${RPW} usermod foo -l bar -d /home/bar -m
	test -d ${HOME}/home/bar || atf_fail "Directory not created"
}

atf_test_case user_mod_uid
user_mod_uid_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:0 ${PW} usermod foo -u 5000
}

atf_test_case user_mod_w_error
user_mod_w_error_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:1 -e match:"pw: Invalid value for default password" \
		${PW} usermod foo -w invalid_value
}

atf_test_case user_mod_w_no
user_mod_w_no_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:0 ${PW} usermod foo -w no
	atf_check -s exit:0 -o match:"^foo:\*" grep "^foo:" $HOME/master.passwd
}

atf_test_case user_mod_w_none
user_mod_w_none_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:0 ${PW} usermod foo -w none
	atf_check -s exit:0 -o match:"^foo::" grep "^foo:" $HOME/master.passwd
}

atf_test_case user_mod_w_random
user_mod_w_random_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	password=`${PW} usermod foo -w random | cat`
	passhash=`awk -F ':' '/^foo:/ {print $2}' $HOME/master.passwd`
	atf_check -s exit:0 -o inline:$passhash \
		$(atf_get_srcdir)/crypt $passhash "$password"
}

atf_test_case user_mod_w_yes
user_mod_w_yes_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:0 ${PW} usermod foo -w yes
	passhash=`awk -F ':' '/^foo:/ {print $2}' $HOME/master.passwd`
	atf_check -s exit:0 -o inline:$passhash \
		$(atf_get_srcdir)/crypt $passhash "foo"
}

atf_test_case user_mod_m
user_mod_m_body() {
	populate_root_etc_skel

	mkdir -p ${HOME}/home
	mkdir -p ${HOME}/skel
	echo "entry" > ${HOME}/skel/.file
	atf_check -s exit:0 ${RPW} useradd foo
	! test -d ${HOME}/home/foo || atf_fail "Directory should not have been created"
	atf_check -s exit:0 ${RPW} usermod foo -m -k /skel
	test -d ${HOME}/home/foo || atf_fail "Directory should have been created"
	test -f ${HOME}/home/foo/.file || atf_fail "Skell files not added"
	echo "entry" > ${HOME}/skel/.file2
	atf_check -s exit:0 ${RPW} usermod foo -m -k /skel
	test -f ${HOME}/home/foo/.file2 || atf_fail "Skell files not added"
	echo > ${HOME}/home/foo/.file2
	atf_check -s exit:0 ${RPW} usermod foo -m -k /skel
	atf_check -s exit:0 -o inline:"\n" cat ${HOME}/home/foo/.file2
}


atf_init_test_cases() {
	atf_add_test_case user_mod
	atf_add_test_case user_mod_noupdate
	atf_add_test_case user_mod_comments
	atf_add_test_case user_mod_comments_noupdate
	atf_add_test_case user_mod_comments_invalid
	atf_add_test_case user_mod_comments_invalid_noupdate
	atf_add_test_case user_mod_nogroups
	atf_add_test_case user_mod_rename
	atf_add_test_case user_mod_name_noupdate
	atf_add_test_case user_mod_rename_too_long
	atf_add_test_case user_mod_rename_multigroups
	atf_add_test_case user_mod_h
	atf_add_test_case user_mod_H
	atf_add_test_case user_mod_renamehome
	atf_add_test_case user_mod_uid
	atf_add_test_case user_mod_w_error
	atf_add_test_case user_mod_w_no
	atf_add_test_case user_mod_w_none
	atf_add_test_case user_mod_w_random
	atf_add_test_case user_mod_w_yes
	atf_add_test_case user_mod_m
}
