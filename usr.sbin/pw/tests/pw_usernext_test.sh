# $FreeBSD$

# Import helper functions
. $(atf_get_srcdir)/helper_functions.shin

# Test usernext after adding a random number of new users.
atf_test_case usernext
usernext_body() {
	populate_etc_skel

	CURRENT=`${PW} usernext | sed -e 's/:.*//'`
	RANDOM=`jot -r 1 1 150`
	MAX=`expr ${CURRENT} + ${RANDOM}`
	while [ "${CURRENT}" -lt "${MAX}" ]
	do
		atf_check -s exit:0 ${PW} useradd test${CURRENT}
		CURRENT=`expr ${CURRENT} + 1`
	done
	atf_check -s exit:0 -o match:"${CURRENT}:${CURRENT}" \
		${PW} usernext
}

# Test usernext when multiple users are added to the same group so 
# that group id doesn't increment at the same pace as new users.
atf_test_case usernext_assigned_group
usernext_assigned_group_body() {
	populate_etc_skel

	CURRENT=`${PW} usernext | sed -e 's/:.*//'`
	CURRENTGID=`${PW} groupnext`
	RANDOM=`jot -r 1 1 150`
	MAX=`expr ${CURRENT} + ${RANDOM}`
	while [ "${CURRENT}" -lt "${MAX}" ]
	do
		atf_check -s exit:0 ${PW} useradd -n test${CURRENT} -g 0
		CURRENT=`expr ${CURRENT} + 1`
	done
	atf_check -s exit:0 -o match:"${CURRENT}:${CURRENTGID}" \
		${PW} usernext
}

atf_init_test_cases() {
	atf_add_test_case usernext
	atf_add_test_case usernext_assigned_group
}
