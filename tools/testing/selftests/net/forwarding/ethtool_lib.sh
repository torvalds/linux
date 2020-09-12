#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

speeds_arr_get()
{
	cmd='/ETHTOOL_LINK_MODE_[^[:space:]]*_BIT[[:space:]]+=[[:space:]]+/ \
		{sub(/,$/, "") \
		sub(/ETHTOOL_LINK_MODE_/,"") \
		sub(/_BIT/,"") \
		sub(/_Full/,"/Full") \
		sub(/_Half/,"/Half");\
		print "["$1"]="$3}'

	awk "${cmd}" /usr/include/linux/ethtool.h
}

ethtool_set()
{
	local cmd="$@"
	local out=$(ethtool -s $cmd 2>&1 | wc -l)

	check_err $out "error in configuration. $cmd"
}

dev_speeds_get()
{
	local dev=$1; shift
	local with_mode=$1; shift
	local adver=$1; shift
	local speeds_str

	if (($adver)); then
		mode="Advertised link modes"
	else
		mode="Supported link modes"
	fi

	speeds_str=$(ethtool "$dev" | \
		# Snip everything before the link modes section.
		sed -n '/'"$mode"':/,$p' | \
		# Quit processing the rest at the start of the next section.
		# When checking, skip the header of this section (hence the 2,).
		sed -n '2,${/^[\t][^ \t]/q};p' | \
		# Drop the section header of the current section.
		cut -d':' -f2)

	local -a speeds_arr=($speeds_str)
	if [[ $with_mode -eq 0 ]]; then
		for ((i=0; i<${#speeds_arr[@]}; i++)); do
			speeds_arr[$i]=${speeds_arr[$i]%base*}
		done
	fi
	echo ${speeds_arr[@]}
}

common_speeds_get()
{
	dev1=$1; shift
	dev2=$1; shift
	with_mode=$1; shift
	adver=$1; shift

	local -a dev1_speeds=($(dev_speeds_get $dev1 $with_mode $adver))
	local -a dev2_speeds=($(dev_speeds_get $dev2 $with_mode $adver))

	comm -12 \
		<(printf '%s\n' "${dev1_speeds[@]}" | sort -u) \
		<(printf '%s\n' "${dev2_speeds[@]}" | sort -u)
}

different_speeds_get()
{
	local dev1=$1; shift
	local dev2=$1; shift
	local with_mode=$1; shift
	local adver=$1; shift

	local -a speeds_arr

	speeds_arr=($(common_speeds_get $dev1 $dev2 $with_mode $adver))
	if [[ ${#speeds_arr[@]} < 2 ]]; then
		check_err 1 "cannot check different speeds. There are not enough speeds"
	fi

	echo ${speeds_arr[0]} ${speeds_arr[1]}
}
