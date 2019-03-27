. ${STF_SUITE}/include/libtest.kshlib
. ${STF_SUITE}/include/commands.cfg

# $FreeBSD$

# Environment-dependent constants.
for d in `geom disk list | awk '/Name:/ {print $3}'`; do
	# Clear the GPT label first to avoid spurious create failures.
	gpart destroy -F $d >/dev/null 2>&1
	if gpart create -s gpt $d >/dev/null 2>&1 ; then
		gpart destroy $d >/dev/null 2>&1 || continue
		DISKS=("${DISKS[@]}" "/dev/$d") #"$DISKS $d"
	fi
	# Don't bother testing any more if we have enough already.
	# Currently we use at most 5 disks plus 1 for temporary disks.
	[ ${#DISKS[@]} -eq 6 ] && break
done
export KEEP="$(zpool list -H -o name)"

# Pull in constants.
. ${STF_SUITE}/include/constants.cfg
