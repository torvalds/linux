#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Testing and monitor the cpu desire performance, frequency, load,
# power consumption and throughput etc. when this script trigger
# gitsource test.
# 1) Download and tar gitsource codes.
# 2) Run gitsource benchmark on specific governors, ondemand or schedutil.
# 3) Run tbench benchmark comparative test on acpi-cpufreq kernel driver.
# 4) Get desire performance, frequency, load by perf.
# 5) Get power consumption and throughput by amd_pstate_trace.py.
# 6) Get run time by /usr/bin/time.
# 7) Analyse test results and save it in file selftest.gitsource.csv.
#8) Plot png images about time, energy and performance per watt for each test.

# protect against multiple inclusion
if [ $FILE_GITSOURCE ]; then
	return 0
else
	FILE_GITSOURCE=DONE
fi

git_name="git-2.15.1"
git_tar="$git_name.tar.gz"
gitsource_url="https://github.com/git/git/archive/refs/tags/v2.15.1.tar.gz"
gitsource_governors=("ondemand" "schedutil")

# $1: governor, $2: round, $3: des-perf, $4: freq, $5: load, $6: time $7: energy, $8: PPW
store_csv_gitsource()
{
	echo "$1, $2, $3, $4, $5, $6, $7, $8" | tee -a $OUTFILE_GIT.csv > /dev/null 2>&1
}

# clear some special lines
clear_csv_gitsource()
{
	if [ -f $OUTFILE_GIT.csv ]; then
		sed -i '/Comprison(%)/d' $OUTFILE_GIT.csv
		sed -i "/$(scaling_name)/d" $OUTFILE_GIT.csv
	fi
}

# find string $1 in file csv and get the number of lines
get_lines_csv_gitsource()
{
	if [ -f $OUTFILE_GIT.csv ]; then
		return `grep -c "$1" $OUTFILE_GIT.csv`
	else
		return 0
	fi
}

pre_clear_gitsource()
{
	post_clear_gitsource
	rm -rf gitsource_*.png
	clear_csv_gitsource
}

post_clear_gitsource()
{
	rm -rf results/tracer-gitsource*
	rm -rf $OUTFILE_GIT*.log
	rm -rf $OUTFILE_GIT*.result
}

install_gitsource()
{
	if [ ! -d $git_name ]; then
		printf "Download gitsource, please wait a moment ...\n\n"
		wget -O $git_tar $gitsource_url > /dev/null 2>&1

		printf "Tar gitsource ...\n\n"
		tar -xzf $git_tar
	fi
}

# $1: governor, $2: loop
run_gitsource()
{
	echo "Launching amd pstate tracer for $1 #$2 tracer_interval: $TRACER_INTERVAL"
	./amd_pstate_trace.py -n tracer-gitsource-$1-$2 -i $TRACER_INTERVAL > /dev/null 2>&1 &

	printf "Make and test gitsource for $1 #$2 make_cpus: $MAKE_CPUS\n"
	cd $git_name
	perf stat -a --per-socket -I 1000 -e power/energy-pkg/ /usr/bin/time -o ../$OUTFILE_GIT.time-gitsource-$1-$2.log make test -j$MAKE_CPUS > ../$OUTFILE_GIT-perf-$1-$2.log 2>&1
	cd ..

	for job in `jobs -p`
	do
		echo "Waiting for job id $job"
		wait $job
	done
}

# $1: governor, $2: loop
parse_gitsource()
{
	awk '{print $5}' results/tracer-gitsource-$1-$2/cpu.csv | sed -e '1d' | sed s/,// > $OUTFILE_GIT-des-perf-$1-$2.log
	avg_des_perf=$(awk 'BEGIN {i=0; sum=0};{i++; sum += $1};END {print sum/i}' $OUTFILE_GIT-des-perf-$1-$2.log)
	printf "Gitsource-$1-#$2 avg des perf: $avg_des_perf\n" | tee -a $OUTFILE_GIT.result

	awk '{print $7}' results/tracer-gitsource-$1-$2/cpu.csv | sed -e '1d' | sed s/,// > $OUTFILE_GIT-freq-$1-$2.log
	avg_freq=$(awk 'BEGIN {i=0; sum=0};{i++; sum += $1};END {print sum/i}' $OUTFILE_GIT-freq-$1-$2.log)
	printf "Gitsource-$1-#$2 avg freq: $avg_freq\n" | tee -a $OUTFILE_GIT.result

	awk '{print $11}' results/tracer-gitsource-$1-$2/cpu.csv | sed -e '1d' | sed s/,// > $OUTFILE_GIT-load-$1-$2.log
	avg_load=$(awk 'BEGIN {i=0; sum=0};{i++; sum += $1};END {print sum/i}' $OUTFILE_GIT-load-$1-$2.log)
	printf "Gitsource-$1-#$2 avg load: $avg_load\n" | tee -a $OUTFILE_GIT.result

	grep user $OUTFILE_GIT.time-gitsource-$1-$2.log | awk '{print $1}' | sed -e 's/user//' > $OUTFILE_GIT-time-$1-$2.log
	time_sum=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum}' $OUTFILE_GIT-time-$1-$2.log)
	printf "Gitsource-$1-#$2 user time(s): $time_sum\n" | tee -a $OUTFILE_GIT.result

	grep Joules $OUTFILE_GIT-perf-$1-$2.log | awk '{print $4}' > $OUTFILE_GIT-energy-$1-$2.log
	en_sum=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum}' $OUTFILE_GIT-energy-$1-$2.log)
	printf "Gitsource-$1-#$2 power consumption(J): $en_sum\n" | tee -a $OUTFILE_GIT.result

	# Permance is the number of run gitsource per second, denoted 1/t, where 1 is the number of run gitsource in t
	# seconds. It is well known that P=E/t, where P is power measured in watts(W), E is energy measured in joules(J),
	# and t is time measured in seconds(s). This means that performance per watt becomes
	#        1/t     1/t     1
	#       ----- = ----- = ---
	#         P      E/t     E
	# with unit given by 1 per joule.
	ppw=`echo "scale=9;1/$en_sum" | bc | awk '{printf "%.9f", $0}'`
	printf "Gitsource-$1-#$2 performance per watt(1/J): $ppw\n" | tee -a $OUTFILE_GIT.result
	printf "\n" | tee -a $OUTFILE_GIT.result

	driver_name=`echo $(scaling_name)`
	store_csv_gitsource "$driver_name-$1" $2 $avg_des_perf $avg_freq $avg_load $time_sum $en_sum $ppw
}

# $1: governor
loop_gitsource()
{
	printf "\nGitsource total test times is $LOOP_TIMES for $1\n\n"
	for i in `seq 1 $LOOP_TIMES`
	do
		run_gitsource $1 $i
		parse_gitsource $1 $i
	done
}

# $1: governor
gather_gitsource()
{
	printf "Gitsource test result for $1 (loops:$LOOP_TIMES)" | tee -a $OUTFILE_GIT.result
	printf "\n--------------------------------------------------\n" | tee -a $OUTFILE_GIT.result

	grep "Gitsource-$1-#" $OUTFILE_GIT.result | grep "avg des perf:" | awk '{print $NF}' > $OUTFILE_GIT-des-perf-$1.log
	avg_des_perf=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum/'$LOOP_TIMES'}' $OUTFILE_GIT-des-perf-$1.log)
	printf "Gitsource-$1 avg des perf: $avg_des_perf\n" | tee -a $OUTFILE_GIT.result

	grep "Gitsource-$1-#" $OUTFILE_GIT.result | grep "avg freq:" | awk '{print $NF}' > $OUTFILE_GIT-freq-$1.log
	avg_freq=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum/'$LOOP_TIMES'}' $OUTFILE_GIT-freq-$1.log)
	printf "Gitsource-$1 avg freq: $avg_freq\n" | tee -a $OUTFILE_GIT.result

	grep "Gitsource-$1-#" $OUTFILE_GIT.result | grep "avg load:" | awk '{print $NF}' > $OUTFILE_GIT-load-$1.log
	avg_load=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum/'$LOOP_TIMES'}' $OUTFILE_GIT-load-$1.log)
	printf "Gitsource-$1 avg load: $avg_load\n" | tee -a $OUTFILE_GIT.result

	grep "Gitsource-$1-#" $OUTFILE_GIT.result | grep "user time(s):" | awk '{print $NF}' > $OUTFILE_GIT-time-$1.log
	time_sum=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum}' $OUTFILE_GIT-time-$1.log)
	printf "Gitsource-$1 total user time(s): $time_sum\n" | tee -a $OUTFILE_GIT.result

	avg_time=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum/'$LOOP_TIMES'}' $OUTFILE_GIT-time-$1.log)
	printf "Gitsource-$1 avg user times(s): $avg_time\n" | tee -a $OUTFILE_GIT.result

	grep "Gitsource-$1-#" $OUTFILE_GIT.result | grep "power consumption(J):" | awk '{print $NF}' > $OUTFILE_GIT-energy-$1.log
	en_sum=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum}' $OUTFILE_GIT-energy-$1.log)
	printf "Gitsource-$1 total power consumption(J): $en_sum\n" | tee -a $OUTFILE_GIT.result

	avg_en=$(awk 'BEGIN {sum=0};{sum += $1};END {print sum/'$LOOP_TIMES'}' $OUTFILE_GIT-energy-$1.log)
	printf "Gitsource-$1 avg power consumption(J): $avg_en\n" | tee -a $OUTFILE_GIT.result

	# Permance is the number of run gitsource per second, denoted 1/t, where 1 is the number of run gitsource in t
	# seconds. It is well known that P=E/t, where P is power measured in watts(W), E is energy measured in joules(J),
	# and t is time measured in seconds(s). This means that performance per watt becomes
	#        1/t     1/t     1
	#       ----- = ----- = ---
	#         P      E/t     E
	# with unit given by 1 per joule.
	ppw=`echo "scale=9;1/$avg_en" | bc | awk '{printf "%.9f", $0}'`
	printf "Gitsource-$1 performance per watt(1/J): $ppw\n" | tee -a $OUTFILE_GIT.result
	printf "\n" | tee -a $OUTFILE_GIT.result

	driver_name=`echo $(scaling_name)`
	store_csv_gitsource "$driver_name-$1" "Average" $avg_des_perf $avg_freq $avg_load $avg_time $avg_en $ppw
}

# $1: base scaling_driver $2: base governor $3: comparison scaling_driver $4: comparison governor
__calc_comp_gitsource()
{
	base=`grep "$1-$2" $OUTFILE_GIT.csv | grep "Average"`
	comp=`grep "$3-$4" $OUTFILE_GIT.csv | grep "Average"`

	if [ -n "$base" -a -n "$comp" ]; then
		printf "\n==================================================\n" | tee -a $OUTFILE_GIT.result
		printf "Gitsource comparison $1-$2 VS $3-$4" | tee -a $OUTFILE_GIT.result
		printf "\n==================================================\n" | tee -a $OUTFILE_GIT.result

		# get the base values
		des_perf_base=`echo "$base" | awk '{print $3}' | sed s/,//`
		freq_base=`echo "$base" | awk '{print $4}' | sed s/,//`
		load_base=`echo "$base" | awk '{print $5}' | sed s/,//`
		time_base=`echo "$base" | awk '{print $6}' | sed s/,//`
		energy_base=`echo "$base" | awk '{print $7}' | sed s/,//`
		ppw_base=`echo "$base" | awk '{print $8}' | sed s/,//`

		# get the comparison values
		des_perf_comp=`echo "$comp" | awk '{print $3}' | sed s/,//`
		freq_comp=`echo "$comp" | awk '{print $4}' | sed s/,//`
		load_comp=`echo "$comp" | awk '{print $5}' | sed s/,//`
		time_comp=`echo "$comp" | awk '{print $6}' | sed s/,//`
		energy_comp=`echo "$comp" | awk '{print $7}' | sed s/,//`
		ppw_comp=`echo "$comp" | awk '{print $8}' | sed s/,//`

		# compare the base and comp values
		des_perf_drop=`echo "scale=4;($des_perf_comp-$des_perf_base)*100/$des_perf_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Gitsource-$1 des perf base: $des_perf_base comprison: $des_perf_comp percent: $des_perf_drop\n" | tee -a $OUTFILE_GIT.result

		freq_drop=`echo "scale=4;($freq_comp-$freq_base)*100/$freq_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Gitsource-$1 freq base: $freq_base comprison: $freq_comp percent: $freq_drop\n" | tee -a $OUTFILE_GIT.result

		load_drop=`echo "scale=4;($load_comp-$load_base)*100/$load_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Gitsource-$1 load base: $load_base comprison: $load_comp percent: $load_drop\n" | tee -a $OUTFILE_GIT.result

		time_drop=`echo "scale=4;($time_comp-$time_base)*100/$time_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Gitsource-$1 time base: $time_base comprison: $time_comp percent: $time_drop\n" | tee -a $OUTFILE_GIT.result

		energy_drop=`echo "scale=4;($energy_comp-$energy_base)*100/$energy_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Gitsource-$1 energy base: $energy_base comprison: $energy_comp percent: $energy_drop\n" | tee -a $OUTFILE_GIT.result

		ppw_drop=`echo "scale=4;($ppw_comp-$ppw_base)*100/$ppw_base" | bc | awk '{printf "%.4f", $0}'`
		printf "Gitsource-$1 performance per watt base: $ppw_base comprison: $ppw_comp percent: $ppw_drop\n" | tee -a $OUTFILE_GIT.result
		printf "\n" | tee -a $OUTFILE_GIT.result

		store_csv_gitsource "$1-$2 VS $3-$4" "Comprison(%)" "$des_perf_drop" "$freq_drop" "$load_drop" "$time_drop" "$energy_drop" "$ppw_drop"
	fi
}

# calculate the comparison(%)
calc_comp_gitsource()
{
	# acpi-cpufreq-ondemand VS acpi-cpufreq-schedutil
	__calc_comp_gitsource ${all_scaling_names[0]} ${gitsource_governors[0]} ${all_scaling_names[0]} ${gitsource_governors[1]}

	# amd-pstate-ondemand VS amd-pstate-schedutil
	__calc_comp_gitsource ${all_scaling_names[1]} ${gitsource_governors[0]} ${all_scaling_names[1]} ${gitsource_governors[1]}

	# acpi-cpufreq-ondemand VS amd-pstate-ondemand
	__calc_comp_gitsource ${all_scaling_names[0]} ${gitsource_governors[0]} ${all_scaling_names[1]} ${gitsource_governors[0]}

	# acpi-cpufreq-schedutil VS amd-pstate-schedutil
	__calc_comp_gitsource ${all_scaling_names[0]} ${gitsource_governors[1]} ${all_scaling_names[1]} ${gitsource_governors[1]}
}

# $1: file_name, $2: title, $3: ylable, $4: column
plot_png_gitsource()
{
	# all_scaling_names[1] all_scaling_names[0] flag
	#    amd-pstate           acpi-cpufreq
	#         N                   N             0
	#         N                   Y             1
	#         Y                   N             2
	#         Y                   Y             3
	ret=`grep -c "${all_scaling_names[1]}" $OUTFILE_GIT.csv`
	if [ $ret -eq 0 ]; then
		ret=`grep -c "${all_scaling_names[0]}" $OUTFILE_GIT.csv`
		if [ $ret -eq 0 ]; then
			flag=0
		else
			flag=1
		fi
	else
		ret=`grep -c "${all_scaling_names[0]}" $OUTFILE_GIT.csv`
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
			"<(sed -n -e 's/,//g' -e '/${all_scaling_names[0]}-${gitsource_governors[0]}/p' $OUTFILE_GIT.csv)" using $4:xtic(2) title "${all_scaling_names[0]}-${gitsource_governors[0]}", \
			"<(sed -n -e 's/,//g' -e '/${all_scaling_names[0]}-${gitsource_governors[1]}/p' $OUTFILE_GIT.csv)" using $4:xtic(2) title "${all_scaling_names[0]}-${gitsource_governors[1]}"
		} else {
			if ($flag == 2) {
				plot \
				"<(sed -n -e 's/,//g' -e '/${all_scaling_names[1]}-${gitsource_governors[0]}/p' $OUTFILE_GIT.csv)" using $4:xtic(2) title "${all_scaling_names[1]}-${gitsource_governors[0]}", \
				"<(sed -n -e 's/,//g' -e '/${all_scaling_names[1]}-${gitsource_governors[1]}/p' $OUTFILE_GIT.csv)" using $4:xtic(2) title "${all_scaling_names[1]}-${gitsource_governors[1]}"
			} else {
				if ($flag == 3 ) {
					plot \
					"<(sed -n -e 's/,//g' -e '/${all_scaling_names[0]}-${gitsource_governors[0]}/p' $OUTFILE_GIT.csv)" using $4:xtic(2) title "${all_scaling_names[0]}-${gitsource_governors[0]}", \
					"<(sed -n -e 's/,//g' -e '/${all_scaling_names[0]}-${gitsource_governors[1]}/p' $OUTFILE_GIT.csv)" using $4:xtic(2) title "${all_scaling_names[0]}-${gitsource_governors[1]}", \
					"<(sed -n -e 's/,//g' -e '/${all_scaling_names[1]}-${gitsource_governors[0]}/p' $OUTFILE_GIT.csv)" using $4:xtic(2) title "${all_scaling_names[1]}-${gitsource_governors[0]}", \
					"<(sed -n -e 's/,//g' -e '/${all_scaling_names[1]}-${gitsource_governors[1]}/p' $OUTFILE_GIT.csv)" using $4:xtic(2) title "${all_scaling_names[1]}-${gitsource_governors[1]}"
				}
			}
		}
		quit
EOF
}

amd_pstate_gitsource()
{
	printf "\n---------------------------------------------\n"
	printf "*** Running gitsource                     ***"
	printf "\n---------------------------------------------\n"

	pre_clear_gitsource

	install_gitsource

	get_lines_csv_gitsource "Governor"
	if [ $? -eq 0 ]; then
		# add titles and unit for csv file
		store_csv_gitsource "Governor" "Round" "Des-perf" "Freq" "Load" "Time" "Energy" "Performance Per Watt"
		store_csv_gitsource "Unit" "" "" "GHz" "" "s" "J" "1/J"
	fi

	backup_governor
	for governor in ${gitsource_governors[*]} ; do
		printf "\nSpecified governor is $governor\n\n"
		switch_governor $governor
		loop_gitsource $governor
		gather_gitsource $governor
	done
	restore_governor

	plot_png_gitsource "gitsource_time.png" "Gitsource Benchmark Time" "Time (s)" 6
	plot_png_gitsource "gitsource_energy.png" "Gitsource Benchmark Energy" "Energy (J)" 7
	plot_png_gitsource "gitsource_ppw.png" "Gitsource Benchmark Performance Per Watt" "Performance Per Watt (1/J)" 8

	calc_comp_gitsource

	post_clear_gitsource
}
