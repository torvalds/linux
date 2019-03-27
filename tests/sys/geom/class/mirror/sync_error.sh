# $FreeBSD$

ATF_TEST=true
. $(atf_get_srcdir)/conf.sh

REG_READ_FP=debug.fail_point.g_mirror_regular_request_read

atf_test_case sync_read_error_2_disks cleanup
sync_read_error_2_disks_head()
{
	atf_set "descr" \
		"Ensure that we properly handle read errors during synchronization."
	atf_set "require.user" "root"
}
sync_read_error_2_disks_body()
{
	geom_atf_test_setup

	f1=$(mktemp ${base}.XXXXXX)
	f2=$(mktemp ${base}.XXXXXX)

	atf_check dd if=/dev/zero bs=1M count=32 of=$f1 status=none
	atf_check truncate -s 32M $f2 

	md1=$(attach_md -t vnode -f ${f1})
	md2=$(attach_md -t vnode -f ${f2})

	atf_check gmirror label $name $md1
	devwait

	atf_check -s ignore -e empty -o not-empty sysctl ${REG_READ_FP}='1*return(5)'

	# If a read error occurs while synchronizing and the mirror contains
	# a single active disk, gmirror has no choice but to fail the
	# synchronization and kick the new disk out of the mirror.
	atf_check gmirror insert $name $md2
	sleep 0.1
	syncwait
	atf_check [ $(gmirror status -s $name | wc -l) -eq 1 ]
	atf_check -s exit:0 -o match:"DEGRADED  $md1 \(ACTIVE\)" \
		gmirror status -s $name
}
sync_read_error_2_disks_cleanup()
{
	atf_check -s ignore -e ignore -o ignore sysctl ${REG_READ_FP}='off'
	gmirror_test_cleanup
}

atf_test_case sync_read_error_3_disks cleanup
sync_read_error_3_disks_head()
{
	atf_set "descr" \
		"Ensure that we properly handle read errors during synchronization."
	atf_set "require.user" "root"
}
sync_read_error_3_disks_body()
{
	geom_atf_test_setup

	f1=$(mktemp ${base}.XXXXXX)
	f2=$(mktemp ${base}.XXXXXX)
	f3=$(mktemp ${base}.XXXXXX)

	atf_check dd if=/dev/random bs=1M count=32 of=$f1 status=none
	atf_check truncate -s 32M $f2
	atf_check truncate -s 32M $f3

	md1=$(attach_md -t vnode -f ${f1})
	md2=$(attach_md -t vnode -f ${f2})
	md3=$(attach_md -t vnode -f ${f3})

	atf_check gmirror label $name $md1
	devwait

	atf_check gmirror insert $name $md2
	syncwait

	atf_check -s exit:0 -e empty -o not-empty sysctl ${REG_READ_FP}='1*return(5)'

	# If a read error occurs while synchronizing a new disk, and we have
	# multiple active disks, we retry the read after an error. The disk
	# which returned the read error is kicked out of the mirror.
	atf_check gmirror insert $name $md3
	syncwait
	atf_check [ $(gmirror status -s $name | wc -l) -eq 2 ]
	atf_check -s exit:0 -o match:"DEGRADED  $md3 \(ACTIVE\)" \
		gmirror status -s $name

	# Make sure that the two active disks are identical. Destroy the
	# mirror first so that the metadata sectors are wiped.
	if $(gmirror status -s $name | grep -q $md1); then
		active=$md1
	else
		active=$md2
	fi
	atf_check gmirror destroy $name
	atf_check cmp /dev/$active /dev/$md3
}
sync_read_error_3_disks_cleanup()
{
	atf_check -s ignore -e ignore -o ignore sysctl ${REG_READ_FP}='off'
	gmirror_test_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case sync_read_error_2_disks
	atf_add_test_case sync_read_error_3_disks
}
