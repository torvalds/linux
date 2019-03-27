#!/bin/sh
#
# hpif - Simple text input filter for lpd for HP-PCL based printers
# Installed in /usr/local/libexec/hpif
#
# Simply copies stdin to stdout.  Ignores all filter arguments.
# Tells printer to treat LF as CR+LF. Writes a form feed character
# after printing job.

printf "\033&k2G" && cat && printf "\f" && exit 0
exit 2
