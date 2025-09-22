/* Public domain. */

#ifndef _LINUX_SOC_APPLE_RTKIT_H
#define _LINUX_SOC_APPLE_RTKIT_H

#include <linux/bitfield.h>

struct apple_rtkit;

struct apple_rtkit_shmem {
	dma_addr_t iova;
	void *buffer;
	size_t size;
	int is_mapped;
};

struct apple_rtkit_ops {
	void (*crashed)(void *);
	void (*recv_message)(void *, uint8_t, uint64_t);
	int (*shmem_setup)(void *, struct apple_rtkit_shmem *);
	void (*shmem_destroy)(void *, struct apple_rtkit_shmem *);
};

struct apple_rtkit *devm_apple_rtkit_init(struct device *, void *,
	    const char *, int, const struct apple_rtkit_ops *);

int	apple_rtkit_send_message(struct apple_rtkit *, uint8_t, uint64_t,
				 struct completion *, int);
int	apple_rtkit_start_ep(struct apple_rtkit *, uint8_t);
int	apple_rtkit_wake(struct apple_rtkit *);

#endif
