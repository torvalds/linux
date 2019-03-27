#!/bin/sh
#
#  ifhp - Print Ghostscript-simulated PostScript on a DesJet 500
#  Installed in /usr/local/libexec/hpif

#
#  Treat LF as CR+LF:
#
printf "\033&k2G" || exit 2

#
#  Read first two characters of the file
#
read first_line
first_two_chars=`expr "$first_line" : '\(..\)'`

if [ "$first_two_chars" = "%!" ]; then
    #
    #  It is PostScript; use Ghostscript to scan-convert and print it
    #
    /usr/local/bin/gs -dSAFER -dNOPAUSE -q -sDEVICE=djet500 -sOutputFile=- - \
        && exit 0

else
    #
    #  Plain text or HP/PCL, so just print it directly; print a form
    #  at the end to eject the last page.
    #
    echo "$first_line" && cat && printf "\f" && exit 0
fi

exit 2
