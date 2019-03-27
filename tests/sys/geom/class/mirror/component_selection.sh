# $FreeBSD$

atf_test_case run_latest_genid cleanup
run_latest_genid_head()
{
	atf_set "descr" \
	    "Ensure that we properly select components (latest genid) during STARTING."
	atf_set "require.user" "root"
}
run_latest_genid_body()
{
	. $(atf_get_srcdir)/conf.sh

	f1=$(mktemp ${base}.XXXXXX)
	f2=$(mktemp ${base}.XXXXXX)
	f3=$(mktemp ${base}.XXXXXX)
	rnd1=$(mktemp ${base}.XXXXXX)
	rnd2=$(mktemp ${base}.XXXXXX)

	atf_check truncate -s 2M $f1
	atf_check truncate -s 2M $f2
	atf_check truncate -s 2M $f3
	dd if=/dev/urandom bs=512 count=1 of="$rnd1"
	dd if=/dev/urandom bs=512 count=1 of="$rnd2"

	md1=$(attach_md -t vnode -f ${f1})
	md2=$(attach_md -t vnode -f ${f2})
	md3=$(attach_md -t vnode -f ${f3})

	# Use a gnop for md1 just for consistency; it's not used for anything.
	atf_check gnop create $md1
	atf_check gnop create $md2
	atf_check gnop create $md3
	# Hardcode component names so that the non-.nop device isn't tasted
	# instead.
	atf_check gmirror label -h $name ${md1}.nop
	devwait

	atf_check gmirror insert -h $name ${md2}.nop
	atf_check gmirror insert -h $name ${md3}.nop
	syncwait

	# Fail mirror 3, writing known contents to mirror 1+2 block 1
	atf_check -s exit:0 -e empty -o empty \
	    gnop configure -w 100 ${md3}.nop
	atf_check -s exit:0 dd if="$rnd1" bs=512 count=1 oseek=1 conv=notrunc \
	    of=/dev/mirror/$name status=none

	disconnectwait nop "${md3}.nop"

	# Should have two mirrors remaining after md3 was evicted
	atf_check [ $(gmirror status -s $name | wc -l) -eq 2 ]
	atf_check -s exit:0 -o match:"DEGRADED  ${md1}.nop \(ACTIVE\)" \
		gmirror status -s $name
	atf_check -s exit:0 -o match:"DEGRADED  ${md2}.nop \(ACTIVE\)" \
		gmirror status -s $name

	# Repeat:
	# Fail mirror 2, writing known contents to mirror 1 block 2
	atf_check -s exit:0 -e empty -o empty \
	    gnop configure -w 100 ${md2}.nop
	atf_check -s exit:0 dd if="$rnd2" bs=512 count=2 oseek=1 conv=notrunc \
	    of=/dev/mirror/$name status=none

	disconnectwait nop "${md2}.nop"

	# Should have one mirror remaining after md2 was evicted
	atf_check [ $(gmirror status -s $name | wc -l) -eq 1 ]
	atf_check -s exit:0 -o match:"DEGRADED  ${md1}.nop \(ACTIVE\)" \
		gmirror status -s $name

	# Stop the mirror and remove the pieces so gmirror can't see them.
	atf_check gmirror stop $name
	atf_check gnop destroy ${md1}.nop
	atf_check gnop destroy ${md2}.nop
	atf_check gnop destroy ${md3}.nop

	# Rebuild; spin up "disk" with lowest genid
	atf_check gnop create $md3
	md3gen=$(gmirror dump /dev/${md3}.nop | grep genid | cut -d: -f2)
	# Assert gmirror is referencing this component for now:
	atf_check [ $(consumerrefs nop ${md3}.nop) = "r1w1e1" ]

	# Adding newer genid should kick out old component
	atf_check gnop create $md2
	md2gen=$(gmirror dump /dev/${md2}.nop | grep genid | cut -d: -f2)
	atf_check [ $md2gen -gt $md3gen ]

	disconnectwait nop "${md3}.nop"

	# Can't test this because 'status' doesn't exist until RUNNING:
	#atf_check [ $(gmirror status -s $name | wc -l) -eq 1 ]
	# But as a substitute, assert gmirror has dropped reference to staler
	# component in favor of newer component:
	atf_check [ $(consumerrefs nop ${md2}.nop) = "r1w1e1" ]

	# ditto
	atf_check gnop create $md1
	md1gen=$(gmirror dump /dev/${md1}.nop | grep genid | cut -d: -f2)
	atf_check [ $md1gen -gt $md2gen ]

	disconnectwait nop "${md2}.nop"

	# Assert gmirror has dropped reference to stale component in favor of
	# newer component:
	atf_check [ $(consumerrefs nop ${md1}.nop) = "r1w1e1" ]

	# gmirror won't start the mirror automatically with only one component
	# ($md0) of configured three, so this waits out the
	# kern.geom.mirror.timeout:
	devwait

	atf_check [ $(gmirror status -s $name | wc -l) -eq 1 ]
	atf_check -s exit:0 -o match:"DEGRADED  ${md1}.nop \(ACTIVE\)" \
		gmirror status -s $name
}
run_latest_genid_cleanup()
{
	. $(atf_get_srcdir)/conf.sh

	if [ -f "$TEST_MDS_FILE" ]; then
		while read test_md; do
			echo "# Removing test gnop: ${test_md}.nop"
			gnop destroy -f "${test_md}.nop" 2>/dev/null || :
		done < "$TEST_MDS_FILE"
	fi
	gmirror_test_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case run_latest_genid
}
