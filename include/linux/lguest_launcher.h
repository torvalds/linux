#ifndef _LINUX_LGUEST_LAUNCHER
#define _LINUX_LGUEST_LAUNCHER
/* Everything the "lguest" userspace program needs to know. */
#include <linux/types.h>

/*D:010
 * Drivers
 *
 * The Guest needs devices to do anything useful.  Since we don't let it touch
 * real devices (think of the damage it could do!) we provide virtual devices.
 * We emulate a PCI bus with virtio devices on it; we used to have our own
 * lguest bus which was far simpler, but this tests the virtio 1.0 standard.
 *
 * Virtio devices are also used by kvm, so we can simply reuse their optimized
 * device drivers.  And one day when everyone uses virtio, my plan will be
 * complete.  Bwahahahah!
 */

/* Write command first word is a request. */
enum lguest_req
{
	LHREQ_INITIALIZE, /* + base, pfnlimit, start */
	LHREQ_GETDMA, /* No longer used */
	LHREQ_IRQ, /* + irq */
	LHREQ_BREAK, /* No longer used */
	LHREQ_EVENTFD, /* No longer used. */
	LHREQ_GETREG, /* + offset within struct pt_regs (then read value). */
	LHREQ_SETREG, /* + offset within struct pt_regs, value. */
	LHREQ_TRAP, /* + trap number to deliver to guest. */
};

/*
 * This is what read() of the lguest fd populates.  trap ==
 * LGUEST_TRAP_ENTRY for an LHCALL_NOTIFY (addr is the
 * argument), 14 for a page fault in the MMIO region (addr is
 * the trap address, insn is the instruction), or 13 for a GPF
 * (insn is the instruction).
 */
struct lguest_pending {
	__u8 trap;
	__u8 insn[7];
	__u32 addr;
};
#endif /* _LINUX_LGUEST_LAUNCHER */
