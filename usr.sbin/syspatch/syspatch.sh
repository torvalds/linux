#!/bin/ksh
#
# $OpenBSD: syspatch.sh,v 1.168 2023/12/13 17:50:23 ajacoutot Exp $
#
# Copyright (c) 2016, 2017 Antoine Jacoutot <ajacoutot@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

set -e
umask 0022
export PATH=/usr/bin:/bin:/usr/sbin:/sbin

err()
{
	echo "${0##*/}: ${1}" 1>&2
	return ${2:-1}
}

usage()
{
	echo "usage: ${0##*/} [-c | -l | -R | -r]" 1>&2
	return 1
}

apply_patch()
{
	local _edir _file _files _kernel _patch=$1 _rc=0 _s _upself=false
	[[ -n ${_patch} ]]

	_edir=${_TMP}/${_patch}

	fetch_and_verify "syspatch${_patch}.tgz"

	trap '' INT
	echo "Installing patch ${_patch##${_OSrev}-}"
	install -d ${_edir} ${_PDIR}/${_patch}

	_kernel=$(sysctl -n kern.osversion)
	[[ ${_kernel%#*} == "GENERIC.MP" ]] &&
		_s="-s @usr/share/relink/kernel/GENERIC/.*@@g" ||
		_s="-s @usr/share/relink/kernel/GENERIC.MP/.*@@g"
	_files="$(tar -xvzphf ${_TMP}/syspatch${_patch}.tgz -C ${_edir} \
		${_s})" || { rm -r ${_PDIR}/${_patch}; return 1; }

	checkfs ${_files}
	create_rollback ${_patch} "${_files}"

	for _file in ${_files}; do
		((_rc == 0)) || break
		[[ ${_file} == usr/sbin/syspatch ]] && _upself=true
		install_file ${_edir}/${_file} /${_file} || _rc=$?
	done

	if ((_rc != 0)); then
		err "Failed to apply patch ${_patch##${_OSrev}-}" 0
		rollback_patch; return ${_rc}
	fi
	# don't fill up /tmp when installing multiple patches at once; non-fatal
	rm -rf ${_edir} ${_TMP}/syspatch${_patch}.tgz
	trap exit INT

	echo ${_files} | grep -Eqv \
		'(^|[[:blank:]]+)usr/share/relink/kernel/GENERI(C|C.MP)/[[:print:]]+([[:blank:]]+|$)' ||
		_KARL=true

	(! ${_upself} || err "updated itself, run it again to install \
missing patches" 2)
}

# quick-and-dirty filesystem status and size checks:
# - assume old files are about the same size as new ones
# - ignore new (nonexistent) files
# - ignore rollback tarball: create_rollback() will handle the failure
# - compute total size of all files per fs, simpler and less margin for error
#   (instead of computing before installing each file)
checkfs()
{
	local _d _dev _df _files="${@}" _sz
	[[ -n ${_files} ]]

	set +e # ignore errors due to:
	# - nonexistent files (i.e. syspatch is installing new files)
	# - broken interpolation due to bogus devices like remote filesystems
	eval $(cd / &&
		stat -qf "_dev=\"\${_dev} %Sd\";
			local %Sd=\"\${%Sd:+\${%Sd}\+}%Uz\"" ${_files}) \
			2>/dev/null
	set -e

	for _d in $(printf '%s\n' ${_dev} | sort -u); do
		[[ ${_d} != "??" ]] || err "Unsupported filesystem, aborting"
		mount | grep -v read-only | grep -q "^/dev/${_d} " ||
			err "Read-only filesystem, aborting"
		_df=$(df -Pk | grep "^/dev/${_d} " | tr -s ' ' | cut -d ' ' -f4)
		_sz=$(($((_d))/1024))
		((_df > _sz)) || err "No space left on ${_d}, aborting"
	done
}

create_rollback()
{
	# XXX annotate new files so we can remove them if we rollback?
	local _file _patch=$1 _rbfiles _rc=0
	[[ -n ${_patch} ]]
	shift
	local _files="${@}"
	[[ -n ${_files} ]]

	for _file in ${_files}; do
		[[ -f /${_file} ]] && _rbfiles="${_rbfiles} ${_file}"
	done

	tar -C / -czf ${_PDIR}/${_patch}/rollback.tgz ${_rbfiles} || _rc=$?

	if ((_rc != 0)); then
		err "Failed to create rollback patch ${_patch##${_OSrev}-}" 0
		rm -r ${_PDIR}/${_patch}; return ${_rc}
	fi
}

fetch_and_verify()
{
	local _tgz=$1 _title="Get/Verify"
	[[ -n ${_tgz} ]]

	[[ -t 0 ]] || echo "${_title} ${_tgz}"
	unpriv -f "${_TMP}/${_tgz}" ftp -N syspatch -VD "${_title}" -o \
		"${_TMP}/${_tgz}" "${_MIRROR}/${_tgz}"

	(cd ${_TMP} && sha256 -qC ${_TMP}/SHA256 ${_tgz})
}

install_file()
{
	# XXX handle hard link, dir->file, file->dir?
	local _dst=$2 _fgrp _fmode _fown _src=$1
	[[ -f ${_src} && -f ${_dst} ]]

	if [[ -h ${_src} ]]; then
		ln -sf $(readlink ${_src}) ${_dst}
	else
		eval $(stat -f "_fmode=%OMp%OLp _fown=%Su _fgrp=%Sg" ${_src})
		install -DFp -m ${_fmode} -o ${_fown} -g ${_fgrp} ${_src} \
			${_dst}
	fi
}

ls_installed()
{
	local _p
	for _p in ${_PDIR}/${_OSrev}-+([[:digit:]])_+([[:alnum:]_-]); do
		[[ -f ${_p}/rollback.tgz ]] && echo ${_p##*/${_OSrev}-}
	done
}

ls_missing()
{
	local _c _f _cmd _l="$(ls_installed)" _p _sha=${_TMP}/SHA256

	# don't output anything on stdout to prevent corrupting the patch list
	unpriv -f "${_sha}.sig" ftp -N syspatch -MVo "${_sha}.sig" \
		"${_MIRROR}/SHA256.sig" >/dev/null
	unpriv -f "${_sha}" signify -Veq -x ${_sha}.sig -m ${_sha} -p \
		/etc/signify/openbsd-${_OSrev}-syspatch.pub >/dev/null

	# sig file less than 3 lines long doesn't list any patch (new release)
	(($(grep -c ".*" ${_sha}.sig) < 3)) && return

	set -o pipefail
	grep -Eo "syspatch${_OSrev}-[[:digit:]]{3}_[[:alnum:]_-]+" ${_sha} |
		while read _c; do _c=${_c##syspatch${_OSrev}-} &&
		[[ -n ${_l} ]] && echo ${_c} | grep -qw -- "${_l}" || echo ${_c}
	done | while read _p; do
		_cmd="ftp -N syspatch -MVo - \
			${_MIRROR}/syspatch${_OSrev}-${_p}.tgz"
		unpriv "${_cmd}" | tar tzf - | while read _f; do
			# no earlier version of _all_ files contained in the tgz
			# exists on the system, it means a missing set: skip it
			[[ -f /${_f} ]] || continue && echo ${_p} && pkill -u \
				_syspatch -xf "${_cmd}" || true && break
		done
	done | sort -V # only used as a buffer to display all patches at once
	set +o pipefail
}

rollback_patch()
{
	local _edir _file _files _patch _rc=0

	_patch="$(ls_installed | tail -1)"
	[[ -n ${_patch} ]] || return 0 # nothing to rollback

	_edir=${_TMP}/${_patch}-rollback
	_patch=${_OSrev}-${_patch}

	trap '' INT
	echo "Reverting patch ${_patch##${_OSrev}-}"
	install -d ${_edir}

	_files="$(tar xvzphf ${_PDIR}/${_patch}/rollback.tgz -C ${_edir})"
	checkfs ${_files} ${_PDIR} # check for read-only /var/syspatch

	for _file in ${_files}; do
		((_rc == 0)) || break
		install_file ${_edir}/${_file} /${_file} || _rc=$?
	done

	((_rc != 0)) || rm -r ${_PDIR}/${_patch} || _rc=$?
	((_rc == 0)) ||
		err "Failed to revert patch ${_patch##${_OSrev}-}" ${_rc}
	rm -rf ${_edir} # don't fill up /tmp when using `-R'; non-fatal
	trap exit INT

	echo ${_files} | grep -Eqv \
		'(^|[[:blank:]]+)usr/share/relink/kernel/GENERI(C|C.MP)/[[:print:]]+([[:blank:]]+|$)' ||
		_KARL=true
}

trap_handler()
{
	set +e # we're trapped
	rm -rf "${_TMP}"

	# in case a patch added a new directory (install -D)
	if [[ -n ${_PATCHES} ]]; then
		mtree -qdef /etc/mtree/4.4BSD.dist -p / -U >/dev/null
		[[ -f /var/sysmerge/xetc.tgz ]] &&
			mtree -qdef /etc/mtree/BSD.x11.dist -p / -U >/dev/null
	fi

	if ${_KARL}; then
		echo -n "Relinking to create unique kernel..."
		if /usr/libexec/reorder_kernel; then
			echo " done; reboot to load the new kernel"
		else
			echo " failed!\n!!! \"/usr/libexec/reorder_kernel\" \
must be run manually to install the new kernel"
			exit 1
		fi
	fi

	${_PATCH_APPLIED} && echo "Errata can be reviewed under ${_PDIR}"
}

unpriv()
{
	local _file=$2 _rc=0 _user=_syspatch

	if [[ $1 == -f && -n ${_file} ]]; then
		>${_file}
		chown "${_user}" "${_file}"
		chmod 0711 ${_TMP}
		shift 2
	fi
	(($# >= 1))

	eval su -s /bin/sh ${_user} -c "'$@'" || _rc=$?

	[[ -n ${_file} ]] && chown root "${_file}"

	return ${_rc}
}

# only run on release (not -current nor -stable)
set -A _KERNV -- $(sysctl -n kern.version |
	sed 's/^OpenBSD \([1-9][0-9]*\.[0-9]\)\([^ ]*\).*/\1 \2/;q')
((${#_KERNV[*]} > 1)) && err "Unsupported release: ${_KERNV[0]}${_KERNV[1]}"

[[ $@ == @(|-[[:alpha:]]) ]] || usage; [[ $@ == @(|-(c|R|r)) ]] &&
	(($(id -u) != 0)) && err "need root privileges"
[[ $@ == @(|-(R|r)) ]] && pgrep -qxf '/bin/ksh .*reorder_kernel' &&
	err "cannot apply patches while reorder_kernel is running"

_OSrev=${_KERNV[0]%.*}${_KERNV[0]#*.}
[[ -n ${_OSrev} ]]

_MIRROR=$(while read _line; do _line=${_line%%#*}; [[ -n ${_line} ]] &&
	print -r -- "${_line}"; done </etc/installurl | tail -1) 2>/dev/null
[[ ${_MIRROR} == @(file|ftp|http|https)://* ]] ||
	_MIRROR=https://cdn.openbsd.org/pub/OpenBSD
_MIRROR="${_MIRROR}/syspatch/${_KERNV[0]}/$(machine)"

_PATCH_APPLIED=false
_PDIR="/var/syspatch"
_TMP=$(mktemp -d -p ${TMPDIR:-/tmp} syspatch.XXXXXXXXXX)
_KARL=false

readonly _KERNV _MIRROR _OSrev _PDIR _TMP

trap 'trap_handler' EXIT
trap exit HUP INT TERM

while getopts clRr arg; do
	case ${arg} in
		c) ls_missing ;;
		l) ls_installed ;;
		R) while [[ -n $(ls_installed) ]]; do rollback_patch; done ;;
		r) rollback_patch ;;
		*) usage ;;
	esac
done
shift $((OPTIND - 1))
(($# != 0)) && usage

# default action: apply all patches
if ((OPTIND == 1)); then
	# remove non matching release /var/syspatch/ content
	for _D in ${_PDIR}/{.[!.],}*; do
		[[ -e ${_D} ]] || continue
		[[ ${_D##*/} == ${_OSrev}-+([[:digit:]])_+([[:alnum:]_-]) ]] &&
			[[ -f ${_D}/rollback.tgz ]] || rm -r ${_D}
	done
	_PATCHES=$(ls_missing) # can't use errexit in a for loop
	[[ -n ${_PATCHES} ]] || exit 2
	for _PATCH in ${_PATCHES}; do
		apply_patch ${_OSrev}-${_PATCH}
		_PATCH_APPLIED=true
	done
fi
