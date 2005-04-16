
/*     
 **********************************************************************
 *     emuadxmg.c - Address space manager for emu10k1 driver 
 *     Copyright 1999, 2000 Creative Labs, Inc. 
 * 
 ********************************************************************** 
 * 
 *     Date                 Author          Summary of changes 
 *     ----                 ------          ------------------ 
 *     October 20, 1999     Bertrand Lee    base code release 
 * 
 ********************************************************************** 
 * 
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version. 
 * 
 *     This program is distributed in the hope that it will be useful, 
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 *     GNU General Public License for more details. 
 * 
 *     You should have received a copy of the GNU General Public 
 *     License along with this program; if not, write to the Free 
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, 
 *     USA. 
 * 
 ********************************************************************** 
 */

#include "hwaccess.h"

/* Allocates emu address space */

int emu10k1_addxmgr_alloc(u32 size, struct emu10k1_card *card)
{
	u16 *pagetable = card->emupagetable;
	u16 index = 0;
	u16 numpages;
	unsigned long flags;

	/* Convert bytes to pages */
	numpages = (size / EMUPAGESIZE) + ((size % EMUPAGESIZE) ? 1 : 0);

	spin_lock_irqsave(&card->lock, flags);

	while (index < (MAXPAGES - 1)) {
		if (pagetable[index] & 0x8000) {
			/* This block of pages is in use, jump to the start of the next block. */
			index += (pagetable[index] & 0x7fff);
		} else {
			/* Found free block */
			if (pagetable[index] >= numpages) {

				/* Block is large enough */

				/* If free block is larger than the block requested
				 * then adjust the size of the block remaining */
				if (pagetable[index] > numpages)
					pagetable[index + numpages] = pagetable[index] - numpages;

				pagetable[index] = (numpages | 0x8000);	/* Mark block as used */

				spin_unlock_irqrestore(&card->lock, flags);

				return index;
			} else {
				/* Block too small, jump to the start of the next block */
				index += pagetable[index];
			}
		}
	}

	spin_unlock_irqrestore(&card->lock, flags);

	return -1;
}

/* Frees a previously allocated emu address space. */

void emu10k1_addxmgr_free(struct emu10k1_card *card, int index)
{
	u16 *pagetable = card->emupagetable;
	u16 origsize = 0;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	if (pagetable[index] & 0x8000) {
		/* Block is allocated - mark block as free */
		origsize = pagetable[index] & 0x7fff;
		pagetable[index] = origsize;

		/* If next block is free, we concat both blocks */
		if (!(pagetable[index + origsize] & 0x8000))
			pagetable[index] += pagetable[index + origsize] & 0x7fff;
	}

	spin_unlock_irqrestore(&card->lock, flags);

	return;
}
