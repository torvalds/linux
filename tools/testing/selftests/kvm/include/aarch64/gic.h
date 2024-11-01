/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM Generic Interrupt Controller (GIC) specific defines
 */

#ifndef SELFTEST_KVM_GIC_H
#define SELFTEST_KVM_GIC_H

enum gic_type {
	GIC_V3,
	GIC_TYPE_MAX,
};

#define MIN_SGI			0
#define MIN_PPI			16
#define MIN_SPI			32
#define MAX_SPI			1019
#define IAR_SPURIOUS		1023

#define INTID_IS_SGI(intid)	(0       <= (intid) && (intid) < MIN_PPI)
#define INTID_IS_PPI(intid)	(MIN_PPI <= (intid) && (intid) < MIN_SPI)
#define INTID_IS_SPI(intid)	(MIN_SPI <= (intid) && (intid) <= MAX_SPI)

void gic_init(enum gic_type type, unsigned int nr_cpus,
		void *dist_base, void *redist_base);
void gic_irq_enable(unsigned int intid);
void gic_irq_disable(unsigned int intid);
unsigned int gic_get_and_ack_irq(void);
void gic_set_eoi(unsigned int intid);
void gic_set_dir(unsigned int intid);

/*
 * Sets the EOI mode. When split is false, EOI just drops the priority. When
 * split is true, EOI drops the priority and deactivates the interrupt.
 */
void gic_set_eoi_split(bool split);
void gic_set_priority_mask(uint64_t mask);
void gic_set_priority(uint32_t intid, uint32_t prio);
void gic_irq_set_active(unsigned int intid);
void gic_irq_clear_active(unsigned int intid);
bool gic_irq_get_active(unsigned int intid);
void gic_irq_set_pending(unsigned int intid);
void gic_irq_clear_pending(unsigned int intid);
bool gic_irq_get_pending(unsigned int intid);
void gic_irq_set_config(unsigned int intid, bool is_edge);

#endif /* SELFTEST_KVM_GIC_H */
