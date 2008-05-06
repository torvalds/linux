/* -*- mode: c; c-basic-offset: 8 -*- */

/* This is the function prototypes for the old legacy MCA interface
 *
 * Please move your driver to the new sysfs based one instead */

#ifndef _LINUX_MCA_LEGACY_H
#define _LINUX_MCA_LEGACY_H

#include <linux/mca.h>

#warning "MCA legacy - please move your driver to the new sysfs api"

/* MCA_NOTFOUND is an error condition.  The other two indicate
 * motherboard POS registers contain the adapter.  They might be
 * returned by the mca_find_adapter() function, and can be used as
 * arguments to mca_read_stored_pos().  I'm not going to allow direct
 * access to the motherboard registers until we run across an adapter
 * that requires it.  We don't know enough about them to know if it's
 * safe.
 *
 * See Documentation/mca.txt or one of the existing drivers for
 * more information.
 */
#define MCA_NOTFOUND	(-1)



/* Returns the slot of the first enabled adapter matching id.  User can
 * specify a starting slot beyond zero, to deal with detecting multiple
 * devices.  Returns MCA_NOTFOUND if id not found.  Also checks the
 * integrated adapters.
 */
extern int mca_find_adapter(int id, int start);
extern int mca_find_unused_adapter(int id, int start);

extern int mca_mark_as_used(int slot);
extern void mca_mark_as_unused(int slot);

/* gets a byte out of POS register (stored in memory) */
extern unsigned char mca_read_stored_pos(int slot, int reg);

/* This can be expanded later.  Right now, it gives us a way of
 * getting meaningful information into the MCA_info structure,
 * so we can have a more interesting /proc/mca.
 */
extern void mca_set_adapter_name(int slot, char* name);

/* These routines actually mess with the hardware POS registers.  They
 * temporarily disable the device (and interrupts), so make sure you know
 * what you're doing if you use them.  Furthermore, writing to a POS may
 * result in two devices trying to share a resource, which in turn can
 * result in multiple devices sharing memory spaces, IRQs, or even trashing
 * hardware.  YOU HAVE BEEN WARNED.
 *
 * You can only access slots with this.  Motherboard registers are off
 * limits.
 */

/* read a byte from the specified POS register. */
extern unsigned char mca_read_pos(int slot, int reg);

/* write a byte to the specified POS register. */
extern void mca_write_pos(int slot, int reg, unsigned char byte);

#endif
