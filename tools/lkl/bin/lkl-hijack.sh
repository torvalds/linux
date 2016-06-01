#!/bin/bash

##
## This wrapper script works to replace system calls symbols such as
## socket(2), recvmsg(2) for the redirection to LKL. Ideally it works
## with any applications, but in practice (tm) it depends on the maturity
## of hijack library (liblkl-hijack.so).
##
## Since LD_PRELOAD technique with setuid/setgid binary is tricky, you may
## need to use sudo (or equivalents) to do it (e.g., ping).
##
## % sudo hijack.sh ping 127.0.0.1
##

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)

export LD_LIBRARY_PATH=${script_dir}/../
if [ -n ${LKL_HIJACK_DEBUG+x}  ]
then
  trap '' TSTP
fi
LD_PRELOAD=liblkl-hijack.so $*
