
#ifndef __AML_RTC_H__
#define __AML_RTC_H__

#define AML_PMU_TAG                 0x00414d4c              /* 'AML' tag */

unsigned int aml_read_rtc_mem_reg(unsigned char reg_id);
unsigned int aml_get_rtc_counter(void);
int aml_write_rtc_mem_reg(unsigned char reg_id, unsigned int data);

#endif

