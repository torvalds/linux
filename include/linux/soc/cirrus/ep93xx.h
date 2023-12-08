/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SOC_EP93XX_H
#define _SOC_EP93XX_H

struct platform_device;

#define EP93XX_CHIP_REV_D0	3
#define EP93XX_CHIP_REV_D1	4
#define EP93XX_CHIP_REV_E0	5
#define EP93XX_CHIP_REV_E1	6
#define EP93XX_CHIP_REV_E2	7

#ifdef CONFIG_ARCH_EP93XX
int ep93xx_pwm_acquire_gpio(struct platform_device *pdev);
void ep93xx_pwm_release_gpio(struct platform_device *pdev);
int ep93xx_ide_acquire_gpio(struct platform_device *pdev);
void ep93xx_ide_release_gpio(struct platform_device *pdev);
int ep93xx_keypad_acquire_gpio(struct platform_device *pdev);
void ep93xx_keypad_release_gpio(struct platform_device *pdev);
int ep93xx_i2s_acquire(void);
void ep93xx_i2s_release(void);
unsigned int ep93xx_chip_revision(void);

#else
static inline int ep93xx_pwm_acquire_gpio(struct platform_device *pdev) { return 0; }
static inline void ep93xx_pwm_release_gpio(struct platform_device *pdev) {}
static inline int ep93xx_ide_acquire_gpio(struct platform_device *pdev) { return 0; }
static inline void ep93xx_ide_release_gpio(struct platform_device *pdev) {}
static inline int ep93xx_keypad_acquire_gpio(struct platform_device *pdev) { return 0; }
static inline void ep93xx_keypad_release_gpio(struct platform_device *pdev) {}
static inline int ep93xx_i2s_acquire(void) { return 0; }
static inline void ep93xx_i2s_release(void) {}
static inline unsigned int ep93xx_chip_revision(void) { return 0; }

#endif

#endif
