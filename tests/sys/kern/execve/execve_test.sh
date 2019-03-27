
bad_interp_len_head()
{
	atf_set "descr" "Bad interpreter length"
}
bad_interp_len_body()
{
	atf_check -s exit:1 -e 'match:No such file or directory' -o empty \
	    -x "cd $(atf_get_srcdir) && ./execve_helper bad_interp_len"
}

empty_head()
{
	atf_set "descr" "Empty file"
}
empty_body()
{
	atf_check -s exit:1 -e 'match:Exec format error' -o empty \
	    -x "cd $(atf_get_srcdir) && ./execve_helper empty"
}

good_aout_head()
{
	atf_set "descr" "Good a.out"
}
good_aout_body()
{
	atf_check -s exit:0 -e empty -o 'match:succeeded' \
	    -x "cd $(atf_get_srcdir) && ./execve_helper ./good_aout"
}

good_script_head()
{
	atf_set "descr" "Good script"
}
good_script_body()
{
	atf_check -s exit:0 -e empty -o 'match:succeeded' \
	    -x "cd $(atf_get_srcdir) && ./execve_helper good_script"
}

non_exist_head()
{
	atf_set "descr" "Non-existent file"
}
non_exist_body()
{
	atf_check -s exit:1 -e 'match:No such file or directory' -o empty \
	    -x "cd $(atf_get_srcdir) && ./execve_helper non_exist"
}

non_exist_shell_head()
{
	atf_set "descr" "Non-existent shell"
}
non_exist_shell_body()
{
	atf_check -s exit:1 -e 'match:No such file or directory' -o empty \
	    -x "cd $(atf_get_srcdir) && ./execve_helper non_exist_shell"
}

script_arg_head()
{
	atf_set "descr" "-x in the shebang"
}
script_arg_body()
{
	atf_check -s exit:0 -e 'match:\+ echo succeeded' -o 'match:succeeded' \
	    -x "cd $(atf_get_srcdir) && ./execve_helper script_arg"
}

script_arg_nospace_head()
{
	atf_set "descr" '-x in the shebang; no space between #! and /bin/sh'
}
script_arg_nospace_body()
{
	atf_check -s exit:0 -e 'match:\+ echo succeeded' -o 'match:succeeded' \
	    -x "cd $(atf_get_srcdir) && ./execve_helper script_arg_nospace"
}

sparse_aout_head()
{
	atf_set "descr" 'Sparse file'
}
sparse_aout_body()
{
	atf_check -s exit:1 -e 'match:Exec format error' -o empty \
	    -x "cd $(atf_get_srcdir) && ./execve_helper sparse_aout"
}

trunc_aout_head()
{
	atf_set "descr" 'Truncated file'
}
trunc_aout_body()
{
	atf_check -s exit:1 -e 'match:Exec format error' -o empty \
	    -x "cd $(atf_get_srcdir) && ./execve_helper trunc_aout"
}

atf_init_test_cases()
{
	atf_add_test_case bad_interp_len
	atf_add_test_case empty
	atf_add_test_case good_aout
	atf_add_test_case good_script
	atf_add_test_case non_exist
	atf_add_test_case non_exist_shell
	atf_add_test_case script_arg
	atf_add_test_case script_arg_nospace
	atf_add_test_case sparse_aout
	atf_add_test_case trunc_aout

}
