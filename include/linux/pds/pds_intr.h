/* SPDX-License-Identifier: (GPL-2.0 OR Linux-OpenIB) OR BSD-2-Clause */
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#ifndef _PDS_INTR_H_
#define _PDS_INTR_H_

/*
 * Interrupt control register
 * @coal_init:        Coalescing timer initial value, in
 *                    device units.  Use @identity->intr_coal_mult
 *                    and @identity->intr_coal_div to convert from
 *                    usecs to device units:
 *
 *                      coal_init = coal_usecs * coal_mutl / coal_div
 *
 *                    When an interrupt is sent the interrupt
 *                    coalescing timer current value
 *                    (@coalescing_curr) is initialized with this
 *                    value and begins counting down.  No more
 *                    interrupts are sent until the coalescing
 *                    timer reaches 0.  When @coalescing_init=0
 *                    interrupt coalescing is effectively disabled
 *                    and every interrupt assert results in an
 *                    interrupt.  Reset value: 0
 * @mask:             Interrupt mask.  When @mask=1 the interrupt
 *                    resource will not send an interrupt.  When
 *                    @mask=0 the interrupt resource will send an
 *                    interrupt if an interrupt event is pending
 *                    or on the next interrupt assertion event.
 *                    Reset value: 1
 * @credits:          Interrupt credits.  This register indicates
 *                    how many interrupt events the hardware has
 *                    sent.  When written by software this
 *                    register atomically decrements @int_credits
 *                    by the value written.  When @int_credits
 *                    becomes 0 then the "pending interrupt" bit
 *                    in the Interrupt Status register is cleared
 *                    by the hardware and any pending but unsent
 *                    interrupts are cleared.
 *                    !!!IMPORTANT!!! This is a signed register.
 * @flags:            Interrupt control flags
 *                       @unmask -- When this bit is written with a 1
 *                       the interrupt resource will set mask=0.
 *                       @coal_timer_reset -- When this
 *                       bit is written with a 1 the
 *                       @coalescing_curr will be reloaded with
 *                       @coalescing_init to reset the coalescing
 *                       timer.
 * @mask_on_assert:   Automatically mask on assertion.  When
 *                    @mask_on_assert=1 the interrupt resource
 *                    will set @mask=1 whenever an interrupt is
 *                    sent.  When using interrupts in Legacy
 *                    Interrupt mode the driver must select
 *                    @mask_on_assert=0 for proper interrupt
 *                    operation.
 * @coalescing_curr:  Coalescing timer current value, in
 *                    microseconds.  When this value reaches 0
 *                    the interrupt resource is again eligible to
 *                    send an interrupt.  If an interrupt event
 *                    is already pending when @coalescing_curr
 *                    reaches 0 the pending interrupt will be
 *                    sent, otherwise an interrupt will be sent
 *                    on the next interrupt assertion event.
 */
struct pds_core_intr {
	u32 coal_init;
	u32 mask;
	u16 credits;
	u16 flags;
#define PDS_CORE_INTR_F_UNMASK		0x0001
#define PDS_CORE_INTR_F_TIMER_RESET	0x0002
	u32 mask_on_assert;
	u32 coalescing_curr;
	u32 rsvd6[3];
};

#ifndef __CHECKER__
static_assert(sizeof(struct pds_core_intr) == 32);
#endif /* __CHECKER__ */

#define PDS_CORE_INTR_CTRL_REGS_MAX		2048
#define PDS_CORE_INTR_CTRL_COAL_MAX		0x3F
#define PDS_CORE_INTR_INDEX_NOT_ASSIGNED	-1

struct pds_core_intr_status {
	u32 status[2];
};

/**
 * enum pds_core_intr_mask_vals - valid values for mask and mask_assert.
 * @PDS_CORE_INTR_MASK_CLEAR:	unmask interrupt.
 * @PDS_CORE_INTR_MASK_SET:	mask interrupt.
 */
enum pds_core_intr_mask_vals {
	PDS_CORE_INTR_MASK_CLEAR	= 0,
	PDS_CORE_INTR_MASK_SET		= 1,
};

/**
 * enum pds_core_intr_credits_bits - Bitwise composition of credits values.
 * @PDS_CORE_INTR_CRED_COUNT:	bit mask of credit count, no shift needed.
 * @PDS_CORE_INTR_CRED_COUNT_SIGNED: bit mask of credit count, including sign bit.
 * @PDS_CORE_INTR_CRED_UNMASK:	unmask the interrupt.
 * @PDS_CORE_INTR_CRED_RESET_COALESCE: reset the coalesce timer.
 * @PDS_CORE_INTR_CRED_REARM:	unmask the and reset the timer.
 */
enum pds_core_intr_credits_bits {
	PDS_CORE_INTR_CRED_COUNT		= 0x7fffu,
	PDS_CORE_INTR_CRED_COUNT_SIGNED		= 0xffffu,
	PDS_CORE_INTR_CRED_UNMASK		= 0x10000u,
	PDS_CORE_INTR_CRED_RESET_COALESCE	= 0x20000u,
	PDS_CORE_INTR_CRED_REARM		= (PDS_CORE_INTR_CRED_UNMASK |
					   PDS_CORE_INTR_CRED_RESET_COALESCE),
};

static inline void
pds_core_intr_coal_init(struct pds_core_intr __iomem *intr_ctrl, u32 coal)
{
	iowrite32(coal, &intr_ctrl->coal_init);
}

static inline void
pds_core_intr_mask(struct pds_core_intr __iomem *intr_ctrl, u32 mask)
{
	iowrite32(mask, &intr_ctrl->mask);
}

static inline void
pds_core_intr_credits(struct pds_core_intr __iomem *intr_ctrl,
		      u32 cred, u32 flags)
{
	if (WARN_ON_ONCE(cred > PDS_CORE_INTR_CRED_COUNT)) {
		cred = ioread32(&intr_ctrl->credits);
		cred &= PDS_CORE_INTR_CRED_COUNT_SIGNED;
	}

	iowrite32(cred | flags, &intr_ctrl->credits);
}

static inline void
pds_core_intr_clean_flags(struct pds_core_intr __iomem *intr_ctrl, u32 flags)
{
	u32 cred;

	cred = ioread32(&intr_ctrl->credits);
	cred &= PDS_CORE_INTR_CRED_COUNT_SIGNED;
	cred |= flags;
	iowrite32(cred, &intr_ctrl->credits);
}

static inline void
pds_core_intr_clean(struct pds_core_intr __iomem *intr_ctrl)
{
	pds_core_intr_clean_flags(intr_ctrl, PDS_CORE_INTR_CRED_RESET_COALESCE);
}

static inline void
pds_core_intr_mask_assert(struct pds_core_intr __iomem *intr_ctrl, u32 mask)
{
	iowrite32(mask, &intr_ctrl->mask_on_assert);
}

#endif /* _PDS_INTR_H_ */
