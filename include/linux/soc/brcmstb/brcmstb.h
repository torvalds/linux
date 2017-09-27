#ifndef __BRCMSTB_SOC_H
#define __BRCMSTB_SOC_H

/*
 * Bus Interface Unit control register setup, must happen early during boot,
 * before SMP is brought up, called by machine entry point.
 */
void brcmstb_biuctrl_init(void);

#endif /* __BRCMSTB_SOC_H */
