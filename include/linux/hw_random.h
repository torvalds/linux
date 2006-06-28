/*
	Hardware Random Number Generator

	Please read Documentation/hw_random.txt for details on use.

	----------------------------------------------------------
	This software may be used and distributed according to the terms
        of the GNU General Public License, incorporated herein by reference.

 */

#ifndef LINUX_HWRANDOM_H_
#define LINUX_HWRANDOM_H_
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/list.h>

/**
 * struct hwrng - Hardware Random Number Generator driver
 * @name:		Unique RNG name.
 * @init:		Initialization callback (can be NULL).
 * @cleanup:		Cleanup callback (can be NULL).
 * @data_present:	Callback to determine if data is available
 *			on the RNG. If NULL, it is assumed that
 *			there is always data available.
 * @data_read:		Read data from the RNG device.
 *			Returns the number of lower random bytes in "data".
 *			Must not be NULL.
 * @priv:		Private data, for use by the RNG driver.
 */
struct hwrng {
	const char *name;
	int (*init)(struct hwrng *rng);
	void (*cleanup)(struct hwrng *rng);
	int (*data_present)(struct hwrng *rng);
	int (*data_read)(struct hwrng *rng, u32 *data);
	unsigned long priv;

	/* internal. */
	struct list_head list;
};

/** Register a new Hardware Random Number Generator driver. */
extern int hwrng_register(struct hwrng *rng);
/** Unregister a Hardware Random Number Generator driver. */
extern void hwrng_unregister(struct hwrng *rng);

#endif /* __KERNEL__ */
#endif /* LINUX_HWRANDOM_H_ */
