/* include/linux/adc.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#ifndef __ASM_ADC_CORE_H
#define __ASM_ADC_CORE_H

#define MAX_ADC_CHN 4
#define MAX_ADC_FIFO_DEPTH 8


struct adc_client {
	int chn;
	int time;
	int result;
	void (*callback)(struct adc_client *, void *, int);
	void *callback_param;

	struct adc_host *adc;
};

struct adc_request {
	int result;
	int chn;
	void (*callback)(struct adc_client *, void *, int);
	void *callback_param;
	struct adc_client *client;
	/* Used in case of sync requests */
	struct completion completion;
#define ASYNC_READ 0
#define SYNC_READ 1
	int status;
};
struct adc_host;
struct adc_ops {
	void (*start)(struct adc_host *);
	void (*stop)(struct adc_host *);
	int (*read)(struct adc_host *);
};
	
	
struct adc_host {
	struct device *dev;
	int is_suspended;
	struct adc_request *queue[MAX_ADC_FIFO_DEPTH];
	int queue_head;
	int queue_tail;
	spinlock_t			lock;
	struct adc_client *cur;
	const struct adc_ops *ops;
	unsigned long		private[0];
};
static inline void *adc_priv(struct adc_host *adc)
{
	return (void *)adc->private;
}
	
extern struct adc_host *adc_alloc_host(int extra, struct device *dev);
extern void adc_free_host(struct adc_host *adc);
extern void adc_core_irq_handle(struct adc_host *adc);


#ifdef CONFIG_ADC
extern struct adc_client *adc_register(int chn,
				void (*callback)(struct adc_client *, void *, int), 
				void *callback_param);
extern void adc_unregister(struct adc_client *client);

extern int adc_sync_read(struct adc_client *client);
extern int adc_async_read(struct adc_client *client);
#else
static inline struct adc_client *adc_register(int chn,
				void (*callback)(struct adc_client *, void *, int),
				void *callback_param)
{
	return NULL;
}
static inline void adc_unregister(struct adc_client *client) {}
static inline int adc_sync_read(struct adc_client *client) { return -EINVAL; }
static inline int adc_async_read(struct adc_client *client) { return -EINVAL; }
#endif

#endif

