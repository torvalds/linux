#	$OpenBSD: GENERIC.MP,v 1.5 2018/07/01 21:05:07 kettenis Exp $

include "arch/arm64/conf/GENERIC"

option	MULTIPROCESSOR
#option	MP_LOCKDEBUG
#option	WITNESS

cpu*		at mainbus?
