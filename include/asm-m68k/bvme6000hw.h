#ifndef _M68K_BVME6000HW_H_
#define _M68K_BVME6000HW_H_

#include <asm/irq.h>

/*
 * PIT structure
 */

#define BVME_PIT_BASE	0xffa00000

typedef struct {
	unsigned char
	pad_a[3], pgcr,
	pad_b[3], psrr,
	pad_c[3], paddr,
	pad_d[3], pbddr,
	pad_e[3], pcddr,
	pad_f[3], pivr,
	pad_g[3], pacr,
	pad_h[3], pbcr,
	pad_i[3], padr,
	pad_j[3], pbdr,
	pad_k[3], paar,
	pad_l[3], pbar,
	pad_m[3], pcdr,
	pad_n[3], psr,
	pad_o[3], res1,
	pad_p[3], res2,
	pad_q[3], tcr,
	pad_r[3], tivr,
	pad_s[3], res3,
	pad_t[3], cprh,
	pad_u[3], cprm,
	pad_v[3], cprl,
	pad_w[3], res4,
	pad_x[3], crh,
	pad_y[3], crm,
	pad_z[3], crl,
	pad_A[3], tsr,
	pad_B[3], res5;
} PitRegs_t, *PitRegsPtr;

#define bvmepit   ((*(volatile PitRegsPtr)(BVME_PIT_BASE)))

#define BVME_RTC_BASE	0xff900000

typedef struct {
	unsigned char
	pad_a[3], msr,
	pad_b[3], t0cr_rtmr,
	pad_c[3], t1cr_omr,
	pad_d[3], pfr_icr0,
	pad_e[3], irr_icr1,
	pad_f[3], bcd_tenms,
	pad_g[3], bcd_sec,
	pad_h[3], bcd_min,
	pad_i[3], bcd_hr,
	pad_j[3], bcd_dom,
	pad_k[3], bcd_mth,
	pad_l[3], bcd_year,
	pad_m[3], bcd_ujcc,
	pad_n[3], bcd_hjcc,
	pad_o[3], bcd_dow,
	pad_p[3], t0lsb,
	pad_q[3], t0msb,
	pad_r[3], t1lsb,
	pad_s[3], t1msb,
	pad_t[3], cmp_sec,
	pad_u[3], cmp_min,
	pad_v[3], cmp_hr,
	pad_w[3], cmp_dom,
	pad_x[3], cmp_mth,
	pad_y[3], cmp_dow,
	pad_z[3], sav_sec,
	pad_A[3], sav_min,
	pad_B[3], sav_hr,
	pad_C[3], sav_dom,
	pad_D[3], sav_mth,
	pad_E[3], ram,
	pad_F[3], test;
} RtcRegs_t, *RtcPtr_t;


#define BVME_I596_BASE	0xff100000

#define BVME_ETHIRQ_REG	0xff20000b

#define BVME_LOCAL_IRQ_STAT  0xff20000f

#define BVME_ETHERR          0x02
#define BVME_ABORT_STATUS    0x08

#define BVME_NCR53C710_BASE	0xff000000

#define BVME_SCC_A_ADDR	0xffb0000b
#define BVME_SCC_B_ADDR	0xffb00003
#define BVME_SCC_RTxC	7372800

#define BVME_CONFIG_REG	0xff500003

#define config_reg_ptr	(volatile unsigned char *)BVME_CONFIG_REG

#define BVME_CONFIG_SW1	0x08
#define BVME_CONFIG_SW2	0x04
#define BVME_CONFIG_SW3	0x02
#define BVME_CONFIG_SW4	0x01


#define BVME_IRQ_TYPE_PRIO	0

#define BVME_IRQ_PRN		0x54
#define BVME_IRQ_I596		0x1a
#define BVME_IRQ_SCSI		0x1b
#define BVME_IRQ_TIMER		0x59
#define BVME_IRQ_RTC		0x1e
#define BVME_IRQ_ABORT		0x1f

/* SCC interrupts */
#define BVME_IRQ_SCC_BASE		0x40
#define BVME_IRQ_SCCB_TX		0x40
#define BVME_IRQ_SCCB_STAT		0x42
#define BVME_IRQ_SCCB_RX		0x44
#define BVME_IRQ_SCCB_SPCOND		0x46
#define BVME_IRQ_SCCA_TX		0x48
#define BVME_IRQ_SCCA_STAT		0x4a
#define BVME_IRQ_SCCA_RX		0x4c
#define BVME_IRQ_SCCA_SPCOND		0x4e

/* Address control registers */

#define BVME_ACR_A32VBA		0xff400003
#define BVME_ACR_A32MSK		0xff410003
#define BVME_ACR_A24VBA		0xff420003
#define BVME_ACR_A24MSK		0xff430003
#define BVME_ACR_A16VBA		0xff440003
#define BVME_ACR_A32LBA		0xff450003
#define BVME_ACR_A24LBA		0xff460003
#define BVME_ACR_ADDRCTL	0xff470003

#define bvme_acr_a32vba		*(volatile unsigned char *)BVME_ACR_A32VBA
#define bvme_acr_a32msk		*(volatile unsigned char *)BVME_ACR_A32MSK
#define bvme_acr_a24vba		*(volatile unsigned char *)BVME_ACR_A24VBA
#define bvme_acr_a24msk		*(volatile unsigned char *)BVME_ACR_A24MSK
#define bvme_acr_a16vba		*(volatile unsigned char *)BVME_ACR_A16VBA
#define bvme_acr_a32lba		*(volatile unsigned char *)BVME_ACR_A32LBA
#define bvme_acr_a24lba		*(volatile unsigned char *)BVME_ACR_A24LBA
#define bvme_acr_addrctl	*(volatile unsigned char *)BVME_ACR_ADDRCTL

#endif
