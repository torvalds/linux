#-
# Copyright (c) 2010,2015 Nathan Whitehorn
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#


#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
        
#include <vm/vm.h>
#include <vm/vm_page.h>

#include <machine/mmuvar.h>

/**
 * MOEA64 kobj methods for 64-bit Book-S page table
 * manipulation routines used, for example, by hypervisors.
 */

INTERFACE moea64;

CODE {
	static moea64_pte_replace_t moea64_pte_replace_default;

	static int64_t moea64_pte_replace_default(mmu_t mmu,
	    struct pvo_entry *pvo, int flags)
	{
		int64_t refchg;

		refchg = MOEA64_PTE_UNSET(mmu, pvo);
		MOEA64_PTE_INSERT(mmu, pvo);

		return (refchg);
	}
}

/**
 * Return ref/changed bits from PTE referenced by _pvo if _pvo is currently in
 * the page table. Returns -1 if _pvo not currently present in the page table.
 */
METHOD int64_t pte_synch {
	mmu_t		_mmu;
	struct pvo_entry *_pvo;
};

/**
 * Clear bits ptebit (a mask) from the low word of the PTE referenced by
 * _pvo. Return previous values of ref/changed bits or -1 if _pvo is not
 * currently in the page table.
 */
METHOD int64_t pte_clear {
	mmu_t		_mmu;
	struct pvo_entry *_pvo;
	uint64_t	_ptebit;
};

/**
 * Invalidate the PTE referenced by _pvo, returning its ref/changed bits.
 * Returns -1 if PTE not currently present in page table.
 */
METHOD int64_t pte_unset {
	mmu_t		_mmu;
	struct pvo_entry *_pvo;
};

/**
 * Update the reference PTE to correspond to the contents of _pvo. Has the
 * same ref/changed semantics as pte_unset() (and should clear R/C bits). May
 * change the PVO's location in the page table or return with it unmapped if
 * PVO_WIRED is not set. By default, does unset() followed by insert().
 * 
 * _flags is a bitmask describing what level of page invalidation should occur:
 *   0 means no invalidation is required
 *   MOEA64_PTE_PROT_UPDATE signifies that the page protection bits are changing
 *   MOEA64_PTE_INVALIDATE requires an invalidation of the same strength as
 *    pte_unset() followed by pte_insert() 
 */
METHOD int64_t pte_replace {
	mmu_t		_mmu;
	struct pvo_entry *_pvo;
	int		_flags;
} DEFAULT moea64_pte_replace_default;

/**
 * Insert a PTE corresponding to _pvo into the page table, returning any errors
 * encountered and (optionally) setting the PVO slot value to some
 * representation of where the entry was placed.
 *
 * Must not replace PTEs marked LPTE_WIRED. If an existing valid PTE is spilled,
 * must synchronize ref/changed bits as in pte_unset().
 */
METHOD int pte_insert {
	mmu_t		_mmu;
	struct pvo_entry *_pvo;
};

