# $FreeBSD$

PIDFILE=ggated.pid
PLAINFILES=plainfiles
PORT=33080
CONF=gg.exports

atf_test_case ggated cleanup
ggated_head()
{
	atf_set "descr" "ggated can proxy geoms"
	atf_set "require.progs" "ggatec ggated"
	atf_set "require.user" "root"
	atf_set "timeout" 60
}

ggated_body()
{
	load_ggate

	us=$(alloc_ggate_dev)
	work=$(alloc_md)
	src=$(alloc_md)

	atf_check -e ignore -o ignore \
	    dd if=/dev/random of=/dev/$work bs=1m count=1 conv=notrunc
	atf_check -e ignore -o ignore \
	    dd if=/dev/random of=/dev/$src bs=1m count=1 conv=notrunc

	echo $CONF >> $PLAINFILES
	echo "127.0.0.1 RW /dev/$work" > $CONF

	atf_check ggated -p $PORT -F $PIDFILE $CONF
	atf_check ggatec create -p $PORT -u $us 127.0.0.1 /dev/$work

	ggate_dev=/dev/ggate${us}

	wait_for_ggate_device ${ggate_dev}

	atf_check -e ignore -o ignore \
	    dd if=/dev/${src} of=${ggate_dev} bs=1m count=1 conv=notrunc

	checksum /dev/$src /dev/$work
}

ggated_cleanup()
{
	common_cleanup
}

atf_test_case ggatel_file cleanup
ggatel_file_head()
{
	atf_set "descr" "ggatel can proxy files"
	atf_set "require.progs" "ggatel"
	atf_set "require.user" "root"
	atf_set "timeout" 15
}

ggatel_file_body()
{
	load_ggate

	us=$(alloc_ggate_dev)

	echo src work >> ${PLAINFILES}
	dd if=/dev/random of=work bs=1m count=1
	dd if=/dev/random of=src bs=1m count=1

	atf_check ggatel create -u $us work

	ggate_dev=/dev/ggate${us}

	wait_for_ggate_device ${ggate_dev}

	atf_check -e ignore -o ignore \
	    dd if=src of=${ggate_dev} bs=1m count=1 conv=notrunc

	checksum src work
}

ggatel_file_cleanup()
{
	common_cleanup
}

atf_test_case ggatel_md cleanup
ggatel_md_head()
{
	atf_set "descr" "ggatel can proxy files"
	atf_set "require.progs" "ggatel"
	atf_set "require.user" "root"
	atf_set "timeout" 15
}

ggatel_md_body()
{
	load_ggate

	us=$(alloc_ggate_dev)
	work=$(alloc_md)
	src=$(alloc_md)

	atf_check -e ignore -o ignore \
	    dd if=/dev/random of=$work bs=1m count=1 conv=notrunc
	atf_check -e ignore -o ignore \
	    dd if=/dev/random of=$src bs=1m count=1 conv=notrunc

	atf_check ggatel create -u $us /dev/$work

	ggate_dev=/dev/ggate${us}

	wait_for_ggate_device ${ggate_dev}

	atf_check -e ignore -o ignore \
	    dd if=/dev/$src of=${ggate_dev} bs=1m count=1 conv=notrunc

	checksum /dev/$src /dev/$work
}

ggatel_md_cleanup()
{
	common_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case ggated
	atf_add_test_case ggatel_file
	atf_add_test_case ggatel_md
}

alloc_ggate_dev()
{
	local us

	us=0
	while [ -c /dev/ggate${us} ]; do
		: $(( us += 1 ))
	done
	echo ${us} > ggate.devs
	echo ${us}
}

alloc_md()
{
	local md

	md=$(mdconfig -a -t malloc -s 1M) || \
		atf_fail "failed to allocate md device"
	echo ${md} >> md.devs
	echo ${md}
}

checksum()
{
	local src work
	src=$1
	work=$2

	src_checksum=$(md5 -q $src)
	work_checksum=$(md5 -q $work)

	if [ "$work_checksum" != "$src_checksum" ]; then
		atf_fail "work md5 checksum didn't match"
	fi

	ggate_checksum=$(md5 -q /dev/ggate${us})
	if [ "$ggate_checksum" != "$src_checksum" ]; then
		atf_fail "ggate md5 checksum didn't match"
	fi
}

common_cleanup()
{
	if [ -f "ggate.devs" ]; then
		while read test_ggate; do
			ggatec destroy -f -u $test_ggate >/dev/null
		done < ggate.devs
		rm ggate.devs
	fi

	if [ -f "$PIDFILE" ]; then
		pkill -F "$PIDFILE"
		rm $PIDFILE
	fi

	if [ -f "PLAINFILES" ]; then
		while read f; do
			rm -f ${f}
		done < ${PLAINFILES}
		rm ${PLAINFILES}
	fi

	if [ -f "md.devs" ]; then
		while read test_md; do
			mdconfig -d -u $test_md 2>/dev/null
		done < md.devs
		rm md.devs
	fi
	true
}

load_ggate()
{
	local class=gate

	# If the geom class isn't already loaded, try loading it.
	if ! kldstat -q -m g_${class}; then
		if ! geom ${class} load; then
			atf_skip "could not load module for geom class=${class}"
		fi
	fi
}

# Bug 204616: ggatel(8) creates /dev/ggate* asynchronously if `ggatel create`
#             isn't called with `-v`.
wait_for_ggate_device()
{
	ggate_device=$1

	while [ ! -c $ggate_device ]; do
		sleep 0.5
	done
}
