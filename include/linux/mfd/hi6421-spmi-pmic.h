/*
 * Header file for device driver Hi6421 PMIC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (C) 2011 Hisilicon.
 *
 * Guodong Xu <guodong.xu@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef	__HISI_PMIC_H
#define	__HISI_PMIC_H

#include <linux/irqdomain.h>

#define HISI_REGS_ENA_PROTECT_TIME	(0) 	/* in microseconds */
#define HISI_ECO_MODE_ENABLE		(1)
#define HISI_ECO_MODE_DISABLE		(0)

typedef int (*pmic_ocp_callback)(char *);
extern int hisi_pmic_special_ocp_register(char *power_name, pmic_ocp_callback handler);

struct irq_mask_info {
	int start_addr;
	int array;
};

struct irq_info {
	int start_addr;
	int array;
};

struct bit_info {
	int addr;
	int bit;
};

struct write_lock {
	int addr;
	int val;
};

struct hisi_pmic {
	struct resource		*res;
	struct device		*dev;
	void __iomem		*regs;
	spinlock_t		lock;
	struct irq_domain	*domain;
	int			irq;
	int			gpio;
	unsigned int	*irqs;
	int			irqnum;
	int			irqarray;
	struct irq_mask_info irq_mask_addr;
	struct irq_info irq_addr;
	int			irqnum1;
	int			irqarray1;
	struct irq_mask_info irq_mask_addr1;
	struct irq_info irq_addr1;
	struct write_lock normal_lock;
	struct write_lock debug_lock;
};

/* 0:disable; 1:enable */
unsigned int get_uv_mntn_status(void);
void clear_uv_mntn_resered_reg_bit(void);
void set_uv_mntn_resered_reg_bit(void);

/* Register Access Helpers */
u32 hisi_pmic_read(struct hisi_pmic *pmic, int reg);
void hisi_pmic_write(struct hisi_pmic *pmic, int reg, u32 val);
void hisi_pmic_rmw(struct hisi_pmic *pmic, int reg, u32 mask, u32 bits);
unsigned int hisi_pmic_reg_read(int addr);
void hisi_pmic_reg_write(int addr, int val);
void hisi_pmic_reg_write_lock(int addr, int val);
int hisi_pmic_array_read(int addr, char *buff, unsigned int len);
int hisi_pmic_array_write(int addr, char *buff, unsigned int len);
extern int hisi_get_pmic_irq_byname(unsigned int pmic_irq_list);
extern int hisi_pmic_get_vbus_status(void);
static inline u32 hisi_pmic_read(struct hisi_pmic *pmic, int reg) { return 0; }
static inline void hisi_pmic_write(struct hisi_pmic *pmic, int reg, u32 val) {}
static inline void hisi_pmic_rmw(struct hisi_pmic *pmic, int reg, u32 mask, u32 bits) {}
static inline unsigned int hisi_pmic_reg_read(int addr) { return 0; }
static inline void hisi_pmic_reg_write(int addr, int val) {}
static inline void hisi_pmic_reg_write_lock(int addr, int val) {}
static inline int hisi_pmic_array_read(int addr, char *buff, unsigned int len) { return 0; }
static inline int hisi_pmic_array_write(int addr, char *buff, unsigned int len) { return 0; }
static inline int hisi_get_pmic_irq_byname(unsigned int pmic_irq_list) { return -1; }
static inline int hisi_pmic_get_vbus_status(void) { return 1; }
static inline u32 hisi_pmic_read_sub_pmu(u8 sid ,int reg) { return 0; }
static inline void hisi_pmic_write_sub_pmu(u8 sid ,int reg, u32 val) {}

enum pmic_irq_list {
	OTMP = 0,
	VBUS_CONNECT,
	VBUS_DISCONNECT,
	ALARMON_R,
	HOLD_6S,
	HOLD_1S,
	POWERKEY_UP,
	POWERKEY_DOWN,
	OCP_SCP_R,
	COUL_R,
	SIM0_HPD_R,
	SIM0_HPD_F,
	SIM1_HPD_R,
	SIM1_HPD_F,
	PMIC_IRQ_LIST_MAX,
};
#endif		/* __HISI_PMIC_H */

