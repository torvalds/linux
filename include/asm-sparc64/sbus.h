/* $Id: sbus.h,v 1.14 2000/02/18 13:50:55 davem Exp $
 * sbus.h: Defines for the Sun SBus.
 *
 * Copyright (C) 1996, 1999 David S. Miller (davem@redhat.com)
 */

#ifndef _SPARC64_SBUS_H
#define _SPARC64_SBUS_H

#include <linux/dma-mapping.h>
#include <linux/ioport.h>

#include <asm/oplib.h>
#include <asm/iommu.h>
#include <asm/scatterlist.h>

/* We scan which devices are on the SBus using the PROM node device
 * tree.  SBus devices are described in two different ways.  You can
 * either get an absolute address at which to access the device, or
 * you can get a SBus 'slot' number and an offset within that slot.
 */

/* The base address at which to calculate device OBIO addresses. */
#define SUN_SBUS_BVADDR        0x00000000
#define SBUS_OFF_MASK          0x0fffffff

/* These routines are used to calculate device address from slot
 * numbers + offsets, and vice versa.
 */

static __inline__ unsigned long sbus_devaddr(int slotnum, unsigned long offset)
{
  return (unsigned long) (SUN_SBUS_BVADDR+((slotnum)<<28)+(offset));
}

static __inline__ int sbus_dev_slot(unsigned long dev_addr)
{
  return (int) (((dev_addr)-SUN_SBUS_BVADDR)>>28);
}

struct sbus_bus;

/* Linux SBUS device tables */
struct sbus_dev {
	struct sbus_bus *bus;	/* Our toplevel parent SBUS	*/
	struct sbus_dev *next;	/* Chain of siblings		*/
	struct sbus_dev *child;	/* Chain of children		*/
	struct sbus_dev *parent;/* Parent device if not toplevel*/
	int prom_node;		/* OBP node of this device	*/
	char prom_name[64];	/* OBP device name property	*/
	int slot;		/* SBUS slot number		*/

	struct resource resource[PROMREG_MAX];

	struct linux_prom_registers reg_addrs[PROMREG_MAX];
	int num_registers, ranges_applied;

	struct linux_prom_ranges device_ranges[PROMREG_MAX];
	int num_device_ranges;

	unsigned int irqs[4];
	int num_irqs;
};

/* This struct describes the SBus(s) found on this machine. */
struct sbus_bus {
	void			*iommu;		/* Opaque IOMMU cookie	*/
	struct sbus_dev		*devices;	/* Tree of SBUS devices	*/
	struct sbus_bus		*next;		/* Next SBUS in system	*/
	int			prom_node;      /* OBP node of SBUS	*/
	char			prom_name[64];	/* Usually "sbus" or "sbi" */
	int			clock_freq;

	struct linux_prom_ranges sbus_ranges[PROMREG_MAX];
	int num_sbus_ranges;

	int portid;
	void *starfire_cookie;
};

extern struct sbus_bus *sbus_root;

/* Device probing routines could find these handy */
#define for_each_sbus(bus) \
        for((bus) = sbus_root; (bus); (bus)=(bus)->next)

#define for_each_sbusdev(device, bus) \
        for((device) = (bus)->devices; (device); (device)=(device)->next)
        
#define for_all_sbusdev(device, bus) \
	for ((bus) = sbus_root; (bus); (bus) = (bus)->next) \
		for ((device) = (bus)->devices; (device); (device) = (device)->next)

/* Driver DVMA interfaces. */
#define sbus_can_dma_64bit(sdev)	(1)
#define sbus_can_burst64(sdev)		(1)
extern void sbus_set_sbus64(struct sbus_dev *, int);

/* These yield IOMMU mappings in consistent mode. */
extern void *sbus_alloc_consistent(struct sbus_dev *, size_t, dma_addr_t *dma_addrp);
extern void sbus_free_consistent(struct sbus_dev *, size_t, void *, dma_addr_t);

#define SBUS_DMA_BIDIRECTIONAL	DMA_BIDIRECTIONAL
#define SBUS_DMA_TODEVICE	DMA_TO_DEVICE
#define SBUS_DMA_FROMDEVICE	DMA_FROM_DEVICE
#define	SBUS_DMA_NONE		DMA_NONE

/* All the rest use streaming mode mappings. */
extern dma_addr_t sbus_map_single(struct sbus_dev *, void *, size_t, int);
extern void sbus_unmap_single(struct sbus_dev *, dma_addr_t, size_t, int);
extern int sbus_map_sg(struct sbus_dev *, struct scatterlist *, int, int);
extern void sbus_unmap_sg(struct sbus_dev *, struct scatterlist *, int, int);

/* Finally, allow explicit synchronization of streamable mappings. */
extern void sbus_dma_sync_single_for_cpu(struct sbus_dev *, dma_addr_t, size_t, int);
#define sbus_dma_sync_single sbus_dma_sync_single_for_cpu
extern void sbus_dma_sync_single_for_device(struct sbus_dev *, dma_addr_t, size_t, int);
extern void sbus_dma_sync_sg_for_cpu(struct sbus_dev *, struct scatterlist *, int, int);
#define sbus_dma_sync_sg sbus_dma_sync_sg_for_cpu
extern void sbus_dma_sync_sg_for_device(struct sbus_dev *, struct scatterlist *, int, int);

#endif /* !(_SPARC64_SBUS_H) */
