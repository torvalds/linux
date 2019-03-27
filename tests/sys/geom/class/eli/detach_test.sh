# $FreeBSD$

. $(atf_get_srcdir)/conf.sh

atf_test_case detach_l cleanup
detach_l_head()
{
	atf_set "descr" "geli detach -l will cause a provider to detach on last close"
	atf_set "require.user" "root"
}
detach_l_body()
{
	geli_test_setup

	sectors=100
	md=$(attach_md -t malloc -s `expr $sectors + 1`)

	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none

	atf_check geli init -B none -P -K keyfile ${md}
	atf_check geli attach -p -k keyfile ${md}

	# Be sure it doesn't detach before 'detach -l'.
	atf_check dd if=/dev/${md}.eli of=/dev/null status=none
	sleep 1
	if [ ! -c /dev/${md}.eli ]; then
		atf_fail "provider detached on last close without detach -l"
	fi
	atf_check geli detach -l ${md}
	if [ ! -c /dev/${md}.eli ]; then
		atf_fail "Provider detached before last close"
	fi
	atf_check dd if=/dev/${md}.eli of=/dev/null status=none
	sleep 1
	if [ -c /dev/${md}.eli ]; then
		atf_fail "Provider did not detach on last close"
	fi
}
detach_l_cleanup()
{
	geli_test_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case detach_l
}
