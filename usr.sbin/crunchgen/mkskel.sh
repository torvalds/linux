#! /bin/sh
#	$OpenBSD: mkskel.sh,v 1.1 2008/08/22 15:18:55 deraadt Exp $

# idea and sed lines taken straight from flex

cat <<!EOF
/* File created via mkskel.sh */

char *crunched_skel[] = {
!EOF

sed 's/\\/&&/g' $* | sed 's/"/\\"/g' | sed 's/.*/  "&",/'

cat <<!EOF
  0
};
!EOF
