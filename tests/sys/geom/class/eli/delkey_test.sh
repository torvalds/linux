#!/bin/sh
# $FreeBSD$

. $(atf_get_srcdir)/conf.sh

atf_test_case delkey cleanup
delkey_head()
{
	atf_set "descr" "geli delkey can destroy the master key"
	atf_set "require.user" "root"
}
delkey_body()
{
	geli_test_setup

	sectors=100
	md=$(attach_md -t malloc -s `expr $sectors + 1`)

	atf_check dd if=/dev/random of=keyfile1 bs=512 count=16 status=none
	atf_check dd if=/dev/random of=keyfile2 bs=512 count=16 status=none
	atf_check dd if=/dev/random of=keyfile3 bs=512 count=16 status=none
	atf_check dd if=/dev/random of=keyfile4 bs=512 count=16 status=none

	atf_check geli init -B none -P -K keyfile1 ${md}
	atf_check geli attach -p -k keyfile1 ${md}
	atf_check -s exit:0 -o ignore geli setkey -n 1 -P -K keyfile2 ${md}

	# Remove key 0 for attached provider.
	atf_check geli delkey -n 0 ${md}
	atf_check geli detach ${md}

	# We cannot use keyfile1 anymore.
	atf_check -s not-exit:0 -e match:"Wrong key" \
		geli attach -p -k keyfile1 ${md}

	# Attach with key 1.
	atf_check geli attach -p -k keyfile2 ${md}

	# We cannot remove last key without -f option (for attached provider).
	atf_check -s not-exit:0 -e match:"This is the last Master Key" \
		geli delkey -n 1 ${md}

	# Remove last key for attached provider.
	atf_check geli delkey -f -n 1 ${md}

	# If there are no valid keys, but provider is attached, we can save situation.
	atf_check -s exit:0 -o ignore geli setkey -n 0 -P -K keyfile3 ${md}
	atf_check geli detach ${md}

	# We cannot use keyfile2 anymore.
	atf_check -s not-exit:0 -e match:"Wrong key" \
		geli attach -p -k keyfile2 ${md}

	# Attach with key 0.
	atf_check geli attach -p -k keyfile3 ${md}

	# Setup key 1.
	atf_check -s exit:0 -o ignore geli setkey -n 1 -P -K keyfile4 ${md}
	atf_check geli detach ${md}

	# Remove key 1 for detached provider.
	atf_check geli delkey -n 1 ${md}

	# We cannot use keyfile4 anymore.
	atf_check -s not-exit:0 -e match:"Wrong key" \
		geli attach -p -k keyfile4 ${md}

	# We cannot remove last key without -f option (for detached provider).
	atf_check -s not-exit:0 -e match:"This is the last Master Key" \
		geli delkey -n 0 ${md}

	# Remove last key for detached provider.
	atf_check geli delkey -f -n 0 ${md}

	# We cannot use keyfile3 anymore.
	atf_check -s not-exit:0 -e match:"No valid keys" \
		geli attach -p -k keyfile3 ${md}
}
delkey_cleanup()
{
	geli_test_cleanup
}

atf_test_case delkey_readonly cleanup
delkey_readonly_head()
{
	atf_set "descr" "geli delkey cannot work on a read-only provider"
	atf_set "require.user" "root"
}
delkey_readonly_body()
{
	geli_test_setup

	sectors=100
	md=$(attach_md -t malloc -s `expr $sectors + 1`)
	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none

	atf_check geli init -B none -P -K keyfile ${md}
	atf_check geli attach -r -p -k keyfile ${md}

	atf_check -s not-exit:0 -e match:"read-only" geli delkey -n 0 ${md}
	# Even with -f (force) it should still fail
	atf_check -s not-exit:0 -e match:"read-only" geli delkey -f -n 0 ${md}
}
delkey_readonly_cleanup()
{
	geli_test_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case delkey
	atf_add_test_case delkey_readonly
}
