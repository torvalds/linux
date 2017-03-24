/*
 * SLIM core rproc driver header
 *
 * Copyright (C) 2016 STMicroelectronics
 *
 * Author: Peter Griffin <peter.griffin@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _ST_REMOTEPROC_SLIM_H
#define _ST_REMOTEPROC_SLIM_H

#define ST_SLIM_MEM_MAX 2
#define ST_SLIM_MAX_CLK 4

enum {
	ST_SLIM_DMEM,
	ST_SLIM_IMEM,
};

/**
 * struct st_slim_mem - slim internal memory structure
 * @cpu_addr: MPU virtual address of the memory region
 * @bus_addr: Bus address used to access the memory region
 * @size: Size of the memory region
 */
struct st_slim_mem {
	void __iomem *cpu_addr;
	phys_addr_t bus_addr;
	size_t size;
};

/**
 * struct st_slim_rproc - SLIM slim core
 * @rproc: rproc handle
 * @mem: slim memory information
 * @slimcore: slim slimcore regs
 * @peri: slim peripheral regs
 * @clks: slim clocks
 */
struct st_slim_rproc {
	struct rproc *rproc;
	struct st_slim_mem mem[ST_SLIM_MEM_MAX];
	void __iomem *slimcore;
	void __iomem *peri;

	/* st_slim_rproc private */
	struct clk *clks[ST_SLIM_MAX_CLK];
};

struct st_slim_rproc *st_slim_rproc_alloc(struct platform_device *pdev,
					char *fw_name);
void st_slim_rproc_put(struct st_slim_rproc *slim_rproc);

#endif
