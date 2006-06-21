#ifndef __ASM_ARM_DMA_H
#define __ASM_ARM_DMA_H

typedef unsigned int dmach_t;

#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/memory.h>
#include <asm/scatterlist.h>

// FIXME - do we really need this? arm26 cant do 'proper' DMA

typedef struct dma_struct dma_t;
typedef unsigned int dmamode_t;

struct dma_ops {
        int     (*request)(dmach_t, dma_t *);           /* optional */
        void    (*free)(dmach_t, dma_t *);              /* optional */
        void    (*enable)(dmach_t, dma_t *);            /* mandatory */
        void    (*disable)(dmach_t, dma_t *);           /* mandatory */
        int     (*residue)(dmach_t, dma_t *);           /* optional */
        int     (*setspeed)(dmach_t, dma_t *, int);     /* optional */
        char    *type;
};

struct dma_struct {
        struct scatterlist buf;         /* single DMA                   */
        int             sgcount;        /* number of DMA SG             */
        struct scatterlist *sg;         /* DMA Scatter-Gather List      */

        unsigned int    active:1;       /* Transfer active              */
        unsigned int    invalid:1;      /* Address/Count changed        */
        unsigned int    using_sg:1;     /* using scatter list?          */
        dmamode_t       dma_mode;       /* DMA mode                     */
        int             speed;          /* DMA speed                    */

        unsigned int    lock;           /* Device is allocated          */
        const char      *device_id;     /* Device name                  */

        unsigned int    dma_base;       /* Controller base address      */
        int             dma_irq;        /* Controller IRQ               */
        int             state;          /* Controller state             */
        struct scatterlist cur_sg;      /* Current controller buffer    */

        struct dma_ops  *d_ops;
};

/* Prototype: void arch_dma_init(dma)
 * Purpose  : Initialise architecture specific DMA
 * Params   : dma - pointer to array of DMA structures
 */
extern void arch_dma_init(dma_t *dma);

extern void isa_init_dma(dma_t *dma);


#define MAX_DMA_ADDRESS         0x03000000
#define MAX_DMA_CHANNELS        3

/* ARC */
#define DMA_VIRTUAL_FLOPPY0     0
#define DMA_VIRTUAL_FLOPPY1     1
#define DMA_VIRTUAL_SOUND       2

/* A5K */
#define DMA_FLOPPY              0

/*
 * DMA modes
 */
#define DMA_MODE_MASK	3

#define DMA_MODE_READ	 0
#define DMA_MODE_WRITE	 1
#define DMA_MODE_CASCADE 2
#define DMA_AUTOINIT	 4

extern spinlock_t  dma_spin_lock;

static inline unsigned long claim_dma_lock(void)
{
	unsigned long flags;
	spin_lock_irqsave(&dma_spin_lock, flags);
	return flags;
}

static inline void release_dma_lock(unsigned long flags)
{
	spin_unlock_irqrestore(&dma_spin_lock, flags);
}

/* Clear the 'DMA Pointer Flip Flop'.
 * Write 0 for LSB/MSB, 1 for MSB/LSB access.
 */
#define clear_dma_ff(channel)

/* Set only the page register bits of the transfer address.
 *
 * NOTE: This is an architecture specific function, and should
 *       be hidden from the drivers
 */
extern void set_dma_page(dmach_t channel, char pagenr);

/* Request a DMA channel
 *
 * Some architectures may need to do allocate an interrupt
 */
extern int  request_dma(dmach_t channel, const char * device_id);

/* Free a DMA channel
 *
 * Some architectures may need to do free an interrupt
 */
extern void free_dma(dmach_t channel);

/* Enable DMA for this channel
 *
 * On some architectures, this may have other side effects like
 * enabling an interrupt and setting the DMA registers.
 */
extern void enable_dma(dmach_t channel);

/* Disable DMA for this channel
 *
 * On some architectures, this may have other side effects like
 * disabling an interrupt or whatever.
 */
extern void disable_dma(dmach_t channel);

/* Test whether the specified channel has an active DMA transfer
 */
extern int dma_channel_active(dmach_t channel);

/* Set the DMA scatter gather list for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA address immediately, but defer it to the enable_dma().
 */
extern void set_dma_sg(dmach_t channel, struct scatterlist *sg, int nr_sg);

/* Set the DMA address for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA address immediately, but defer it to the enable_dma().
 */
extern void set_dma_addr(dmach_t channel, unsigned long physaddr);

/* Set the DMA byte count for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA count immediately, but defer it to the enable_dma().
 */
extern void set_dma_count(dmach_t channel, unsigned long count);

/* Set the transfer direction for this channel
 *
 * This should not be called if a DMA channel is enabled,
 * especially since some DMA architectures don't update the
 * DMA transfer direction immediately, but defer it to the
 * enable_dma().
 */
extern void set_dma_mode(dmach_t channel, dmamode_t mode);

/* Set the transfer speed for this channel
 */
extern void set_dma_speed(dmach_t channel, int cycle_ns);

/* Get DMA residue count. After a DMA transfer, this
 * should return zero. Reading this while a DMA transfer is
 * still in progress will return unpredictable results.
 * If called before the channel has been used, it may return 1.
 * Otherwise, it returns the number of _bytes_ left to transfer.
 */
extern int  get_dma_residue(dmach_t channel);

#ifndef NO_DMA
#define NO_DMA	255
#endif

#endif /* _ARM_DMA_H */
