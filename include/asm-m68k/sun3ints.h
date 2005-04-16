/*
 * sun3ints.h -- Linux/Sun3 interrupt handling code definitions
 *
 * Erik Verbruggen (erik@bigmama.xtdnet.nl)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef SUN3INTS_H
#define SUN3INTS_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <asm/segment.h>
#include <asm/intersil.h>
#include <asm/oplib.h>

#define SUN3_INT_VECS 192

void sun3_enable_irq(unsigned int irq);
void sun3_disable_irq(unsigned int irq);
int sun3_request_irq(unsigned int irq,
                     irqreturn_t (*handler)(int, void *, struct pt_regs *),
                     unsigned long flags, const char *devname, void *dev_id
		    );
extern void sun3_init_IRQ (void);
extern irqreturn_t (*sun3_default_handler[]) (int, void *, struct pt_regs *);
extern irqreturn_t (*sun3_inthandler[]) (int, void *, struct pt_regs *);
extern void sun3_free_irq (unsigned int irq, void *dev_id);
extern void sun3_enable_interrupts (void);
extern void sun3_disable_interrupts (void);
extern int show_sun3_interrupts(struct seq_file *, void *);
extern irqreturn_t sun3_process_int(int, struct pt_regs *);
extern volatile unsigned char* sun3_intreg;

/* master list of VME vectors -- don't fuck with this */
#define SUN3_VEC_FLOPPY 0x40
#define SUN3_VEC_VMESCSI0 0x40
#define SUN3_VEC_VMESCSI1 0x41
#define SUN3_VEC_CG 0xA8


#endif /* SUN3INTS_H */
