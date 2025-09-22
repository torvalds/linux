/*	$OpenBSD: iosf.c,v 1.3 2025/06/25 20:26:32 miod Exp $ */

/*
 * Copyright (c) 2023 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <dev/ic/iosfvar.h>

#define IOSF_MBI_MASK_HI		0xffffff00
#define IOSF_MBI_MASK_LO		0x000000ff
#define IOSF_MBI_ENABLE			0x000000f0

#define IOSF_MBI_MCR_OP_SHIFT		24
#define IOSF_MBI_MCR_PORT_SHIFT		16
#define IOSF_MBI_MCR_OFFSET_SHIFT	8

/* IOSF sideband read/write opcodes */
#define IOSF_MBI_OP_MMIO_READ		0x00
#define IOSF_MBI_OP_MMIO_WRITE		0x01
#define IOSF_MBI_OP_CFG_READ		0x04
#define IOSF_MBI_OP_CFG_WRITE		0x05
#define IOSF_MBI_OP_CR_READ		0x06
#define IOSF_MBI_OP_CR_WRITE		0x07
#define IOSF_MBI_OP_REG_READ		0x10
#define IOSF_MBI_OP_REG_WRITE		0x11
#define IOSF_MBI_OP_ESRAM_READ		0x12
#define IOSF_MBI_OP_ESRAM_WRITE		0x13

/* Baytrail */
#define IOSF_BT_MBI_UNIT_AUNIT		0x00
#define IOSF_BT_MBI_UNIT_SMC		0x01
#define IOSF_BT_MBI_UNIT_CPU		0x02
#define IOSF_BT_MBI_UNIT_BUNIT		0x03
#define IOSF_BT_MBI_UNIT_PMC		0x04
#define IOSF_BT_MBI_UNIT_GFX		0x06
#define IOSF_BT_MBI_UNIT_SMI		0x0C
#define IOSF_BT_MBI_UNIT_CCK		0x14
#define IOSF_BT_MBI_UNIT_USB		0x43
#define IOSF_BT_MBI_UNIT_SATA		0xA3
#define IOSF_BT_MBI_UNIT_PCIE		0xA6

/* semaphore bits */
#define IOSF_PUNIT_SEM_BIT		(1 << 0)
#define IOSF_PUNIT_SEM_ACQUIRE		(1 << 1)

struct cfdriver iosf_cd = {
	NULL, "iosf", DV_DULL
};

/*
 * serialise register ops
 */
static struct mutex iosf_mbi_mtx = MUTEX_INITIALIZER(IPL_HIGH);

/*
 * rwlock for kernel to coordinate access to the mbi with
 */
static struct rwlock iosf_lock = RWLOCK_INITIALIZER("iosf");

/*
 * drivers provide an iosf_mbi that acts as a backend for the code below.
 */
static struct iosf_mbi *iosf_mbi;

void
iosf_mbi_attach(struct iosf_mbi *mbi)
{
	/*
	 * assume this is serialised by autoconf being run sequentially
	 * during boot.
	 */

	if (iosf_mbi == NULL || iosf_mbi->mbi_prio < mbi->mbi_prio)
		iosf_mbi = mbi;
}

static inline uint32_t
iosf_mbi_mcr(uint8_t op, uint8_t port, uint32_t offset)
{
	uint32_t rv = IOSF_MBI_ENABLE;
	rv |= op << IOSF_MBI_MCR_OP_SHIFT;
	rv |= port << IOSF_MBI_MCR_PORT_SHIFT;
	rv |= (offset & IOSF_MBI_MASK_LO) << IOSF_MBI_MCR_OFFSET_SHIFT;
	return (rv);
}

static inline uint32_t
iosf_mbi_mcrx(uint32_t offset)
{
	return (offset & IOSF_MBI_MASK_HI);
}

/*
 * serialised mbi mdr operations
 */

static uint32_t
iosf_mbi_mdr_read(struct iosf_mbi *mbi, uint8_t port, uint8_t op,
    uint32_t offset)
{
	uint32_t mcr, mcrx, mdr;

	mcr = iosf_mbi_mcr(op, port, offset);
	mcrx = iosf_mbi_mcrx(offset);

	mtx_enter(&iosf_mbi_mtx);
	mdr = (*mbi->mbi_mdr_rd)(mbi, mcr, mcrx);
	mtx_leave(&iosf_mbi_mtx);

	return (mdr);
}

static void
iosf_mbi_mdr_write(struct iosf_mbi *mbi, uint8_t port, uint8_t op,
    uint32_t offset, uint32_t mdr)
{
	uint32_t mcr, mcrx;

	mcr = iosf_mbi_mcr(op, port, offset);
	mcrx = iosf_mbi_mcrx(offset);

	mtx_enter(&iosf_mbi_mtx);
	(*mbi->mbi_mdr_wr)(mbi, mcr, mcrx, mdr);
	mtx_leave(&iosf_mbi_mtx);
}

static void
iosf_mbi_mdr_modify(struct iosf_mbi *mbi, uint8_t port, uint8_t op,
    uint32_t offset, uint32_t bits, uint32_t mask)
{
	uint32_t mcr, mcrx, mdr;

	mcr = iosf_mbi_mcr(op, port, offset);
	mcrx = iosf_mbi_mcrx(offset);

	mtx_enter(&iosf_mbi_mtx);
	mdr = (*mbi->mbi_mdr_rd)(mbi, mcr, mcrx);

	CLR(mdr, mask);
	SET(mdr, bits & mask); 

	(*mbi->mbi_mdr_wr)(mbi, mcr, mcrx, mdr);
	mtx_leave(&iosf_mbi_mtx);
}

#ifdef nyetyet
/*
 * linux compat api
 */

int
iosf_mbi_read(uint8_t port, uint8_t opcode, uint32_t offset, uint32_t *mdrp)
{
	struct iosf_mbi *mbi;

	mbi = iosf_mbi;
	if (mbi == NULL)
		return (ENODEV);

	/* check port != BT_MBI_UNIT_GFX? */

	*mdrp = iosf_mbi_mdr_read(mbi, port, opcode, offset);

	return (0);
}

int
iosf_mbi_write(uint8_t port, uint8_t opcode, uint32_t offset, uint32_t mdr)
{
	struct iosf_mbi *mbi;

	mbi = iosf_mbi;
	if (mbi == NULL)
		return (ENODEV);

	/* check port != BT_MBI_UNIT_GFX? */

	iosf_mbi_mdr_write(mbi, port, opcode, offset, mdr);

	return (0);
}

int
iosf_mbi_modify(uint8_t port, uint8_t opcode, uint32_t offset,
    uint32_t bits, uint32_t mask)
{
	struct iosf_mbi *mbi;

	mbi = iosf_mbi;
	if (mbi == NULL)
		return (ENODEV);

	/* check port != BT_MBI_UNIT_GFX? */

	iosf_mbi_mdr_modify(mbi, port, opcode, offset, bits, mask);

	return (0);
}
#endif

int
iosf_mbi_available(void)
{
	return (iosf_mbi != NULL);
}

static uint32_t
iosf_mbi_sem_get(struct iosf_mbi *mbi)
{
	uint32_t sem;

	sem = iosf_mbi_mdr_read(mbi,
	    IOSF_BT_MBI_UNIT_PMC, IOSF_MBI_OP_REG_READ, mbi->mbi_semaddr);

	return (ISSET(sem, IOSF_PUNIT_SEM_BIT));
}

static void
iosf_mbi_sem_reset(struct iosf_mbi *mbi)
{
	iosf_mbi_mdr_modify(mbi,
	    IOSF_BT_MBI_UNIT_PMC, IOSF_MBI_OP_REG_READ, mbi->mbi_semaddr,
	    0, IOSF_PUNIT_SEM_BIT);
}

void
iosf_mbi_punit_acquire(void)
{
	rw_enter_write(&iosf_lock);
}

void
iosf_mbi_punit_release(void)
{
	rw_exit_write(&iosf_lock);
}

void
iosf_mbi_assert_punit_acquired(void)
{
	int s;

	if (splassert_ctl == 0)
		return;

	s = rw_status(&iosf_lock);
	if (s != RW_WRITE)
		splassert_fail(RW_WRITE, s, __func__);
}

static void
iosf_sem_wait(uint64_t usec, int waitok)
{
	if (waitok)
		tsleep_nsec(&nowake, PRIBIO, "iosfsem", USEC_TO_NSEC(usec));
	else
		delay(usec);
}

#include <dev/i2c/i2cvar.h>

int
iosf_i2c_acquire(int flags)
{
	struct iosf_mbi *mbi;
	int waitok = !cold && !ISSET(flags, I2C_F_POLL);
	unsigned int i;

	mbi = iosf_mbi;
	if (mbi == NULL)
		return (0);

	if (waitok)
		rw_enter_write(&iosf_lock);
	else if (iosf_lock.rwl_owner != 0)
		panic("%s", __func__);

	/* XXX disable C6 and C7 states */

	iosf_mbi_mdr_write(mbi, IOSF_BT_MBI_UNIT_PMC, IOSF_MBI_OP_REG_WRITE,
	    mbi->mbi_semaddr, IOSF_PUNIT_SEM_ACQUIRE);

	for (i = 0; i < 50; i++) {
		if (iosf_mbi_sem_get(mbi)) {
			/* success! */
			return (0);
		}

		iosf_sem_wait(10000, waitok);
	}

	iosf_mbi_sem_reset(mbi);

	if (waitok)
		rw_exit_write(&iosf_lock);
	else if (iosf_lock.rwl_owner != 0)
		panic("%s", __func__);

	return (EWOULDBLOCK);
}

void
iosf_i2c_release(int flags)
{
	struct iosf_mbi *mbi;
	int waitok = !cold && !ISSET(flags, I2C_F_POLL);

	mbi = iosf_mbi;
	if (mbi == NULL)
		return;

	iosf_mbi_sem_reset(mbi);

	if (waitok)
		rw_exit_write(&iosf_lock);
	else if (iosf_lock.rwl_owner != 0)
		panic("%s", __func__);
}
