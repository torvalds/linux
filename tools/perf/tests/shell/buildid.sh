#!/bin/bash
# build id cache operations
# SPDX-License-Identifier: GPL-2.0

# skip if there's no readelf
if ! [ -x "$(command -v readelf)" ]; then
	echo "failed: no readelf, install binutils"
	exit 2
fi

# skip if there's no compiler
if ! [ -x "$(command -v cc)" ]; then
	echo "failed: no compiler, install gcc"
	exit 2
fi

# check what we need to test windows binaries
add_pe=1
run_pe=1
if ! perf version --build-options | grep -q 'libbfd: .* on '; then
	echo "WARNING: perf not built with libbfd. PE binaries will not be tested."
	add_pe=0
	run_pe=0
fi
if ! which wine > /dev/null; then
	echo "WARNING: wine not found. PE binaries will not be run."
	run_pe=0
fi

# set up wine
if [ ${run_pe} -eq 1 ]; then
	wineprefix=$(mktemp -d /tmp/perf.wineprefix.XXX)
	export WINEPREFIX=${wineprefix}
	# clear display variables to prevent wine from popping up dialogs
	unset DISPLAY
	unset WAYLAND_DISPLAY
fi

build_id_dir=
ex_source=$(mktemp /tmp/perf_buildid_test.ex.XXX.c)
ex_md5=$(mktemp /tmp/perf_buildid_test.ex.MD5.XXX)
ex_sha1=$(mktemp /tmp/perf_buildid_test.ex.SHA1.XXX)
ex_pe=$(dirname $0)/../pe-file.exe
data=$(mktemp /tmp/perf_buildid_test.data.XXX)
log_out=$(mktemp /tmp/perf_buildid_test.log.out.XXX)
log_err=$(mktemp /tmp/perf_buildid_test.log.err.XXX)

cleanup() {
	rm -f ${ex_source} ${ex_md5} ${ex_sha1} ${data} ${log_out} ${log_err}
	if [ ${run_pe} -eq 1 ]; then
		rm -r ${wineprefix}
	fi
        if [ -d ${build_id_dir} ]; then
		rm -rf ${build_id_dir}
        fi
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

# Test program based on the noploop workload.
cat <<EOF > ${ex_source}
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t done;

static void sighandler(int sig)
{
	(void)sig;
	done = 1;
}

int main(int argc, const char **argv)
{
	int sec = 1;

	if (argc > 1)
		sec = atoi(argv[1]);

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);
	alarm(sec);

	while (!done)
		continue;

	return 0;
}
EOF
cc -Wl,--build-id=sha1 ${ex_source} -o ${ex_sha1} -x c -
cc -Wl,--build-id=md5 ${ex_source} -o ${ex_md5} -x c -
echo "test binaries: ${ex_sha1} ${ex_md5} ${ex_pe}"

get_build_id()
{
	case $1 in
	*.exe)
		# We don't have a tool that can pull a nicely formatted build-id out of
		# a PE file, but we can extract the whole section with objcopy and
		# format it ourselves. The .buildid section is a Debug Directory
		# containing a CodeView entry:
		#     https://docs.microsoft.com/en-us/windows/win32/debug/pe-format#debug-directory-image-only
		#     https://github.com/dotnet/runtime/blob/da94c022576a5c3bbc0e896f006565905eb137f9/docs/design/specs/PE-COFF.md
		# The build-id starts at byte 33 and must be rearranged into a GUID.
		id=`objcopy -O binary --only-section=.buildid $1 /dev/stdout | \
			cut -c 33-48 | hexdump -ve '/1 "%02x"' | \
			sed 's@^\(..\)\(..\)\(..\)\(..\)\(..\)\(..\)\(..\)\(..\)\(.*\)0a$@\4\3\2\1\6\5\8\7\9@'`
		;;
	*)
		id=`readelf -n ${1} 2>/dev/null | grep 'Build ID' | awk '{print $3}'`
		;;
	esac
	echo ${id}
}

check()
{
	file=$1
	perf_data=$2

	id=$(get_build_id $file)
	echo "build id: ${id}"

	id_file=${id#??}
	id_dir=${id%$id_file}
	link=$build_id_dir/.build-id/$id_dir/$id_file
	echo "link: ${link}"

	if [ ! -h $link ]; then
		echo "failed: link ${link} does not exist"
		exit 1
	fi

	cached_file=${build_id_dir}/.build-id/$id_dir/`readlink ${link}`/elf
	echo "file: ${cached_file}"

	# Check for file permission of original file
	# in case of pe-file.exe file
	echo $1 | grep ".exe"
	if [ $? -eq 0 ]; then
		if [ -x $1 ] && [ ! -x $cached_file ]; then
			echo "failed: file ${cached_file} executable does not exist"
			exit 1
		fi

		if [ ! -x $cached_file ] && [ ! -e $cached_file ]; then
			echo "failed: file ${cached_file} does not exist"
			exit 1
		fi
	elif [ ! -x $cached_file ]; then
		echo "failed: file ${cached_file} does not exist"
		exit 1
	fi

	diff ${cached_file} ${1}
	if [ $? -ne 0 ]; then
		echo "failed: ${cached_file} do not match"
		exit 1
	fi

	${perf} buildid-cache -l | grep -q ${id}
	if [ $? -ne 0 ]; then
		echo "failed: ${id} is not reported by \"perf buildid-cache -l\""
		exit 1
	fi

	if [ -n "${perf_data}" ]; then
		${perf} buildid-list -i ${perf_data} | grep -q ${id}
		if [ $? -ne 0 ]; then
			echo "failed: ${id} is not reported by \"perf buildid-list -i ${perf_data}\""
			exit 1
		fi
	fi

	echo "OK for ${1}"
}

test_add()
{
	build_id_dir=$(mktemp -d /tmp/perf_buildid_test.debug.XXX)
	perf="perf --buildid-dir ${build_id_dir}"

	${perf} buildid-cache -v -a ${1}
	if [ $? -ne 0 ]; then
		echo "failed: add ${1} to build id cache"
		exit 1
	fi

	check ${1}

	rm -rf ${build_id_dir}
}

test_remove()
{
	build_id_dir=$(mktemp -d /tmp/perf_buildid_test.debug.XXX)
	perf="perf --buildid-dir ${build_id_dir}"

	${perf} buildid-cache -v -a ${1}
	if [ $? -ne 0 ]; then
		echo "failed: add ${1} to build id cache"
		exit 1
	fi

	id=$(get_build_id ${1})
	if ! ${perf} buildid-cache -l | grep -q ${id}; then
		echo "failed: ${id} not in cache"
		exit 1
	fi

	${perf} buildid-cache -v -r ${1}
	if [ $? -ne 0 ]; then
		echo "failed: remove ${id} from build id cache"
		exit 1
	fi

	if ${perf} buildid-cache -l | grep -q ${id}; then
		echo "failed: ${id} still in cache after remove"
		exit 1
	fi

	echo "remove: OK"
	rm -rf ${build_id_dir}
}

test_purge()
{
	build_id_dir=$(mktemp -d /tmp/perf_buildid_test.debug.XXX)
	perf="perf --buildid-dir ${build_id_dir}"

	id1=$(get_build_id ${ex_sha1})
	${perf} buildid-cache -v -a ${ex_sha1}
	if ! ${perf} buildid-cache -l | grep -q ${id1}; then
		echo "failed: ${id1} not in cache"
		exit 1
	fi

	id2=$(get_build_id ${ex_md5})
	${perf} buildid-cache -v -a ${ex_md5}
	if ! ${perf} buildid-cache -l | grep -q ${id2}; then
		echo "failed: ${id2} not in cache"
		exit 1
	fi

	# Purge by path
	${perf} buildid-cache -v -p ${ex_sha1}
	if [ $? -ne 0 ]; then
		echo "failed: purge build id cache of ${ex_sha1}"
		exit 1
	fi

	${perf} buildid-cache -v -p ${ex_md5}
	if [ $? -ne 0 ]; then
		echo "failed: purge build id cache of ${ex_md5}"
		exit 1
	fi

	# Verify both are gone
	if ${perf} buildid-cache -l | grep -q ${id1}; then
		echo "failed: ${id1} still in cache after purge"
		exit 1
	fi

	if ${perf} buildid-cache -l | grep -q ${id2}; then
		echo "failed: ${id2} still in cache after purge"
		exit 1
	fi

	echo "purge: OK"
	rm -rf ${build_id_dir}
}

test_record()
{
	build_id_dir=$(mktemp -d /tmp/perf_buildid_test.debug.XXX)
	perf="perf --buildid-dir ${build_id_dir}"

	echo "running: perf record $*"
	${perf} record --buildid-all -o ${data} "$@" 1>${log_out} 2>${log_err}
	if [ $? -ne 0 ]; then
		echo "failed: record $*"
		echo "see log: ${log_err}"
		exit 1
	fi

	args="$*"
	check ${args##* } ${data}

	rm -f ${log_out} ${log_err}
	rm -rf ${build_id_dir}
	rm -rf ${data}
}

# add binaries manual via perf buildid-cache -a
test_add ${ex_sha1}
test_add ${ex_md5}
if [ ${add_pe} -eq 1 ]; then
	test_add ${ex_pe}
fi

# add binaries via perf record post processing
test_record ${ex_sha1}
test_record ${ex_md5}
if [ ${run_pe} -eq 1 ]; then
	test_record wine ${ex_pe}
fi

# remove binaries manually via perf buildid-cache -r
test_remove ${ex_sha1}
test_remove ${ex_md5}
if [ ${add_pe} -eq 1 ]; then
	test_remove ${ex_pe}
fi

# purge binaries manually via perf buildid-cache -p
test_purge

cleanup
exit 0
