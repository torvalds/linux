# $FreeBSD$

# Import helper functions
. $(atf_get_srcdir)/helper_functions.shin

# Test add user
atf_test_case user_add
user_add_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd test
	atf_check -s exit:0 -o match:"^test:.*" \
		grep "^test:.*" $HOME/master.passwd
}

# Test add user with option -N
atf_test_case user_add_noupdate
user_add_noupdate_body() {
	populate_etc_skel

	atf_check -s exit:0 -o match:"^test:.*" ${PW} useradd test -N
	atf_check -s exit:1 -o empty grep "^test:.*" $HOME/master.passwd
}

# Test add user with comments
atf_test_case user_add_comments
user_add_comments_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd test -c 'Test User,work!,123,user@example.com'
	atf_check -s exit:0 -o match:'^test:.*:Test User,work!,123,user@example.com:' \
		grep '^test:.*:Test User,work!,123,user@example.com:' $HOME/master.passwd
}

# Test add user with comments and option -N
atf_test_case user_add_comments_noupdate
user_add_comments_noupdate_body() {
	populate_etc_skel

	atf_check -s exit:0 -o match:"^test:.*:Test User,work,123,456:" \
		${PW} useradd test -c "Test User,work,123,456" -N
	atf_check -s exit:1 -o empty grep "^test:.*" $HOME/master.passwd
}

# Test add user with invalid comments
atf_test_case user_add_comments_invalid
user_add_comments_invalid_body() {
	populate_etc_skel

	atf_check -s exit:65 -e match:"invalid character" \
		${PW} useradd test -c "Test User,work,123:456,456"
	atf_check -s exit:1 -o empty \
		grep "^test:.*:Test User,work,123:456,456:" $HOME/master.passwd
}

# Test add user with invalid comments and option -N
atf_test_case user_add_comments_invalid_noupdate
user_add_comments_invalid_noupdate_body() {
	populate_etc_skel

	atf_check -s exit:65 -e match:"invalid character" \
		${PW} useradd test -c "Test User,work,123:456,456" -N
	atf_check -s exit:1 -o empty grep "^test:.*" $HOME/master.passwd
}

# Test add user with alternate homedir
atf_test_case user_add_homedir
user_add_homedir_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd test -d /foo/bar
	atf_check -s exit:0 -o match:"^test:\*:.*::0:0:User &:/foo/bar:.*" \
		${PW} usershow test
}

# Test add user with account expiration as an epoch date
atf_test_case user_add_account_expiration_epoch
user_add_account_expiration_epoch_body() {
	populate_etc_skel

	DATE=`date -j -v+1d "+%s"`
	atf_check -s exit:0 ${PW} useradd test -e ${DATE}
	atf_check -s exit:0 -o match:"^test:\*:.*::0:${DATE}:.*" \
		${PW} usershow test
}

# Test add user with account expiration as a DD-MM-YYYY date
atf_test_case user_add_account_expiration_date_numeric
user_add_account_expiration_date_numeric_body() {
	populate_etc_skel

	DATE=`date -j -v+1d "+%d-%m-%Y"`
	EPOCH=`date -j -f "%d-%m-%Y %H:%M:%S" "${DATE} 00:00:00" "+%s"`
	atf_check -s exit:0 ${PW} useradd test -e ${DATE}
	atf_check -s exit:0 -o match:"^test:\*:.*::0:${EPOCH}:User &:.*" \
		${PW} usershow test
}

# Test add user with account expiration as a DD-MM-YYYY date
atf_test_case user_add_account_expiration_date_month
user_add_account_expiration_date_month_body() {
	populate_etc_skel

	DATE=`date -j -v+1d "+%d-%b-%Y"`
	EPOCH=`date -j -f "%d-%b-%Y %H:%M:%S" "${DATE} 00:00:00" "+%s"`
	atf_check -s exit:0 ${PW} useradd test -e ${DATE}
	atf_check -s exit:0 -o match:"^test:\*:.*::0:${EPOCH}:User &:.*" \
		${PW} usershow test
}

# Test add user with account expiration as a relative date
atf_test_case user_add_account_expiration_date_relative
user_add_account_expiration_date_relative_body() {
	populate_etc_skel

	EPOCH=`date -j -v+13m "+%s"`
	BUF=`expr $EPOCH + 5`
	atf_check -s exit:0 ${PW} useradd test -e +13o
	TIME=`${PW} usershow test | awk -F ':' '{print $7}'`
	[ ! -z $TIME -a $TIME -ge $EPOCH -a $TIME -lt $BUF ] || \
		atf_fail "Expiration time($TIME) was not within $EPOCH - $BUF seconds."
}

# Test add user with password expiration as an epoch date
atf_test_case user_add_password_expiration_epoch
user_add_password_expiration_epoch_body() {
	populate_etc_skel

	DATE=`date -j -v+1d "+%s"`
	atf_check -s exit:0 ${PW} useradd test -p ${DATE}
	atf_check -s exit:0 -o match:"^test:\*:.*::${DATE}:0:.*" \
		${PW} usershow test
}

# Test add user with password expiration as a DD-MM-YYYY date
atf_test_case user_add_password_expiration_date_numeric
user_add_password_expiration_date_numeric_body() {
	populate_etc_skel

	DATE=`date -j -v+1d "+%d-%m-%Y"`
	EPOCH=`date -j -f "%d-%m-%Y %H:%M:%S" "${DATE} 00:00:00" "+%s"`
	atf_check -s exit:0 ${PW} useradd test -p ${DATE}
	atf_check -s exit:0 -o match:"^test:\*:.*::${EPOCH}:0:User &:.*" \
		${PW} usershow test
}

# Test add user with password expiration as a DD-MMM-YYYY date
atf_test_case user_add_password_expiration_date_month
user_add_password_expiration_date_month_body() {
	populate_etc_skel

	DATE=`date -j -v+1d "+%d-%b-%Y"`
	EPOCH=`date -j -f "%d-%b-%Y %H:%M:%S" "${DATE} 00:00:00" "+%s"`
	atf_check -s exit:0 ${PW} useradd test -p ${DATE}
	atf_check -s exit:0 -o match:"^test:\*:.*::${EPOCH}:0:User &:.*" \
		${PW} usershow test
}

# Test add user with password expiration as a relative date
atf_test_case user_add_password_expiration_date_relative
user_add_password_expiration_date_relative_body() {
	populate_etc_skel

	EPOCH=`date -j -v+13m "+%s"`
	BUF=`expr $EPOCH + 5`
	atf_check -s exit:0 ${PW} useradd test -p +13o
	TIME=`${PW} usershow test | awk -F ':' '{print $6}'`
	[ ! -z $TIME -a $TIME -ge $EPOCH -a $TIME -lt $BUF ] || \
		atf_fail "Expiration time($TIME) was not within $EPOCH - $BUF seconds."
}

atf_test_case user_add_name_too_long
user_add_name_too_long_body() {
	populate_etc_skel
	atf_check -e match:"too long" -s exit:64 \
		${PW} useradd name_very_vert_very_very_very_long
}

atf_test_case user_add_name_with_spaces
user_add_name_with_spaces_body() {
	populate_etc_skel
	atf_check -s exit:65 -e match:"invalid character" \
		  ${PW} useradd 'test user'
	atf_check -s exit:1 -o empty grep "^test user:.*" $HOME/master.passwd
	# Try again with -n which uses a slightly different code path.
	atf_check -s exit:65 -e match:"invalid character" \
		  ${PW} useradd -n 'test user'
	atf_check -s exit:1 -o empty grep "^test user:.*" $HOME/master.passwd
}

atf_test_case user_add_name_with_spaces_and_gid_specified
user_add_name_with_spaces_and_gid_specified_body() {
	populate_etc_skel
	gid=12345
	user_name="test user"
	# pw useradd should fail because of the space in the user
	# name, not because the group doesn't exist.
	atf_check -s exit:65 -e match:"invalid character" \
		  ${PW} useradd "${user_name}" -g ${gid}
	atf_check -s exit:1 -o empty grep "^${user_name}:.*" $HOME/master.passwd
	# Try again with -n which uses a slightly different code path.
	atf_check -s exit:65 -e match:"invalid character" \
		  ${PW} useradd -n "${user_name}" -g ${gid}
	atf_check -s exit:1 -o empty grep "^${user_name}:.*" $HOME/master.passwd
	# Make sure the user isn't added even if the group exists
	atf_check -s exit:0 ${PW} groupadd blafasel -g ${gid}
	atf_check -s exit:65 -e match:"invalid character" \
		  ${PW} useradd "${user_name}" -g ${gid}
	atf_check -s exit:1 -o empty grep "^${user_name}:.*" $HOME/master.passwd
	# Try again with the -n option.
	atf_check -s exit:65 -e match:"invalid character" \
		  ${PW} useradd -n "${user_name}" -g ${gid}
	atf_check -s exit:1 -o empty grep "^${user_name}:.*" $HOME/master.passwd
}

atf_test_case user_add_expiration
user_add_expiration_body() {
	populate_etc_skel

	atf_check -s exit:0 \
		${PW} useradd foo -e 20-03-2037
	atf_check -o inline:"foo:*:1001:1001::0:2121120000:User &:/home/foo:/bin/sh\n" \
		-s exit:0 grep "^foo" ${HOME}/master.passwd
	atf_check -s exit:0 ${PW} userdel foo
	atf_check -s exit:0 \
		${PW} useradd foo -e 20-03-37
	atf_check -o inline:"foo:*:1001:1001::0:2121120000:User &:/home/foo:/bin/sh\n" \
		-s exit:0 grep "^foo" ${HOME}/master.passwd
	atf_check -s exit:0 ${PW} userdel foo
	atf_check -s exit:0 \
		${PW} useradd foo -e 20-Mar-2037
	atf_check -o inline:"foo:*:1001:1001::0:2121120000:User &:/home/foo:/bin/sh\n" \
		-s exit:0 grep "^foo" ${HOME}/master.passwd
	atf_check -s exit:0 ${PW} userdel foo
	atf_check -e inline:"pw: Invalid date\n" -s exit:1 \
		${PW} useradd foo -e 20-Foo-2037
	atf_check -e inline:"pw: Invalid date\n" -s exit:1 \
		${PW} useradd foo -e 20-13-2037
	atf_check -s exit:0 ${PW} useradd foo -e "12:00 20-03-2037"
	atf_check -s exit:0 ${PW} userdel foo
	atf_check -e inline:"pw: Invalid date\n" -s exit:1 \
		${PW} useradd foo -e "12 20-03-2037"
	atf_check -s exit:0 ${PW} useradd foo -e "20-03-2037	12:00"
	atf_check -s exit:0 ${PW} userdel foo
}

atf_test_case user_add_invalid_user_entry
user_add_invalid_user_entry_body() {
	touch ${HOME}/master.passwd
	touch ${HOME}/group

	pwd_mkdb -p -d ${HOME} ${HOME}/master.passwd || \
		atf_fail "generate passwd from master.passwd"
	atf_check -s exit:0 ${PW} useradd foo
	echo "foo1:*:1002" >> ${HOME}/master.passwd
	atf_check -s exit:1 -e match:"Invalid user entry" ${PW} useradd foo2
}

atf_test_case user_add_invalid_group_entry
user_add_invalid_group_entry_body() {
	touch ${HOME}/master.passwd
	touch ${HOME}/group

	pwd_mkdb -p -d ${HOME} ${HOME}/master.passwd || \
		atf_fail "generate passwd from master.passwd"
	atf_check -s exit:0 ${PW} useradd foo
	echo 'foo1:*:1002' >> group
	atf_check -s exit:1 -e match:"Invalid group entry" ${PW} useradd foo2
}

atf_test_case user_add_password_from_h
user_add_password_from_h_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo -h 0 <<-EOF
	$(echo mypassword)
	EOF
	passhash=`awk -F ':' '/^foo:/ {print $2}' $HOME/master.passwd`
	atf_check -s exit:0 -o inline:$passhash \
		$(atf_get_srcdir)/crypt $passhash "mypassword"
}

atf_test_case user_add_R
user_add_R_body() {
	populate_root_etc_skel

	atf_check -s exit:0 ${RPW} useradd foo
	atf_check -s exit:0 ${RPW} useradd bar -m
	test -d ${HOME}/home || atf_fail "Home parent directory not created"
	test -d ${HOME}/home/bar || atf_fail "Directory not created"
	atf_check -s exit:0 ${RPW} userdel bar
	test -d ${HOME}/home/bar || atf_fail "Directory removed"
	atf_check -s exit:0 ${RPW} useradd bar
	atf_check -s exit:0 ${RPW} userdel bar -r
	[ ! -d ${HOME}/home/bar ] || atf_fail "Directory not removed"
}

atf_test_case user_add_R_symlink
user_add_R_symlink_body() {
	populate_root_etc_skel

	mkdir ${HOME}/usr
	atf_check -s exit:0 ${RPW} useradd foo -m
	test -d ${HOME}/usr/home || atf_fail "Home parent directory not created"
	test -h ${HOME}/home || atf_fail "/home directory is not a symlink"
	atf_check -s exit:0 -o inline:"usr/home\n" readlink ${HOME}/home
}

atf_test_case user_add_skel
user_add_skel_body() {
	populate_root_etc_skel

	mkdir ${HOME}/skel
	echo "a" > ${HOME}/skel/.a
	echo "b" > ${HOME}/skel/b
	mkdir ${HOME}/skel/c
	mkdir ${HOME}/skel/c/d
	mkdir ${HOME}/skel/dot.plop
	echo "c" > ${HOME}/skel/c/d/dot.c
	mkdir ${HOME}/home
	ln -sf /nonexistent ${HOME}/skel/c/foo
	atf_check -s exit:0 ${RPW} useradd foo -k /skel -m
	test -d ${HOME}/home/foo || atf_fail "Directory not created"
	test -f ${HOME}/home/foo/.a || atf_fail "File not created"
	atf_check -o file:${HOME}/skel/.a -s exit:0 cat ${HOME}/home/foo/.a
	atf_check -o file:${HOME}/skel/b -s exit:0 cat ${HOME}/home/foo/b
	test -d ${HOME}/home/foo/c || atf_fail "Dotted directory in skel not copied"
	test -d ${HOME}/home/foo/.plop || atf_fail "Directory in skell not created"
	atf_check -o inline:"/nonexistent\n" -s ignore readlink -f ${HOME}/home/foo/c/foo
	atf_check -o file:${HOME}/skel/c/d/dot.c -s exit:0 cat ${HOME}/home/foo/c/d/.c
}

atf_test_case user_add_uid0
user_add_uid0_body() {
	populate_etc_skel
	atf_check -e inline:"pw: WARNING: new account \`foo' has a uid of 0 (superuser access!)\n" \
		-s exit:0 ${PW} useradd foo -u 0 -g 0 -d /root -s /bin/sh -c "Bourne-again Superuser" -o
	atf_check \
		-o inline:"foo:*:0:0::0:0:Bourne-again Superuser:/root:/bin/sh\n" \
		-s exit:0 ${PW} usershow foo
}

atf_test_case user_add_uid_too_large
user_add_uid_too_large_body() {
	populate_etc_skel
	atf_check -s exit:64 -e inline:"pw: Bad id '9999999999999': too large\n" \
		${PW} useradd -n test1 -u 9999999999999
}

atf_test_case user_add_bad_shell
user_add_bad_shell_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo -s sh
	atf_check -s exit:78 -e ignore ${PW} useradd bar -s badshell
}

atf_test_case user_add_already_exists
user_add_already_exists_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo
	atf_check -s exit:65 \
		-e inline:"pw: login name \`foo' already exists\n" \
		${PW} useradd foo
}

atf_test_case user_add_w_error
user_add_w_error_body() {
	populate_etc_skel

	atf_check -s exit:1 -e match:"pw: Invalid value for default password" \
		${PW} useradd foo -w invalid_value
}

atf_test_case user_add_w_no
user_add_w_no_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo -w no
	atf_check -s exit:0 -o match:"^foo:\*" grep "^foo:" $HOME/master.passwd
}

atf_test_case user_add_w_none
user_add_w_none_body() {
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd foo -w none
	atf_check -s exit:0 -o match:"^foo::" grep "^foo:" $HOME/master.passwd
}

atf_test_case user_add_w_random
user_add_w_random_body() {
	populate_etc_skel

	password=`${PW} useradd foo -w random | cat`
	passhash=`awk -F ':' '/^foo:/ {print $2}' $HOME/master.passwd`
	atf_check -s exit:0 -o inline:$passhash \
		$(atf_get_srcdir)/crypt $passhash "$password"
}

atf_test_case user_add_w_yes
user_add_w_yes_body() {
	populate_etc_skel
	password=`${PW} useradd foo -w random | cat`
	passhash=`awk -F ':' '/^foo:/ {print $2}' $HOME/master.passwd`
	atf_check -s exit:0 -o inline:$passhash \
		$(atf_get_srcdir)/crypt $passhash "$password"
}

atf_test_case user_add_with_pw_conf
user_add_with_pw_conf_body()
{
	populate_etc_skel
	atf_check -s exit:0 \
		${PW} useradd -D -C ${HOME}/pw.conf \
		-u 2000,32767 -i 2000,32767
	atf_check -s exit:0 \
		-o inline:"minuid = 2000\nmaxuid = 32767\nmingid = 2000\nmaxgid = 32767\n" \
		grep "^m.*id =" ${HOME}/pw.conf
	atf_check -s exit:0 \
		${PW} useradd foo -C ${HOME}/pw.conf
}

atf_test_case user_add_defaultgroup
user_add_defaultgroup_body()
{
	populate_etc_skel
	echo 'defaultgroup = "plop"' > ${HOME}/pw.conf
	atf_check -s exit:0 \
		${PW} groupadd plop -g 442
	atf_check -s exit:0 \
		${PW} useradd foo -C ${HOME}/pw.conf
	atf_check -s exit:0 \
		-o inline:"foo:*:1001:442::0:0:User &:/home/foo:/bin/sh\n" \
		${PW} usershow foo
}

atf_test_case user_add_conf_defaultpasswd
user_add_conf_defaultpasswd_body()
{
	populate_etc_skel

	atf_check -s exit:0 ${PW} useradd -D -w no
	atf_check -o inline:"defaultpasswd = \"no\"\n" \
	    grep defaultpasswd ${HOME}/pw.conf
	atf_check -s exit:0 ${PW} useradd -D -w none
	atf_check -o inline:"defaultpasswd = \"none\"\n" \
	    grep defaultpasswd ${HOME}/pw.conf
	atf_check -s exit:0 ${PW} useradd -D -w random
	atf_check -o inline:"defaultpasswd = \"random\"\n" \
	    grep defaultpasswd ${HOME}/pw.conf
	atf_check -s exit:0 ${PW} useradd -D -w yes
	atf_check -o inline:"defaultpasswd = \"yes\"\n" \
	    grep defaultpasswd ${HOME}/pw.conf
}

atf_init_test_cases() {
	atf_add_test_case user_add
	atf_add_test_case user_add_noupdate
	atf_add_test_case user_add_comments
	atf_add_test_case user_add_comments_noupdate
	atf_add_test_case user_add_comments_invalid
	atf_add_test_case user_add_comments_invalid_noupdate
	atf_add_test_case user_add_homedir
	atf_add_test_case user_add_account_expiration_epoch
	atf_add_test_case user_add_account_expiration_date_numeric
	atf_add_test_case user_add_account_expiration_date_month
	atf_add_test_case user_add_account_expiration_date_relative
	atf_add_test_case user_add_password_expiration_epoch
	atf_add_test_case user_add_password_expiration_date_numeric
	atf_add_test_case user_add_password_expiration_date_month
	atf_add_test_case user_add_password_expiration_date_relative
	atf_add_test_case user_add_name_too_long
	atf_add_test_case user_add_name_with_spaces
	atf_add_test_case user_add_name_with_spaces_and_gid_specified
	atf_add_test_case user_add_expiration
	atf_add_test_case user_add_invalid_user_entry
	atf_add_test_case user_add_invalid_group_entry
	atf_add_test_case user_add_password_from_h
	atf_add_test_case user_add_R
	atf_add_test_case user_add_R_symlink
	atf_add_test_case user_add_skel
	atf_add_test_case user_add_uid0
	atf_add_test_case user_add_uid_too_large
	atf_add_test_case user_add_bad_shell
	atf_add_test_case user_add_already_exists
	atf_add_test_case user_add_w_error
	atf_add_test_case user_add_w_no
	atf_add_test_case user_add_w_none
	atf_add_test_case user_add_w_random
	atf_add_test_case user_add_w_yes
	atf_add_test_case user_add_with_pw_conf
	atf_add_test_case user_add_defaultgroup

	atf_add_test_case user_add_conf_defaultpasswd
}
