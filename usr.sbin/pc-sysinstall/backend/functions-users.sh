#!/bin/sh
#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2010 iXsystems, Inc.  All rights reserved.
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

# Functions which runs commands on the system

. ${BACKEND}/functions.sh
. ${BACKEND}/functions-parse.sh


# Function which checks and sets up auto-login for a user if specified
check_autologin()
{
  get_value_from_cfg autoLoginUser
  if [ -n "${VAL}"  -a "${INSTALLTYPE}" = "PCBSD" ]
  then
    AUTOU="${VAL}"
    # Add the auto-login user line
    sed -i.bak "s/AutoLoginUser=/AutoLoginUser=${AUTOU}/g" ${FSMNT}/usr/local/kde4/share/config/kdm/kdmrc

    # Add the auto-login user line
    sed -i.bak "s/AutoLoginEnable=false/AutoLoginEnable=true/g" ${FSMNT}/usr/local/kde4/share/config/kdm/kdmrc

  fi
};

# Function which actually runs the adduser command on the filesystem
add_user()
{
 ARGS="${1}"

 if [ -e "${FSMNT}/.tmpPass" ]
 then
   # Add a user with a supplied password
   run_chroot_cmd "cat /.tmpPass | pw useradd ${ARGS}"
   rc_halt "rm ${FSMNT}/.tmpPass"
 else
   # Add a user with no password
   run_chroot_cmd "cat /.tmpPass | pw useradd ${ARGS}"
 fi

};

# Function which reads in the config, and adds any users specified
setup_users()
{

  # We are ready to start setting up the users, lets read the config
  while read line
  do

    echo $line | grep -q "^userName=" 2>/dev/null
    if [ $? -eq 0 ]
    then
      get_value_from_string "${line}"
      USERNAME="$VAL"
    fi

    echo $line | grep -q "^userComment=" 2>/dev/null
    if [ $? -eq 0 ]
    then
      get_value_from_string "${line}"
      USERCOMMENT="$VAL"
    fi

    echo $line | grep -q "^userPass=" 2>/dev/null
    if [ $? -eq 0 ]
    then
      get_value_from_string "${line}"
      USERPASS="$VAL"
    fi

    echo $line | grep -q "^userEncPass=" 2>/dev/null
    if [ $? -eq 0 ]
    then
      get_value_from_string "${line}"
      USERENCPASS="$VAL"
    fi

    echo $line | grep -q "^userShell=" 2>/dev/null
    if [ $? -eq 0 ]
    then
      get_value_from_string "${line}"
      strip_white_space "$VAL"
      USERSHELL="$VAL"
    fi

    echo $line | grep -q "^userHome=" 2>/dev/null
    if [ $? -eq 0 ]
    then
      get_value_from_string "${line}"
      USERHOME="$VAL"
    fi

    echo $line | grep -q "^userGroups=" 2>/dev/null
    if [ $? -eq 0 ]
    then
      get_value_from_string "${line}"
      USERGROUPS="$VAL"
    fi


    echo $line | grep -q "^commitUser" 2>/dev/null
    if [ $? -eq 0 ]
    then
      # Found our flag to commit this user, lets check and do it
      if [ -n "${USERNAME}" ]
      then

        # Now add this user to the system, by building our args list
        ARGS="-n ${USERNAME}"

        if [ -n "${USERCOMMENT}" ]
        then
          ARGS="${ARGS} -c \"${USERCOMMENT}\""
        fi
         
        if [ -n "${USERPASS}" ]
        then
          ARGS="${ARGS} -h 0"
          echo "${USERPASS}" >${FSMNT}/.tmpPass
	elif [ -n "${USERENCPASS}" ] 
	then
          ARGS="${ARGS} -H 0"
          echo "${USERENCPASS}" >${FSMNT}/.tmpPass
        else
          ARGS="${ARGS} -h -"
          rm ${FSMNT}/.tmpPass 2>/dev/null 2>/dev/null
        fi

        if [ -n "${USERSHELL}" ]
        then
          ARGS="${ARGS} -s \"${USERSHELL}\""
        else
          ARGS="${ARGS} -s \"/nonexistant\""
        fi
         
        if [ -n "${USERHOME}" ]
        then
          ARGS="${ARGS} -m -d \"${USERHOME}\""
        fi

        if [ -n "${USERGROUPS}" ]
        then
          ARGS="${ARGS} -G \"${USERGROUPS}\""
        fi

        add_user "${ARGS}"

        # Unset our vars before looking for any more users
        unset USERNAME USERCOMMENT USERPASS USERENCPASS USERSHELL USERHOME USERGROUPS
      else
        exit_err "ERROR: commitUser was called without any userName= entry!!!" 
      fi
    fi

  done <${CFGF}


  # Check if we need to enable a user to auto-login to the desktop
  check_autologin

};
