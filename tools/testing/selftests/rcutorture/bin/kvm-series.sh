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

T="`mktemp -d ${TMPDIR-/tmp}/kvm-series.sh.XXXXXX`"; export T
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
config_list_len="`echo ${config_list} | wc -w | awk '{ print $1; }'`"

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
sha1_list_len="`echo ${sha1_list} | wc -w | awk '{ print $1; }'`"

shift
shift

RCUTORTURE="`pwd`/tools/testing/selftests/rcutorture"; export RCUTORTURE
PATH=${RCUTORTURE}/bin:$PATH; export PATH
RES="${RCUTORTURE}/res"; export RES
. functions.sh

ret=0
nbuildfail=0
nrunfail=0
nsuccess=0
ncpus=0
buildfaillist=
runfaillist=
successlist=
cursha1="`git rev-parse --abbrev-ref HEAD`"
ds="`date +%Y.%m.%d-%H.%M.%S`-series"
DS="${RES}/${ds}"; export DS
startdate="`date`"
starttime="`get_starttime`"

echo " --- " $scriptname $args | tee -a $T/log
echo " --- Results directory: " $ds | tee -a $T/log

# Do all builds.  Iterate through commits within a given scenario
# because builds normally go faster from one commit to the next within a
# given scenario.  In contrast, switching scenarios on each rebuild will
# often force a full rebuild due to Kconfig differences, for example,
# turning preemption on and off.  Defer actual runs in order to run
# lots of them concurrently on large systems.
touch $T/torunlist
n2build="$((config_list_len*sha1_list_len))"
nbuilt=0
for config in ${config_list}
do
	sha_n=0
	for sha in ${sha1_list}
	do
		sha1=${sha_n}.${sha} # Enable "sort -k1nr" to list commits in order.
		echo
		echo Starting ${config}/${sha1} "($((nbuilt+1)) of ${n2build})" at `date` | tee -a $T/log
		git checkout --detach "${sha}"
		tools/testing/selftests/rcutorture/bin/kvm.sh --configs "$config" --datestamp "$ds/${config}/${sha1}" --duration 1 --build-only --trust-make "$@"
		curret=$?
		if test "${curret}" -ne 0
		then
			nbuildfail=$((nbuildfail+1))
			buildfaillist="$buildfaillist ${config}/${sha1}(${curret})"
		else
			batchncpus="`grep -v "^# cpus=" "${DS}/${config}/${sha1}/batches" | awk '{ sum += $3 } END { print sum }'`"
			echo run_one_qemu ${sha_n} ${config}/${sha1} ${batchncpus} >> $T/torunlist
			if test "${ncpus}" -eq 0
			then
				ncpus="`grep "^# cpus=" "${DS}/${config}/${sha1}/batches" | sed -e 's/^# cpus=//'`"
				case "${ncpus}" in
				^[0-9]*$)
					;;
				*)
					ncpus=0
					;;
				esac
			fi
		fi
		if test "${ret}" -eq 0
		then
			ret=${curret}
		fi
		sha_n=$((sha_n+1))
		nbuilt=$((nbuilt+1))
	done
done

# If the user did not specify the number of CPUs, use them all.
if test "${ncpus}" -eq 0
then
	ncpus="`identify_qemu_vcpus`"
fi

cpusused=0
touch $T/successlistfile
touch $T/faillistfile
n2run="`wc -l $T/torunlist | awk '{ print $1; }'`"
nrun=0

# do_run_one_qemu ds resultsdir qemu_curout
#
# Start the specified qemu run and record its success or failure.
do_run_one_qemu () {
	local ret
	local ds="$1"
	local resultsdir="$2"
	local qemu_curout="$3"

	tools/testing/selftests/rcutorture/bin/kvm-again.sh "${DS}/${resultsdir}" --link inplace-force > ${qemu_curout} 2>&1
	ret=$?
	if test "${ret}" -eq 0
	then
		echo ${resultsdir} >> $T/successlistfile
		# Successful run, so remove large files.
		rm -f ${DS}/${resultsdir}/{vmlinux,bzImage,System.map,Module.symvers}
	else
		echo "${resultsdir}(${ret})" >> $T/faillistfile
	fi
}

# cleanup_qemu_batch batchncpus
#
# Update success and failure lists, files, and counts at the end of
# a batch.
cleanup_qemu_batch () {
	local batchncpus="$1"

	echo Waiting, cpusused=${cpusused}, ncpus=${ncpus} `date` | tee -a $T/log
	wait
	cpusused="${batchncpus}"
	nsuccessbatch="`wc -l $T/successlistfile | awk '{ print $1 }'`"
	nsuccess=$((nsuccess+nsuccessbatch))
	successlist="$successlist `cat $T/successlistfile`"
	rm $T/successlistfile
	touch $T/successlistfile
	nfailbatch="`wc -l $T/faillistfile | awk '{ print $1 }'`"
	nrunfail=$((nrunfail+nfailbatch))
	runfaillist="$runfaillist `cat $T/faillistfile`"
	rm $T/faillistfile
	touch $T/faillistfile
}

# run_one_qemu sha_n config/sha1 batchncpus
#
# Launch into the background the sha_n-th qemu job whose results directory
# is config/sha1 and which uses batchncpus CPUs.  Once we reach a job that
# would overflow the number of available CPUs, wait for the previous jobs
# to complete and record their results.
run_one_qemu () {
	local sha_n="$1"
	local config_sha1="$2"
	local batchncpus="$3"
	local qemu_curout

	cpusused=$((cpusused+batchncpus))
	if test "${cpusused}" -gt $ncpus
	then
		cleanup_qemu_batch "${batchncpus}"
	fi
	echo Starting ${config_sha1} using ${batchncpus} CPUs "($((nrun+1)) of ${n2run})" `date`
	qemu_curout="${DS}/${config_sha1}/qemu-series"
	do_run_one_qemu "$ds" "${config_sha1}" ${qemu_curout} &
	nrun="$((nrun+1))"
}

# Re-ordering the runs will mess up the affinity chosen at build time
# (among other things, over-using CPU 0), so suppress it.
TORTURE_NO_AFFINITY="no-affinity"; export TORTURE_NO_AFFINITY

# Run the kernels (if any) that built correctly.
echo | tee -a $T/log # Put a blank line between build and run messages.
. $T/torunlist
cleanup_qemu_batch "${batchncpus}"

# Get back to initial checkout/SHA-1.
git checkout "${cursha1}"

# Throw away leading and trailing space characters for fmt.
successlist="`echo ${successlist} | sed -e 's/^ *//' -e 's/ *$//'`"
buildfaillist="`echo ${buildfaillist} | sed -e 's/^ *//' -e 's/ *$//'`"
runfaillist="`echo ${runfaillist} | sed -e 's/^ *//' -e 's/ *$//'`"

# Print lists of successes, build failures, and run failures, if any.
if test "${nsuccess}" -gt 0
then
	echo | tee -a $T/log
	echo ${nsuccess} SUCCESSES: | tee -a $T/log
	echo ${successlist} | fmt | tee -a $T/log
fi
if test "${nbuildfail}" -gt 0
then
	echo | tee -a $T/log
	echo ${nbuildfail} BUILD FAILURES: | tee -a $T/log
	echo ${buildfaillist} | fmt | tee -a $T/log
fi
if test "${nrunfail}" -gt 0
then
	echo | tee -a $T/log
	echo ${nrunfail} RUN FAILURES: | tee -a $T/log
	echo ${runfaillist} | fmt | tee -a $T/log
fi

# If there were build or runtime failures, map them to commits.
if test "${nbuildfail}" -gt 0 || test "${nrunfail}" -gt 0
then
	echo | tee -a $T/log
	echo Build failures across commits: | tee -a $T/log
	echo ${buildfaillist} | tr ' ' '\012' | sed -e 's,^[^/]*/,,' -e 's/([0-9]*)//' |
		sort | uniq -c | sort -k2n | tee -a $T/log
fi

# Print run summary.
echo | tee -a $T/log
echo Started at $startdate, ended at `date`, duration `get_starttime_duration $starttime`. | tee -a $T/log
echo Summary: Successes: ${nsuccess} " "Build Failures: ${nbuildfail} " "Runtime Failures: ${nrunfail}| tee -a $T/log
cp $T/log ${DS}

exit "${ret}"
