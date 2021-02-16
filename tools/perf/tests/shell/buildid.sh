#!/bin/sh
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

ex_md5=$(mktemp /tmp/perf.ex.MD5.XXX)
ex_sha1=$(mktemp /tmp/perf.ex.SHA1.XXX)

echo 'int main(void) { return 0; }' | cc -Wl,--build-id=sha1 -o ${ex_sha1} -x c -
echo 'int main(void) { return 0; }' | cc -Wl,--build-id=md5 -o ${ex_md5} -x c -

echo "test binaries: ${ex_sha1} ${ex_md5}"

check()
{
	id=`readelf -n ${1} 2>/dev/null | grep 'Build ID' | awk '{print $3}'`

	echo "build id: ${id}"

	link=${build_id_dir}/.build-id/${id:0:2}/${id:2}
	echo "link: ${link}"

	if [ ! -h $link ]; then
		echo "failed: link ${link} does not exist"
		exit 1
	fi

	file=${build_id_dir}/.build-id/${id:0:2}/`readlink ${link}`/elf
	echo "file: ${file}"

	if [ ! -x $file ]; then
		echo "failed: file ${file} does not exist"
		exit 1
	fi

	diff ${file} ${1}
	if [ $? -ne 0 ]; then
		echo "failed: ${file} do not match"
		exit 1
	fi

	${perf} buildid-cache -l | grep $id
	if [ $? -ne 0 ]; then
		echo "failed: ${id} is not reported by \"perf buildid-cache -l\""
		exit 1
	fi

	echo "OK for ${1}"
}

test_add()
{
	build_id_dir=$(mktemp -d /tmp/perf.debug.XXX)
	perf="perf --buildid-dir ${build_id_dir}"

	${perf} buildid-cache -v -a ${1}
	if [ $? -ne 0 ]; then
		echo "failed: add ${1} to build id cache"
		exit 1
	fi

	check ${1}

	rm -rf ${build_id_dir}
}

test_record()
{
	data=$(mktemp /tmp/perf.data.XXX)
	build_id_dir=$(mktemp -d /tmp/perf.debug.XXX)
	perf="perf --buildid-dir ${build_id_dir}"

	${perf} record --buildid-all -o ${data} ${1}
	if [ $? -ne 0 ]; then
		echo "failed: record ${1}"
		exit 1
	fi

	check ${1}

	rm -rf ${build_id_dir}
	rm -rf ${data}
}

# add binaries manual via perf buildid-cache -a
test_add ${ex_sha1}
test_add ${ex_md5}

# add binaries via perf record post processing
test_record ${ex_sha1}
test_record ${ex_md5}

# cleanup
rm ${ex_sha1} ${ex_md5}

exit ${err}
