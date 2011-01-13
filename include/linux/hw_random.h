/*
	Hardware Random Number Generator

	Please read Documentation/hw_random.txt for details on use.

	----------------------------------------------------------
	This software may be used and distributed according to the terms
        of the GNU General Public License, incorporated herein by reference.

 */

#ifndef LINUX_HWRANDOM_H_
#define LINUX_HWRANDOM_H_

#include <linux/types.h>
#include <linux/list.h>

/**
 * struct hwrng - Hardware Random Number Generator driver
 * @name:		Unique RNG name.
 * @init:		Initialization callback (can be NULL).
 * @cleanup:		Cleanup callback (can be NULL).
 * @data_present:	Callback to determine if data is available
 *			on the RNG. If NULL, it is assumed that
 *			there is always data available.  *OBSOLETE*
 * @data_read:		Read data from the RNG device.
 *			Returns the number of lower random bytes in "data".
 *			Must not be NULL.    *OSOLETE*
 * @read:		New API. drivers can fill up to max bytes of data
 *			into the buffer. The buffer is aligned for any type.
 * @priv:		Private data, for use by the RNG driver.
 */
struct hwrng {
	const char *name;
	int (*init)(struct hwrng *rng);
	void (*cleanup)(struct hwrng *rng);
	int (*data_present)(struct hwrng *rng, int wait);
	int (*data_read)(struct hwrng *rng, u32 *data);
	int (*read)(struct hwrng *rng, void *data, size_t max, bool wait);
	unsigned long priv;

	/* internal. */
	struct list_head list;
};

/** Register a new Hardware Random Number Generator driver. */
extern int hwrng_register(struct hwrng *rng);
/** Unregister a Hardware Random Number Generator driver. */
extern void hwrng_unregister(struct hwrng *rng);

#endif /* LINUX_HWRANDOM_H_ */
