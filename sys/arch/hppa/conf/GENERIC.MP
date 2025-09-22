#	$OpenBSD: GENERIC.MP,v 1.1 2010/06/26 17:43:26 jsing Exp $

include "arch/hppa/conf/GENERIC"

option MULTIPROCESSOR
#option MP_LOCKDEBUG

cpu*   at mainbus?
