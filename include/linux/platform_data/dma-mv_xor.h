/*
 * arch/arm/plat-orion/include/plat/mv_xor.h
 *
 * Marvell XOR platform device data definition file.
 */

#ifndef __PLAT_MV_XOR_H
#define __PLAT_MV_XOR_H

#include <linux/dmaengine.h>
#include <linux/mbus.h>

#define MV_XOR_NAME	"mv_xor"

struct mv_xor_channel_data {
	int				hw_id;
	dma_cap_mask_t			cap_mask;
	size_t				pool_size;
};

struct mv_xor_platform_data {
	struct mv_xor_channel_data    *channels;
};

#endif
