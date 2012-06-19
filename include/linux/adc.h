/* include/linux/adc.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#ifndef __ASM_ADC_CORE_H
#define __ASM_ADC_CORE_H

#include <linux/list.h>
#include <linux/wait.h>

enum host_chn_shift{
        SARADC_CHN_SHIFT = 0,
        CUSTOM_CHN_SHIFT = 28
};

enum host_chn_mask{
        SARADC_CHN_MASK = 0x0000000f,  // saradc: 0 -- 15
        CUSTOM_CHN_MASK = 0xf0000000,
};

struct adc_host;
struct adc_client {
        unsigned int index;
        unsigned int chn;
        unsigned int is_finished;
        unsigned int flags;
        int result;
	struct adc_host *adc;
        struct list_head list;
        wait_queue_head_t	wait;
	void (*callback)(struct adc_client *, void *, int);
	void *callback_param;
};

#ifdef CONFIG_ADC
struct adc_client *adc_register(int chn,
				void (*callback)(struct adc_client *, void *, int), 
				void *callback_param);
void adc_unregister(struct adc_client *client);

int adc_sync_read(struct adc_client *client);
int adc_async_read(struct adc_client *client);
#else
static inline struct adc_client *adc_register(int chn,
				void (*callback)(struct adc_client *, void *, int),
				void *callback_param)
{
	return NULL;
}
static inline void adc_unregister(struct adc_client *client) {}
static inline int adc_sync_read(struct adc_client *client) { return -1; }
static inline int adc_async_read(struct adc_client *client) { return -1; }
#endif

#endif

