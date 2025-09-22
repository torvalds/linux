/*	$OpenBSD: cp0access.S,v 1.24 2022/08/22 00:35:06 cheloha Exp $ */

/*
 * Copyright (c) 2001-2003 Opsycon AB  (www.opsycon.se / www.opsycon.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 *  Low level code to manage processor specific registers.
 */

#include <sys/errno.h>
#include <sys/syscall.h>

#include <machine/param.h>
#include <machine/asm.h>
#include <machine/cpu.h>
#include <mips64/mips_cpu.h>
#include <machine/regnum.h>

#include "assym.h"

	.set	mips3
	.set	noreorder		# Noreorder is default style!

/*
 * Set/clear software interrupt.
 */

LEAF(setsoftintr0, 0)
	MFC0	v0, COP_0_CAUSE_REG	# read cause register
	MFC0_HAZARD
	NOP
	or	v0, v0, SOFT_INT_MASK_0	# set soft clock interrupt
	MTC0	v0, COP_0_CAUSE_REG	# save it
	MTC0_HAZARD
	j	ra
	 NOP
END(setsoftintr0)

LEAF(clearsoftintr0, 0)
	MFC0	v0, COP_0_CAUSE_REG	# read cause register
	MFC0_HAZARD
	NOP
	and	v0, v0, ~SOFT_INT_MASK_0	# clear soft clock interrupt
	MTC0	v0, COP_0_CAUSE_REG	# save it
	MTC0_HAZARD
	j	ra
	 NOP
END(clearsoftintr0)

LEAF(setsoftintr1, 0)
	MFC0	v0, COP_0_CAUSE_REG	# read cause register
	MFC0_HAZARD
	NOP
	or	v0, v0, SOFT_INT_MASK_1	# set soft net interrupt
	MTC0	v0, COP_0_CAUSE_REG	# save it
	MTC0_HAZARD
	j	ra
	 NOP
END(setsoftintr1)

LEAF(clearsoftintr1, 0)
	MFC0	v0, COP_0_CAUSE_REG	# read cause register
	MFC0_HAZARD
	NOP
	and	v0, v0, ~SOFT_INT_MASK_1	# clear soft net interrupt
	MTC0	v0, COP_0_CAUSE_REG	# save it
	MTC0_HAZARD
	j	ra
	 NOP
END(clearsoftintr1)

/*
 * Set/change interrupt priority routines.
 * These routines return the previous state.
 */

LEAF(enableintr, 0)
	MFC0	v0, COP_0_STATUS_REG	# read status register
	MFC0_HAZARD
	NOP
	or	v1, v0, SR_INT_ENAB
	MTC0	v1, COP_0_STATUS_REG	# enable all interrupts
	MTC0_SR_IE_HAZARD
	j	ra
	 NOP
END(enableintr)

LEAF(disableintr, 0)
	MFC0	v0, COP_0_STATUS_REG	# read status register
	MFC0_HAZARD
	NOP
	and	v1, v0, ~SR_INT_ENAB
	MTC0	v1, COP_0_STATUS_REG	# disable all interrupts
	MTC0_SR_IE_HAZARD
	j	ra
	 NOP
END(disableintr)

LEAF(updateimask, 0)
	lw	t0, idle_mask
	not	a0, a0			# 1 means masked so invert.
	and	a0, t0			# never upgrade to higher than max
	MFC0	v0, COP_0_STATUS_REG
	MFC0_HAZARD
	LI	v1, ~SR_INT_MASK
	and	v1, v0
	and	v0, a0, SR_INT_MASK
	or	v1, v0
	MTC0	v1, COP_0_STATUS_REG
	MTC0_SR_IE_HAZARD
	ori	v1, SR_INT_ENAB		# enable interrupts
	MTC0	v1, COP_0_STATUS_REG
	MTC0_SR_IE_HAZARD
	jr	ra
	 move	v0, v1
END(updateimask)

LEAF(setsr, 0)
	MTC0	a0, COP_0_STATUS_REG
	MTC0_SR_IE_HAZARD
	jr	ra
	 move	v0, a0
END(setsr)

LEAF(getsr, 0)
	MFC0	v0, COP_0_STATUS_REG
	MFC0_HAZARD
	j	ra
	 NOP
END(getsr)

LEAF(cp0_get_config, 0)
	MFC0	v0, COP_0_CONFIG
	MFC0_HAZARD
	j	ra
	 NOP
END(cp0_get_config)

/* WARNING!
   Needs to be invoked from uncached address if changing cache settings. */
LEAF(cp0_set_config, 0)
	MTC0	a0, COP_0_CONFIG
	MTC0_HAZARD
	j	ra
	 NOP
END(cp0_set_config)

LEAF(cp0_get_prid, 0)
	MFC0	v0, COP_0_PRID
	MFC0_HAZARD
	j	ra
	NOP
END(cp0_get_prid)

LEAF(cp0_get_count, 0)
	MFC0	v0, COP_0_COUNT
	MFC0_HAZARD
	j	ra
	NOP
END(cp0_get_count)

LEAF(cp0_set_compare, 0)
	MTC0	a0, COP_0_COMPARE
	MTC0_HAZARD
	j	ra
	NOP
END(cp0_set_compare)

LEAF(cpu_rnd_messybits, 0)
	MFC0	v0, COP_0_COUNT
	MFC0_HAZARD
	MFC0	v1, COP_0_BAD_VADDR
	MFC0_HAZARD
	xor	v0, v1
	j	ra
	 NOP
END(cpu_rnd_messybits)

LEAF(cp0_get_cause, 0)
	MFC0	v0, COP_0_CAUSE_REG
	MFC0_HAZARD
	j	ra
	 NOP
END(cp0_get_cause)
