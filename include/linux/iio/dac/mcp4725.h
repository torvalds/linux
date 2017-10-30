/*
 * MCP4725 DAC driver
 *
 * Copyright (C) 2012 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef IIO_DAC_MCP4725_H_
#define IIO_DAC_MCP4725_H_

/**
 * struct mcp4725_platform_data - MCP4725/6 DAC specific data.
 * @use_vref: Whether an external reference voltage on Vref pin should be used.
 *            Additional vref-supply must be specified when used.
 * @vref_buffered: Controls buffering of the external reference voltage.
 *
 * Vref related settings are available only on MCP4756. See
 * Documentation/devicetree/bindings/iio/dac/mcp4725.txt for more information.
 */
struct mcp4725_platform_data {
	bool use_vref;
	bool vref_buffered;
};

#endif /* IIO_DAC_MCP4725_H_ */
