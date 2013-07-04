/*
 *  rt5025_gauge.h
 *  fuel-gauge driver
 *  revision 0.1
 */

#ifndef __LINUX_RT5025_GAUGE_H_
#define __LINUX_RT5025_GAUGE_H_

#define GPIO_GAUGE_ALERT 4

struct rt5025_gauge_callbacks {
	void (*rt5025_gauge_irq_handler)(void);
	void (*rt5025_gauge_set_status)(int status);
	void (*rt5025_gauge_suspend)(void);
	void (*rt5025_gauge_resume)(void);
	void (*rt5025_gauge_remove)(void);
};


typedef enum{
	CHG,
	DCHG
}operation_mode;

typedef enum{
	MAXTEMP,
	MINTEMP,
	MAXVOLT,
	MINVOLT1,
	MINVOLT2,
	TEMP_RLS,
	VOLT_RLS,
	LAST_TYPE
}alert_type;	

#endif  /* #ifndef __LINUX_RT5025_GAUGE_H_ */
