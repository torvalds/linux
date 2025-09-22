#	$OpenBSD: GENERIC.MP,v 1.1 2020/07/21 21:38:31 kettenis Exp $

include "arch/powerpc64/conf/GENERIC"

option	MULTIPROCESSOR
#option	MP_LOCKDEBUG
#option	WITNESS

cpu*		at mainbus?
