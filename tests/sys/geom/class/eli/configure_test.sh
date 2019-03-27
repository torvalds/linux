# $FreeBSD$

. $(atf_get_srcdir)/conf.sh

atf_test_case configure_b_B cleanup
configure_b_B_head()
{
	atf_set "descr" "geli configure -b will set the BOOT flag"
	atf_set "require.user" "root"
}
configure_b_B_body()
{
	geli_test_setup

	sectors=100
	md=$(attach_md -t malloc -s `expr $sectors + 1`)

	atf_check geli init -B none -P -K /dev/null ${md}

	atf_check -s exit:0 -o match:'flags: 0x0$' geli dump ${md}

	atf_check geli init -B none -b -P -K /dev/null ${md}

	atf_check -s exit:0 -o match:'flags: 0x2$' geli dump ${md}

	atf_check geli configure -B ${md}

	atf_check -s exit:0 -o match:'flags: 0x0$' geli dump ${md}

	atf_check geli configure -b ${md}

	atf_check -s exit:0 -o match:'flags: 0x2$' geli dump ${md}

	atf_check geli attach -p -k /dev/null ${md}

	atf_check -s exit:0 -o match:'^Flags: .*BOOT' geli list ${md}.eli

	atf_check geli configure -B ${md}

	atf_check -o not-match:'^Flags: .*BOOT' geli list ${md}.eli

	atf_check -s exit:0 -o match:'flags: 0x0$' geli dump ${md}

	atf_check geli configure -b ${md}

	atf_check -s exit:0 -o match:'^Flags: .*BOOT' geli list ${md}.eli

	atf_check -s exit:0 -o match:'flags: 0x2$' geli dump ${md}

	atf_check geli detach ${md}
}
configure_b_B_cleanup()
{
	geli_test_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case configure_b_B
}
