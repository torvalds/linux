/*
 *  OMAP GPMC (General Purpose Memory Controller) defines
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/platform_data/gpmc-omap.h>

#define GPMC_CONFIG_WP		0x00000005

/* IRQ numbers in GPMC IRQ domain for legacy boot use */
#define GPMC_IRQ_FIFOEVENTENABLE	0
#define GPMC_IRQ_COUNT_EVENT		1

/**
 * gpmc_nand_ops - Interface between NAND and GPMC
 * @nand_write_buffer_empty: get the NAND write buffer empty status.
 */
struct gpmc_nand_ops {
	bool (*nand_writebuffer_empty)(void);
};

struct gpmc_nand_regs;

#if IS_ENABLED(CONFIG_OMAP_GPMC)
struct gpmc_nand_ops *gpmc_omap_get_nand_ops(struct gpmc_nand_regs *regs,
					     int cs);
#else
static inline gpmc_nand_ops *gpmc_omap_get_nand_ops(struct gpmc_nand_regs *regs,
						    int cs)
{
	return NULL;
}
#endif /* CONFIG_OMAP_GPMC */

/*--------------------------------*/

/* deprecated APIs */
#if IS_ENABLED(CONFIG_OMAP_GPMC)
void gpmc_update_nand_reg(struct gpmc_nand_regs *reg, int cs);
#else
static inline void gpmc_update_nand_reg(struct gpmc_nand_regs *reg, int cs)
{
}
#endif /* CONFIG_OMAP_GPMC */
/*--------------------------------*/

extern int gpmc_calc_timings(struct gpmc_timings *gpmc_t,
			     struct gpmc_settings *gpmc_s,
			     struct gpmc_device_timings *dev_t);

struct device_node;

extern int gpmc_get_client_irq(unsigned irq_config);

extern unsigned int gpmc_ticks_to_ns(unsigned int ticks);

extern void gpmc_cs_write_reg(int cs, int idx, u32 val);
extern int gpmc_calc_divider(unsigned int sync_clk);
extern int gpmc_cs_set_timings(int cs, const struct gpmc_timings *t,
			       const struct gpmc_settings *s);
extern int gpmc_cs_program_settings(int cs, struct gpmc_settings *p);
extern int gpmc_cs_request(int cs, unsigned long size, unsigned long *base);
extern void gpmc_cs_free(int cs);
extern int gpmc_configure(int cmd, int wval);
extern void gpmc_read_settings_dt(struct device_node *np,
				  struct gpmc_settings *p);

extern void omap3_gpmc_save_context(void);
extern void omap3_gpmc_restore_context(void);

struct gpmc_timings;
struct omap_nand_platform_data;
struct omap_onenand_platform_data;

#if IS_ENABLED(CONFIG_MTD_NAND_OMAP2)
extern int gpmc_nand_init(struct omap_nand_platform_data *d,
			  struct gpmc_timings *gpmc_t);
#else
static inline int gpmc_nand_init(struct omap_nand_platform_data *d,
				 struct gpmc_timings *gpmc_t)
{
	return 0;
}
#endif

#if IS_ENABLED(CONFIG_MTD_ONENAND_OMAP2)
extern void gpmc_onenand_init(struct omap_onenand_platform_data *d);
#else
#define board_onenand_data	NULL
static inline void gpmc_onenand_init(struct omap_onenand_platform_data *d)
{
}
#endif
