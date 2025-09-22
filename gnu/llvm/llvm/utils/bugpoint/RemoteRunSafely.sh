#!/bin/sh
#
# Program:  RemoteRunSafely.sh
#
# Synopsis: This script simply runs another program remotely using ssh.
#           It always returns the another program exit code or exit with
#           code 255 which indicates that the program could not be executed.
#
# Syntax: 
#
#   RemoteRunSafely.sh <hostname> [-l <login_name>] [-p <port>]
#                <program> <args...>
#
#   where:
#     <hostname>    is the remote host to execute the program,
#     <login_name>  is the username on the remote host,
#     <port>        is the port used by the remote client,
#     <program>     is the path to the program to run,
#     <args...>     are the arguments to pass to the program.
#

printUsageAndExit()
{
  echo "Usage:"
  echo "./RemoteRunSafely.sh <hostname> [-l <login_name>] [-p <port>] " \
       "<program> <args...>"
  exit 255
}

moreArgsExpected()
{
  # $1 - remaining number of arguments
  # $2 - number of arguments to shift
  if [ $1 -lt $2 ]
  then
    echo "Error: Wrong number of arguments."
    printUsageAndExit
  fi
}

# Save a copy of the original arguments in a string before we
# clobber them with the shift command.
ORIG_ARGS="$*"
#DEBUG: echo 'GOT: '$ORIG_ARGS

moreArgsExpected $# 1
RHOST=$1
shift 1

RUSER=`id -un`
RCLIENT=ssh
RPORT=
WORKING_DIR=

moreArgsExpected $# 1
if [ $1 = "-l" ]; then
  moreArgsExpected $# 2
  RUSER=$2
  shift 2
fi
moreArgsExpected $# 1
if [ $1 = "-p" ]; then
  moreArgsExpected $# 2
  RPORT="-p $2"
  shift 2
fi

moreArgsExpected $# 1
PROGRAM=$(basename $1)
WORKING_DIR=$(dirname $1)
shift 1

#DEBUG: echo 'DIR='${0%%`basename $0`}
#DEBUG: echo 'RHOST='$RHOST
#DEBUG: echo 'RUSER='$RUSER
#DEBUG: echo 'PROGRAM='$PROGRAM
#DEBUG: echo 'WORKING_DIR='$WORKING_DIR
#DEBUG: echo 'ARGS='$*

# Sanity check
if [ "$RHOST" = "" -o "$PROGRAM" = "" ]; then
  printUsageAndExit
fi

# Local program file must exist and be execuatble
local_program=$WORKING_DIR"/"$PROGRAM
if [ ! -x "$local_program" ]; then
  echo "File "$local_program" does not exist or is not an executable.."
  exit 255
fi

connection=$RUSER'@'$RHOST
remote="./"$PROGRAM
(
  cat $local_program |        \
  $RCLIENT $connection $RPORT \
   'rm -f '$remote' ; '       \
   'cat > '$remote' ; chmod +x '$remote' ; '$remote' '$*' ; ' \
   'err=$? ; rm -f '$remote' ; exit $err'
)
err=$?

#DEBUG: echo script exit $err
exit $err

