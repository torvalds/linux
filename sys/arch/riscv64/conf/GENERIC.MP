#	$OpenBSD: GENERIC.MP,v 1.1 2021/06/29 21:27:52 kettenis Exp $

include "arch/riscv64/conf/GENERIC"

option	MULTIPROCESSOR
#option	MP_LOCKDEBUG
#option	WITNESS

cpu*		at mainbus?
