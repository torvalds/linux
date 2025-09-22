#!/bin/sh
# $OpenBSD: t2.sh,v 1.1 2005/04/15 07:06:35 markus Exp $

# tar.c, rev 1.36
CUR=$1
OBJ=$2
uudecode -p << '_EOF' | tar ztf - 2> /dev/null | cmp -s $CUR/t2.out /dev/stdin
begin 644 foo
M'XL(`````````^W1L1&`,`@%4$9A!$(@S!/=0./^)A9Z9ZEBQ6M^0_&YGS@+
M.",B4SVRNQ)/!(F8BY5,_1#1)#&@>A<;MK75!4:==^[/U6G^HIZW/VH^V%^E
/Q/XAA.!I!W_0A94`"```
`
end
_EOF
