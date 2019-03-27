#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2002-2004 Michael Telahun Makonnen. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#       Email: Mike Makonnen <mtm@FreeBSD.Org>
#
# $FreeBSD$
#

# err msg
#	Display $msg on stderr, unless we're being quiet.
#
err() {
	if [ -z "$quietflag" ]; then
		echo 1>&2 ${THISCMD}: ERROR: $*
	fi
}

# info msg
#	Display $msg on stdout, unless we're being quiet.
#
info() {
	if [ -z "$quietflag" ]; then
		echo ${THISCMD}: INFO: $*
	fi
}

# get_nextuid
#	Output the value of $_uid if it is available for use. If it
#	is not, output the value of the next higher uid that is available.
#	If a uid is not specified, output the first available uid, as indicated
#	by pw(8).
#
get_nextuid () {
	_uid=$1
	_nextuid=

	if [ -z "$_uid" ]; then
		_nextuid="`${PWCMD} usernext | cut -f1 -d:`"
	else
		while : ; do
			${PWCMD} usershow $_uid > /dev/null 2>&1
			if [ ! "$?" -eq 0 ]; then
				_nextuid=$_uid
				break
			fi
			_uid=$(($_uid + 1))
		done
	fi
	echo $_nextuid
}

# show_usage
#	Display usage information for this utility.
#
show_usage() {
	echo "usage: ${THISCMD} [options]"
	echo "  options may include:"
	echo "  -C		save to the configuration file only"
	echo "  -D		do not attempt to create the home directory"
	echo "  -E		disable this account after creation"
	echo "  -G		additional groups to add accounts to"
	echo "  -L		login class of the user"
	echo "  -M		file permission for home directory"
	echo "  -N		do not read configuration file"
	echo "  -S		a nonexistent shell is not an error"
	echo "  -d		home directory"
	echo "  -f		file from which input will be received"
	echo "  -g		default login group"
	echo "  -h		display this usage message"
	echo "  -k		path to skeleton home directory"
	echo "  -m		user welcome message file"
	echo "  -q		absolute minimal user feedback"
	echo "  -s		shell"
	echo "  -u		uid to start at"
	echo "  -w		password type: no, none, yes or random"
}

# valid_shells
#	Outputs a list of valid shells from /etc/shells. Only the
#	basename of the shell is output.
#
valid_shells() {
	_prefix=
	cat ${ETCSHELLS} |
	while read _path _junk ; do
		case $_path in
		\#*|'')
			;;
		*)
			echo -n "${_prefix}`basename $_path`"
			_prefix=' '
			;;
		esac
	done

	# /usr/sbin/nologin is a special case
	[ -x "${NOLOGIN_PATH}" ] && echo -n " ${NOLOGIN}"
}

# fullpath_from_shell shell
#	Given $shell, which is either the full path to a shell or
#	the basename component of a valid shell, get the
#	full path to the shell from the /etc/shells file.
#
fullpath_from_shell() {
	_shell=$1
	[ -z "$_shell" ] && return 1

	# /usr/sbin/nologin is a special case; it needs to be handled
	# before the cat | while loop, since a 'return' from within
	# a subshell will not terminate the function's execution, and
	# the path to the nologin shell might be printed out twice.
	#
	if [ "$_shell" = "${NOLOGIN}" -o \
	    "$_shell" = "${NOLOGIN_PATH}" ]; then
		echo ${NOLOGIN_PATH}
		return 0;
	fi

	cat ${ETCSHELLS} |
	while read _path _junk ; do
		case "$_path" in
		\#*|'')
			;;
		*)
			if [ "$_path" = "$_shell" -o \
			    "`basename $_path`" = "$_shell" ]; then
				echo $_path
				return 0
			fi
			;;
		esac
	done

	return 1
}

# shell_exists shell
#	If the given shell is listed in ${ETCSHELLS} or it is
#	the nologin shell this function will return 0.
#	Otherwise, it will return 1. If shell is valid but
#	the path is invalid or it is not executable it
#	will emit an informational message saying so.
#
shell_exists() {
	_sh="$1"
	_shellchk="${GREPCMD} '^$_sh$' ${ETCSHELLS} > /dev/null 2>&1"

	if ! eval $_shellchk; then
		# The nologin shell is not listed in /etc/shells.
		if [ "$_sh" != "${NOLOGIN_PATH}" ]; then
			err "Invalid shell ($_sh) for user $username."
			return 1
		fi
	fi
	! [ -x "$_sh" ] &&
	    info "The shell ($_sh) does not exist or is not executable."

	return 0
}

# save_config
#	Save some variables to a configuration file.
#	Note: not all script variables are saved, only those that
#	      it makes sense to save.
#
save_config() {
	echo "# Configuration file for adduser(8)."     >  ${ADDUSERCONF}
	echo "# NOTE: only *some* variables are saved." >> ${ADDUSERCONF}
	echo "# Last Modified on `${DATECMD}`."		>> ${ADDUSERCONF}
	echo ''				>> ${ADDUSERCONF}
	echo "defaultHomePerm=$uhomeperm" >> ${ADDUSERCONF}
	echo "defaultLgroup=$ulogingroup" >> ${ADDUSERCONF}
	echo "defaultclass=$uclass"	>> ${ADDUSERCONF}
	echo "defaultgroups=$ugroups"	>> ${ADDUSERCONF}
	echo "passwdtype=$passwdtype" 	>> ${ADDUSERCONF}
	echo "homeprefix=$homeprefix" 	>> ${ADDUSERCONF}
	echo "defaultshell=$ushell"	>> ${ADDUSERCONF}
	echo "udotdir=$udotdir"		>> ${ADDUSERCONF}
	echo "msgfile=$msgfile"		>> ${ADDUSERCONF}
	echo "disableflag=$disableflag" >> ${ADDUSERCONF}
	echo "uidstart=$uidstart"       >> ${ADDUSERCONF}
}

# add_user
#	Add a user to the user database. If the user chose to send a welcome
#	message or lock the account, do so.
#
add_user() {

	# Is this a configuration run? If so, don't modify user database.
	#
	if [ -n "$configflag" ]; then
		save_config
		return
	fi

	_uid=
	_name=
	_comment=
	_gecos=
	_home=
	_group=
	_grouplist=
	_shell=
	_class=
	_dotdir=
	_expire=
	_pwexpire=
	_passwd=
	_upasswd=
	_passwdmethod=

	_name="-n '$username'"
	[ -n "$uuid" ] && _uid='-u "$uuid"'
	[ -n "$ulogingroup" ] && _group='-g "$ulogingroup"'
	[ -n "$ugroups" ] && _grouplist='-G "$ugroups"'
	[ -n "$ushell" ] && _shell='-s "$ushell"'
	[ -n "$uclass" ] && _class='-L "$uclass"'
	[ -n "$ugecos" ] && _comment='-c "$ugecos"'
	[ -n "$udotdir" ] && _dotdir='-k "$udotdir"'
	[ -n "$uexpire" ] && _expire='-e "$uexpire"'
	[ -n "$upwexpire" ] && _pwexpire='-p "$upwexpire"'
	if [ -z "$Dflag" -a -n "$uhome" ]; then
		# The /nonexistent home directory is special. It
		# means the user has no home directory.
		if [ "$uhome" = "$NOHOME" ]; then
			_home='-d "$uhome"'
		else
			# Use home directory permissions if specified
			if [ -n "$uhomeperm" ]; then
				_home='-m -d "$uhome" -M "$uhomeperm"'
			else
				_home='-m -d "$uhome"'
			fi
		fi
	elif [ -n "$Dflag" -a -n "$uhome" ]; then
		_home='-d "$uhome"'
	fi
	case $passwdtype in
	no)
		_passwdmethod="-w no"
		_passwd="-h -"
		;;
	yes)
		# Note on processing the password: The outer double quotes
		# make literal everything except ` and \ and $.
		# The outer single quotes make literal ` and $.
		# We can ensure the \ isn't treated specially by specifying
		# the -r switch to the read command used to obtain the input.
		#
		_passwdmethod="-w yes"
		_passwd="-h 0"
		_upasswd='echo "$upass" |'
		;;
	none)
		_passwdmethod="-w none"
		;;
	random)
		_passwdmethod="-w random"
		;;
	esac

	_pwcmd="$_upasswd ${PWCMD} useradd $_uid $_name $_group $_grouplist $_comment"
	_pwcmd="$_pwcmd $_shell $_class $_home $_dotdir $_passwdmethod $_passwd"
	_pwcmd="$_pwcmd $_expire $_pwexpire"

	if ! _output=`eval $_pwcmd` ; then
		err "There was an error adding user ($username)."
		return 1
	else
		info "Successfully added ($username) to the user database."
		if [ "random" = "$passwdtype" ]; then
			randompass="$_output"
			info "Password for ($username) is: $randompass"
		fi
	fi

	if [ -n "$disableflag" ]; then
		if ${PWCMD} lock $username ; then
			info "Account ($username) is locked."
		else
			info "Account ($username) could NOT be locked."
		fi
	fi

	_line=
	_owner=
	_perms=
	if [ -n "$msgflag" ]; then
		[ -r "$msgfile" ] && {
			# We're evaluating the contents of an external file.
			# Let's not open ourselves up for attack. _perms will
			# be empty if it's writeable only by the owner. _owner
			# will *NOT* be empty if the file is owned by root.
			#
			_dir="`dirname $msgfile`"
			_file="`basename $msgfile`"
			_perms=`/usr/bin/find $_dir -name $_file -perm +07022 -prune`
			_owner=`/usr/bin/find $_dir -name $_file -user 0 -prune`
			if [ -z "$_owner" -o -n "$_perms" ]; then
				err "The message file ($msgfile) may be writeable only by root."
				return 1
			fi
			cat "$msgfile" |
			while read _line ; do
				eval echo "$_line"
			done | ${MAILCMD} -s"Welcome" ${username}
			info "Sent welcome message to ($username)."
		}
	fi
}

# get_user
#	Reads username of the account from standard input or from a global
#	variable containing an account line from a file. The username is
#	required. If this is an interactive session it will prompt in
#	a loop until a username is entered. If it is batch processing from
#	a file it will output an error message and return to the caller.
#
get_user() {
	_input=

	# No need to take down user names if this is a configuration saving run.
	[ -n "$configflag" ] && return

	while : ; do
		if [ -z "$fflag" ]; then
			echo -n "Username: "
			read _input
		else
			_input="`echo "$fileline" | cut -f1 -d:`"
		fi

		# There *must* be a username, and it must not exist. If
		# this is an interactive session give the user an
		# opportunity to retry.
		#
		if [ -z "$_input" ]; then
			err "You must enter a username!"
			[ -z "$fflag" ] && continue
		fi
		${PWCMD} usershow $_input > /dev/null 2>&1
		if [ "$?" -eq 0 ]; then
			err "User exists!"
			[ -z "$fflag" ] && continue
		fi
		break
	done
	username="$_input"
}

# get_gecos
#	Reads extra information about the user. Can be used both in interactive
#	and batch (from file) mode.
#
get_gecos() {
	_input=

	# No need to take down additional user information for a configuration run.
	[ -n "$configflag" ] && return

	if [ -z "$fflag" ]; then
		echo -n "Full name: "
		read _input
	else
		_input="`echo "$fileline" | cut -f7 -d:`"
	fi
	ugecos="$_input"
}

# get_shell
#	Get the account's shell. Works in interactive and batch mode. It
#	accepts either the base name of the shell or the full path.
#	If an invalid shell is entered it will simply use the default shell.
#
get_shell() {
	_input=
	_fullpath=
	ushell="$defaultshell"

	# Make sure the current value of the shell is a valid one
	if [ -z "$Sflag" ]; then
		if ! shell_exists $ushell ; then
			info "Using default shell ${defaultshell}."
			ushell="$defaultshell"
		fi
	fi

	if [ -z "$fflag" ]; then
		echo -n "Shell ($shells) [`basename $ushell`]: "
		read _input
	else
		_input="`echo "$fileline" | cut -f9 -d:`"
	fi
	if [ -n "$_input" ]; then
		if [ -n "$Sflag" ]; then
			ushell="$_input"
		else
			_fullpath=`fullpath_from_shell $_input`
			if [ -n "$_fullpath" ]; then
				ushell="$_fullpath"
			else
				err "Invalid shell ($_input) for user $username."
				info "Using default shell ${defaultshell}."
				ushell="$defaultshell"
			fi
		fi
	fi
}

# get_homedir
#	Reads the account's home directory. Used both with interactive input
#	and batch input.
#
get_homedir() {
	_input=
	if [ -z "$fflag" ]; then
		echo -n "Home directory [${homeprefix}/${username}]: "
		read _input
	else
		_input="`echo "$fileline" | cut -f8 -d:`"
	fi

	if [ -n "$_input" ]; then
		uhome="$_input"
		# if this is a configuration run, then user input is the home
		# directory prefix. Otherwise it is understood to
		# be $prefix/$user
		#
		[ -z "$configflag" ] && homeprefix="`dirname $uhome`" || homeprefix="$uhome"
	else
		uhome="${homeprefix}/${username}"
	fi
}

# get_homeperm
#	Reads the account's home directory permissions.
#
get_homeperm() {
	uhomeperm=$defaultHomePerm
	_input=
	_prompt=

	if [ -n "$uhomeperm" ]; then
		_prompt="Home directory permissions [${uhomeperm}]: "
	else
		_prompt="Home directory permissions (Leave empty for default): "
	fi
	if [ -z "$fflag" ]; then
		echo -n "$_prompt"
		read _input
	fi

	if [ -n "$_input" ]; then
		uhomeperm="$_input"
	fi
}

# get_uid
#	Reads a numeric userid in an interactive or batch session. Automatically
#	allocates one if it is not specified.
#
get_uid() {
	uuid=${uidstart}
	_input=
	_prompt=

	if [ -n "$uuid" ]; then
		uuid=`get_nextuid $uuid`
		_prompt="Uid [$uuid]: "
	else
		_prompt="Uid (Leave empty for default): "
	fi
	if [ -z "$fflag" ]; then
		echo -n "$_prompt"
		read _input
	else
		_input="`echo "$fileline" | cut -f2 -d:`"
	fi

	[ -n "$_input" ] && uuid=$_input
	uuid=`get_nextuid $uuid`
	uidstart=$uuid
}

# get_class
#	Reads login class of account. Can be used in interactive or batch mode.
#
get_class() {
	uclass="$defaultclass"
	_input=
	_class=${uclass:-"default"}

	if [ -z "$fflag" ]; then
		echo -n "Login class [$_class]: "
		read _input
	else
		_input="`echo "$fileline" | cut -f4 -d:`"
	fi

	[ -n "$_input" ] && uclass="$_input"
}

# get_logingroup
#	Reads user's login group. Can be used in both interactive and batch
#	modes. The specified value can be a group name or its numeric id.
#	This routine leaves the field blank if nothing is provided and
#	a default login group has not been set. The pw(8) command
#	will then provide a login group with the same name as the username.
#
get_logingroup() {
	ulogingroup="$defaultLgroup"
	_input=

	if [ -z "$fflag" ]; then
		echo -n "Login group [${ulogingroup:-$username}]: "
		read _input
	else
		_input="`echo "$fileline" | cut -f3 -d:`"
	fi

	# Pw(8) will use the username as login group if it's left empty
	[ -n "$_input" ] && ulogingroup="$_input"
}

# get_groups
#	Read additional groups for the user. It can be used in both interactive
#	and batch modes.
#
get_groups() {
	ugroups="$defaultgroups"
	_input=
	_group=${ulogingroup:-"${username}"}

	if [ -z "$configflag" ]; then
		[ -z "$fflag" ] && echo -n "Login group is $_group. Invite $username"
		[ -z "$fflag" ] && echo -n " into other groups? [$ugroups]: "
	else
		[ -z "$fflag" ] && echo -n "Enter additional groups [$ugroups]: "
	fi
	read _input

	[ -n "$_input" ] && ugroups="$_input"
}

# get_expire_dates
#	Read expiry information for the account and also for the password. This
#	routine is used only from batch processing mode.
#
get_expire_dates() {
	upwexpire="`echo "$fileline" | cut -f5 -d:`"
	uexpire="`echo "$fileline" | cut -f6 -d:`"
}

# get_password
#	Read the password in batch processing mode. The password field matters
#	only when the password type is "yes" or "random". If the field is empty and the
#	password type is "yes", then it assumes the account has an empty passsword
#	and changes the password type accordingly. If the password type is "random"
#	and the password field is NOT empty, then it assumes the account will NOT
#	have a random password and set passwdtype to "yes."
#
get_password() {
	# We may temporarily change a password type. Make sure it's changed
	# back to whatever it was before we process the next account.
	#
	[ -n "$savedpwtype" ] && {
		passwdtype=$savedpwtype
		savedpwtype=
	}

	# There may be a ':' in the password
	upass=${fileline#*:*:*:*:*:*:*:*:*:}

	if [ -z "$upass" ]; then
		case $passwdtype in
		yes)
			# if it's empty, assume an empty password
			passwdtype=none
			savedpwtype=yes
			;;
		esac
	else
		case $passwdtype in
		random)
			passwdtype=yes
			savedpwtype=random
			;;
		esac
	fi
}

# input_from_file
#	Reads a line of account information from standard input and
#	adds it to the user database.
#
input_from_file() {
	_field=

	while read -r fileline ; do
		case "$fileline" in
		\#*|'')
			;;
		*)
			get_user || continue
			get_gecos
			get_uid
			get_logingroup
			get_class
			get_shell
			get_homedir
			get_homeperm
			get_password
			get_expire_dates
			ugroups="$defaultgroups"

			add_user
			;;
		esac
	done
}

# input_interactive
#	Prompts for user information interactively, and commits to
#	the user database.
#
input_interactive() {
	_disable=
	_pass=
	_passconfirm=
	_random="no"
	_emptypass="no"
	_usepass="yes"
	_logingroup_ok="no"
	_groups_ok="no"
	case $passwdtype in
	none)
		_emptypass="yes"
		_usepass="yes"
		;;
	no)
		_usepass="no"
		;;
	random)
		_random="yes"
		;;
	esac

	get_user
	get_gecos
	get_uid

	# The case where group = user is handled elsewhere, so
	# validate any other groups the user is invited to.
	until [ "$_logingroup_ok" = yes ]; do
		get_logingroup
		_logingroup_ok=yes
		if [ -n "$ulogingroup" -a "$username" != "$ulogingroup" ]; then
			if ! ${PWCMD} show group $ulogingroup > /dev/null 2>&1; then
				echo "Group $ulogingroup does not exist!"
				_logingroup_ok=no
			fi
		fi
	done
	until [ "$_groups_ok" = yes ]; do
		get_groups
		_groups_ok=yes
		for i in $ugroups; do
			if [ "$username" != "$i" ]; then
				if ! ${PWCMD} show group $i > /dev/null 2>&1; then
					echo "Group $i does not exist!"
					_groups_ok=no
				fi
			fi
		done
	done

	get_class
	get_shell
	get_homedir
	get_homeperm

	while : ; do
		echo -n "Use password-based authentication? [$_usepass]: "
		read _input
		[ -z "$_input" ] && _input=$_usepass
		case $_input in
		[Nn][Oo]|[Nn])
			passwdtype="no"
			;;
		[Yy][Ee][Ss]|[Yy][Ee]|[Yy])
			while : ; do
				echo -n "Use an empty password? (yes/no) [$_emptypass]: "
				read _input
				[ -n "$_input" ] && _emptypass=$_input
				case $_emptypass in
				[Nn][Oo]|[Nn])
					echo -n "Use a random password? (yes/no) [$_random]: "
					read _input
					[ -n "$_input" ] && _random="$_input"
					case $_random in
					[Yy][Ee][Ss]|[Yy][Ee]|[Yy])
						passwdtype="random"
						break
						;;
					esac
					passwdtype="yes"
					[ -n "$configflag" ] && break
					trap 'stty echo; exit' 0 1 2 3 15
					stty -echo
					echo -n "Enter password: "
					read -r upass
					echo''
					echo -n "Enter password again: "
					read -r _passconfirm
					echo ''
					stty echo
					# if user entered a blank password
					# explicitly ask again.
					[ -z "$upass" -a -z "$_passconfirm" ] \
					    && continue
					;;
				[Yy][Ee][Ss]|[Yy][Ee]|[Yy])
					passwdtype="none"
					break;
					;;
				*)
					# invalid answer; repeat the loop
					continue
					;;
				esac
				if [ "$upass" != "$_passconfirm" ]; then
					echo "Passwords did not match!"
					continue
				fi
				break
			done
			;;
		*)
			# invalid answer; repeat loop
			continue
			;;
		esac
		break;
	done
	_disable=${disableflag:-"no"}
	while : ; do
		echo -n "Lock out the account after creation? [$_disable]: "
		read _input
		[ -z "$_input" ] && _input=$_disable
		case $_input in
		[Nn][Oo]|[Nn])
			disableflag=
			;;
		[Yy][Ee][Ss]|[Yy][Ee]|[Yy])
			disableflag=yes
			;;
		*)
			# invalid answer; repeat loop
			continue
			;;
		esac
		break
	done
	
	# Display the information we have so far and prompt to
	# commit it.
	#
	_disable=${disableflag:-"no"}
	[ -z "$configflag" ] && printf "%-10s : %s\n" Username $username
	case $passwdtype in
	yes)
		_pass='*****'
		;;
	no)
		_pass='<disabled>'
		;;
	none)
		_pass='<blank>'
		;;
	random)
		_pass='<random>'
		;;
	esac
	[ -z "$configflag" ] && printf "%-10s : %s\n" "Password" "$_pass"
	[ -n "$configflag" ] && printf "%-10s : %s\n" "Pass Type" "$passwdtype"
	[ -z "$configflag" ] && printf "%-10s : %s\n" "Full Name" "$ugecos"
	[ -z "$configflag" ] && printf "%-10s : %s\n" "Uid" "$uuid"
	printf "%-10s : %s\n" "Class" "$uclass"
	printf "%-10s : %s %s\n" "Groups" "${ulogingroup:-$username}" "$ugroups"
	printf "%-10s : %s\n" "Home" "$uhome"
	printf "%-10s : %s\n" "Home Mode" "$uhomeperm"
	printf "%-10s : %s\n" "Shell" "$ushell"
	printf "%-10s : %s\n" "Locked" "$_disable"
	while : ; do
		echo -n "OK? (yes/no): "
		read _input
		case $_input in
		[Nn][Oo]|[Nn])
			return 1
			;;
		[Yy][Ee][Ss]|[Yy][Ee]|[Yy])
			add_user
			;;
		*)
			continue
			;;
		esac
		break
	done
	return 0
}

#### END SUBROUTINE DEFINITION ####

THISCMD=`/usr/bin/basename $0`
DEFAULTSHELL=/bin/sh
ADDUSERCONF="${ADDUSERCONF:-/etc/adduser.conf}"
PWCMD="${PWCMD:-/usr/sbin/pw}"
MAILCMD="${MAILCMD:-mail}"
ETCSHELLS="${ETCSHELLS:-/etc/shells}"
NOHOME="/nonexistent"
NOLOGIN="nologin"
NOLOGIN_PATH="/usr/sbin/nologin"
GREPCMD="/usr/bin/grep"
DATECMD="/bin/date"

# Set default values
#
username=
uuid=
uidstart=
ugecos=
ulogingroup=
uclass=
uhome=
uhomeperm=
upass=
ushell=
udotdir=/usr/share/skel
ugroups=
uexpire=
upwexpire=
shells="`valid_shells`"
passwdtype="yes"
msgfile=/etc/adduser.msg
msgflag=
quietflag=
configflag=
fflag=
infile=
disableflag=
Dflag=
Sflag=
readconfig="yes"
homeprefix="/home"
randompass=
fileline=
savedpwtype=
defaultclass=
defaultLgroup=
defaultgroups=
defaultshell="${DEFAULTSHELL}"
defaultHomePerm=

# Make sure the user running this program is root. This isn't a security
# measure as much as it is a useful method of reminding the user to
# 'su -' before he/she wastes time entering data that won't be saved.
#
procowner=${procowner:-`/usr/bin/id -u`}
if [ "$procowner" != "0" ]; then
	err 'you must be the super-user (uid 0) to use this utility.'
	exit 1
fi

# Override from our conf file
# Quickly go through the commandline line to see if we should read
# from our configuration file. The actual parsing of the commandline
# arguments happens after we read in our configuration file (commandline
# should override configuration file).
#
for _i in $* ; do
	if [ "$_i" = "-N" ]; then
		readconfig=
		break;
	fi
done
if [ -n "$readconfig" ]; then
	# On a long-lived system, the first time this script is run it
	# will barf upon reading the configuration file for its perl predecessor.
	if ( . ${ADDUSERCONF} > /dev/null 2>&1 ); then
		[ -r ${ADDUSERCONF} ] && . ${ADDUSERCONF} > /dev/null 2>&1
	fi
fi 

# Process command-line options
#
for _switch ; do
	case $_switch in
	-L)
		defaultclass="$2"
		shift; shift
		;;
	-C)
		configflag=yes
		shift
		;;
	-D)
		Dflag=yes
		shift
		;;
	-E)
		disableflag=yes
		shift
		;;
	-k)
		udotdir="$2"
		shift; shift
		;;
	-f)
		[ "$2" != "-" ] && infile="$2"
		fflag=yes
		shift; shift
		;;
	-g)
		defaultLgroup="$2"
		shift; shift
		;;
	-G)
		defaultgroups="$2"
		shift; shift
		;;
	-h)
		show_usage
		exit 0
		;;
	-d)
		homeprefix="$2"
		shift; shift
		;;
	-m)
		case "$2" in
		[Nn][Oo])
			msgflag=
			;;
		*)
			msgflag=yes
			msgfile="$2"
			;;
		esac
		shift; shift
		;;
	-M)
		defaultHomePerm=$2
		shift; shift
		;;
	-N)
		readconfig=
		shift
		;;
	-w)
		case "$2" in
		no|none|random|yes)
			passwdtype=$2
			;;
		*)
			show_usage
			exit 1
			;;
		esac
		shift; shift
		;;
	-q)
		quietflag=yes
		shift
		;;
	-s)
		defaultshell="`fullpath_from_shell $2`"
		shift; shift
		;;
	-S)
		Sflag=yes
		shift
		;;
	-u)
		uidstart=$2
		shift; shift
		;;
	esac
done

# If the -f switch was used, get input from a file. Otherwise,
# this is an interactive session.
#
if [ -n "$fflag" ]; then
	if [ -z "$infile" ]; then
		input_from_file
	elif [ -n "$infile" ]; then
		if [ -r "$infile" ]; then
			input_from_file < $infile
		else
			err "File ($infile) is unreadable or does not exist."
		fi
	fi
else
	input_interactive
	while : ; do
		if [ -z "$configflag" ]; then
			echo -n "Add another user? (yes/no): "
		else
			echo -n "Re-edit the default configuration? (yes/no): "
		fi
		read _input
		case $_input in
		[Yy][Ee][Ss]|[Yy][Ee]|[Yy])
			uidstart=`get_nextuid $uidstart`
			input_interactive
			continue
			;;
		[Nn][Oo]|[Nn])
			echo "Goodbye!"
			;;
		*)
			continue
			;;
		esac
		break
	done
fi
