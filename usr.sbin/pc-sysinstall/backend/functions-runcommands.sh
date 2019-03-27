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

run_chroot_cmd()
{
  CMD="$@"
  echo_log "Running chroot command: ${CMD}"
  echo "$CMD" >${FSMNT}/.runcmd.sh
  chmod 755 ${FSMNT}/.runcmd.sh
  chroot ${FSMNT} sh /.runcmd.sh
  RES=$?

  rm ${FSMNT}/.runcmd.sh
  return ${RES}
};

run_chroot_script()
{
  SCRIPT="$@"
  SBASE=`basename $SCRIPT`

  cp ${SCRIPT} ${FSMNT}/.$SBASE
  chmod 755 ${FSMNT}/.${SBASE}

  echo_log "Running chroot script: ${SCRIPT}"
  chroot ${FSMNT} /.${SBASE}
  RES=$?

  rm ${FSMNT}/.${SBASE}
  return ${RES}
};


run_ext_cmd()
{
  CMD="$@"
  # Make sure to export FSMNT, in case cmd needs it
  export FSMNT
  echo_log "Running external command: ${CMD}"
  echo "${CMD}"> ${TMPDIR}/.runcmd.sh
  chmod 755 ${TMPDIR}/.runcmd.sh
  sh ${TMPDIR}/.runcmd.sh
  RES=$?

  rm ${TMPDIR}/.runcmd.sh
  return ${RES}
};


# Starts the user setup
run_commands()
{
  while read line
  do
    # Check if we need to run any chroot command
    echo $line | grep -q ^runCommand=  2>/dev/null
    if [ $? -eq 0 ]
    then
      get_value_from_string "$line"
      run_chroot_cmd "$VAL"
    fi

    # Check if we need to run any chroot script
    echo $line | grep -q ^runScript= 2>/dev/null
    if [ $? -eq 0 ]
    then
      get_value_from_string "$line"
      run_chroot_script "$VAL"
    fi

    # Check if we need to run any chroot command
    echo $line | grep -q ^runExtCommand= 2>/dev/null
    if [ $? -eq 0 ]
    then
      get_value_from_string "$line"
      run_ext_cmd "$VAL"
    fi

  done <${CFGF}

};
