#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

RUN_BENCH="sudo ./bench -w3 -d10 -a"

function header()
{
	local len=${#1}

	printf "\n%s\n" "$1"
	for i in $(seq 1 $len); do printf '='; done
	printf '\n'
}

function subtitle()
{
	local len=${#1}
	printf "\t%s\n" "$1"
}

function hits()
{
	echo "$*" | sed -E "s/.*hits\s+([0-9]+\.[0-9]+ ± [0-9]+\.[0-9]+M\/s).*/\1/"
}

function drops()
{
	echo "$*" | sed -E "s/.*drops\s+([0-9]+\.[0-9]+ ± [0-9]+\.[0-9]+M\/s).*/\1/"
}

function percentage()
{
	echo "$*" | sed -E "s/.*Percentage\s=\s+([0-9]+\.[0-9]+).*/\1/"
}

function total()
{
	echo "$*" | sed -E "s/.*total operations\s+([0-9]+\.[0-9]+ ± [0-9]+\.[0-9]+M\/s).*/\1/"
}

function summarize()
{
	bench="$1"
	summary=$(echo $2 | tail -n1)
	printf "%-20s %s (drops %s)\n" "$bench" "$(hits $summary)" "$(drops $summary)"
}

function summarize_percentage()
{
	bench="$1"
	summary=$(echo $2 | tail -n1)
	printf "%-20s %s%%\n" "$bench" "$(percentage $summary)"
}

function summarize_total()
{
	bench="$1"
	summary=$(echo $2 | tail -n1)
	printf "%-20s %s\n" "$bench" "$(total $summary)"
}
