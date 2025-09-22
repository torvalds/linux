#	$OpenBSD: GENERIC.MP,v 1.1 2020/07/19 14:40:34 visa Exp $

include "arch/loongson/conf/GENERIC"

option	MULTIPROCESSOR
#option	MP_LOCKDEBUG

option	MIPS_PTE64

cpu*	at mainbus?
