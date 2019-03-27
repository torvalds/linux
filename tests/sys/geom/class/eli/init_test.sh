#!/bin/sh
# $FreeBSD$

. $(atf_get_srcdir)/conf.sh

init_test()
{
	cipher=$1
	secsize=$2
	ealgo=${cipher%%:*}
	keylen=${cipher##*:}

	atf_check -s exit:0 -e ignore \
		geli init -B none -e $ealgo -l $keylen -P -K keyfile \
		-s $secsize ${md}
	atf_check geli attach -p -k keyfile ${md}

	atf_check dd if=rnd of=/dev/${md}.eli bs=${secsize} count=${sectors} \
		status=none

	md_rnd=`dd if=rnd bs=${secsize} count=${sectors} status=none | md5`
	atf_check_equal 0 $?
	md_ddev=`dd if=/dev/${md}.eli bs=${secsize} count=${sectors} status=none | md5`
	atf_check_equal 0 $?
	md_edev=`dd if=/dev/${md} bs=${secsize} count=${sectors} status=none | md5`
	atf_check_equal 0 $?

	if [ ${md_rnd} != ${md_ddev} ]; then
		atf_fail "Miscompare for ealgo=${ealgo} keylen=${keylen} sec=${secsize}"
	fi
	if [ ${md_rnd} == ${md_edev} ]; then
		atf_fail "Data was not encrypted for ealgo=${ealgo} keylen=${keylen} sec=${secsize}"
	fi
}
atf_test_case init cleanup
init_head()
{
	atf_set "descr" "Basic I/O with geli"
	atf_set "require.user" "root"
	atf_set "timeout" 600
}
init_body()
{
	geli_test_setup

	sectors=32

	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none
	atf_check dd if=/dev/random of=rnd bs=$MAX_SECSIZE count=${sectors} \
		status=none
	for_each_geli_config_nointegrity init_test
}
init_cleanup()
{
	geli_test_cleanup
}

atf_test_case init_B cleanup
init_B_head()
{
	atf_set "descr" "init -B can select an alternate backup metadata file"
	atf_set "require.user" "root"
}
init_B_body()
{
	geli_test_setup

	sectors=100

	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none

	md=$(attach_md -t malloc -s $sectors)

	# -B none
	rm -f /var/backups/${md}.eli
	atf_check -s exit:0 -o ignore geli init -B none -P -K keyfile ${md}
	if [ -f /var/backups/${md}.eli ]; then
		atf_fail "geli created a backup file even with -B none"
	fi

	# no -B
	rm -f /var/backups/${md}.eli
	atf_check -s exit:0 -o ignore geli init -P -K keyfile ${md}
	if [ ! -f /var/backups/${md}.eli ]; then
		atf_fail "geli did not create a backup file"
	fi
	atf_check geli clear ${md}
	atf_check -s not-exit:0 -e ignore geli attach -p -k keyfile ${md}
	atf_check -s exit:0 -o ignore geli restore /var/backups/${md}.eli ${md}
	atf_check -s exit:0 -o ignore geli attach -p -k keyfile ${md}
	atf_check geli detach ${md}
	rm -f /var/backups/${md}.eli

	# -B file
	rm -f backupfile
	atf_check -s exit:0 -o ignore \
		geli init -B backupfile -P -K keyfile ${md}
	if [ ! -f backupfile ]; then
		atf_fail "geli init -B did not create a backup file"
	fi
	atf_check geli clear ${md}
	atf_check -s not-exit:0 -e ignore geli attach -p -k keyfile ${md}
	atf_check geli restore backupfile ${md}
	atf_check geli attach -p -k keyfile ${md}
}
init_B_cleanup()
{
	geli_test_cleanup
}

atf_test_case init_J cleanup
init_J_head()
{
	atf_set "descr" "init -J accepts a passfile"
	atf_set "require.user" "root"
}
init_J_body()
{
	geli_test_setup

	sectors=100
	md=$(attach_md -t malloc -s `expr $sectors + 1`)

	atf_check dd if=/dev/random of=keyfile0 bs=512 count=16 status=none
	atf_check dd if=/dev/random of=keyfile1 bs=512 count=16 status=none
	dd if=/dev/random bs=512 count=16 status=none | sha1 > passfile0
	atf_check_equal 0 $?
	dd if=/dev/random bs=512 count=16 status=none | sha1 > passfile1
	atf_check_equal 0 $?

	for iter in -1 0 64; do
		atf_check -s not-exit:0 -e ignore \
			geli init -i ${iter} -B none -J passfile0 -P ${md}
		atf_check -s not-exit:0 -e ignore \
			geli init -i ${iter} -B none -J passfile0 -P -K keyfile0 ${md}
		atf_check geli init -i ${iter} -B none -J passfile0 -K keyfile0 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile0 -p ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -j passfile0 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -j keyfile0 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k passfile0 -p ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -j keyfile0 -k passfile0 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -j keyfile0 -k keyfile0 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -j passfile0 -k passfile0 ${md}
		atf_check -s exit:0 -e ignore \
			geli attach -j passfile0 -k keyfile0 ${md}
		atf_check -s exit:0 -e ignore geli detach ${md}
		atf_check -s exit:0 -e ignore -x \
			"cat keyfile0 | geli attach -j passfile0 -k - ${md}"
		atf_check -s exit:0 -e ignore geli detach ${md}
		atf_check -s exit:0 -e ignore -x \
			"cat passfile0 | geli attach -j - -k keyfile0 ${md}"
		atf_check -s exit:0 -e ignore geli detach ${md}

		atf_check -s not-exit:0 -e ignore \
			geli init -i ${iter} -B none -J passfile0 -J passfile1 -P ${md}
		atf_check -s not-exit:0 -e ignore \
			geli init -i ${iter} -B none -J passfile0 -J passfile1 -P -K keyfile0 -K keyfile1 ${md}
		atf_check -s exit:0 -e ignore \
			geli init -i ${iter} -B none -J passfile0 -J passfile1 -K keyfile0 -K keyfile1 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile0 -p ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile1 -p ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -j passfile0 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -j passfile1 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile0 -k keyfile1 -p ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -j passfile0 -j passfile1 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile0 -j passfile0 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile0 -j passfile1 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile1 -j passfile0 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile1 -j passfile1 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile0 -j passfile0 -j passfile1 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile1 -j passfile0 -j passfile1 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile0 -k keyfile1 -j passfile0 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile0 -k keyfile1 -j passfile1 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile1 -k keyfile0 -j passfile0 -j passfile1 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile0 -k keyfile1 -j passfile1 -j passfile0 ${md}
		atf_check -s not-exit:0 -e ignore \
			geli attach -k keyfile1 -k keyfile0 -j passfile1 -j passfile0 ${md}
		atf_check -s exit:0 -e ignore \
			geli attach -j passfile0 -j passfile1 -k keyfile0 -k keyfile1 ${md}
		atf_check -s exit:0 -e ignore geli detach ${md}
		atf_check -s exit:0 -e ignore -x \
			"cat passfile0 | geli attach -j - -j passfile1 -k keyfile0 -k keyfile1 ${md}"
		atf_check -s exit:0 -e ignore geli detach ${md}
		atf_check -s exit:0 -e ignore -x \
			"cat passfile1 | geli attach -j passfile0 -j - -k keyfile0 -k keyfile1 ${md}"
		atf_check -s exit:0 -e ignore geli detach ${md}
		atf_check -s exit:0 -e ignore -x \
			"cat keyfile0 | geli attach -j passfile0 -j passfile1 -k - -k keyfile1 ${md}"
		atf_check -s exit:0 -e ignore geli detach ${md}
		atf_check -s exit:0 -e ignore -x \
			"cat keyfile1 | geli attach -j passfile0 -j passfile1 -k keyfile0 -k - ${md}"
		atf_check -s exit:0 -e ignore geli detach ${md}
		atf_check -s exit:0 -e ignore -x \
			"cat keyfile0 keyfile1 | geli attach -j passfile0 -j passfile1 -k - ${md}"
		atf_check -s exit:0 -e ignore geli detach ${md}
		atf_check -s exit:0 -e ignore -x \
			"cat passfile0 passfile1 | awk '{printf \"%s\", \$0}' | geli attach -j - -k keyfile0 -k keyfile1 ${md}"
		atf_check -s exit:0 -e ignore geli detach ${md}
	done
}
init_J_cleanup()
{
	geli_test_cleanup
}

init_a_test()
{
	cipher=$1
	aalgo=$2
	secsize=$3
	ealgo=${cipher%%:*}
	keylen=${cipher##*:}

	atf_check -s exit:0 -e ignore \
		geli init -B none -a $aalgo -e $ealgo -l $keylen -P -K keyfile \
		-s $secsize ${md}
	atf_check geli attach -p -k keyfile ${md}

	atf_check dd if=rnd of=/dev/${md}.eli bs=${secsize} count=${sectors} status=none

	md_rnd=`dd if=rnd bs=${secsize} count=${sectors} status=none | md5`
	atf_check_equal 0 $?
	md_ddev=`dd if=/dev/${md}.eli bs=${secsize} count=${sectors} status=none | md5`
	atf_check_equal 0 $?

	if [ ${md_rnd} != ${md_ddev} ]; then
		atf_fail "Miscompare for aalgo=${aalgo} ealgo=${ealgo} keylen=${keylen} sec=${secsize}"
	fi
}
atf_test_case init_a cleanup
init_a_head()
{
	atf_set "descr" "I/O with geli and HMACs"
	atf_set "require.user" "root"
	atf_set "timeout" 3600
}
init_a_body()
{
	geli_test_setup

	sectors=100

	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none
	atf_check dd if=/dev/random of=rnd bs=$MAX_SECSIZE count=${sectors} \
		status=none
	for_each_geli_config init_a_test
	true
}
init_a_cleanup()
{
	geli_test_cleanup
}

init_alias_test() {
	ealgo=$1
	keylen=$2
	expected_ealgo=$3
	expected_keylen=$4

	atf_check geli init -B none -e $ealgo -l $keylen -P -K keyfile ${md}
	atf_check geli attach -p -k keyfile ${md}
	real_ealgo=`geli list ${md}.eli | awk '/EncryptionAlgorithm/ {print $2}'`
	real_keylen=`geli list ${md}.eli | awk '/KeyLength/ {print $2}'`

	if [ "${real_ealgo}" != "${expected_ealgo}" ]; then
		atf_fail "expected ${expected_ealgo} but got ${real_ealgo}"
	fi

	if [ "${real_keylen}" != "${expected_keylen}" ]; then
		atf_fail "expected ${expected_keylen} but got ${real_keylen}"
	fi
	atf_check geli detach ${md}
}
atf_test_case init_alias cleanup
init_alias_head()
{
	atf_set "descr" "geli init accepts cipher aliases"
	atf_set "require.user" "root"
}
init_alias_body()
{
	geli_test_setup

	md=$(attach_md -t malloc -s 1024k)
	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none

	for spec in aes:0:AES-XTS:128 aes:128:AES-XTS:128 aes:256:AES-XTS:256 \
		3des:0:3DES-CBC:192 3des:192:3DES-CBC:192 \
		blowfish:0:Blowfish-CBC:128 blowfish:128:Blowfish-CBC:128 \
		blowfish:160:Blowfish-CBC:160 blowfish:192:Blowfish-CBC:192 \
		blowfish:224:Blowfish-CBC:224 blowfish:256:Blowfish-CBC:256 \
		blowfish:288:Blowfish-CBC:288 blowfish:352:Blowfish-CBC:352 \
		blowfish:384:Blowfish-CBC:384 blowfish:416:Blowfish-CBC:416 \
		blowfish:448:Blowfish-CBC:448 \
		camellia:0:CAMELLIA-CBC:128 camellia:128:CAMELLIA-CBC:128 \
		camellia:256:CAMELLIA-CBC:256 ; do

		ealgo=`echo $spec | cut -d : -f 1`
		keylen=`echo $spec | cut -d : -f 2`
		expected_ealgo=`echo $spec | cut -d : -f 3`
		expected_keylen=`echo $spec | cut -d : -f 4`

		init_alias_test $ealgo $keylen $expected_ealgo $expected_keylen
	done
}
init_alias_cleanup()
{
	geli_test_cleanup
}

atf_test_case init_i_P cleanup
init_i_P_head()
{
	atf_set "descr" "geli: Options -i and -P are mutually exclusive"
	atf_set "require.user" "root"
}
init_i_P_body()
{
	geli_test_setup

	sectors=100
	md=$(attach_md -t malloc -s `expr $sectors + 1`)

	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none

	atf_check -s not-exit:0 -e "match:Options -i and -P are mutually exclusive"\
		geli init -B none -i 64 -P -K keyfile $md
}
init_i_P_cleanup()
{
	geli_test_cleanup
}

atf_test_case nokey cleanup
nokey_head()
{
	atf_set "descr" "geli init fails if called with no key component"
	atf_set "require.user" "root"
}
nokey_body()
{
	geli_test_setup

	sectors=100
	md=$(attach_md -t malloc -s `expr $sectors + 1`)

	atf_check -s not-exit:0 -e match:"No key components given" \
		geli init -B none -P ${md}
}
nokey_cleanup()
{
	geli_test_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case init
	atf_add_test_case init_B
	atf_add_test_case init_J
	atf_add_test_case init_a
	atf_add_test_case init_alias
	atf_add_test_case init_i_P
	atf_add_test_case nokey
}
