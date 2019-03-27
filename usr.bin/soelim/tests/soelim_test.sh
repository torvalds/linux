# $FreeBSD$

atf_test_case stdin
stdin_head()
{
	atf_set "descr" "stdin functionality"
}

stdin_body()
{
	# no file after .so
	atf_check \
		-o inline:".so\n" \
		-e empty \
		-s exit:0 \
		soelim <<-EOF
.so
EOF

	# only space after .so
	atf_check \
		-o inline:".so  	\n" \
		-e empty \
		-s exit:0 \
		soelim <<-EOF
.so  	
EOF

	# explicit stdin
	atf_check \
		-o inline:".so\n" \
		-e empty \
		-s exit:0 \
		soelim - <<-EOF
.so
EOF

	atf_check \
		-o empty \
		-e inline:"soelim: can't open 'afile': No such file or directory\n" \
		-s exit:1 \
		soelim <<-EOF
.so afile
EOF

	atf_check \
		-o inline:".soafile\n" \
		-e empty \
		-s exit:0 \
		soelim <<-EOF
.soafile
EOF

	atf_check \
		-o empty \
		-e inline:"soelim: can't open 'afile': No such file or directory\n" \
		-s exit:1 \
		soelim -C <<-EOF
.soafile
EOF
}

atf_test_case files
files_head()
{
	atf_set "descr" "testing files"
}

files_body()
{
	atf_check \
		-o inline:"This is a test\n" \
		-e inline:"soelim: can't open 'nonexistingfile': No such file or directory\n" \
		-s exit:1 \
		soelim $(atf_get_srcdir)/nonexisting.in

	cp $(atf_get_srcdir)/basic .
	atf_check \
		-o file:$(atf_get_srcdir)/basic.out \
		-e empty \
		-s exit:0 \
		soelim $(atf_get_srcdir)/basic.in

	rm -f basic
	atf_check \
		-o file:$(atf_get_srcdir)/basic.out \
		-e empty \
		-s exit:0 \
		soelim -I$(atf_get_srcdir) $(atf_get_srcdir)/basic.in

	atf_check \
		-o file:$(atf_get_srcdir)/basic-with-space.out \
		-e empty \
		-s exit:0 \
		soelim -I$(atf_get_srcdir) $(atf_get_srcdir)/basic-with-space.in

}

atf_init_test_cases()
{
	atf_add_test_case stdin
	atf_add_test_case files
}
