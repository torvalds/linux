/* linux/include/asm-arm/plat-s3c24xx/cpu.h
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for S3C24XX CPU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* todo - fix when rmk changes iodescs to use `void __iomem *` */

#define IODESC_ENT(x) { (unsigned long)S3C24XX_VA_##x, __phys_to_pfn(S3C24XX_PA_##x), S3C24XX_SZ_##x, MT_DEVICE }

#ifndef MHZ
#define MHZ (1000*1000)
#endif

#define print_mhz(m) ((m) / MHZ), ((m / 1000) % 1000)

/* forward declaration */
struct s3c24xx_uart_resources;
struct platform_device;
struct s3c2410_uartcfg;
struct map_desc;

/* core initialisation functions */

extern void s3c24xx_init_irq(void);

extern void s3c24xx_init_io(struct map_desc *mach_desc, int size);

extern void s3c24xx_init_uarts(struct s3c2410_uartcfg *cfg, int no);

extern void s3c24xx_init_clocks(int xtal);

extern void s3c24xx_init_uartdevs(char *name,
				  struct s3c24xx_uart_resources *res,
				  struct s3c2410_uartcfg *cfg, int no);

/* the board structure is used at first initialsation time
 * to get info such as the devices to register for this
 * board. This is done because platfrom_add_devices() cannot
 * be called from the map_io entry.
*/

struct s3c24xx_board {
	struct platform_device  **devices;
	unsigned int              devices_count;

	struct clk		**clocks;
	unsigned int		  clocks_count;
};

extern void s3c24xx_set_board(struct s3c24xx_board *board);

/* timer for 2410/2440 */

struct sys_timer;
extern struct sys_timer s3c24xx_timer;

/* system device classes */

extern struct sysdev_class s3c2410_sysclass;
extern struct sysdev_class s3c2412_sysclass;
extern struct sysdev_class s3c2440_sysclass;
extern struct sysdev_class s3c2442_sysclass;
extern struct sysdev_class s3c2443_sysclass;
