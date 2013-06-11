/*
 * This header provides constants for OMAP pinctrl bindings.
 *
 * Copyright (C) 2009 Nokia
 * Copyright (C) 2009-2010 Texas Instruments
 */

#ifndef _DT_BINDINGS_PINCTRL_OMAP_H
#define _DT_BINDINGS_PINCTRL_OMAP_H

/* 34xx mux mode options for each pin. See TRM for options */
#define MUX_MODE0	0
#define MUX_MODE1	1
#define MUX_MODE2	2
#define MUX_MODE3	3
#define MUX_MODE4	4
#define MUX_MODE5	5
#define MUX_MODE6	6
#define MUX_MODE7	7

/* 24xx/34xx mux bit defines */
#define PULL_ENA		(1 << 3)
#define PULL_UP			(1 << 4)
#define ALTELECTRICALSEL	(1 << 5)

/* 34xx specific mux bit defines */
#define INPUT_EN		(1 << 8)
#define OFF_EN			(1 << 9)
#define OFFOUT_EN		(1 << 10)
#define OFFOUT_VAL		(1 << 11)
#define OFF_PULL_EN		(1 << 12)
#define OFF_PULL_UP		(1 << 13)
#define WAKEUP_EN		(1 << 14)

/* 44xx specific mux bit defines */
#define WAKEUP_EVENT		(1 << 15)

/* Active pin states */
#define PIN_OUTPUT		0
#define PIN_OUTPUT_PULLUP	(PIN_OUTPUT | PULL_ENA | PULL_UP)
#define PIN_OUTPUT_PULLDOWN	(PIN_OUTPUT | PULL_ENA)
#define PIN_INPUT		INPUT_EN
#define PIN_INPUT_PULLUP	(PULL_ENA | INPUT_EN | PULL_UP)
#define PIN_INPUT_PULLDOWN	(PULL_ENA | INPUT_EN)

/* Off mode states */
#define PIN_OFF_NONE		0
#define PIN_OFF_OUTPUT_HIGH	(OFF_EN | OFFOUT_EN | OFFOUT_VAL)
#define PIN_OFF_OUTPUT_LOW	(OFF_EN | OFFOUT_EN)
#define PIN_OFF_INPUT_PULLUP	(OFF_EN | OFF_PULL_EN | OFF_PULL_UP)
#define PIN_OFF_INPUT_PULLDOWN	(OFF_EN | OFF_PULL_EN)
#define PIN_OFF_WAKEUPENABLE	WAKEUP_EN

#endif

