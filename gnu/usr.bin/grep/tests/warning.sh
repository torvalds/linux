#!/bin/sh
#
# Tell them not to be alarmed.

: ${srcdir=.}

failures=0

#
cat <<\EOF

Please, do not be alarmed if some of the tests failed.
Report them to <bug-gnu-utils@gnu.org>,
with the line number, the name of the file,
and grep version number 'grep --version'.
Make sure you have the word grep in the subject.
Thank You.

EOF
