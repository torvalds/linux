#	$OpenBSD: GENERIC.MP,v 1.1 2014/01/26 17:40:11 miod Exp $

include "arch/alpha/conf/GENERIC"

option	MULTIPROCESSOR
#option	MP_LOCKDEBUG

cpu*		at mainbus?
