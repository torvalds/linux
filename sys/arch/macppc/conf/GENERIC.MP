#	$OpenBSD: GENERIC.MP,v 1.2 2020/04/15 08:09:33 mpi Exp $

include "arch/macppc/conf/GENERIC"

option	MULTIPROCESSOR
#option	MP_LOCKDEBUG
#option	WITNESS

cpu*	at mainbus?
