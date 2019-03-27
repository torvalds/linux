# $FreeBSD$

ntest=1
os=`uname -s`

echo ${dir} | egrep '^/' >/dev/null 2>&1
if [ $? -eq 0 ]; then
	maindir="${dir}/../.."
else
	maindir="`pwd`/${dir}/../.."
fi

# Set up correct command names and switches
if [ -z "${LUSTRE}" ]; then
	ZPOOL="zpool"
	ZFS="zfs"
	ZDB="zdb"
	zpool_f_flag="-f"
else
	ZPOOL="lzpool"
	ZFS="lzfs"
	ZDB="lzdb"
	zpool_f_flag="-F"
	no_mountpoint=1
fi

# Use correct arguments to cmd line programs
stat --version 2>/dev/null | grep GNU >/dev/null
if [ $? -eq 0 ]; then
	GNU_STAT="yes"
fi
if [ "${os}" = "SunOS" ]; then
	import_flags="-d /dev/lofi"
	mount_t_flag="-F"
else
	mount_t_flag="-t"
fi

die()
{
	echo "${1}" > /dev/stderr
	echo "Bail out!"
	exit 1
}

calcsum()
{
	dd if="${1}" bs=1M 2>/dev/null | openssl md5
}

create_file()
{
	name="${1}"
	size="${2}"

	dd if=/dev/urandom of=${name} bs=${size} count=1 >/dev/null 2>&1
	sync
}

expect()
{
	eorig="${1}"
	eexp=`echo "${eorig}" | egrep -v '^[ 	]*$' | sed 's/^[ 	][ 	]*//g;s/[ 	][ 	]*$//g;s/[ 	][ 	]*/ /g;s/$/%EoL%/' | xargs`
	shift
	gorig=`sh -c "$*" 2>&1`
	got=`echo "${gorig}" | egrep -v '^[ 	]*$' | sed 's/^[ 	][ 	]*//g;s/[ 	][ 	]*$//g;s/[ 	][ 	]*/ /g;s/$/%EoL%/' | xargs`
	echo "${got}" | egrep "${eexp}" >/dev/null
	if [ $? -eq 0 ]; then
		echo "ok ${ntest} ${add_msg}"
	else
		echo "not ok ${ntest} ${add_msg}"
		echo "# ----- expected from: $*"
		echo "${eorig}" | sed 's/^/# /'
		echo "# ----- got:"
		echo "${gorig}" | sed 's/^/# /'
		echo "# ----- end"
	fi
	ntest=`expr $ntest + 1`
}

expect_ok()
{
	out=`$* 2>&1`
	ec=$?
	if [ $ec -eq 0 ]; then
		echo "ok ${ntest} ${add_msg}"
		echo "# ----- expected success from: $*"
		if [ ! -z "${out}" ]; then
			echo "# ----- output (exit code=${ec}):"
			echo "${out}" | sed 's/^/# /'
			echo "# ----- end"
		fi
	else
		echo "not ok ${ntest} ${add_msg}"
		echo "# ----- expected success from: $*"
		echo "# ----- output (exit code=${ec}):"
		echo "${out}" | sed 's/^/# /'
		echo "# ----- end"
	fi
	ntest=`expr $ntest + 1`
}

expect_fl()
{
	out=`$* 2>&1`
	ec=$?
	if [ $ec -ne 0 ]; then
		echo "ok ${ntest} ${add_msg}"
		echo "# ----- expected failure from: $*"
		if [ ! -z "${out}" ]; then
			echo "# ----- output (exit code=${ec}):"
			echo "${out}" | sed 's/^/# /'
			echo "# ----- end"
		fi
	else
		echo "not ok ${ntest} ${add_msg}"
		echo "# ----- expected failure from: $*"
		echo "# ----- output (exit code=${ec}):"
		echo "${out}" | sed 's/^/# /'
		echo "# ----- end"
	fi
	ntest=`expr $ntest + 1`
}

quick_exit()
{
	echo "1..1"
	echo "ok 1"
	exit 0
}

# Set up a scratch tmpfs directory (Linux only)
setup_tmpfs()
{
	cmd="mktemp -d /tmp/zfs-regression.XXXXXXXXXX"
	TMPDIR=`${cmd}` || die "failed: ${cmd}"
	cmd="mount -t tmpfs none ${TMPDIR}"
	${cmd} || die "failed: ${cmd}"
}

# Clean up the tmpfs directory (Linux only)
cleanup_tmpfs()
{
	if [ -n "${TMPDIR}" ]; then
		cmd="umount ${TMPDIR} && rmdir ${TMPDIR}"
		eval "${cmd}" || die "failed: ${cmd}"
	fi
}

# Truncate a file
truncate_cmd()
{
	size="${1}"
	file="${2}"

	cmd="dd if=/dev/null of=${file} bs=1 count=0 seek=${size}"
	${cmd} > /dev/null 2>&1 || die "failed: ${cmd}"
}

# Create a memory-backed block device
create_memdisk()
{
	size="${1}"
	devname="${2}"

	if [ "${os}" = "FreeBSD" ]; then
		if [ -n "${devname}" ]; then
			devparam="-u ${devname}"
		fi
		cmd="mdconfig -a -t swap -s ${size} ${devparam}"
		DISKNAME=`$cmd 2>/dev/null` || die "failed: ${cmd}"
		if [ -n "${devname}" ]; then
			DISKNAME="${devname}"
		fi
		FDISKNAME="/dev/${DISKNAME}"
	elif [ "${os}" = "SunOS" ]; then
		cmd="mktemp /tmp/zfstest.XXXXXXXXXX"
		fname=`${cmd}` || die "failed: ${cmd}"

		truncate_cmd "${size}" "${fname}"

		if [ -n "${devname}" ]; then
			cmd="lofiadm -a ${fname} ${devname}"
			${cmd} || die "failed: ${cmd}"
			DISKNAME="${devname}"
		else
			cmd="lofiadm -a ${fname}"
			DISKNAME=`${cmd}` || die "failed: ${cmd}"
		fi
		FDISKNAME="${DISKNAME}"
	elif [ "${os}" = "Linux" ]; then
		if [ -z "${TMPDIR_DISKS}" ]; then
			setup_tmpfs
			TMPDIR_DISKS="${TMPDIR}"
		fi

		cmd="mktemp ${TMPDIR_DISKS}/disk.XXXXXXXXXX"
		fname=`${cmd}` || die "failed: ${cmd}"

		truncate_cmd "${size}" "${fname}"

		if [ -n "${devname}" ]; then
			devname=`echo ${devname} | cut -c 9-`
			cmd="losetup /dev/${devname} ${fname} 2>&1"
			eval ${cmd} || die "failed: ${cmd}"
			DISKNAME="${devname}"
		else
			cmd="losetup -s -f ${fname} 2>&1"
			diskname=`eval ${cmd}`

			if [ "${diskname}" = "losetup: could not find any free loop device" ]; then
				# If there are no free loopback devices, create one more
				max=`echo /dev/loop* | awk 'BEGIN { RS=" "; FS="loop" } {if ($2 > max) max = $2} END {print max}'`
				max=$((max + 1))
				cmd="mknod /dev/loop${max} b 7 ${max}"
				${cmd} || die "failed: ${cmd}"

				cmd="losetup -s -f ${fname}"
				diskname=`${cmd}` || die "failed: ${cmd}"
			fi
			DISKNAME=`eval echo ${diskname} | sed 's/^\/dev\///'`
		fi
		ln /dev/${DISKNAME} /dev/zfstest_${DISKNAME}
		DISKNAME="zfstest_${DISKNAME}"
		FDISKNAME="/dev/${DISKNAME}"
	else
		die "Sorry, your OS is not supported"
	fi
}

# Destroy a memory-backed block device
destroy_memdisk()
{
	disk="${1}"

	if [ "${os}" = "FreeBSD" ]; then
		cmd="mdconfig -d -u ${disk}"
		${cmd} || die "failed: ${cmd}"
	elif [ "${os}" = "SunOS" ]; then
		cmd="lofiadm ${disk}"
		fname=`${cmd}` || die "failed: ${cmd}"

		cmd="lofiadm -d ${disk}"
		${cmd} || die "failed: ${cmd}"

		cmd="rm ${fname}"
		${cmd} || die "failed: ${cmd}"
	elif [ "${os}" = "Linux" ]; then
		cmd="rm /dev/${disk}"
		${cmd} || die "failed: ${cmd}"
		disk=`echo ${disk} | cut -c 9-`

		cmd="losetup /dev/${disk} | awk '{print substr(\$3, 2, length(\$3)-2)}'"
		fname=`eval ${cmd}` || die "failed: ${cmd}"

		cmd="losetup -d /dev/${disk}"
		${cmd} || die "failed: ${cmd}"

		cmd="rm ${fname}"
		${cmd} || die "failed: ${cmd}"
	else
		die "Sorry, your OS is not supported"
	fi
}

disks_create()
{
	if [ -z "${ndisks}" ]; then
		start=0
	else
		start=${ndisks}
	fi
	ndisks=$((start+$1))
	n=$((ndisks-$start))
	if [ -z "${2}" ]; then
		size="96M"
	else
		size="${2}"
	fi
	for i in `nums $n $start`; do
		create_memdisk ${size}
		eval disk${i}="${DISKNAME}"
		eval fdisk${i}="${FDISKNAME}"
	done
}

disks_destroy()
{
	for i in `nums $ndisks 0`; do
		eval disk=\$disk${i}
		if [ ! -z "${disk}" ]; then
			destroy_memdisk ${disk}
		fi
	done
	[ -n "${TMPDIR_DISKS}" ] && TMPDIR="${TMPDIR_DISKS}" cleanup_tmpfs
	return 0
}

disk_create()
{
	diskno=${1}
	eval disk=\$disk${diskno}
	if [ ! -z ${disk} ]; then
		die "disk${diskno} is already set"
	fi
	dname=${2}
	if [ -z "${3}" ]; then
		size="96M"
	else
		size="${3}"
	fi
	create_memdisk ${size} ${dname}
	[ "${DISKNAME}" = "${dname}" ] || die "${DISKNAME} != ${dname}"
	eval disk${diskno}="${DISKNAME}"
	eval fdisk${diskno}="${FDISKNAME}"
}

disk_destroy()
{
	eval disk=\$disk${1}
	destroy_memdisk ${disk}
	eval disk${1}=""
}

files_create()
{
	if [ -z "${nfiles}" ]; then
		start=0
	else
		start=${nfiles}
	fi
	nfiles=$((start+$1))
	n=$((nfiles-$start))
	if [ -z "${2}" ]; then
		size="96M"
	else
		size="${2}"
	fi
	for i in `nums $n $start`; do
		if [ "${os}" = "Linux" ]; then
			if [ -z "${TMPDIR_FILES}" ]; then
				setup_tmpfs
				TMPDIR_FILES="${TMPDIR}"
			fi
			file=`mktemp ${TMPDIR_FILES}/zfstest.XXXXXXXX`
		else
			file=`mktemp /tmp/zfstest.XXXXXXXX`
		fi
		truncate_cmd ${size} ${file}
		eval file${i}=${file}
	done
}

files_destroy()
{
	for i in `nums $nfiles 0`; do
		eval file=\$file${i}
		rm -f ${file}
	done
	nfiles=0
	[ -n "${TMPDIR_FILES}" ] && TMPDIR="${TMPDIR_FILES}" cleanup_tmpfs
	return 0
}

name_create()
{
	echo "zfstest_`dd if=/dev/urandom bs=1k count=1 2>/dev/null | openssl md5 | awk '{ print $NF }'`"
}

names_create()
{
	nnames=$1
	for i in `nums $nnames 0`; do
		eval name${i}=`name_create`
	done
}

is_mountpoint()
{
	dir="${1}"
	if [ ! -d "${dir}" ]; then
		return 1
	fi
	if [ -n "${GNU_STAT}" ]; then
		statcmd="stat -c"
	else
		statcmd="stat -f"
	fi
	if [ "`${statcmd} '%d' ${dir} 2>/dev/null`" -eq "`${statcmd} '%d' ${dir}/.. 2>/dev/null`" ]; then
		return 1
	fi
	return 0
}

nums()
{
	which jot >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		jot ${1} ${2}
		return $?
	fi

	start="${2}"
	[ -z "${start}" ] && start="1";
	end=$((${1}+${start}-1))

	which seq >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		seq ${start} ${end}
		return $?
	fi

	i=1
	while :; do
		echo $i
		if [ $i -eq ${1} ]; then
			break
		fi
		i=$((i+1))
	done
}

wait_for_resilver()
{
	for i in `nums 64`; do
		${ZPOOL} status ${1} | grep replacing >/dev/null
		if [ $? -ne 0 ]; then
			break
		fi
		sleep 1
	done
}

get_guid()
{ 
	${ZDB} -l ${1} | grep -B1 ${1} | grep guid | head -n1 | awk 'BEGIN {FS="="} {print $2}'
} 
