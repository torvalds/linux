/*******************************************************************************
*
*      "cs4281_wrapper.c" --  Cirrus Logic-Crystal CS4281 linux audio driver.
*
*      Copyright (C) 2000,2001  Cirrus Logic Corp.  
*            -- tom woller (twoller@crystal.cirrus.com) or
*               (audio@crystal.cirrus.com).
*
*      This program is free software; you can redistribute it and/or modify
*      it under the terms of the GNU General Public License as published by
*      the Free Software Foundation; either version 2 of the License, or
*      (at your option) any later version.
*
*      This program is distributed in the hope that it will be useful,
*      but WITHOUT ANY WARRANTY; without even the implied warranty of
*      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*      GNU General Public License for more details.
*
*      You should have received a copy of the GNU General Public License
*      along with this program; if not, write to the Free Software
*      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
* 12/20/00 trw - new file. 
*
*******************************************************************************/

#include <linux/spinlock.h>

static int cs4281_resume_null(struct pci_dev *pcidev) { return 0; }
static int cs4281_suspend_null(struct pci_dev *pcidev, pm_message_t state) { return 0; }

#define free_dmabuf(state, dmabuf) \
	pci_free_consistent(state->pcidev, \
			    PAGE_SIZE << (dmabuf)->buforder, \
			    (dmabuf)->rawbuf, (dmabuf)->dmaaddr);
#define free_dmabuf2(state, dmabuf) \
	pci_free_consistent((state)->pcidev, \
				    PAGE_SIZE << (state)->buforder_tmpbuff, \
				    (state)->tmpbuff, (state)->dmaaddr_tmpbuff);
#define cs4x_pgoff(vma) ((vma)->vm_pgoff)

