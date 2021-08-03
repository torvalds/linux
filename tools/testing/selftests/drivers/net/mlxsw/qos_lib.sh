# SPDX-License-Identifier: GPL-2.0

check_rate()
{
	local rate=$1; shift
	local min=$1; shift
	local what=$1; shift

	if ((rate > min)); then
		return 0
	fi

	echo "$what $(humanize $ir) < $(humanize $min)" > /dev/stderr
	return 1
}

measure_rate()
{
	local sw_in=$1; shift   # Where the traffic ingresses the switch
	local host_in=$1; shift # Where it ingresses another host
	local counter=$1; shift # Counter to use for measurement
	local what=$1; shift

	local interval=10
	local i
	local ret=0

	# Dips in performance might cause momentary ingress rate to drop below
	# 1Gbps. That wouldn't saturate egress and MC would thus get through,
	# seemingly winning bandwidth on account of UC. Demand at least 2Gbps
	# average ingress rate to somewhat mitigate this.
	local min_ingress=2147483648

	for i in {5..0}; do
		local t0=$(ethtool_stats_get $host_in $counter)
		local u0=$(ethtool_stats_get $sw_in $counter)
		sleep $interval
		local t1=$(ethtool_stats_get $host_in $counter)
		local u1=$(ethtool_stats_get $sw_in $counter)

		local ir=$(rate $u0 $u1 $interval)
		local er=$(rate $t0 $t1 $interval)

		if check_rate $ir $min_ingress "$what ingress rate"; then
			break
		fi

		# Fail the test if we can't get the throughput.
		if ((i == 0)); then
			ret=1
		fi
	done

	echo $ir $er
	return $ret
}

bail_on_lldpad()
{
	if systemctl is-active --quiet lldpad; then

		cat >/dev/stderr <<-EOF
		WARNING: lldpad is running

			lldpad will likely configure DCB, and this test will
			configure Qdiscs. mlxsw does not support both at the
			same time, one of them is arbitrarily going to overwrite
			the other. That will cause spurious failures (or,
			unlikely, passes) of this test.
		EOF

		if [[ -z $ALLOW_LLDPAD ]]; then
			cat >/dev/stderr <<-EOF

				If you want to run the test anyway, please set
				an environment variable ALLOW_LLDPAD to a
				non-empty string.
			EOF
			exit 1
		else
			return
		fi
	fi
}
