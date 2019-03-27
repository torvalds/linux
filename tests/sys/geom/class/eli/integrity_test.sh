# $FreeBSD$

. $(atf_get_srcdir)/conf.sh

copy_test() {
	cipher=$1
	aalgo=$2
	secsize=$3
	ealgo=${cipher%%:*}
	keylen=${cipher##*:}

	atf_check -s exit:0 -e ignore \
		geli init -B none -a $aalgo -e $ealgo -l $keylen -P \
		-K keyfile -s $secsize ${md}
	atf_check geli attach -p -k keyfile ${md}

	atf_check dd if=rnd of=/dev/${md}.eli bs=${secsize} count=1 status=none

	# Copy first small sector to the second small sector.
	# This should be detected as corruption.
	atf_check dd if=backing_file of=sector bs=512 count=1 \
		conv=notrunc status=none
	atf_check dd if=sector of=backing_file bs=512 count=1 seek=1 \
		conv=notrunc status=none

	atf_check -s not-exit:0 -e ignore \
		dd if=/dev/${md}.eli of=/dev/null bs=${secsize} count=1

	# Fix the corruption
	atf_check dd if=rnd of=/dev/${md}.eli bs=${secsize} count=2 status=none
	atf_check dd if=/dev/${md}.eli of=/dev/null bs=${secsize} count=2 \
		status=none

	# Copy first big sector to the second big sector.
	# This should be detected as corruption.
	ms=`diskinfo /dev/${md} | awk '{print $3 - 512}'`
	ns=`diskinfo /dev/${md}.eli | awk '{print $4}'`
	usecsize=`echo "($ms / $ns) - (($ms / $ns) % 512)" | bc`
	atf_check dd if=backing_file bs=512 count=$(( ${usecsize} / 512 )) \
		seek=$(( $secsize / 512 )) of=sector conv=notrunc status=none
	atf_check dd of=backing_file bs=512 count=$(( ${usecsize} / 512 )) \
		seek=$(( $secsize / 256 )) if=sector conv=notrunc status=none
	atf_check -s not-exit:0 -e ignore \
		dd if=/dev/${md}.eli of=/dev/null bs=${secsize} count=$ns
}

atf_test_case copy cleanup
copy_head()
{
	atf_set "descr" "geli will detect misdirected writes as corruption"
	atf_set "require.user" "root"
	atf_set "timeout" 3600
}
copy_body()
{
	geli_test_setup

	sectors=2

	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none
	dd if=/dev/random of=rnd bs=${MAX_SECSIZE} count=${sectors} status=none

	for_each_geli_config copy_test backing_file
}
copy_cleanup()
{
	geli_test_cleanup
}


data_test() {
	cipher=$1
	aalgo=$2
	secsize=$3
	ealgo=${cipher%%:*}
	keylen=${cipher##*:}

	atf_check -s exit:0 -e ignore \
		geli init -B none -a $aalgo -e $ealgo -l $keylen -P -K keyfile \
		-s $secsize ${md}

	# Corrupt 8 bytes of data.
	atf_check dd if=/dev/${md} of=sector bs=512 count=1 status=none
	atf_check dd if=rnd of=sector bs=1 count=8 seek=64 conv=notrunc status=none
	atf_check dd if=sector of=/dev/${md} bs=512 count=1 status=none
	atf_check geli attach -p -k keyfile ${md}

	# Try to read from the corrupt sector
	atf_check -s not-exit:0 -e ignore \
		dd if=/dev/${md}.eli of=/dev/null bs=${secsize} count=1
}

atf_test_case data cleanup
data_head()
{
	atf_set "descr" "With HMACs, geli will detect data corruption"
	atf_set "require.user" "root"
	atf_set "timeout" 1800
}
data_body()
{
	geli_test_setup

	sectors=2

	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none
	dd if=/dev/random of=rnd bs=${MAX_SECSIZE} count=${sectors} status=none
	for_each_geli_config data_test
}
data_cleanup()
{
	geli_test_cleanup
}

hmac_test() {
	cipher=$1
	aalgo=$2
	secsize=$3
	ealgo=${cipher%%:*}
	keylen=${cipher##*:}

	atf_check -s exit:0 -e ignore \
		geli init -B none -a $aalgo -e $ealgo -l $keylen -P -K keyfile \
		-s $secsize ${md}

	# Corrupt 8 bytes of HMAC.
	atf_check dd if=/dev/${md} of=sector bs=512 count=1 status=none
	atf_check dd if=rnd of=sector bs=1 count=16 conv=notrunc status=none
	atf_check dd if=sector of=/dev/${md} bs=512 count=1 status=none
	atf_check geli attach -p -k keyfile ${md}

	# Try to read from the corrupt sector
	atf_check -s not-exit:0 -e ignore \
		dd if=/dev/${md}.eli of=/dev/null bs=${secsize} count=1
}

atf_test_case hmac cleanup
hmac_head()
{
	atf_set "descr" "geli will detect corruption of HMACs"
	atf_set "require.user" "root"
	atf_set "timeout" 1800
}
hmac_body()
{
	geli_test_setup

	sectors=2

	atf_check dd if=/dev/random of=keyfile bs=512 count=16 status=none
	dd if=/dev/random of=rnd bs=${MAX_SECSIZE} count=${sectors} status=none
	for_each_geli_config hmac_test
}
hmac_cleanup()
{
	geli_test_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case copy
	atf_add_test_case data
	atf_add_test_case hmac
}
