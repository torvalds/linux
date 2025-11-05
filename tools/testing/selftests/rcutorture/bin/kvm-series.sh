#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Usage: kvm-series.sh config-list commit-id-list [ kvm.sh parameters ]
#
# Tests the specified list of unadorned configs ("TREE01 SRCU-P" but not
# "CFLIST" or "3*TRACE01") and an indication of a set of commits to test,
# then runs each commit through the specified list of commits using kvm.sh.
# The runs are grouped into a -series/config/commit directory tree.
# Each run defaults to a duration of one minute.
#
# Run in top-level Linux source directory.  Please note that this is in
# no way a replacement for "git bisect"!!!
#
# This script is intended to replace kvm-check-branches.sh by providing
# ease of use and faster execution.

T="`mktemp -d ${TMPDIR-/tmp}/kvm-series.sh.XXXXXX`"
trap 'rm -rf $T' 0

scriptname=$0
args="$*"

config_list="${1}"
if test -z "${config_list}"
then
	echo "$0: Need a quoted list of --config arguments for first argument."
	exit 1
fi
if test -z "${config_list}" || echo "${config_list}" | grep -q '\*'
then
	echo "$0: Repetition ('*') not allowed in config list."
	exit 1
fi

commit_list="${2}"
if test -z "${commit_list}"
then
	echo "$0: Need a list of commits (e.g., HEAD^^^..) for second argument."
	exit 2
fi
git log --pretty=format:"%h" "${commit_list}" > $T/commits
ret=$?
if test "${ret}" -ne 0
then
	echo "$0: Invalid commit list ('${commit_list}')."
	exit 2
fi
sha1_list=`cat $T/commits`

shift
shift

RCUTORTURE="`pwd`/tools/testing/selftests/rcutorture"; export RCUTORTURE
PATH=${RCUTORTURE}/bin:$PATH; export PATH
. functions.sh

ret=0
nfail=0
nsuccess=0
faillist=
successlist=
cursha1="`git rev-parse --abbrev-ref HEAD`"
ds="`date +%Y.%m.%d-%H.%M.%S`-series"
startdate="`date`"
starttime="`get_starttime`"

echo " --- " $scriptname $args | tee -a $T/log
echo " --- Results directory: " $ds | tee -a $T/log

for config in ${config_list}
do
	sha_n=0
	for sha in ${sha1_list}
	do
		sha1=${sha_n}.${sha} # Enable "sort -k1nr" to list commits in order.
		echo Starting ${config}/${sha1} at `date` | tee -a $T/log
		git checkout "${sha}"
		time tools/testing/selftests/rcutorture/bin/kvm.sh --configs "$config" --datestamp "$ds/${config}/${sha1}" --duration 1 "$@"
		curret=$?
		if test "${curret}" -ne 0
		then
			nfail=$((nfail+1))
			faillist="$faillist ${config}/${sha1}(${curret})"
		else
			nsuccess=$((nsuccess+1))
			successlist="$successlist ${config}/${sha1}"
			# Successful run, so remove large files.
			rm -f ${RCUTORTURE}/$ds/${config}/${sha1}/{vmlinux,bzImage,System.map,Module.symvers}
		fi
		if test "${ret}" -eq 0
		then
			ret=${curret}
		fi
		sha_n=$((sha_n+1))
	done
done
git checkout "${cursha1}"

echo ${nsuccess} SUCCESSES: | tee -a $T/log
echo ${successlist} | fmt | tee -a $T/log
echo | tee -a $T/log
echo ${nfail} FAILURES: | tee -a $T/log
echo ${faillist} | fmt | tee -a $T/log
if test -n "${faillist}"
then
	echo | tee -a $T/log
	echo Failures across commits: | tee -a $T/log
	echo ${faillist} | tr ' ' '\012' | sed -e 's,^[^/]*/,,' -e 's/([0-9]*)//' |
		sort | uniq -c | sort -k2n | tee -a $T/log
fi
echo Started at $startdate, ended at `date`, duration `get_starttime_duration $starttime`. | tee -a $T/log
echo Summary: Successes: ${nsuccess} Failures: ${nfail} | tee -a $T/log
cp $T/log tools/testing/selftests/rcutorture/res/${ds}

exit "${ret}"
