#!/bin/ksh
#
# $OpenBSD: sysupgrade.sh,v 1.58 2025/02/03 18:55:55 florian Exp $
#
# Copyright (c) 1997-2015 Todd Miller, Theo de Raadt, Ken Westerback
# Copyright (c) 2015 Robert Peichaer <rpe@openbsd.org>
# Copyright (c) 2016, 2017 Antoine Jacoutot <ajacoutot@openbsd.org>
# Copyright (c) 2019 Christian Weisgerber <naddy@openbsd.org>
# Copyright (c) 2019 Florian Obser <florian@openbsd.org>
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

ARCH=$(uname -m)
SETSDIR=/home/_sysupgrade

err()
{
	echo "${0##*/}: ${1}" 1>&2
	return ${2:-1}
}

usage()
{
	echo "usage: ${0##*/} [-fkns] [-b base-directory] [-R version] [installurl | path]" 1>&2
	return 1
}

unpriv()
{
	local _file _rc=0 _user=_syspatch

	if [[ $1 == -f ]]; then
		_file=$2
		shift 2
	fi
 	if [[ -n ${_file} ]]; then
		>${_file}
		chown "${_user}" "${_file}"
	fi
	(($# >= 1))

	eval su -s /bin/sh ${_user} -c "'$@'" || _rc=$?

	[[ -n ${_file} ]] && chown root "${_file}"

	return ${_rc}
}

# Remove all occurrences of first argument from list formed by the remaining
# arguments.
rmel() {
	local _a=$1 _b _c

	shift
	for _b; do
		[[ $_a != "$_b" ]] && _c="${_c:+$_c }$_b"
	done
	echo -n "$_c"
}

SNAP=false
FILE=false
FORCE=false
FORCE_VERSION=false
KEEP=false
REBOOT=true
WHAT='release'

VERSION=$(uname -r)
NEXT_VERSION=$(echo ${VERSION} + 0.1 | bc)

while getopts b:fknrR:s arg; do
	case ${arg} in
	b)	SETSDIR=${OPTARG}/_sysupgrade;;
	f)	FORCE=true;;
	k)	KEEP=true;;
	n)	REBOOT=false;;
	r)	;;
	R)	FORCE_VERSION=true
		[[ ${OPTARG} == @([0-9]|[0-9][0-9]).[0-9] ]] ||
		    err "invalid version: ${OPTARG}"
		NEXT_VERSION=${OPTARG};;
	s)	SNAP=true;;
	*)	usage;;
	esac
done

(($(id -u) != 0)) && err "need root privileges"

shift $(( OPTIND -1 ))

case $# in
0)	MIRROR=$(sed 's/#.*//;/^$/d' /etc/installurl) 2>/dev/null ||
		MIRROR=https://cdn.openbsd.org/pub/OpenBSD
	;;
1)	MIRROR=$1
	;;
*)	usage
esac
[[ $MIRROR == @(file|ftp|http|https)://* ]] ||
	FILE=true
$FORCE_VERSION && $SNAP &&
	err "incompatible options: -s -R $NEXT_VERSION"
$FORCE && ! $SNAP &&
	err "incompatible options: -f without -s"

if $SNAP; then
	WHAT='snapshot'
	URL=${MIRROR}/snapshots/${ARCH}/
else
	URL=${MIRROR}/${NEXT_VERSION}/${ARCH}/
	$FORCE_VERSION || ALT_URL=${MIRROR}/${VERSION}/${ARCH}/
fi

# Oh wait, this is a path install
if $FILE; then
	URL=file://$MIRROR/
	ALT_URL=
fi

install -d -o 0 -g 0 -m 0755 ${SETSDIR}
cd ${SETSDIR}

echo "Fetching from ${URL}"
if ! unpriv -f SHA256.sig ftp -N sysupgrade -Vmo SHA256.sig ${URL}SHA256.sig; then
	if [[ -n ${ALT_URL} ]]; then
		echo "Fetching from ${ALT_URL}"
		unpriv -f SHA256.sig ftp -N sysupgrade -Vmo SHA256.sig ${ALT_URL}SHA256.sig
		URL=${ALT_URL}
		NEXT_VERSION=${VERSION}
	else
		exit 1
	fi
fi

# The key extracted from SHA256.sig must precisely match a pattern
KEY=$(head -1 < SHA256.sig | cut -d' ' -f5 | \
	egrep '^openbsd-[[:digit:]]{2,3}-base.pub$' || true)
if [[ -z $KEY ]]; then
	echo "Invalid SHA256.sig file"
	exit 1
fi

# If required key is not in the system, get it from a signed bundle
if ! [[ -r /etc/signify/$KEY ]]; then
	HAVEKEY=$(cd /etc/signify && ls -1 openbsd-*-base.pub | \
	    tail -2 | head -1 | cut -d- -f2)
	BUNDLE=sigbundle-${HAVEKEY}.tgz
	FWKEY=$(echo $KEY | sed -e 's/base/fw/')
	echo "Adding missing keys from bundle $BUNDLE"
	unpriv -f ${BUNDLE} ftp -N sysupgrade -Vmo $BUNDLE https://ftp.openbsd.org/pub/OpenBSD/signify/$BUNDLE
	signify -Vzq -m - -x $BUNDLE | (cd /etc/signify && tar xfz - $KEY $FWKEY)
	rm $BUNDLE
fi

unpriv -f SHA256 signify -Ve -x SHA256.sig -m SHA256
rm SHA256.sig

if cmp -s /var/db/installed.SHA256 SHA256 && ! $FORCE; then
	echo "Already on latest ${WHAT}."
	exit 0
fi

unpriv -f BUILDINFO ftp -N sysupgrade -Vmo BUILDINFO ${URL}BUILDINFO
unpriv cksum -qC SHA256 BUILDINFO

if [[ -e /var/db/installed.BUILDINFO ]]; then
	installed_build_ts=$(cut -f3 -d' ' /var/db/installed.BUILDINFO)
	build_ts=$(cut -f3 -d' ' BUILDINFO)
	if (( $build_ts <= $installed_build_ts )) && ! $FORCE; then
		echo "Downloaded ${WHAT} is older than installed system. Use -f to force downgrade."
		exit 1
	fi
fi

# INSTALL.*, bsd*, *.tgz
SETS=$(sed -n -e 's/^SHA256 (\(.*\)) .*/\1/' \
    -e '/^INSTALL\./p;/^bsd/p;/\.tgz$/p' SHA256)

OLD_FILES=$(ls)
OLD_FILES=$(rmel SHA256 $OLD_FILES)
DL=$SETS

[[ -n ${OLD_FILES} ]] && echo Verifying old sets.
for f in ${OLD_FILES}; do
	if cksum -C SHA256 $f >/dev/null 2>&1; then
		DL=$(rmel $f ${DL})
		OLD_FILES=$(rmel $f ${OLD_FILES})
	fi
done

[[ -n ${OLD_FILES} ]] && rm ${OLD_FILES}
for f in ${DL}; do
	unpriv -f $f ftp -N sysupgrade -Vmo ${f} ${URL}${f}
done

if [[ -n ${DL} ]]; then
	echo Verifying sets.
	unpriv cksum -qC SHA256 ${DL}
fi

cat <<__EOT >/auto_upgrade.conf
Location of sets = disk
Pathname to the sets = ${SETSDIR}/
Directory does not contain SHA256.sig. Continue without verification = yes
__EOT

if ! ${KEEP}; then
	CLEAN=$(echo BUILDINFO SHA256 ${SETS} | sed -e 's/ /,/g')
	cat <<__EOT > /etc/rc.firsttime
rm -f ${SETSDIR}/{${CLEAN}}
__EOT
fi

echo Fetching updated firmware.
set -A _NEXTKERNV -- $(what bsd |
	sed -n '2s/^[[:blank:]]OpenBSD \([1-9][0-9]*\.[0-9]\)\([^ ]*\).*/\1 \2/p')

if [[ ${_NEXTKERNV[1]} == '-current' ]]; then
	FW_URL=http://firmware.openbsd.org/firmware/snapshots/
else
	FW_URL=http://firmware.openbsd.org/firmware/${_NEXTKERNV[0]}/
fi
VNAME="${_NEXTKERNV[0]}" fw_update -p ${FW_URL} || true

install -F -m 700 bsd.rd /bsd.upgrade
logger -t sysupgrade -p kern.info "installed new /bsd.upgrade. Old kernel version: $(sysctl -n kern.version)"
sync

if ${REBOOT}; then
	echo Upgrading.
	exec reboot
else
	echo "Will upgrade on next reboot"
fi
