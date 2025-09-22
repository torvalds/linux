#!/bin/ksh -
#
# $OpenBSD: sysmerge.sh,v 1.236 2025/07/13 08:15:46 bentley Exp $
#
# Copyright (c) 2008-2014 Antoine Jacoutot <ajacoutot@openbsd.org>
# Copyright (c) 1998-2003 Douglas Barton <DougB@FreeBSD.org>
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
#

umask 0022
export PATH=/usr/bin:/bin:/usr/sbin:/sbin

usage() {
	echo "usage: ${0##*/} [-bdp]" >&2 && exit 1
}

# OpenBSD /etc/rc v1.456
stripcom() {
	local _file=$1 _line

	[[ -s $_file ]] || return

	while read _line ; do
		_line=${_line%%#*}
		[[ -n $_line ]] && print -r -- "$_line"
	done <$_file
}

sm_error() {
	(($#)) && echo "!!!! $@"
	rm -rf ${_TMPROOT}
	exit 1
}

sm_trap() {
	rm -f /var/sysmerge/{etc,pkg,xetc}sum
	sm_error
}

trap "sm_trap" 1 2 3 13 15

sm_info() {
	(($#)) && echo "---- $@" || true
}

sm_warn() {
	(($#)) && echo "**** $@" || true
}

sm_extract_sets() {
	${PKGMODE} && return
	local _e _x _set

	[[ -f /var/sysmerge/etc.tgz ]] && _e=etc
	[[ -f /var/sysmerge/xetc.tgz ]] && _x=xetc
	[[ -z ${_e}${_x} ]] && sm_error "cannot find sets to extract"

	for _set in ${_e} ${_x}; do
		tar -xzphf \
			/var/sysmerge/${_set}.tgz || \
			sm_error "failed to extract ${_set}.tgz"
	done
}

sm_rotate_bak() {
	local _b

	for _b in $(jot 4 3 0); do
		[[ -d ${_BKPDIR}.${_b} ]] && \
			mv ${_BKPDIR}.${_b} ${_BKPDIR}.$((_b+1))
	done
	rm -rf ${_BKPDIR}.4
	[[ -d ${_BKPDIR} ]] && mv ${_BKPDIR} ${_BKPDIR}.0
	# make sure this function is only run _once_ per sysmerge invocation
	unset -f sm_rotate_bak
}

# get pkg @sample information
exec_espie() {
	local _tmproot

	_tmproot=${_TMPROOT} /usr/bin/perl <<'EOF'
use strict;
use warnings;

package OpenBSD::PackingElement;

sub walk_sample
{
}

package OpenBSD::PackingElement::Sampledir;
sub walk_sample
{
	my $item = shift;
	print "0-DIR", " ",
	      $item->{owner} // "root", " ",
	      $item->{group} // "wheel", " ",
	      $item->{mode} // "0755", " ",
	      $ENV{'_tmproot'}, $item->fullname,
	      "\n";
}

package OpenBSD::PackingElement::Sample;
sub walk_sample
{
	my $item = shift;
	print "1-FILE", " ",
	      $item->{owner} // "root", " ",
	      $item->{group} // "wheel", " ",
	      $item->{mode} // "0644", " ",
	      $item->{copyfrom}->fullname, " ",
	      $ENV{'_tmproot'}, $item->fullname,
	      "\n";
}

package main;
use OpenBSD::PackageInfo;
use OpenBSD::PackingList;

for my $i (installed_packages()) {
	my $plist = OpenBSD::PackingList->from_installation($i);
	$plist->walk_sample();
}
EOF
}

sm_cp_pkg_samples() {
	! ${PKGMODE} && return
	local _install_args _i _ret=0 _sample

	# access to full base system hierarchy is implied in packages
	mtree -qdef /etc/mtree/4.4BSD.dist -U >/dev/null
	mtree -qdef /etc/mtree/BSD.x11.dist -U >/dev/null

	# @sample directories are processed first
	exec_espie | sort -u | while read _i; do
		set -A _sample -- ${_i}
		_install_args="-o ${_sample[1]} -g ${_sample[2]} -m ${_sample[3]}"
		if [[ ${_sample[0]} == "0-DIR" ]]; then
			install -d ${_install_args} ${_sample[4]} || _ret=1
		else
			# directory we want to copy the @sample file into
			# does not exist and is not a @sample so we have no
			# knowledge of the required owner/group/mode
			# (e.g. /var/www/usr/sbin in mail/femail,-chroot)
			_pkghier=${_sample[5]%/*}
			if [[ ! -d ${_pkghier#${_TMPROOT}} ]]; then
				sm_warn "skipping ${_sample[5]#${_TMPROOT}}: ${_pkghier#${_TMPROOT}} does not exist"
				continue
			else
				# non-default prefix (e.g. mail/roundcubemail)
				install -d ${_pkghier}
			fi
			install ${_install_args} \
				${_sample[4]} ${_sample[5]} || _ret=1
		fi
	done

	if [[ ${_ret} -eq 0 ]]; then
		find . -type f -exec sha256 '{}' + | sort \
			>./var/sysmerge/pkgsum || _ret=1
	fi
	[[ ${_ret} -ne 0 ]] && \
		sm_error "failed to populate packages @samples and create sum file"
}

sm_run() {
	local _auto_upg _c _c1 _c2 _cursum _diff _i _k _j _cfdiff _cffiles
	local _ignorefiles _cvsid1 _cvsid2 _matchsum _mismatch

	sm_extract_sets
	sm_add_user_grp
	sm_cp_pkg_samples

	# From 7.8 onwards, font cache isn't owned by root
	chown -R _fc-cache:_fc-cache /var/cache/fontconfig

	for _i in etcsum xetcsum pkgsum; do
		if [[ -f /var/sysmerge/${_i} && \
			-f ./var/sysmerge/${_i} ]] && \
			! ${DIFFMODE}; then
			# redirect stderr: file may not exist
			_matchsum=$(sha256 -c /var/sysmerge/${_i} 2>/dev/null | \
				sed -n 's/^(SHA256) \(.*\): OK$/\1/p')
			# delete file in temproot if it has not changed since
			# last release and is present in current installation
			for _j in ${_matchsum}; do
				# skip sum files
				[[ ${_j} == ./var/sysmerge/${_i} ]] && continue
				[[ -f ${_j#.} && -f ${_j} ]] && \
					rm ${_j}
			done

			# set auto-upgradable files
			_mismatch=$(diff -u ./var/sysmerge/${_i} /var/sysmerge/${_i} | \
				sed -n 's/^+SHA256 (\(.*\)).*/\1/p')
			for _k in ${_mismatch}; do
				# skip sum files
				[[ ${_k} == ./var/sysmerge/${_i} ]] && continue
				# compare CVS Id first so if the file hasn't been modified,
				# it will be deleted from temproot and ignored from comparison;
				# several files are generated from scripts so CVS ID is not a
				# reliable way of detecting changes: leave for a full diff
				if ! ${PKGMODE} && \
					[[ ${_k} != ./etc/@(fbtab|ttys) && \
					! -h ${_k} ]]; then
					_cvsid1=$(sed -n "/[$]OpenBSD:.*Exp [$]/{p;q;}" ${_k#.} 2>/dev/null)
					_cvsid2=$(sed -n "/[$]OpenBSD:.*Exp [$]/{p;q;}" ${_k} 2>/dev/null)
					[[ -n ${_cvsid1} ]] && \
						[[ ${_cvsid1} == ${_cvsid2} ]] && \
						[[ -f ${_k} ]] && rm ${_k} && \
						continue
				fi
				# redirect stderr: file may not exist
				_cursum=$(cd / && sha256 ${_k} 2>/dev/null)
				grep -q "${_cursum}" /var/sysmerge/${_i} && \
					! grep -q "${_cursum}" ./var/sysmerge/${_i} && \
					_auto_upg="${_auto_upg} ${_k}"
			done
			[[ -n ${_auto_upg} ]] && set -A AUTO_UPG -- ${_auto_upg}
		fi
		[[ -f ./var/sysmerge/${_i} ]] && \
			mv ./var/sysmerge/${_i} /var/sysmerge/${_i}
	done

	# files we don't want/need to deal with
	_ignorefiles="/etc/group
		      /etc/localtime
		      /etc/master.passwd
		      /etc/motd
		      /etc/passwd
		      /etc/pwd.db
		      /etc/spwd.db
		      /var/db/locate.database
		      /var/mail/root"
	# in case X(7) is not installed, xetcsum is not removed by the loop above
	_ignorefiles="${_ignorefiles} /var/sysmerge/xetcsum"
	[[ -f /etc/sysmerge.ignore ]] && \
		_ignorefiles="${_ignorefiles} $(stripcom /etc/sysmerge.ignore)"
	for _i in ${_ignorefiles}; do
		rm -f ./${_i}
	done

	# aliases(5) needs to be handled last in case mailer.conf(5) changes
	_c1=$(find . -type f -or -type l | grep -v '^./etc/mail/aliases$')
	[[ -f ./etc/mail/aliases ]] && _c2="./etc/mail/aliases"
	for COMPFILE in ${_c1} ${_c2}; do
		IS_BIN=false
		IS_LINK=false
		TARGET=${COMPFILE#.}

		# links need to be treated in a different way
		if [[ -h ${COMPFILE} ]]; then
			IS_LINK=true
			[[ -h ${TARGET} && \
				$(readlink ${COMPFILE}) == $(readlink ${TARGET}) ]] && \
				rm ${COMPFILE} && continue
		elif [[ -f ${TARGET} ]]; then
			# empty files = binaries (to avoid comparison);
			# only process them if they don't exist on the system
			if [[ ! -s ${COMPFILE} ]]; then
				rm ${COMPFILE} && continue
			fi

			_diff=$(diff -q ${TARGET} ${COMPFILE} 2>&1)
			# files are the same: delete
			[[ $? -eq 0 ]] && rm ${COMPFILE} && continue
			# disable sdiff for binaries
			echo "${_diff}" | head -1 | grep -q "Binary files" && \
				IS_BIN=true
		else
			# missing files = binaries (to avoid comparison)
			IS_BIN=true
		fi

		sm_diff_loop
	done
}

sm_install() {
	local _dmode _fgrp _fmode _fown
	local _instdir=${TARGET%/*}
	[[ -z ${_instdir} ]] && _instdir="/"

	_dmode=$(stat -f "%OMp%OLp" .${_instdir}) || return
	eval $(stat -f "_fmode=%OMp%OLp _fown=%Su _fgrp=%Sg" ${COMPFILE}) || return

	if [[ ! -d ${_instdir} ]]; then
		install -d -o root -g wheel -m ${_dmode} "${_instdir}" || return
	fi

	if ${IS_LINK}; then
		_linkt=$(readlink ${COMPFILE})
		(cd ${_instdir} && ln -sf ${_linkt} . && rm ${_TMPROOT}/${COMPFILE})
		return
	fi

	if [[ -f ${TARGET} ]]; then
		if typeset -f sm_rotate_bak >/dev/null; then
			sm_rotate_bak || return
		fi
		mkdir -p ${_BKPDIR}/${_instdir} || return
		cp -p ${TARGET} ${_BKPDIR}/${_instdir} || return
	fi

	if ! install -Fm ${_fmode} -o ${_fown} -g ${_fgrp} ${COMPFILE} ${_instdir}; then
		rm ${_BKPDIR}/${COMPFILE} && return 1
	fi
	rm ${COMPFILE}

	case ${TARGET} in
	/etc/login.conf)
		if [[ -f /etc/login.conf.db ]]; then
			echo " (running cap_mkdb(1), needs a relog)"
			sm_warn $(cap_mkdb /etc/login.conf 2>&1)
		else
			echo
		fi
		;;
	/etc/mail/aliases)
		if [[ -f /etc/mail/aliases.db ]]; then
			echo " (running newaliases(8))"
			sm_warn $(newaliases 2>&1 >/dev/null)
		else
			echo
		fi
		;;
	*)
		echo
		;;
	esac
}

sm_add_user_grp() {
	local _name _c _d _e _f _G _g _L _pass _s _u
	local _gr=./etc/group
	local _pw=./etc/master.passwd

	${PKGMODE} && return

	while IFS=: read -r -- _name _pass _g _G; do
		if ! getent group ${_name} >/dev/null; then
			getent group ${_g} >/dev/null && \
				sm_warn "Not adding group ${_name}, GID ${_g} already exists" && \
				continue
			echo "===> Adding the ${_name} group"
			groupadd -g ${_g} ${_name}
		fi
	done <${_gr}

	while IFS=: read -r -- _name _pass _u _g _L _f _e _c _d _s
	do
		if [[ ${_name} != root ]]; then
			if ! getent passwd ${_name} >/dev/null; then
				getent passwd ${_u} >/dev/null && \
					sm_warn "Not adding user ${_name}, UID ${_u} already exists" && \
					continue
				echo "===> Adding the ${_name} user"
				[[ -z ${_L} ]] || _L="-L ${_L}"
				useradd -c "${_c}" -d ${_d} -e ${_e} -f ${_f} \
					-g ${_g} ${_L} -s ${_s} -u ${_u} \
					${_name} >/dev/null
			fi
		fi
	done <${_pw}
}

sm_warn_valid() {
	# done as a separate function to print a warning with the
	# filename above output from the check command
	local _res

	_res=$(eval $* 2>&1)
	if [[ $? -ne 0 || -n ${_res} ]]; then
	       sm_warn "${_file} appears to be invalid"
	       echo "${_res}"
	fi
}

sm_check_validity() {
	local _file=$1.merged
	local _fail

	case $1 in
	./etc/ssh/sshd_config)
		sm_warn_valid sshd -f ${_file} -t ;;
	./etc/pf.conf)
		sm_warn_valid pfctl -nf ${_file} ;;
	./etc/login.conf)
		sm_warn_valid "cap_mkdb -f ${_TMPROOT}/login.conf.check ${_file} || true"
		rm -f ${_TMPROOT}/login.conf.check.db ;;
	esac
}

sm_merge_loop() {
	local _instmerged _tomerge
	echo "===> Type h at the sdiff prompt (%) to get usage help\n"
	_tomerge=true
	while ${_tomerge}; do
		cp -p ${COMPFILE} ${COMPFILE}.merged
		sdiff -as -w $(tput -T ${TERM:-vt100} cols) -o ${COMPFILE}.merged \
			${TARGET} ${COMPFILE}
		_instmerged=v
		while [[ ${_instmerged} == v ]]; do
			echo
			echo "  Use 'e' to edit the merged file"
			echo "  Use 'i' to install the merged file"
			echo "  Use 'n' to view a diff between the merged and new files"
			echo "  Use 'o' to view a diff between the old and merged files"
			echo "  Use 'r' to re-do the merge"
			echo "  Use 'v' to view the merged file"
			echo "  Use 'x' to delete the merged file and go back to previous menu"
			echo "  Default is to leave the temporary file to deal with by hand"
			echo
			sm_check_validity ${COMPFILE}
			echo -n "===> How should I deal with the merged file? [Leave it for later] "
			read _instmerged
			case ${_instmerged} in
			[eE])
				echo "editing merged file...\n"
				${EDITOR} ${COMPFILE}.merged
				_instmerged=v
				;;
			[iI])
				mv ${COMPFILE}.merged ${COMPFILE}
				echo -n "\n===> Merging ${TARGET}"
				sm_install || \
					(echo && sm_warn "problem merging ${TARGET}")
				_tomerge=false
				;;
			[nN])
				(
					echo "comparison between merged and new files:\n"
					diff -u ${COMPFILE}.merged ${COMPFILE}
				) | ${PAGER}
				_instmerged=v
				;;
			[oO])
				(
					echo "comparison between old and merged files:\n"
					diff -u ${TARGET} ${COMPFILE}.merged
				) | ${PAGER}
				_instmerged=v
				;;
			[rR])
				rm ${COMPFILE}.merged
				;;
			[vV])
				${PAGER} ${COMPFILE}.merged
				;;
			[xX])
				rm ${COMPFILE}.merged
				return 1
				;;
			'')
				_tomerge=false
				;;
			*)
				echo "invalid choice: ${_instmerged}"
				_instmerged=v
				;;
			esac
		done
	done
}

sm_diff_loop() {
	local i _handle _nonexistent

	${BATCHMODE} && _handle=todo || _handle=v

	FORCE_UPG=false
	_nonexistent=false
	while [[ ${_handle} == @(v|todo) ]]; do
		if [[ -f ${TARGET} && -f ${COMPFILE} ]] && ! ${IS_LINK}; then
			if ! ${DIFFMODE}; then
				# automatically install files if current != new
				# and current = old
				for i in ${AUTO_UPG[@]}; do \
					[[ ${i} == ${COMPFILE} ]] && FORCE_UPG=true
				done
				# automatically install files which differ
				# only by CVS Id or that are binaries
				if [[ -z $(diff -q -I'[$]OpenBSD:.*$' ${TARGET} ${COMPFILE}) ]] || \
					${FORCE_UPG} || ${IS_BIN}; then
					echo -n "===> Updating ${TARGET}"
					sm_install || \
						(echo && sm_warn "problem updating ${TARGET}")
					return
				fi
			fi
			if [[ ${_handle} == v ]]; then
				(
					echo "\n========================================================================\n"
					echo "===> Displaying differences between ${COMPFILE} and installed version:"
					echo
					diff -u ${TARGET} ${COMPFILE}
				) | ${PAGER}
				echo
			fi
		else
			# file does not exist on the target system
			if ${DIFFMODE}; then
				_nonexistent=true
				${BATCHMODE} || echo "\n===> Missing ${TARGET}\n"
			elif ${IS_LINK}; then
				echo "===> Linking ${TARGET}"
				sm_install || \
					sm_warn "problem creating ${TARGET} link"
				return
			else
				echo -n "===> Installing ${TARGET}"
				sm_install || \
					(echo && sm_warn "problem installing ${TARGET}")
				return
			fi
		fi

		if ! ${BATCHMODE}; then
			echo "  Use 'd' to delete the temporary ${COMPFILE}"
			echo "  Use 'i' to install the temporary ${COMPFILE}"
			if ! ${_nonexistent} && ! ${IS_BIN} && \
				! ${IS_LINK}; then
				echo "  Use 'm' to merge the temporary and installed versions"
				echo "  Use 'v' to view the diff results again"
			fi
			echo
			echo "  Default is to leave the temporary file to deal with by hand"
			echo
			echo -n "How should I deal with this? [Leave it for later] "
			read _handle
		else
			unset _handle
		fi

		case ${_handle} in
		[dD])
			rm ${COMPFILE}
			echo "\n===> Deleting ${COMPFILE}"
			;;
		[iI])
			echo
			if ${IS_LINK}; then
				echo "===> Linking ${TARGET}"
				sm_install || \
					sm_warn "problem creating ${TARGET} link"
			else
				echo -n "===> Updating ${TARGET}"
				sm_install || \
					(echo && sm_warn "problem updating ${TARGET}")
			fi
			;;
		[mM])
			if ! ${_nonexistent} && ! ${IS_BIN} && ! ${IS_LINK}; then
				sm_merge_loop || _handle=todo
			else
				echo "invalid choice: ${_handle}\n"
				_handle=todo
			fi
			;;
		[vV])
			if ! ${_nonexistent} && ! ${IS_BIN} && ! ${IS_LINK}; then
				_handle=v
			else
				echo "invalid choice: ${_handle}\n"
				_handle=todo
			fi
			;;
		'')
			echo -n
			;;
		*)
			echo "invalid choice: ${_handle}\n"
			_handle=todo
			continue
			;;
		esac
	done
}

sm_post() {
	local _f

	cd ${_TMPROOT} && \
		find . -type d -depth -empty -exec rmdir -p '{}' + 2>/dev/null
	rmdir ${_TMPROOT} 2>/dev/null

	if [[ -d ${_TMPROOT} ]]; then
		for _f in $(find ${_TMPROOT} ! -type d ! -name \*.merged -size +0)
		do
			sm_info "${_f##*${_TMPROOT}} unhandled, re-run ${0##*/} to merge the new version"
			! ${DIFFMODE} && [[ -f ${_f} ]] && \
				sed -i "/$(sha256 -q ${_f})/d" /var/sysmerge/*sum
		done
	fi

	mtree -qdef /etc/mtree/4.4BSD.dist -p / -U >/dev/null
	[[ -f /var/sysmerge/xetc.tgz ]] && \
		mtree -qdef /etc/mtree/BSD.x11.dist -p / -U >/dev/null
}

BATCHMODE=false
DIFFMODE=false
PKGMODE=false

while getopts bdp arg; do
	case ${arg} in
	b)	BATCHMODE=true;;
	d)	DIFFMODE=true;;
	p)	PKGMODE=true;;
	*)	usage;;
	esac
done
shift $(( OPTIND -1 ))
[[ $# -ne 0 ]] && usage

[[ $(id -u) -ne 0 ]] && echo "${0##*/}: need root privileges" && exit 1

# global constants
_BKPDIR=/var/sysmerge/backups
_RELINT=$(uname -r | tr -d '.') || exit 1
_TMPROOT=$(mktemp -d -p ${TMPDIR:-/tmp} sysmerge.XXXXXXXXXX) || exit 1
readonly _BKPDIR _RELINT _TMPROOT

[[ -z ${VISUAL} ]] && EDITOR=${EDITOR:-/usr/bin/vi} || EDITOR=${VISUAL}
PAGER=${PAGER:-/usr/bin/more}

mkdir -p ${_TMPROOT} || sm_error "cannot create ${_TMPROOT}"
cd ${_TMPROOT} || sm_error "cannot enter ${_TMPROOT}"

sm_run && sm_post
