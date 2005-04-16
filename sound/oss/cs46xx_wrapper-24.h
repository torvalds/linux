/*******************************************************************************
*
*      "cs46xx_wrapper.c" --  Cirrus Logic-Crystal CS46XX linux audio driver.
*
*      Copyright (C) 2000,2001  Cirrus Logic Corp.  
*            -- tom woller (twoller@crystal.cirrus.com) or
*               (pcaudio@crystal.cirrus.com).
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
* 01/11/2001 trw - new file from cs4281 wrapper code. 
*
*******************************************************************************/
#ifndef __CS46XX_WRAPPER24_H
#define __CS46XX_WRAPPER24_H

#include <linux/spinlock.h>

#define CS_OWNER .owner =
#define CS_THIS_MODULE THIS_MODULE,
static inline void cs46xx_null(struct pci_dev *pcidev) { return; }
#define cs4x_mem_map_reserve(page) SetPageReserved(page)
#define cs4x_mem_map_unreserve(page) ClearPageReserved(page)

#define free_dmabuf(card, dmabuf) \
	pci_free_consistent((card)->pci_dev, \
			    PAGE_SIZE << (dmabuf)->buforder, \
			    (dmabuf)->rawbuf, (dmabuf)->dmaaddr);
#define free_dmabuf2(card, dmabuf) \
	pci_free_consistent((card)->pci_dev, \
				    PAGE_SIZE << (dmabuf)->buforder_tmpbuff, \
				    (dmabuf)->tmpbuff, (dmabuf)->dmaaddr_tmpbuff);
#define cs4x_pgoff(vma) ((vma)->vm_pgoff)

#define RSRCISIOREGION(dev,num) ((dev)->resource[(num)].start != 0 && \
	 ((dev)->resource[(num)].flags & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO)
#define RSRCISMEMORYREGION(dev,num) ((dev)->resource[(num)].start != 0 && \
	 ((dev)->resource[(num)].flags & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_MEMORY)
#define RSRCADDRESS(dev,num) ((dev)->resource[(num)].start)
#define PCI_GET_DRIVER_DATA pci_get_drvdata
#define PCI_SET_DRIVER_DATA pci_set_drvdata
#define PCI_SET_DMA_MASK(pcidev,mask) pcidev->dma_mask = mask

#endif
