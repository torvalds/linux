#!/bin/sh

DESTDIR=$1

#
# Write out tabset file: arguments are prefix, number of tab stops,
# tab stop sequence, postfix.
#
write_tabset() {
	printf "$1"
	for i in $(seq $2); do printf "$3"; done
	printf "$4"
}

# Tabset files for HP terminals.
write_tabset '\r\e3\r' 13 '        \e1' '\n' >$DESTDIR/std
write_tabset '\r\e3\r' 9 '        \e1' '' >$DESTDIR/stdcrt

# Tabset for VT100 and derivatives.
write_tabset '\r\n\e[3g\n' 15 '\eH        ' '\eH\n' >$DESTDIR/vt100

# Tabset for VT3xx and VT4xx and derivatives.
printf '\n\e[3g\n\eP2$t9/17/25/33/41/49/57/65/73/81/89/97/105/113/121/129\e\\\n' >$DESTDIR/vt300

exit 0
