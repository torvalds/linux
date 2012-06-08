/*
 * MCP4725 DAC driver
 *
 * Copyright (C) 2012 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef IIO_DAC_MCP4725_H_
#define IIO_DAC_MCP4725_H_

struct mcp4725_platform_data {
	u16 vref_mv;
};

#endif /* IIO_DAC_MCP4725_H_ */
