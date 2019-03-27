#!/bin/sh
# $FreeBSD$

TEST_MDS_FILE="${TMPDIR}/test_mds.$(basename $0)"

devwait()
{
	while :; do
		if [ -c /dev/${class}/${name} ]; then
			return
		fi
		sleep 0.2
	done
}

attach_md()
{
	local test_md

	test_md=$(mdconfig -a "$@") || exit
	echo $test_md >> $TEST_MDS_FILE || exit
	echo $test_md
}

detach_md()
{
	local test_md unit

	test_md=$1
	unit=${test_md#md}
	mdconfig -d -u $unit || exit
	sed -i '' "/^${test_md}$/d" $TEST_MDS_FILE || exit
}

geom_test_cleanup()
{
	local test_md

	if [ -f "$TEST_MDS_FILE" ]; then
		while read test_md; do
			# The "#" tells the TAP parser this is a comment
			echo "# Removing test memory disk: $test_md"
			mdconfig -d -u $test_md
		done < $TEST_MDS_FILE
		rm -f "$TEST_MDS_FILE"
	fi
}

geom_load_class_if_needed()
{
	local class=$1

	# If the geom class isn't already loaded, try loading it.
	if ! kldstat -q -m g_${class}; then
		if ! geom ${class} load; then
			echo "could not load module for geom class=${class}"
			return 1
		fi
	fi
	return 0
}

geom_atf_test_setup()
{
	if ! error_message=$(geom_load_class_if_needed $class); then
		atf_skip "$error_message"
	fi
}

geom_tap_test_setup()
{
	if ! error_message=$(geom_load_class_if_needed $class); then
		echo "1..0 # SKIP $error_message"
		exit 0
	fi
}

: ${ATF_TEST=false}
if ! $ATF_TEST; then
	geom_tap_test_setup
fi
