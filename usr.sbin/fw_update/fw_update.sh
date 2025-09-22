#!/bin/ksh
#	$OpenBSD: fw_update.sh,v 1.65 2025/05/12 23:48:12 afresh1 Exp $
#
# Copyright (c) 2021,2023 Andrew Hewus Fresh <afresh1@openbsd.org>
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

set -o errexit -o pipefail -o nounset -o noclobber -o noglob
set +o monitor
export PATH=/usr/bin:/bin:/usr/sbin:/sbin

CFILE=SHA256.sig
DESTDIR=${DESTDIR:-}
FWPATTERNS="${DESTDIR}/usr/share/misc/firmware_patterns"

unset DMESG
unset FWURL
unset FWPUB_KEY

DRYRUN=false
integer VERBOSE=0
DELETE=false
DOWNLOAD=true
INSTALL=true
LOCALSRC=
ENABLE_SPINNER=false
[ -t 1 ] && ENABLE_SPINNER=true

integer STATUS_FD=1
integer WARN_FD=2
FD_DIR=

unset FTPPID
unset LOCKPID
unset FWPKGTMP
REMOVE_LOCALSRC=false
DROP_PRIVS=true

status() { echo -n "$*" >&"$STATUS_FD"; }
warn()   { echo    "$*" >&"$WARN_FD"; }

cleanup() {
	set +o errexit # ignore errors from killing ftp

	if [ -d "$FD_DIR" ]; then
		echo "" >&"$STATUS_FD"
		((STATUS_FD == 3)) && exec 3>&-
		((WARN_FD   == 4)) && exec 4>&-

		[ -s "$FD_DIR/status" ] && cat "$FD_DIR/status"
		[ -s "$FD_DIR/warn"   ] && cat "$FD_DIR/warn" >&2

		rm -rf "$FD_DIR"
	fi

	[ "${FTPPID:-}" ] && kill -TERM -"$FTPPID" 2>/dev/null
	[ "${LOCKPID:-}" ] && kill -TERM -"$LOCKPID" 2>/dev/null
	[ "${FWPKGTMP:-}" ] && rm -rf "$FWPKGTMP"
	"$REMOVE_LOCALSRC" && rm -rf "$LOCALSRC"
	[ -e "$CFILE" ] && [ ! -s "$CFILE" ] && rm -f "$CFILE"
}
trap cleanup EXIT

tmpdir() {
	local _i=1 _dir

	# The installer lacks mktemp(1), do it by hand
	if [ -x /usr/bin/mktemp ]; then
		_dir=$( mktemp -d "${1}-XXXXXXXXX" )
	else
		until _dir="${1}.$_i.$RANDOM" && mkdir -- "$_dir" 2>/dev/null; do
		    ((++_i < 10000)) || return 1
		done
	fi

	echo "$_dir"
}

spin() {
	if ! "$ENABLE_SPINNER"; then
		sleep 1
		return 0
	fi

	{
		for p in '/' '-' '\\' '|' '/' '-' '\\' '|'; do
			echo -n "$p"'\b'
			sleep 0.125
		done
		echo -n " "'\b' 
	}>/dev/tty
}

fetch() {
	local _src="${FWURL}/${1##*/}" _dst=$1 _user=_file _exit _error=''
	local _ftp_errors="$FD_DIR/ftp_errors"
	rm -f "$_ftp_errors"

	# The installer uses a limited doas(1) as a tiny su(1)
	set -o monitor # make sure ftp gets its own process group
	(
	_flags=-vm
	case "$VERBOSE" in
		0|1) _flags=-VM ; exec 2>"$_ftp_errors" ;;
		  2) _flags=-Vm ;;
	esac

	if ! "$DROP_PRIVS"; then
		/usr/bin/ftp -N error -D 'Get/Verify' $_flags -o- "$_src" > "$_dst"
	elif [ -x /usr/bin/su ]; then
		exec /usr/bin/su -s /bin/ksh "$_user" -c \
		    "/usr/bin/ftp -N error -D 'Get/Verify' $_flags -o- '$_src'" > "$_dst"
	else
		exec /usr/bin/doas -u "$_user" \
		    /usr/bin/ftp -N error -D 'Get/Verify' $_flags -o- "$_src" > "$_dst"
	fi
	) & FTPPID=$!
	set +o monitor

	SECONDS=0
	_last=0
	while kill -0 -"$FTPPID" 2>/dev/null; do
		if [[ $SECONDS -gt 12 ]]; then
			set -- $( ls -ln "$_dst" 2>/dev/null )
			if [[ $_last -ne $5 ]]; then
				_last=$5
				SECONDS=0
				spin
			else
				kill -INT -"$FTPPID" 2>/dev/null
				_error=" (timed out)"
			fi
		else
			spin
		fi
	done

	set +o errexit
	wait "$FTPPID"
	_exit=$?
	set -o errexit

	unset FTPPID

	if ((_exit != 0)); then
		rm -f "$_dst"

		# ftp doesn't provide useful exit codes
		# so we have to grep its STDERR.
		# _exit=2 means don't keep trying
		_exit=2

		# If it was 404, we might succeed at another file
		if [ -s "$_ftp_errors" ] && \
		    grep -q "404 Not Found" "$_ftp_errors"; then
			_exit=1
			_error=" (404 Not Found)"
			rm -f "$_ftp_errors"
		fi

		warn "Cannot fetch $_src$_error"
	fi

	# If we have ftp errors, print them out,
	# removing any cntrl characters (like 0x0d),
	# and any leading blank lines.
	if [ -s "$_ftp_errors" ]; then
		sed -e 's/[[:cntrl:]]//g' \
		    -e '/./,$!d' "$_ftp_errors" >&"$WARN_FD"
	fi

	return "$_exit"
}

# If we fail to fetch the CFILE, we don't want to try again
# but we might be doing this in a subshell so write out
# a blank file indicating failure.
check_cfile() {
	if [ -e "$CFILE" ]; then
		[ -s "$CFILE" ] || return 2
		return 0
	fi
	if ! fetch_cfile; then
		echo -n > "$CFILE"
		return 2
	fi
	return 0
}

fetch_cfile() {
	if "$DOWNLOAD"; then
		set +o noclobber # we want to get the latest CFILE
		fetch "$CFILE" || return 1
		set -o noclobber
		signify -qVep "$FWPUB_KEY" -x "$CFILE" -m /dev/null \
		    2>&"$WARN_FD" || {
		        warn "Signature check of SHA256.sig failed"
		        rm -f "$CFILE"
			return 1
		    }
	elif [ ! -e "$CFILE" ]; then
		warn "${0##*/}: $CFILE: No such file or directory"
		return 1
	fi

	return 0
}

verify() {
	check_cfile || return $?
	# The installer sha256 lacks -C, do it by hand
	if ! grep -Fqx "SHA256 (${1##*/}) = $( /bin/sha256 -qb "$1" )" "$CFILE"
	then
		((VERBOSE != 1)) && warn "Checksum test for ${1##*/} failed."
		return 1
	fi

	return 0
}

# When verifying existing files that we are going to re-download
# if VERBOSE is 0, don't show the checksum failure of an existing file.
verify_existing() {
	local _v=$VERBOSE
	check_cfile || return $?

	((_v == 0)) && "$DOWNLOAD" && _v=1
	( VERBOSE=$_v verify "$@" )
}

devices_in_dmesg() {
	local IFS
	local _d _m _dmesgtail _last='' _nl='
'

	# The dmesg can contain multiple boots, only look in the last one
	_dmesgtail="$( echo ; sed -n 'H;/^OpenBSD/h;${g;p;}' "$DMESG" )"

	grep -v '^[[:space:]]*#' "$FWPATTERNS" |
	    while read -r _d _m; do
		[ "$_d" = "$_last" ]  && continue
		[ "$_m" ]             || _m="${_nl}${_d}[0-9] at "
		[ "$_m" = "${_m#^}" ] || _m="${_nl}${_m#^}"

		IFS='*'
		set -- $_m
		unset IFS

		case $# in
		    1|2|3) [[ $_dmesgtail = *$1*([!$_nl])${2-}*([!$_nl])${3-}* ]] || continue;;
		    *) warn "${0##*/}: Bad pattern '${_m#$_nl}' in $FWPATTERNS"; exit 1 ;;
		esac

		echo "$_d"
		_last="$_d"
	    done
}

firmware_filename() {
	check_cfile || return $?
	sed -n "s/.*(\($1-firmware-.*\.tgz\)).*/\1/p" "$CFILE" | sed '$!d'
}

firmware_devicename() {
	local _d="${1##*/}"
	_d="${_d%-firmware-*}"
	echo "$_d"
}

lock_db() {
	local _waited
	[ "${LOCKPID:-}" ] && return 0

	# The installer doesn't have perl, so we can't lock there
	[ -e /usr/bin/perl ] || return 0

	set -o monitor
	perl <<-'EOL' |&
		no lib ('/usr/local/libdata/perl5/site_perl');
		use v5.36;
		use OpenBSD::PackageInfo qw< lock_db >;

		$|=1;

		$0 = "fw_update: lock_db";
		my $waited = 0;
		package OpenBSD::FwUpdateState {
			use parent 'OpenBSD::BaseState';
			sub errprint ($self, @p) {
				if ($p[0] && $p[0] =~ /already locked/) {
					$waited++;
					$p[0] = " " . $p[0]
					    if !$ENV{VERBOSE};
				}
				$self->SUPER::errprint(@p);
			}

		}
		lock_db(0, 'OpenBSD::FwUpdateState');

		say "$$ $waited";

		# Wait for STDOUT to be readable, which won't happen
		# but if our parent exits unexpectedly it will close.
		my $rin = '';
		vec($rin, fileno(STDOUT), 1) = 1;
		select $rin, '', '', undef;
EOL
	set +o monitor

	read -rp LOCKPID _waited

	if ((_waited)); then
		! ((VERBOSE)) && status "${0##*/}:"
	fi

	return 0
}

available_firmware() {
	check_cfile || return $?
	sed -n 's/.*(\(.*\)-firmware.*/\1/p' "$CFILE"
}

installed_firmware() {
	local _pre="$1" _match="$2" _post="$3" _firmware _fw
	set -sA _firmware -- $(
	    set +o noglob
	    grep -Fxl '@option firmware' \
		"${DESTDIR}/var/db/pkg/"$_pre"$_match"$_post"/+CONTENTS" \
		2>/dev/null || true
	    set -o noglob
	)

	[ "${_firmware[*]:-}" ] || return 0
	for _fw in "${_firmware[@]}"; do
		_fw="${_fw%/+CONTENTS}"
		echo "${_fw##*/}"
	done
}

detect_firmware() {
	local _devices _last='' _d

	set -sA _devices -- $(
	    devices_in_dmesg
	    for _d in $( installed_firmware '*' '-firmware-' '*' ); do
		firmware_devicename "$_d"
	    done
	)

	[ "${_devices[*]:-}" ] || return 0
	for _d in "${_devices[@]}"; do
		[ "$_last" = "$_d" ] && continue
		echo "$_d"
		_last="$_d"
	done
}

add_firmware () {
	local _f="${1##*/}" _m="${2:-Install}"
	local _pkgdir="${DESTDIR}/var/db/pkg" _pkg
	FWPKGTMP="$( tmpdir "${DESTDIR}/var/db/pkg/.firmware" )"
	local _flags=-vm
	case "$VERBOSE" in
		0|1) _flags=-VM ;;
		2|3) _flags=-Vm ;;
	esac

	ftp -N "${0##/}" -D "$_m" "$_flags" -o- "file:${1}" |
		tar -s ",^\+,${FWPKGTMP}/+," \
		    -s ",^firmware,${DESTDIR}/etc/firmware," \
		    -C / -zxphf - "+*" "firmware/*"


	[ -s "${FWPKGTMP}/+CONTENTS" ] &&
	    _pkg="$( sed -n '/^@name /{s///p;q;}' "${FWPKGTMP}/+CONTENTS" )"

	if [ ! "${_pkg:-}" ]; then
		warn "Failed to extract name from $1, partial install"
		rm -rf "$FWPKGTMP"
		unset FWPKGTMP
		return 1
	fi

	if [ -e "$_pkgdir/$_pkg" ]; then
		warn "Failed to register: $_pkgdir/$_pkg is not firmware"
		rm -rf "$FWPKGTMP"
		unset FWPKGTMP
		return 1
	fi

	ed -s "${FWPKGTMP}/+CONTENTS" <<EOL
/^@comment pkgpath/ -1a
@option manual-installation
@option firmware
@comment install-script
.
w
EOL

	chmod 755 "$FWPKGTMP"
	mv "$FWPKGTMP" "$_pkgdir/$_pkg"
	unset FWPKGTMP
}

remove_files() {
	local _r
	# Use rm -f, not removing files/dirs is probably not worth failing over
	for _r in "$@" ; do
		if [ -d "$_r" ]; then
			# The installer lacks rmdir,
			# but we only want to remove empty directories.
			set +o noglob
			[ "$_r/*" = "$( echo "$_r"/* )" ] && rm -rf "$_r"
			set -o noglob
		else
			rm -f "$_r"
		fi
	done
}

delete_firmware() {
	local _cwd _pkg="$1" _pkgdir="${DESTDIR}/var/db/pkg" _remove _l

	# TODO: Check hash for files before deleting
	((VERBOSE > 2)) && echo -n "Uninstall $_pkg ..."
	_cwd="${_pkgdir}/$_pkg"

	if [ ! -e "$_cwd/+CONTENTS" ] ||
	    ! grep -Fxq '@option firmware' "$_cwd/+CONTENTS"; then
		warn "${0##*/}: $_pkg does not appear to be firmware"
		return 2
	fi

	set -A _remove -- "${_cwd}/+CONTENTS" "${_cwd}"

	while read -r _l; do
		case "$_l" in
		@cwd\ *) _cwd="${DESTDIR}${_l##@cwd+( )}"
		  ;;
		@*) continue
		  ;;
		*) set -A _remove -- "$_cwd/$_l" "${_remove[@]}"
		  ;;
		esac
	done < "${_pkgdir}/${_pkg}/+CONTENTS"

	remove_files "${_remove[@]}"

	((VERBOSE > 2)) && echo " done."

	return 0
}

unregister_firmware() {
	local _d="$1" _pkgdir="${DESTDIR}/var/db/pkg" _fw

	set -A installed -- $( installed_firmware '' "$d-firmware-" '*' )
	if [ "${installed:-}" ]; then
		for _fw in "${installed[@]}"; do
			((VERBOSE)) && echo "Unregister $_fw"
			"$DRYRUN" && continue
			remove_files \
			    "$_pkgdir/$_fw/+CONTENTS" \
			    "$_pkgdir/$_fw/+DESC" \
			    "$_pkgdir/$_fw/"
		done
		return 0
	fi

	return 1
}

set_fw_paths() {
	local _version="${VNAME:-}" _fwdir
	unset VNAME

	if [ ! "$_version" ]; then
		_version=$(sed -nE \
		    '/^OpenBSD ([0-9]+\.[0-9][^ ]*) .*/{s//\1/;h;};${g;p;}' \
		    "$DMESG")
	
		# If VNAME was set in the environment instead of the DMESG,
		# looking in the DMESG for "current" is wrong.
		# Setting VNAME is undocumented anyway.
		[ "${_version#*-}" = current ] && _fwdir=snapshots

		_version=${_version%-*}
	fi
	
	[ "${FWURL:-}" ] ||
	     FWURL=http://firmware.openbsd.org/firmware/${_fwdir:-$_version}

	# TODO: Would it be better to use the untrusted comment in CFILE?
	_version=${_version%.*}${_version#*.}
	FWPUB_KEY=${DESTDIR}/etc/signify/openbsd-${_version}-fw.pub
}

usage() {
	echo "usage: ${0##*/} [-adFlnv] [-D path] [-p path] [driver | file ...]"
	exit 1
}

ALL=false
LIST=false
DMESG=/var/run/dmesg.boot

while getopts :adD:Flnp:v name
do
	case "$name" in
	a) ALL=true ;;
	d) DELETE=true ;;
	D) DMESG="$OPTARG" ;;
	F) INSTALL=false ;;
	l) LIST=true ;;
	n) DRYRUN=true ;;
	p) FWURL="$OPTARG" ;;
	v) ((++VERBOSE)) ;;
	:)
	    warn "${0##*/}: option requires an argument -- -$OPTARG"
	    usage
	    ;;
	?)
	    warn "${0##*/}: unknown option -- -$OPTARG"
	    usage
	    ;;
	esac
done
shift $((OPTIND - 1))

# When listing, provide a clean output
"$LIST" && VERBOSE=1 ENABLE_SPINNER=false

# Progress bars, not spinner When VERBOSE > 1
((VERBOSE > 1)) && ENABLE_SPINNER=false

if [ -x /usr/bin/id ] && [ "$(/usr/bin/id -u)" != 0 ]; then
	if ! "$INSTALL" || "$LIST"; then
		# When we aren't in the installer,
		# allow downloading as the current user.
		DROP_PRIVS=false
	else
		warn "need root privileges"
		exit 1
	fi
fi

if [ "${FWURL:-}" ] && ! "$INSTALL" ; then
	warn "Cannot use -F and -p"
	usage
fi

if [ ! -s "$DMESG" ]; then
	warn "${0##*/}: $DMESG: No such file or directory"
	exit 1
fi

set_fw_paths

if [[ $FWURL != @(ftp|http?(s))://* ]]; then
	FWURL="${FWURL#file:}"
	! [ -d "$FWURL" ] &&
	    warn "The path must be a URL or an existing directory" &&
	    exit 1

	DOWNLOAD=false
	LOCALSRC="$FWURL"
	FWURL="file:$FWURL"
fi

set -sA devices -- "$@"

FD_DIR="$( tmpdir "${DESTDIR}/tmp/${0##*/}-fd" )"
# When being verbose, save the status line for the end.
if ((VERBOSE)); then
	exec 3>"${FD_DIR}/status"
	STATUS_FD=3
fi
# Control "warning" messages to avoid the middle of a line.
# Things that we don't expect to send to STDERR
# still go there so the output, while it may be ugly, isn't lost
exec 4>"${FD_DIR}/warn"
WARN_FD=4

status "${0##*/}:"

if "$DELETE"; then
	! "$INSTALL" && warn "Cannot use -F and -d" && usage
	lock_db

	# Show the "Uninstall" message when just deleting not upgrading
	((VERBOSE)) && VERBOSE=3

	set -A installed
	if [ "${devices[*]:-}" ]; then
		"$ALL" && warn "Cannot use -a and devices/files" && usage

		set -A installed -- $(
		    for d in "${devices[@]}"; do
			f="${d##*/}"  # only care about the name
			f="${f%.tgz}" # allow specifying the package name
			[ "$( firmware_devicename "$f" )" = "$f" ] && f="$f-firmware"

			set -A i -- $( installed_firmware '' "$f-" '*' )

			if [ "${i[*]:-}" ]; then
				echo "${i[@]}"
			else
				warn "No firmware found for '$d'"
			fi
		    done
		)
	elif "$ALL"; then
		set -A installed -- $( installed_firmware '*' '-firmware-' '*' )
	else
		set -A installed -- $(
		    set -- $( devices_in_dmesg )
		    for f in $( installed_firmware '*' -firmware- '*' ); do
		        n="$( firmware_devicename "$f" )"
		        for d; do
		            [ "$d" = "$n" ] && continue 2
		        done
		        echo "$f"
		    done
		)
	fi

	status " delete "

	comma=''
	if [ "${installed:-}" ]; then
		for fw in "${installed[@]}"; do
			status "$comma$( firmware_devicename "$fw" )"
			comma=,
			if "$DRYRUN"; then
				((VERBOSE)) && echo "Delete $fw"
			elif "$LIST"; then
				echo "$fw"
			else
				delete_firmware "$fw" || {
					status " ($fw failed)"
					continue
				}
			fi
		done
	fi

	[ "$comma" ] || status none

	# no status when listing
	"$LIST" && rm -f "$FD_DIR/status"

	exit
fi

! "$INSTALL" && ! "$LIST" && ! "$DRYRUN" && LOCALSRC="${LOCALSRC:-.}"

if [ ! "$LOCALSRC" ]; then
	LOCALSRC="$( tmpdir "${DESTDIR}/tmp/${0##*/}" )"
	REMOVE_LOCALSRC=true
fi

CFILE="$LOCALSRC/$CFILE"

if [ "${devices[*]:-}" ]; then
	"$ALL" && warn "Cannot use -a and devices/files" && usage
elif "$ALL"; then
	set -sA devices -- $( available_firmware )
else
	((VERBOSE > 1)) && echo -n "Detect firmware ..."
	set -sA devices -- $( detect_firmware )
	((VERBOSE > 1)) &&
	    { [ "${devices[*]:-}" ] && echo " found." || echo " done." ; }
fi


set -A add ''
set -A update ''
kept=''
unregister=''

"$LIST" && ! "$INSTALL" &&
    echo "$FWURL/${CFILE##*/}"

if [ "${devices[*]:-}" ]; then
	lock_db
	for f in "${devices[@]}"; do
		d="$( firmware_devicename "$f" )"

		if "$LIST" && "$INSTALL"; then
			echo "$d"
			continue
		fi

		verify_existing=true
		if [ "$f" = "$d" ]; then
			f=$( firmware_filename "$d" ) || {
				# Fetching the CFILE here is often the
				# first attempt to talk to FWURL
				# If it fails, no point in continuing.
				if (($? > 1)); then
					status " failed."
					exit 1
				fi

				# otherwise we can try the next firmware
				continue
			}
			if [ ! "$f" ]; then
				if "$INSTALL" && unregister_firmware "$d"; then
					unregister="$unregister,$d"
				else
					warn "Unable to find firmware for $d"
				fi
				continue
			fi
		elif ! "$INSTALL" && ! grep -Fq "($f)" "$CFILE" ; then
			warn "Cannot download local file $f"
			exit 1
		else
			# Don't verify files specified on the command-line
			verify_existing=false
		fi

		if "$LIST"; then
			echo "$FWURL/$f"
			continue
		fi

		set -A installed
		if "$INSTALL"; then
			set -A installed -- \
			    $( installed_firmware '' "$d-firmware-" '*' )

			if [ "${installed[*]:-}" ]; then
				for i in "${installed[@]}"; do
					if [ "${f##*/}" = "$i.tgz" ]; then
						((VERBOSE > 2)) \
						    && echo "Keep $i"
						kept="$kept,$d"
						continue 2
					fi
				done
			fi
		fi

		# Fetch an unqualified file into LOCALSRC
		# if it doesn't exist in the current directory.
		if [ "$f" = "${f##/}" ] && [ ! -e "$f" ]; then
			f="$LOCALSRC/$f"
		fi

		if "$verify_existing" && [ -e "$f" ]; then
			pending_status=false
			if ((VERBOSE == 1)); then
				echo -n "Verify ${f##*/} ..."
				pending_status=true
			elif ((VERBOSE > 1)) && ! "$INSTALL"; then
				echo "Keep/Verify ${f##*/}"
			fi

			if "$DRYRUN" || verify_existing "$f"; then
				"$pending_status" && echo " done."
				if ! "$INSTALL"; then
					kept="$kept,$d"
					continue
				fi
			elif "$DOWNLOAD"; then
				"$pending_status" && echo " failed."
				((VERBOSE > 1)) && echo "Refetching $f"
				rm -f "$f"
			else
				"$pending_status" && echo " failed."
				continue
			fi
		fi

		if [ "${installed[*]:-}" ]; then
			set -A update -- "${update[@]}" "$f"
		else
			set -A add -- "${add[@]}" "$f"
		fi

	done
fi

if "$LIST"; then
	# No status when listing
	rm -f "$FD_DIR/status"
	exit
fi

if "$INSTALL"; then
	status " add "
	action=Install
else
	status " download "
	action=Download
fi

comma=''
[ "${add[*]}" ] || status none
for f in "${add[@]}" _update_ "${update[@]}"; do
	[ "$f" ] || continue
	if [ "$f" = _update_ ]; then
		comma=''
		"$INSTALL" || continue
		action=Update
		status "; update "
		[ "${update[*]}" ] || status none
		continue
	fi
	d="$( firmware_devicename "$f" )"
	status "$comma$d"
	comma=,

	pending_status=false
	if [ -e "$f" ]; then
		if "$DRYRUN"; then
			((VERBOSE)) && echo "$action ${f##*/}"
		else
			if ((VERBOSE == 1)); then
				echo -n "Install ${f##*/} ..."
				pending_status=true
			fi
		fi
	elif "$DOWNLOAD"; then
		if "$DRYRUN"; then
			((VERBOSE)) && echo "Get/Verify ${f##*/}"
		else
			if ((VERBOSE == 1)); then
				echo -n "Get/Verify ${f##*/} ..."
				pending_status=true
			fi
			fetch  "$f" &&
			verify "$f" || {
				integer e=$?

				"$pending_status" && echo " failed."
				status " failed (${f##*/})"

				if ((VERBOSE)) && [ -s "$FD_DIR/warn" ]; then
					cat "$FD_DIR/warn" >&2
					rm -f "$FD_DIR/warn"
				fi

				# Fetch or verify exited > 1
				# which means we don't keep trying.
				((e > 1)) && exit 1

				continue
			}
		fi
	elif "$INSTALL"; then
		warn "Cannot install ${f##*/}, not found"
		continue
	fi

	if ! "$INSTALL"; then
		"$pending_status" && echo " done."
		continue
	fi

	if ! "$DRYRUN"; then
		if [ "$action" = Update ]; then
			for i in $( installed_firmware '' "$d-firmware-" '*' )
			do
				delete_firmware "$i" || {
					"$pending_status" &&
					    echo -n " (remove $i failed)"
					status " (remove $i failed)"

					continue
				}
				#status " (removed $i)"
			done
		fi

		add_firmware "$f" "$action" || {
			"$pending_status" && echo " failed."
			status " failed (${f##*/})"
			continue
		}
	fi

	if "$pending_status"; then
		if [ "$action" = Install ]; then
			echo " installed."
		else
			echo " updated."
		fi
	fi
done

[ "$unregister" ] && status "; unregister ${unregister:#,}"
[ "$kept"       ] && status "; keep ${kept:#,}"

exit 0
