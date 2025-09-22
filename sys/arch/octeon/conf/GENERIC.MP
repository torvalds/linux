#	$OpenBSD: GENERIC.MP,v 1.1 2013/06/01 17:09:41 jasper Exp $

include "arch/octeon/conf/GENERIC"

option MULTIPROCESSOR
#option MP_LOCKDEBUG

cpu*   at mainbus?
