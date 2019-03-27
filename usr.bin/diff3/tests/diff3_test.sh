# $FreeBSD$

atf_test_case diff3
atf_test_case diff3_lesssimple

diff3_body()
{
	atf_check -o file:$(atf_get_srcdir)/1.out \
		diff3 $(atf_get_srcdir)/1.txt $(atf_get_srcdir)/2.txt $(atf_get_srcdir)/3.txt

	atf_check -o file:$(atf_get_srcdir)/2.out \
		diff3 -e $(atf_get_srcdir)/1.txt $(atf_get_srcdir)/2.txt $(atf_get_srcdir)/3.txt

	atf_check -o file:$(atf_get_srcdir)/3.out \
		diff3 -E -L 1 -L 2 -L 3 $(atf_get_srcdir)/1.txt $(atf_get_srcdir)/2.txt $(atf_get_srcdir)/3.txt

	atf_check -o file:$(atf_get_srcdir)/4.out \
		diff3 -X -L 1 -L 2 -L 3 $(atf_get_srcdir)/1.txt $(atf_get_srcdir)/2.txt $(atf_get_srcdir)/3.txt

	atf_check -o file:$(atf_get_srcdir)/5.out \
		diff3 -x $(atf_get_srcdir)/1.txt $(atf_get_srcdir)/2.txt $(atf_get_srcdir)/3.txt

	atf_check -o file:$(atf_get_srcdir)/6.out \
		diff3 -3 $(atf_get_srcdir)/1.txt $(atf_get_srcdir)/2.txt $(atf_get_srcdir)/3.txt

	atf_check -o file:$(atf_get_srcdir)/7.out \
		diff3 -i $(atf_get_srcdir)/1.txt $(atf_get_srcdir)/2.txt $(atf_get_srcdir)/3.txt

#	atf_check -o file:$(atf_get_srcdir)/8.out \
#		diff3 -A -L 1 -L 2 -L 3 $(atf_get_srcdir)/1.txt $(atf_get_srcdir)/2.txt $(atf_get_srcdir)/3.txt

#	atf_check -s exit:1 -o file:$(atf_get_srcdir)/9.out \
#		diff3 -m -L 1 -L 2 -L 3 $(atf_get_srcdir)/1.txt $(atf_get_srcdir)/2.txt $(atf_get_srcdir)/3.txt
}

diff3_lesssimple_body()
{
	atf_check -s exit:1 -o file:$(atf_get_srcdir)/10.out \
		diff3 -m -L 1 -L 2 -L 3 $(atf_get_srcdir)/4.txt $(atf_get_srcdir)/5.txt $(atf_get_srcdir)/6.txt
}

atf_init_test_cases()
{
	atf_add_test_case diff3
#	atf_add_test_case diff3_lesssimple
}
