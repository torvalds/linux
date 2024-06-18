#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Testing and monitor the cpu desire performance, frequency, load,
# power consumption and throughput etc.when this script trigger tbench
# test cases.
# 1) Run tbench benchmark on specific governors, ondemand or schedutil.
# 2) Run tbench benchmark comparative test on acpi-cpufreq kernel driver.
# 3) Get desire performance, frequency, load by perf.
# 4) Get power consumption and throughput by amd_pstate_trace.py.
# 5) Analyse test results and save it in file selftest.tbench.csv.
# 6) Plot png images about performance, energy and performance per watt for each test.

# protect against multiple inclusion
if [ $FILE_TBENCH ]; then
	return 0
else
	FILE_TBENCH=DONE
fi

tbench_governors=("ondemand" "schedutil")

# $1: governor, $2: round, $3: des-perf, $4: freq, $5: load, $6: performance, $7: energy, $8: performance per watt
store_csv_tbench()
{
	echo "$1, $2, $3, $4, $5, $6, $7, $8" | tee -a $OUTFILE_TBENCH.csv > /dev/null 2>&1
}

# clear some special lines
clear_csv_tbench()
{
	if [ -f $OUTFILE_TBENCH.csv ]; then
		sed -i '/Comprison(%)/d' $OUTFILE_TBENCH.csv
		sed -i "/$(scaling_name)/d" $OUTFILE_TBENCH.csv
	fi
}

# find string $1 in file csv and get the number of lines
get_lines_csv_tbench()
{
	if [ -f $OUTFILE_TBENCH.csv ]; then
		return `grep -c "$1" $OUTFILE_TBENCH.csv`
	else
		return 0
	fi
}

pre_clear_tbench()
{
	post_clear_tbench
	rm -rf tbench_*.png
	clear_csv_tbench
}

post_clear_tbench()
{
	rm -rf results/tracer-tbench*
	rm -rf $OUTFILE_TBENCH*.log
	rm -rf $OUTFILE_TBENCH*.result

}

# $1: governor, $2: loop
run_tbench()
{
	echo "Launching amd pstate tracer for $1 #$2 tracer_interval: $TRACER_INTERVAL"
	$TRACER -n tracer-tbench-$1-$2 -i $TRACER_INTERVAL > /dev/null 2>&1 &

	printf "Test tbench for $1 #$2 time_limit: $TIME_LIMIT procs_num: $PROCESS_NUM\n"
	tbench_srv > /dev/null 2>&1 &
	$PERF stat -a --per-socket -I 1000 -e power/energy-pkg/ tbench -t $TIME_LIMIT $PROCESS_NUM > $OUTFILE_TBENCH-perf-$1-$2.log 2>&1

	pid=`pidof tbench_srv`
	kill $pid

	for job in `jobs -p`
	do
		echo "Waiting for job id $job"
		wait $job
	done
}

# $1: governor, $2: loop
parse_tbench()
{
	awk '{print $5}' results/tracer-tbench-$1-$2/cpu.csv | sed -e '1d' | sed s/,// > $OUTFILE_TBENCH-des-perf-$1-$2.log
	avg_des_perf=$(awk 'BEGIN {i=0; sum=0};{i++; sum += $1};END {print sum/i}' $OUTFILE_TBENCH-des-perf-$1-$2.log)
	printf "Tbench-$1-#$2 avg des perf: $avg_des_perf\n" | tee -a $OUTFILE_TBENCH.result

	awk '{print $7}' results/tracer-tbench-$1-$2/cpu.csv | sed -e '1d' | sed s/,// > $OUTFILE_TBENCH-freq-$1-$2.log
	avg_freq=$(awk 'BEGIN {i=0; sum=0};{i++; sum += $1};END {print sum/i}' $OUTFILE_TBENCH-freq-$1-$2.log)
	printf "Tbench-$1-#$2 avg freq: $avg_freq\n" | tee -a $OUTFILE_TBENCH.result

	awk '{print $11}' results/tracer-tbench-$1-$2/cpu.csv | sed -e '1d' | sed s/,// > $OUTFILE_TBENCH-load-$1-$2.log
	avg_load=$(awk 'BEGIN {i=0; sum=0};{i++; sum += $1};END {print sum/i}' $OUTFILE_TBENCH-load-$1-$2.log)
	printf "Tbench-$1-#$2 avg load: $avg_load\n" | tee -a $OUTFILE_TBENCH.result

	grep Throughput $OUTFILE_TBENCH-perf-$1-$2.log | awk '{print $2}' > $OUTFILE_TBENCH-throughput-$1-$2.log
	tp_sum=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum}' $OUTFILE_TBENCH-throughput-$1-$2.log)
	printf "Tbench-$1-#$2 throughput(MB/s): $tp_sum\n" | tee -a $OUTFILE_TBENCH.result

	grep Joules $OUTFILE_TBENCH-perf-$1-$2.log | awk '{print $4}' > $OUTFILE_TBENCH-energy-$1-$2.log
	en_sum=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum}' $OUTFILE_TBENCH-energy-$1-$2.log)
	printf "Tbench-$1-#$2 power consumption(J): $en_sum\n" | tee -a $OUTFILE_TBENCH.result

	# Permance is throughput per second, denoted T/t, where T is throught rendered in t seconds.
	# It is well known that P=E/t, where P is power measured in watts(W), E is energy measured in joules(J),
	# and t is time measured in seconds(s). This means that performance per watt becomes
	#       T/t   T/t    T
	#       --- = --- = ---
	#        P    E/t    E
	# with unit given by MB per joule.
	ppw=`echo "scale=4;($TIME_LIMIT-1)*$tp_sum/$en_sum" | bc | awk '{printf "%.4f", $0}'`
	printf "Tbench-$1-#$2 performance per watt(MB/J): $ppw\n" | tee -a $OUTFILE_TBENCH.result
	printf "\n" | tee -a $OUTFILE_TBENCH.result

	driver_name=`echo $(scaling_name)`
	store_csv_tbench "$driver_name-$1" $2 $avg_des_perf $avg_freq $avg_load $tp_sum $en_sum $ppw
}

# $1: governor
loop_tbench()
{
	printf "\nTbench total test times is $LOOP_TIMES for $1\n\n"
	for i in `seq 1 $LOOP_TIMES`
	do
		run_tbench $1 $i
		parse_tbench $1 $i
	done
}

# $1: governor
gather_tbench()
{
	printf "Tbench test result for $1 (loops:$LOOP_TIMES)" | tee -a $OUTFILE_TBENCH.result
	printf "\n--------------------------------------------------\n" | tee -a $OUTFILE_TBENCH.result

	grep "Tbench-$1-#" $OUTFILE_TBENCH.result | grep "avg des perf:" | awk '{print $NF}' > $OUTFILE_TBENCH-des-perf-$1.log
	avg_des_perf=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum/'$LOOP_TIMES'}' $OUTFILE_TBENCH-des-perf-$1.log)
	printf "Tbench-$1 avg des perf: $avg_des_perf\n" | tee -a $OUTFILE_TBENCH.result

	grep "Tbench-$1-#" $OUTFILE_TBENCH.result | grep "avg freq:" | awk '{print $NF}' > $OUTFILE_TBENCH-freq-$1.log
	avg_freq=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum/'$LOOP_TIMES'}' $OUTFILE_TBENCH-freq-$1.log)
	printf "Tbench-$1 avg freq: $avg_freq\n" | tee -a $OUTFILE_TBENCH.result

	grep "Tbench-$1-#" $OUTFILE_TBENCH.result | grep "avg load:" | awk '{print $NF}' > $OUTFILE_TBENCH-load-$1.log
	avg_load=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum/'$LOOP_TIMES'}' $OUTFILE_TBENCH-load-$1.log)
	printf "Tbench-$1 avg load: $avg_load\n" | tee -a $OUTFILE_TBENCH.result

	grep "Tbench-$1-#" $OUTFILE_TBENCH.result | grep "throughput(MB/s):" | awk '{print $NF}' > $OUTFILE_TBENCH-throughput-$1.log
	tp_sum=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum}' $OUTFILE_TBENCH-throughput-$1.log)
	printf "Tbench-$1 total throughput(MB/s): $tp_sum\n" | tee -a $OUTFILE_TBENCH.result

	avg_tp=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum/'$LOOP_TIMES'}' $OUTFILE_TBENCH-throughput-$1.log)
	printf "Tbench-$1 avg throughput(MB/s): $avg_tp\n" | tee -a $OUTFILE_TBENCH.result

	grep "Tbench-$1-#" $OUTFILE_TBENCH.result | grep "power consumption(J):" | awk '{print $NF}' > $OUTFILE_TBENCH-energy-$1.log
	en_sum=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum}' $OUTFILE_TBENCH-energy-$1.log)
	printf "Tbench-$1 total power consumption(J): $en_sum\n" | tee -a $OUTFILE_TBENCH.result

	avg_en=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum/'$LOOP_TIMES'}' $OUTFILE_TBENCH-energy-$1.log)
	printf "Tbench-$1 avg power consumption(J): $avg_en\n" | tee -a $OUTFILE_TBENCH.result

	# Permance is throughput per second, denoted T/t, where T is throught rendered in t seconds.
	# It is well known that P=E/t, where P is power measured in watts(W), E is energy measured in joules(J),
	# and t is time measured in seconds(s). This means that performance per watt becomes
	#       T/t   T/t    T
	#       --- = --- = ---
	#        P    E/t    E
	# with unit given by MB per joule.
	ppw=`echo "scale=4;($TIME_LIMIT-1)*$avg_tp/$avg_en" | bc | awk '{printf "%.4f", $0}'`
	printf "Tbench-$1 performance per watt(MB/J): $ppw\n" | tee -a $OUTFILE_TBENCH.result
	printf "\n" | tee -a $OUTFILE_TBENCH.result

	driver_name=`echo $(scaling_name)`
	store_csv_tbench "$driver_name-$1" "Average" $avg_des_perf $avg_freq $avg_load $avg_tp $avg_en $ppw
}

# $1: base scaling_driver $2: base governor $3: comparative scaling_driver $4: comparative governor
__calc_comp_tbench()
{
	base=`grep "$1-$2" $OUTFILE_TBENCH.csv | grep "Average"`
	comp=`grep "$3-$4" $OUTFILE_TBENCH.csv | grep "Average"`

	if [ -n "$base" -a -n "$comp" ]; then
		printf "\n==================================================\n" | tee -a $OUTFILE_TBENCH.result
		printf "Tbench comparison $1-$2 VS $3-$4" | tee -a $OUTFILE_TBENCH.result
		printf "\n==================================================\n" | tee -a $OUTFILE_TBENCH.result

		# get the base values
		des_perf_base=`echo "$base" | awk '{print $3}' | sed s/,//`
		freq_base=`echo "$base" | awk '{print $4}' | sed s/,//`
		load_base=`echo "$base" | awk '{print $5}' | sed s/,//`
		perf_base=`echo "$base" | awk '{print $6}' | sed s/,//`
		energy_base=`echo "$base" | awk '{print $7}' | sed s/,//`
		ppw_base=`echo "$base" | awk '{print $8}' | sed s/,//`

		# get the comparative values
		des_perf_comp=`echo "$comp" | awk '{print $3}' | sed s/,//`
		freq_comp=`echo "$comp" | awk '{print $4}' | sed s/,//`
		load_comp=`echo "$comp" | awk '{print $5}' | sed s/,//`
		perf_comp=`echo "$comp" | awk '{print $6}' | sed s/,//`
		energy_comp=`echo "$comp" | awk '{print $7}' | sed s/,//`
		ppw_comp=`echo "$comp" | awk '{print $8}' | sed s/,//`

		# compare the base and comp values
		des_perf_drop=`echo "scale=4;($des_perf_comp-$des_perf_base)*100/$des_perf_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Tbench-$1 des perf base: $des_perf_base comprison: $des_perf_comp percent: $des_perf_drop\n" | tee -a $OUTFILE_TBENCH.result

		freq_drop=`echo "scale=4;($freq_comp-$freq_base)*100/$freq_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Tbench-$1 freq base: $freq_base comprison: $freq_comp percent: $freq_drop\n" | tee -a $OUTFILE_TBENCH.result

		load_drop=`echo "scale=4;($load_comp-$load_base)*100/$load_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Tbench-$1 load base: $load_base comprison: $load_comp percent: $load_drop\n" | tee -a $OUTFILE_TBENCH.result

		perf_drop=`echo "scale=4;($perf_comp-$perf_base)*100/$perf_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Tbench-$1 perf base: $perf_base comprison: $perf_comp percent: $perf_drop\n" | tee -a $OUTFILE_TBENCH.result

		energy_drop=`echo "scale=4;($energy_comp-$energy_base)*100/$energy_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Tbench-$1 energy base: $energy_base comprison: $energy_comp percent: $energy_drop\n" | tee -a $OUTFILE_TBENCH.result

		ppw_drop=`echo "scale=4;($ppw_comp-$ppw_base)*100/$ppw_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Tbench-$1 performance per watt base: $ppw_base comprison: $ppw_comp percent: $ppw_drop\n" | tee -a $OUTFILE_TBENCH.result
		printf "\n" | tee -a $OUTFILE_TBENCH.result

		store_csv_tbench "$1-$2 VS $3-$4" "Comprison(%)" "$des_perf_drop" "$freq_drop" "$load_drop" "$perf_drop" "$energy_drop" "$ppw_drop"
	fi
}

# calculate the comparison(%)
calc_comp_tbench()
{
	# acpi-cpufreq-ondemand VS acpi-cpufreq-schedutil
	__calc_comp_tbench ${all_scaling_names[0]} ${tbench_governors[0]} ${all_scaling_names[0]} ${tbench_governors[1]}

	# amd-pstate-ondemand VS amd-pstate-schedutil
	__calc_comp_tbench ${all_scaling_names[1]} ${tbench_governors[0]} ${all_scaling_names[1]} ${tbench_governors[1]}

	# acpi-cpufreq-ondemand VS amd-pstate-ondemand
	__calc_comp_tbench ${all_scaling_names[0]} ${tbench_governors[0]} ${all_scaling_names[1]} ${tbench_governors[0]}

	# acpi-cpufreq-schedutil VS amd-pstate-schedutil
	__calc_comp_tbench ${all_scaling_names[0]} ${tbench_governors[1]} ${all_scaling_names[1]} ${tbench_governors[1]}
}

# $1: file_name, $2: title, $3: ylable, $4: column
plot_png_tbench()
{
	# all_scaling_names[1] all_scaling_names[0] flag
	#    amd-pstate           acpi-cpufreq
	#         N                   N             0
	#         N                   Y             1
	#         Y                   N             2
	#         Y                   Y             3
	ret=`grep -c "${all_scaling_names[1]}" $OUTFILE_TBENCH.csv`
	if [ $ret -eq 0 ]; then
		ret=`grep -c "${all_scaling_names[0]}" $OUTFILE_TBENCH.csv`
		if [ $ret -eq 0 ]; then
			flag=0
		else
			flag=1
		fi
	else
		ret=`grep -c "${all_scaling_names[0]}" $OUTFILE_TBENCH.csv`
		if [ $ret -eq 0 ]; then
			flag=2
		else
			flag=3
		fi
	fi

	gnuplot << EOF
		set term png
		set output "$1"

		set title "$2"
		set xlabel "Test Cycles (round)"
		set ylabel "$3"

		set grid
		set style data histogram
		set style fill solid 0.5 border
		set boxwidth 0.8

		if ($flag == 1) {
			plot \
			"<(sed -n -e 's/,//g' -e '/${all_scaling_names[0]}-${tbench_governors[0]}/p' $OUTFILE_TBENCH.csv)" using $4:xtic(2) title "${all_scaling_names[0]}-${tbench_governors[0]}", \
			"<(sed -n -e 's/,//g' -e '/${all_scaling_names[0]}-${tbench_governors[1]}/p' $OUTFILE_TBENCH.csv)" using $4:xtic(2) title "${all_scaling_names[0]}-${tbench_governors[1]}"
		} else {
			if ($flag == 2) {
				plot \
				"<(sed -n -e 's/,//g' -e '/${all_scaling_names[1]}-${tbench_governors[0]}/p' $OUTFILE_TBENCH.csv)" using $4:xtic(2) title "${all_scaling_names[1]}-${tbench_governors[0]}", \
				"<(sed -n -e 's/,//g' -e '/${all_scaling_names[1]}-${tbench_governors[1]}/p' $OUTFILE_TBENCH.csv)" using $4:xtic(2) title "${all_scaling_names[1]}-${tbench_governors[1]}"
			} else {
				if ($flag == 3 ) {
					plot \
					"<(sed -n -e 's/,//g' -e '/${all_scaling_names[0]}-${tbench_governors[0]}/p' $OUTFILE_TBENCH.csv)" using $4:xtic(2) title "${all_scaling_names[0]}-${tbench_governors[0]}", \
					"<(sed -n -e 's/,//g' -e '/${all_scaling_names[0]}-${tbench_governors[1]}/p' $OUTFILE_TBENCH.csv)" using $4:xtic(2) title "${all_scaling_names[0]}-${tbench_governors[1]}", \
					"<(sed -n -e 's/,//g' -e '/${all_scaling_names[1]}-${tbench_governors[0]}/p' $OUTFILE_TBENCH.csv)" using $4:xtic(2) title "${all_scaling_names[1]}-${tbench_governors[0]}", \
					"<(sed -n -e 's/,//g' -e '/${all_scaling_names[1]}-${tbench_governors[1]}/p' $OUTFILE_TBENCH.csv)" using $4:xtic(2) title "${all_scaling_names[1]}-${tbench_governors[1]}"
				}
			}
		}
		quit
EOF
}

amd_pstate_tbench()
{
	printf "\n---------------------------------------------\n"
	printf "*** Running tbench                        ***"
	printf "\n---------------------------------------------\n"

	pre_clear_tbench

	get_lines_csv_tbench "Governor"
	if [ $? -eq 0 ]; then
		# add titles and unit for csv file
		store_csv_tbench "Governor" "Round" "Des-perf" "Freq" "Load" "Performance" "Energy" "Performance Per Watt"
		store_csv_tbench "Unit" "" "" "GHz" "" "MB/s" "J" "MB/J"
	fi

	backup_governor
	for governor in ${tbench_governors[*]} ; do
		printf "\nSpecified governor is $governor\n\n"
		switch_governor $governor
		loop_tbench $governor
		gather_tbench $governor
	done
	restore_governor

	plot_png_tbench "tbench_perfromance.png" "Tbench Benchmark Performance" "Performance" 6
	plot_png_tbench "tbench_energy.png" "Tbench Benchmark Energy" "Energy (J)" 7
	plot_png_tbench "tbench_ppw.png" "Tbench Benchmark Performance Per Watt" "Performance Per Watt (MB/J)" 8

	calc_comp_tbench

	post_clear_tbench
}
