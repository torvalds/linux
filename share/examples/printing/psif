#!/bin/sh
#
#  psif - Print PostScript or plain text on a PostScript printer
#  Script version; NOT the version that comes with lprps
#  Installed in /usr/local/libexec/psif
#

read first_line
first_two_chars=`expr "$first_line" : '\(..\)'`

if [ "$first_two_chars" = "%!" ]; then
   #
   #  PostScript job, print it.
   #
   echo "$first_line" && cat && printf "\004" && exit 0
   exit 2
else
   #
   #  Plain text, convert it, then print it.
   #
   ( echo "$first_line"; cat ) | /usr/local/bin/textps && printf "\004" && exit 0
   exit 2
fi
