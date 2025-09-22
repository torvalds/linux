#	$OpenBSD: GENERIC.MP,v 1.11 2018/06/03 05:18:33 deraadt Exp $
#
#	GENERIC.MP - sample multiprocessor kernel
#

include "arch/i386/conf/GENERIC"

option		MULTIPROCESSOR	# Multiple processor support
#option		MP_LOCKDEBUG
#option		WITNESS

cpu*		at mainbus?
