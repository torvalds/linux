#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2000 Alexandre Peixoto
# All rights reserved.
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

# Change ipfw(8) rules with safety guarantees for remote operation
#
# Invoke this script to edit ${firewall_script}. It will call ${EDITOR},
# or vi(1) if the environment variable is not set, for you to edit
# ${firewall_script}, ask for confirmation, and then run
# ${firewall_script}. You can then examine the output of ipfw list and
# confirm whether you want the new version or not.
#
# If no answer is received in 30 seconds, the previous
# ${firewall_script} is run, restoring the old rules (this assumes ipfw
# flush is present in it).
#
# If the new rules are confirmed, they'll replace ${firewall_script} and
# the previous ones will be copied to ${firewall_script}.{date}. Mail
# will also be sent to root with a unified diff of the rule change.
#
# Unapproved rules are kept in ${firewall_script}.new, and you are
# offered the option of changing them instead of the present rules when
# you call this script.
#
# This script could be improved by using version control
# software.

if [ -r /etc/defaults/rc.conf ]; then
	. /etc/defaults/rc.conf
	source_rc_confs
elif [ -r /etc/rc.conf ]; then
	. /etc/rc.conf
fi

EDITOR=${EDITOR:-/usr/bin/vi}
PAGER=${PAGER:-/usr/bin/more}

tempfoo=`basename $0`
TMPFILE=`mktemp -t ${tempfoo}` || exit 1

get_yes_no() {
	while true
	do
		echo -n "$1 (Y/N) ? " 
		read -t 30 a
		if [ $? != 0 ]; then
			a="No";
		        return;
		fi
		case $a in
			[Yy]) a="Yes";
			      return;;
			[Nn]) a="No";
			      return;;
			*);;
		esac
	done
}

restore_rules() {
	nohup sh ${firewall_script} </dev/null >/dev/null 2>&1
	rm ${TMPFILE}
	exit 1
}

case "${firewall_type}" in
[Cc][Ll][Ii][Ee][Nn][Tt]|\
[Cc][Ll][Oo][Ss][Ee][Dd]|\
[Oo][Pp][Ee][Nn]|\
[Ss][Ii][Mm][Pp][Ll][Ee]|\
[Uu][Nn][Kk][Nn][Oo][Ww][Nn])
	edit_file="${firewall_script}"
	rules_edit=no
	;;
*)
	if [ -r "${firewall_type}" ]; then
		edit_file="${firewall_type}"
		rules_edit=yes
	fi
	;;
esac

if [ -f ${edit_file}.new ]; then
	get_yes_no "A new rules file already exists, do you want to use it"
	[ $a = 'No' ] && cp ${edit_file} ${edit_file}.new
else 
	cp ${edit_file} ${edit_file}.new
fi

trap restore_rules SIGHUP

${EDITOR} ${edit_file}.new

get_yes_no "Do you want to install the new rules"

[ $a = 'No' ] && exit 1

cat <<!
The rules will be changed now. If the message 'Type y to keep the new
rules' does not appear on the screen or the y key is not pressed in 30
seconds, the original rules will be restored.
The TCP/IP connections might be broken during the change. If so, restore
the ssh/telnet connection being used.
!

if [ ${rules_edit} = yes ]; then
	nohup sh ${firewall_script} ${firewall_type}.new \
	    < /dev/null > ${TMPFILE} 2>&1
else
	nohup sh ${firewall_script}.new \
	    < /dev/null > ${TMPFILE} 2>&1
fi
sleep 2;
get_yes_no "Would you like to see the resulting new rules"
[ $a = 'Yes' ] && ${PAGER} ${TMPFILE}
get_yes_no "Type y to keep the new rules"
[ $a != 'Yes' ] && restore_rules

DATE=`date "+%Y%m%d%H%M"`
cp ${edit_file} ${edit_file}.$DATE
mv ${edit_file}.new ${edit_file} 
cat <<!
The new rules are now installed. The previous rules have been preserved in
the file ${edit_file}.$DATE
!
diff -F "^# .*[A-Za-z]" -u ${edit_file}.$DATE ${edit_file} \
    | mail -s "`hostname` Firewall rule change" root
rm ${TMPFILE}
exit 0
