#!/bin/sh
# $FreeBSD$

. $(atf_get_srcdir)/conf.sh

atf_test_case resize cleanup
resize_head()
{
	atf_set "descr" "geli resize will resize a geli provider"
	atf_set "require.user" "root"
}
resize_body()
{
	geli_test_setup

	BLK=512
	BLKS_PER_MB=2048

	md=$(attach_md -t malloc -s40m)

	# Initialise
	atf_check -s exit:0 -o ignore gpart create -s BSD ${md}
	atf_check -s exit:0 -o ignore gpart add -t freebsd-ufs -s 10m ${md}

	echo secret >tmp.key
	atf_check geli init -Bnone -PKtmp.key ${md}a
	atf_check geli attach -pk tmp.key ${md}a

	atf_check -s exit:0 -o ignore newfs -U ${md}a.eli
	atf_check -s exit:7 -o ignore fsck_ffs -Ffy ${md}a.eli

	# Doing a backup, resize & restore must be forced (with -f) as geli
	# verifies that the provider size in the metadata matches the consumer.

	atf_check geli backup ${md}a tmp.meta
	atf_check geli detach ${md}a.eli
	atf_check -s exit:0 -o match:resized gpart resize -i1 -s 20m ${md}
	atf_check -s not-exit:0 -e ignore geli attach -pktmp.key ${md}a
	atf_check -s not-exit:0 -e ignore geli restore tmp.meta ${md}a
	atf_check geli restore -f tmp.meta ${md}a
	atf_check geli attach -pktmp.key ${md}a
	atf_check -s exit:0 -o ignore growfs -y ${md}a.eli
	atf_check -s exit:7 -o ignore fsck_ffs -Ffy ${md}a.eli

	# Now do the resize properly

	atf_check geli detach ${md}a.eli
	atf_check -s exit:0 -o match:resized gpart resize -i1 -s 30m ${md}
	atf_check geli resize -s20m ${md}a
	atf_check -s not-exit:0 -e match:"Inconsistent provider.*metadata" \
		geli resize -s20m ${md}a
	atf_check geli attach -pktmp.key ${md}a
	atf_check -s exit:0 -o ignore growfs -y ${md}a.eli
	atf_check -s exit:7 -o ignore fsck_ffs -Ffy ${md}a.eli

	atf_check geli detach ${md}a.eli
	atf_check -s exit:0 -o ignore gpart destroy -F $md


	# Verify that the man page example works, changing ada0 to $md,
	# 1g to 20m, 2g to 30m and keyfile to tmp.key, and adding -B none
	# to geli init.

	atf_check -s exit:0 -o ignore gpart create -s GPT $md
	atf_check -s exit:0 -o ignore gpart add -s 20m -t freebsd-ufs -i 1 $md
	atf_check geli init -B none -K tmp.key -P ${md}p1
	atf_check -s exit:0 -o match:resized gpart resize -s 30m -i 1 $md
	atf_check geli resize -s 20m ${md}p1
	atf_check geli attach -k tmp.key -p ${md}p1
}
resize_cleanup()
{
	if [ -f "$TEST_MDS_FILE" ]; then
		while read md; do
			[ -c /dev/${md}a.eli ] && \
				geli detach ${md}a.eli 2>/dev/null
			[ -c /dev/${md}p1.eli ] && \
				geli detach ${md}p1.eli
			[ -c /dev/${md}.eli ] && \
				geli detach ${md}.eli 2>/dev/null
			mdconfig -d -u $md 2>/dev/null
		done < $TEST_MDS_FILE
	fi
}

atf_init_test_cases()
{
	atf_add_test_case resize
}
