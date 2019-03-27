# $FreeBSD$

. $(atf_get_srcdir)/conf.sh

atf_test_case kill cleanup
kill_head()
{
	atf_set "descr" "geli kill will wipe a provider's metadata"
	atf_set "require.user" "root"
}
kill_body()
{
	geli_test_setup

	sectors=100
	md=$(attach_md -t malloc -s `expr $sectors + 1`)

	atf_check dd if=/dev/random of=keyfile1 bs=512 count=16 status=none
	atf_check dd if=/dev/random of=keyfile2 bs=512 count=16 status=none

	atf_check geli init -B none -P -K keyfile1 ${md}
	atf_check geli attach -p -k keyfile1 ${md}
	atf_check -s exit:0 -o ignore geli setkey -n 1 -P -K keyfile2 ${md}

	# Kill attached provider.
	atf_check geli kill ${md}
	sleep 1
	# Provider should be automatically detached.
	if [ -c /dev/${md}.eli ]; then
		atf_fail "Provider did not detach when killed"
	fi

	# We cannot use keyfile1 anymore.
	atf_check -s not-exit:0 -e match:"Cannot read metadata" \
		geli attach -p -k keyfile1 ${md}

	# We cannot use keyfile2 anymore.
	atf_check -s not-exit:0 -e match:"Cannot read metadata" \
		geli attach -p -k keyfile2 ${md}

	atf_check geli init -B none -P -K keyfile1 ${md}
	atf_check -s exit:0 -o ignore \
		geli setkey -n 1 -p -k keyfile1 -P -K keyfile2 ${md}

	# Should be possible to attach with keyfile1.
	atf_check geli attach -p -k keyfile1 ${md}
	atf_check geli detach ${md}

	# Should be possible to attach with keyfile2.
	atf_check geli attach -p -k keyfile2 ${md}
	atf_check geli detach ${md}

	# Kill detached provider.
	atf_check geli kill ${md}

	# We cannot use keyfile1 anymore.
	atf_check -s not-exit:0 -e match:"Cannot read metadata" \
		geli attach -p -k keyfile1 ${md}

	# We cannot use keyfile2 anymore.
	atf_check -s not-exit:0 -e match:"Cannot read metadata" \
		geli attach -p -k keyfile2 ${md}
}
kill_cleanup()
{
	geli_test_cleanup
}

atf_test_case kill_readonly cleanup
kill_readonly_head()
{
	atf_set "descr" "geli kill will not destroy the keys of a readonly provider"
	atf_set "require.user" "root"
}
kill_readonly_body()
{
	geli_test_setup

	sectors=100
	md=$(attach_md -t malloc -s `expr $sectors + 1`)
	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none

	atf_check geli init -B none -P -K keyfile ${md}
	# Attach read-only
	atf_check geli attach -r -p -k keyfile ${md}

	atf_check geli kill ${md}
	# The provider will be detached
	atf_check [ ! -c /dev/${md}.eli ]
	# But its keys should not be destroyed
	atf_check geli attach -p -k keyfile ${md}
}
kill_readonly_cleanup()
{
	geli_test_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case kill
	atf_add_test_case kill_readonly
}
