#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest frmework requirement - SKIP code is 4.
ksft_skip=4

ensure_write_succ()
{
	file=$1
	content=$2
	reason=$3

	if ! echo "$content" > "$file"
	then
		echo "writing $content to $file failed"
		echo "expected success because $reason"
		exit 1
	fi
}

ensure_write_fail()
{
	file=$1
	content=$2
	reason=$3

	if (echo "$content" > "$file") 2> /dev/null
	then
		echo "writing $content to $file succeed ($fail_reason)"
		echo "expected failure because $reason"
		exit 1
	fi
}

ensure_dir()
{
	dir=$1
	to_ensure=$2
	if [ "$to_ensure" = "exist" ] && [ ! -d "$dir" ]
	then
		echo "$dir dir is expected but not found"
		exit 1
	elif [ "$to_ensure" = "not_exist" ] && [ -d "$dir" ]
	then
		echo "$dir dir is not expected but found"
		exit 1
	fi
}

ensure_file()
{
	file=$1
	to_ensure=$2
	permission=$3
	if [ "$to_ensure" = "exist" ]
	then
		if [ ! -f "$file" ]
		then
			echo "$file is expected but not found"
			exit 1
		fi
		perm=$(stat -c "%a" "$file")
		if [ ! "$perm" = "$permission" ]
		then
			echo "$file permission: expected $permission but $perm"
			exit 1
		fi
	elif [ "$to_ensure" = "not_exist" ] && [ -f "$dir" ]
	then
		echo "$file is not expected but found"
		exit 1
	fi
}

test_range()
{
	range_dir=$1
	ensure_dir "$range_dir" "exist"
	ensure_file "$range_dir/min" "exist" 600
	ensure_file "$range_dir/max" "exist" 600
}

test_tried_regions()
{
	tried_regions_dir=$1
	ensure_dir "$tried_regions_dir" "exist"
	ensure_file "$tried_regions_dir/total_bytes" "exist" "400"
}

test_stats()
{
	stats_dir=$1
	ensure_dir "$stats_dir" "exist"
	for f in nr_tried sz_tried nr_applied sz_applied qt_exceeds
	do
		ensure_file "$stats_dir/$f" "exist" "400"
	done
}

test_filter()
{
	filter_dir=$1
	ensure_file "$filter_dir/type" "exist" "600"
	ensure_write_succ "$filter_dir/type" "anon" "valid input"
	ensure_write_succ "$filter_dir/type" "memcg" "valid input"
	ensure_write_succ "$filter_dir/type" "addr" "valid input"
	ensure_write_succ "$filter_dir/type" "target" "valid input"
	ensure_write_fail "$filter_dir/type" "foo" "invalid input"
	ensure_file "$filter_dir/matching" "exist" "600"
	ensure_file "$filter_dir/memcg_path" "exist" "600"
	ensure_file "$filter_dir/addr_start" "exist" "600"
	ensure_file "$filter_dir/addr_end" "exist" "600"
	ensure_file "$filter_dir/damon_target_idx" "exist" "600"
}

test_filters()
{
	filters_dir=$1
	ensure_dir "$filters_dir" "exist"
	ensure_file "$filters_dir/nr_filters" "exist" "600"
	ensure_write_succ  "$filters_dir/nr_filters" "1" "valid input"
	test_filter "$filters_dir/0"

	ensure_write_succ  "$filters_dir/nr_filters" "2" "valid input"
	test_filter "$filters_dir/0"
	test_filter "$filters_dir/1"

	ensure_write_succ "$filters_dir/nr_filters" "0" "valid input"
	ensure_dir "$filters_dir/0" "not_exist"
	ensure_dir "$filters_dir/1" "not_exist"
}

test_watermarks()
{
	watermarks_dir=$1
	ensure_dir "$watermarks_dir" "exist"
	ensure_file "$watermarks_dir/metric" "exist" "600"
	ensure_file "$watermarks_dir/interval_us" "exist" "600"
	ensure_file "$watermarks_dir/high" "exist" "600"
	ensure_file "$watermarks_dir/mid" "exist" "600"
	ensure_file "$watermarks_dir/low" "exist" "600"
}

test_weights()
{
	weights_dir=$1
	ensure_dir "$weights_dir" "exist"
	ensure_file "$weights_dir/sz_permil" "exist" "600"
	ensure_file "$weights_dir/nr_accesses_permil" "exist" "600"
	ensure_file "$weights_dir/age_permil" "exist" "600"
}

test_goal()
{
	goal_dir=$1
	ensure_dir "$goal_dir" "exist"
	ensure_file "$goal_dir/target_value" "exist" "600"
	ensure_file "$goal_dir/current_value" "exist" "600"
}

test_goals()
{
	goals_dir=$1
	ensure_dir "$goals_dir" "exist"
	ensure_file "$goals_dir/nr_goals" "exist" "600"

	ensure_write_succ  "$goals_dir/nr_goals" "1" "valid input"
	test_goal "$goals_dir/0"

	ensure_write_succ  "$goals_dir/nr_goals" "2" "valid input"
	test_goal "$goals_dir/0"
	test_goal "$goals_dir/1"

	ensure_write_succ  "$goals_dir/nr_goals" "0" "valid input"
	ensure_dir "$goals_dir/0" "not_exist"
	ensure_dir "$goals_dir/1" "not_exist"
}

test_quotas()
{
	quotas_dir=$1
	ensure_dir "$quotas_dir" "exist"
	ensure_file "$quotas_dir/ms" "exist" 600
	ensure_file "$quotas_dir/bytes" "exist" 600
	ensure_file "$quotas_dir/reset_interval_ms" "exist" 600
	test_weights "$quotas_dir/weights"
	test_goals "$quotas_dir/goals"
}

test_access_pattern()
{
	access_pattern_dir=$1
	ensure_dir "$access_pattern_dir" "exist"
	test_range "$access_pattern_dir/age"
	test_range "$access_pattern_dir/nr_accesses"
	test_range "$access_pattern_dir/sz"
}

test_scheme()
{
	scheme_dir=$1
	ensure_dir "$scheme_dir" "exist"
	ensure_file "$scheme_dir/action" "exist" "600"
	test_access_pattern "$scheme_dir/access_pattern"
	ensure_file "$scheme_dir/apply_interval_us" "exist" "600"
	test_quotas "$scheme_dir/quotas"
	test_watermarks "$scheme_dir/watermarks"
	test_filters "$scheme_dir/filters"
	test_stats "$scheme_dir/stats"
	test_tried_regions "$scheme_dir/tried_regions"
}

test_schemes()
{
	schemes_dir=$1
	ensure_dir "$schemes_dir" "exist"
	ensure_file "$schemes_dir/nr_schemes" "exist" 600

	ensure_write_succ  "$schemes_dir/nr_schemes" "1" "valid input"
	test_scheme "$schemes_dir/0"

	ensure_write_succ  "$schemes_dir/nr_schemes" "2" "valid input"
	test_scheme "$schemes_dir/0"
	test_scheme "$schemes_dir/1"

	ensure_write_succ "$schemes_dir/nr_schemes" "0" "valid input"
	ensure_dir "$schemes_dir/0" "not_exist"
	ensure_dir "$schemes_dir/1" "not_exist"
}

test_region()
{
	region_dir=$1
	ensure_dir "$region_dir" "exist"
	ensure_file "$region_dir/start" "exist" 600
	ensure_file "$region_dir/end" "exist" 600
}

test_regions()
{
	regions_dir=$1
	ensure_dir "$regions_dir" "exist"
	ensure_file "$regions_dir/nr_regions" "exist" 600

	ensure_write_succ  "$regions_dir/nr_regions" "1" "valid input"
	test_region "$regions_dir/0"

	ensure_write_succ  "$regions_dir/nr_regions" "2" "valid input"
	test_region "$regions_dir/0"
	test_region "$regions_dir/1"

	ensure_write_succ "$regions_dir/nr_regions" "0" "valid input"
	ensure_dir "$regions_dir/0" "not_exist"
	ensure_dir "$regions_dir/1" "not_exist"
}

test_target()
{
	target_dir=$1
	ensure_dir "$target_dir" "exist"
	ensure_file "$target_dir/pid_target" "exist" "600"
	test_regions "$target_dir/regions"
}

test_targets()
{
	targets_dir=$1
	ensure_dir "$targets_dir" "exist"
	ensure_file "$targets_dir/nr_targets" "exist" 600

	ensure_write_succ  "$targets_dir/nr_targets" "1" "valid input"
	test_target "$targets_dir/0"

	ensure_write_succ  "$targets_dir/nr_targets" "2" "valid input"
	test_target "$targets_dir/0"
	test_target "$targets_dir/1"

	ensure_write_succ "$targets_dir/nr_targets" "0" "valid input"
	ensure_dir "$targets_dir/0" "not_exist"
	ensure_dir "$targets_dir/1" "not_exist"
}

test_intervals()
{
	intervals_dir=$1
	ensure_dir "$intervals_dir" "exist"
	ensure_file "$intervals_dir/aggr_us" "exist" "600"
	ensure_file "$intervals_dir/sample_us" "exist" "600"
	ensure_file "$intervals_dir/update_us" "exist" "600"
}

test_monitoring_attrs()
{
	monitoring_attrs_dir=$1
	ensure_dir "$monitoring_attrs_dir" "exist"
	test_intervals "$monitoring_attrs_dir/intervals"
	test_range "$monitoring_attrs_dir/nr_regions"
}

test_context()
{
	context_dir=$1
	ensure_dir "$context_dir" "exist"
	ensure_file "$context_dir/avail_operations" "exit" 400
	ensure_file "$context_dir/operations" "exist" 600
	test_monitoring_attrs "$context_dir/monitoring_attrs"
	test_targets "$context_dir/targets"
	test_schemes "$context_dir/schemes"
}

test_contexts()
{
	contexts_dir=$1
	ensure_dir "$contexts_dir" "exist"
	ensure_file "$contexts_dir/nr_contexts" "exist" 600

	ensure_write_succ  "$contexts_dir/nr_contexts" "1" "valid input"
	test_context "$contexts_dir/0"

	ensure_write_fail "$contexts_dir/nr_contexts" "2" "only 0/1 are supported"
	test_context "$contexts_dir/0"

	ensure_write_succ "$contexts_dir/nr_contexts" "0" "valid input"
	ensure_dir "$contexts_dir/0" "not_exist"
}

test_kdamond()
{
	kdamond_dir=$1
	ensure_dir "$kdamond_dir" "exist"
	ensure_file "$kdamond_dir/state" "exist" "600"
	ensure_file "$kdamond_dir/pid" "exist" 400
	test_contexts "$kdamond_dir/contexts"
}

test_kdamonds()
{
	kdamonds_dir=$1
	ensure_dir "$kdamonds_dir" "exist"

	ensure_file "$kdamonds_dir/nr_kdamonds" "exist" "600"

	ensure_write_succ  "$kdamonds_dir/nr_kdamonds" "1" "valid input"
	test_kdamond "$kdamonds_dir/0"

	ensure_write_succ  "$kdamonds_dir/nr_kdamonds" "2" "valid input"
	test_kdamond "$kdamonds_dir/0"
	test_kdamond "$kdamonds_dir/1"

	ensure_write_succ "$kdamonds_dir/nr_kdamonds" "0" "valid input"
	ensure_dir "$kdamonds_dir/0" "not_exist"
	ensure_dir "$kdamonds_dir/1" "not_exist"
}

test_damon_sysfs()
{
	damon_sysfs=$1
	if [ ! -d "$damon_sysfs" ]
	then
		echo "$damon_sysfs not found"
		exit $ksft_skip
	fi

	test_kdamonds "$damon_sysfs/kdamonds"
}

check_dependencies()
{
	if [ $EUID -ne 0 ]
	then
		echo "Run as root"
		exit $ksft_skip
	fi
}

check_dependencies
test_damon_sysfs "/sys/kernel/mm/damon/admin"
