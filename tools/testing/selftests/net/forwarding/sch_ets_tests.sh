# SPDX-License-Identifier: GPL-2.0

# Global interface:
#  $put -- port under test (e.g. $swp2)
#  collect_stats($streams...) -- A function to get stats for individual streams
#  ets_start_traffic($band) -- Start traffic for this band
#  ets_change_qdisc($op, $dev, $nstrict, $quanta...) -- Add or change qdisc

# WS describes the Qdisc configuration. It has one value per band (so the
# number of array elements indicates the number of bands). If the value is
# 0, it is a strict band, otherwise the it's a DRR band and the value is
# that band's quantum.
declare -a WS

qdisc_describe()
{
	local nbands=${#WS[@]}
	local nstrict=0
	local i

	for ((i = 0; i < nbands; i++)); do
		if ((!${WS[$i]})); then
			: $((nstrict++))
		fi
	done

	echo -n "ets bands $nbands"
	if ((nstrict)); then
		echo -n " strict $nstrict"
	fi
	if ((nstrict < nbands)); then
		echo -n " quanta"
		for ((i = nstrict; i < nbands; i++)); do
			echo -n " ${WS[$i]}"
		done
	fi
}

__strict_eval()
{
	local desc=$1; shift
	local d=$1; shift
	local total=$1; shift
	local above=$1; shift

	RET=0

	if ((! total)); then
		check_err 1 "No traffic observed"
		log_test "$desc"
		return
	fi

	local ratio=$(echo "scale=2; 100 * $d / $total" | bc -l)
	if ((above)); then
		test $(echo "$ratio > 95.0" | bc -l) -eq 1
		check_err $? "Not enough traffic"
		log_test "$desc"
		log_info "Expected ratio >95% Measured ratio $ratio"
	else
		test $(echo "$ratio < 5" | bc -l) -eq 1
		check_err $? "Too much traffic"
		log_test "$desc"
		log_info "Expected ratio <5% Measured ratio $ratio"
	fi
}

strict_eval()
{
	__strict_eval "$@" 1
}

notraf_eval()
{
	__strict_eval "$@" 0
}

__ets_dwrr_test()
{
	local -a streams=("$@")

	local low_stream=${streams[0]}
	local seen_strict=0
	local -a t0 t1 d
	local stream
	local total
	local i

	echo "Testing $(qdisc_describe), streams ${streams[@]}"

	for stream in ${streams[@]}; do
		ets_start_traffic $stream
		defer stop_traffic $!
	done

	sleep 10

	t0=($(collect_stats "${streams[@]}"))

	sleep 10

	t1=($(collect_stats "${streams[@]}"))
	d=($(for ((i = 0; i < ${#streams[@]}; i++)); do
		 echo $((${t1[$i]} - ${t0[$i]}))
	     done))
	total=$(echo ${d[@]} | sed 's/ /+/g' | bc)

	for ((i = 0; i < ${#streams[@]}; i++)); do
		local stream=${streams[$i]}
		if ((seen_strict)); then
			notraf_eval "band $stream" ${d[$i]} $total
		elif ((${WS[$stream]} == 0)); then
			strict_eval "band $stream" ${d[$i]} $total
			seen_strict=1
		elif ((stream == low_stream)); then
			# Low stream is used as DWRR evaluation reference.
			continue
		else
			multipath_eval "bands $low_stream:$stream" \
				       ${WS[$low_stream]} ${WS[$stream]} \
				       ${d[0]} ${d[$i]}
		fi
	done
}

ets_dwrr_test_012()
{
	in_defer_scope \
		__ets_dwrr_test 0 1 2
}

ets_dwrr_test_01()
{
	in_defer_scope \
		__ets_dwrr_test 0 1
}

ets_dwrr_test_12()
{
	in_defer_scope \
		__ets_dwrr_test 1 2
}

ets_qdisc_setup()
{
	local dev=$1; shift
	local nstrict=$1; shift
	local -a quanta=("$@")

	local ndwrr=${#quanta[@]}
	local nbands=$((nstrict + ndwrr))
	local nstreams=$(if ((nbands > 3)); then echo 3; else echo $nbands; fi)
	local priomap=$(seq 0 $((nstreams - 1)))
	local i

	WS=($(
		for ((i = 0; i < nstrict; i++)); do
			echo 0
		done
		for ((i = 0; i < ndwrr; i++)); do
			echo ${quanta[$i]}
		done
	))

	ets_change_qdisc $dev $nstrict "$priomap" ${quanta[@]}
}

ets_set_dwrr_uniform()
{
	ets_qdisc_setup $put 0 3300 3300 3300
}

ets_set_dwrr_varying()
{
	ets_qdisc_setup $put 0 5000 3500 1500
}

ets_set_strict()
{
	ets_qdisc_setup $put 3
}

ets_set_mixed()
{
	ets_qdisc_setup $put 1 5000 2500 1500
}

ets_change_quantum()
{
	tc class change dev $put classid 10:2 ets quantum 8000
	WS[1]=8000
}

ets_set_dwrr_two_bands()
{
	ets_qdisc_setup $put 0 5000 2500
}

ets_test_strict()
{
	ets_set_strict
	xfail_on_slow ets_dwrr_test_01
	xfail_on_slow ets_dwrr_test_12
}

ets_test_mixed()
{
	ets_set_mixed
	xfail_on_slow ets_dwrr_test_01
	xfail_on_slow ets_dwrr_test_12
}

ets_test_dwrr()
{
	ets_set_dwrr_uniform
	xfail_on_slow ets_dwrr_test_012

	ets_set_dwrr_varying
	xfail_on_slow ets_dwrr_test_012

	ets_change_quantum
	xfail_on_slow ets_dwrr_test_012

	ets_set_dwrr_two_bands
	xfail_on_slow ets_dwrr_test_01
}
