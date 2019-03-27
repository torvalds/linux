# $FreeBSD$

atf_test_case nominal
nominal_head()
{
	atf_set "descr" "Basic tests on timeout(1) utility"
}

nominal_body()
{
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		timeout 5 true
}

atf_test_case time_unit
time_unit_head()
{
	atf_set "descr" "Test parsing the default time unit"
}

time_unit_body()
{
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		timeout 1d true

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		timeout 1h true

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		timeout 1m true

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		timeout 1s true
}

atf_test_case no_timeout
no_timeout_head()
{
	atf_set "descr" "Test disabled timeout"
}

no_timeout_body()
{
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		timeout 0 true
}

atf_test_case exit_numbers
exit_numbers_head()
{
	atf_set "descr" "Test exit numbers"
}

exit_numbers_body()
{
	atf_check \
		-o empty \
		-e empty \
		-s exit:2 \
		-x timeout 5 sh -c \'exit 2\'

	atf_check \
		-o empty \
		-e empty \
		-s exit:124 \
		timeout .1 sleep 1

	# With preserv status exit should be 128 + TERM aka 143
	atf_check \
		-o empty \
		-e empty \
		-s exit:143 \
		timeout --preserve-status .1 sleep 10

	atf_check \
		-o empty \
		-e empty \
		-s exit:124 \
		timeout -s1 -k1 .1 sleep 10

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		-x sh -c 'trap "" CHLD; exec timeout 10 true'
}

atf_test_case with_a_child
with_a_child_head()
{
	atf_set "descr" "When starting with a child (coreutils bug#9098)"
}

with_a_child_body()
{
	out=$(sleep .1 & exec timeout .5 sh -c 'sleep 2; echo foo')
	status=$?
	test "$out" = "" && test $status = 124 || atf_fail

}

atf_test_case invalid_timeout
invalid_timeout_head()
{
	atf_set "descr" "Invalid timeout"
}

invalid_timeout_body()
{
	atf_check \
		-o empty \
		-e inline:"timeout: invalid duration\n" \
		-s exit:125 \
		timeout invalid sleep 0

	atf_check \
		-o empty \
		-e inline:"timeout: invalid duration\n" \
		-s exit:125 \
		timeout --kill-after=invalid 1 sleep 0

	atf_check \
		-o empty \
		-e inline:"timeout: invalid duration\n" \
		-s exit:125 \
		timeout 42D sleep 0

	atf_check \
		-o empty \
		-e inline:"timeout: invalid duration\n" \
		-s exit:125 \
		timeout 999999999999999999999999999999999999999999999999999999999999d sleep 0

	atf_check \
		-o empty \
		-e inline:"timeout: invalid duration\n" \
		-s exit:125 \
		timeout 2.34e+5d sleep 0
}

atf_test_case invalid_signal
invalid_signal_head()
{
	atf_set "descr" "Invalid signal"
}

invalid_signal_body()
{
	atf_check \
		-o empty \
		-e inline:"timeout: invalid signal\n" \
		-s exit:125 \
		timeout --signal=invalid 1 sleep 0
}

atf_test_case invalid_command
invalid_command_head()
{
	atf_set "descr" "Invalid command"
}

invalid_command_body()
{
	atf_check \
		-o empty \
		-e inline:"timeout: exec(.): Permission denied\n" \
		-s exit:126 \
		timeout 10 .
}

atf_test_case no_such_command
no_such_command_head()
{
	atf_set "descr" "No such command"
}

no_such_command_body()
{
	atf_check \
		-o empty \
		-e inline:"timeout: exec(enoexists): No such file or directory\n" \
		-s exit:127 \
		timeout 10 enoexists
}

atf_init_test_cases()
{
	atf_add_test_case nominal
	atf_add_test_case time_unit
	atf_add_test_case no_timeout
	atf_add_test_case exit_numbers
	atf_add_test_case with_a_child
	atf_add_test_case invalid_timeout
	atf_add_test_case invalid_signal
	atf_add_test_case invalid_command
	atf_add_test_case no_such_command
}
