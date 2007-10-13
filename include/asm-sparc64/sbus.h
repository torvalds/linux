/* sbus.h: Defines for the Sun SBus.
 *
 * Copyright (C) 1996, 1999, 2007 David S. Miller (davem@davemloft.net)
 */

#ifndef _SPARC64_SBUS_H
#define _SPARC64_SBUS_H

#include <linux/dma-mapping.h>
#include <linux/ioport.h>

#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/of_device.h>
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
	struct of_device	ofdev;
	struct sbus_bus		*bus;
	struct sbus_dev		*next;
	struct sbus_dev		*child;
	struct sbus_dev		*parent;
	int prom_node;	
	char prom_name[64];
	int slot;

	struct resource resource[PROMREG_MAX];

	struct linux_prom_registers reg_addrs[PROMREG_MAX];
	int num_registers;

	struct linux_prom_ranges device_ranges[PROMREG_MAX];
	int num_device_ranges;

	unsigned int irqs[4];
	int num_irqs;
};
#define to_sbus_device(d) container_of(d, struct sbus_dev, ofdev.dev)

/* This struct describes the SBus(s) found on this machine. */
struct sbus_bus {
	struct of_device	ofdev;
	struct sbus_dev		*devices;	/* Tree of SBUS devices	*/
	struct sbus_bus		*next;		/* Next SBUS in system	*/
	int			prom_node;      /* OBP node of SBUS	*/
	char			prom_name[64];	/* Usually "sbus" or "sbi" */
	int			clock_freq;

	struct linux_prom_ranges sbus_ranges[PROMREG_MAX];
	int num_sbus_ranges;

	int portid;
};
#define to_sbus(d) container_of(d, struct sbus_bus, ofdev.dev)

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
extern void sbus_fill_device_irq(struct sbus_dev *);

static inline void *sbus_alloc_consistent(struct sbus_dev *sdev , size_t size,
					  dma_addr_t *dma_handle)
{
	return dma_alloc_coherent(&sdev->ofdev.dev, size,
				  dma_handle, GFP_ATOMIC);
}

static inline void sbus_free_consistent(struct sbus_dev *sdev, size_t size,
					void *vaddr, dma_addr_t dma_handle)
{
	return dma_free_coherent(&sdev->ofdev.dev, size, vaddr, dma_handle);
}

#define SBUS_DMA_BIDIRECTIONAL	DMA_BIDIRECTIONAL
#define SBUS_DMA_TODEVICE	DMA_TO_DEVICE
#define SBUS_DMA_FROMDEVICE	DMA_FROM_DEVICE
#define	SBUS_DMA_NONE		DMA_NONE

/* All the rest use streaming mode mappings. */
static inline dma_addr_t sbus_map_single(struct sbus_dev *sdev, void *ptr,
					 size_t size, int direction)
{
	return dma_map_single(&sdev->ofdev.dev, ptr, size,
			      (enum dma_data_direction) direction);
}

static inline void sbus_unmap_single(struct sbus_dev *sdev,
				     dma_addr_t dma_addr, size_t size,
				     int direction)
{
	dma_unmap_single(&sdev->ofdev.dev, dma_addr, size,
			 (enum dma_data_direction) direction);
}

static inline int sbus_map_sg(struct sbus_dev *sdev, struct scatterlist *sg,
			      int nents, int direction)
{
	return dma_map_sg(&sdev->ofdev.dev, sg, nents,
			  (enum dma_data_direction) direction);
}

static inline void sbus_unmap_sg(struct sbus_dev *sdev, struct scatterlist *sg,
				 int nents, int direction)
{
	dma_unmap_sg(&sdev->ofdev.dev, sg, nents,
		     (enum dma_data_direction) direction);
}

/* Finally, allow explicit synchronization of streamable mappings. */
static inline void sbus_dma_sync_single_for_cpu(struct sbus_dev *sdev,
						dma_addr_t dma_handle,
						size_t size, int direction)
{
	dma_sync_single_for_cpu(&sdev->ofdev.dev, dma_handle, size,
				(enum dma_data_direction) direction);
}
#define sbus_dma_sync_single sbus_dma_sync_single_for_cpu

static inline void sbus_dma_sync_single_for_device(struct sbus_dev *sdev,
						   dma_addr_t dma_handle,
						   size_t size, int direction)
{
	/* No flushing needed to sync cpu writes to the device.  */
}

static inline void sbus_dma_sync_sg_for_cpu(struct sbus_dev *sdev,
					    struct scatterlist *sg,
					    int nents, int direction)
{
	dma_sync_sg_for_cpu(&sdev->ofdev.dev, sg, nents,
			    (enum dma_data_direction) direction);
}
#define sbus_dma_sync_sg sbus_dma_sync_sg_for_cpu

static inline void sbus_dma_sync_sg_for_device(struct sbus_dev *sdev,
					       struct scatterlist *sg,
					       int nents, int direction)
{
	/* No flushing needed to sync cpu writes to the device.  */
}

extern void sbus_arch_bus_ranges_init(struct device_node *, struct sbus_bus *);
extern void sbus_setup_iommu(struct sbus_bus *, struct device_node *);
extern void sbus_setup_arch_props(struct sbus_bus *, struct device_node *);
extern int sbus_arch_preinit(void);
extern void sbus_arch_postinit(void);

#endif /* !(_SPARC64_SBUS_H) */
