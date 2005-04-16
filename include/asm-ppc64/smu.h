/*
 * Definitions for talking to the SMU chip in newer G5 PowerMacs
 */

#include <linux/config.h>

/*
 * Basic routines for use by architecture. To be extended as
 * we understand more of the chip
 */
extern int smu_init(void);
extern int smu_present(void);
extern void smu_shutdown(void);
extern void smu_restart(void);
extern int smu_get_rtc_time(struct rtc_time *time);
extern int smu_set_rtc_time(struct rtc_time *time);

/*
 * SMU command buffer absolute address, exported by pmac_setup,
 * this is allocated very early during boot.
 */
extern unsigned long smu_cmdbuf_abs;
