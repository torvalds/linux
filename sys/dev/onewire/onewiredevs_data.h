/*	$OpenBSD: onewiredevs_data.h,v 1.6 2008/02/23 23:21:19 miod Exp $	*/

/*
 * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.
 *
 * Generated from:
 *	OpenBSD: onewiredevs,v 1.4 2008/02/23 23:20:52 miod Exp 
 */

struct onewire_family {
	int		of_type;
	const char	*of_name;
};

static const struct onewire_family onewire_famtab[] = {
	{ ONEWIRE_FAMILY_DS1990, "ID" },
	{ ONEWIRE_FAMILY_DS1991, "MultiKey" },
	{ ONEWIRE_FAMILY_DS1994, "4kb NVRAM + RTC" },
	{ ONEWIRE_FAMILY_DS2405, "Addressable Switch" },
	{ ONEWIRE_FAMILY_DS1993, "4kb NVRAM" },
	{ ONEWIRE_FAMILY_DS1992, "1kb NVRAM" },
	{ ONEWIRE_FAMILY_DS1982, "1kb EPROM" },
	{ ONEWIRE_FAMILY_DS1995, "16kb NVRAM" },
	{ ONEWIRE_FAMILY_DS2505, "16kb EPROM" },
	{ ONEWIRE_FAMILY_DS1996, "64kb NVRAM" },
	{ ONEWIRE_FAMILY_DS2506, "64kb EPROM" },
	{ ONEWIRE_FAMILY_DS1920, "Temperature" },
	{ ONEWIRE_FAMILY_DS2406, "Addressable Switch + 1kb NVRAM" },
	{ ONEWIRE_FAMILY_DS2430, "256b EEPROM" },
	{ ONEWIRE_FAMILY_DS195X, "Java" },
	{ ONEWIRE_FAMILY_DS28E04, "4kb EEPROM + PIO" },
	{ ONEWIRE_FAMILY_DS2423, "4kb NVRAM + Counter" },
	{ ONEWIRE_FAMILY_DS2409, "Microlan Coupler" },
	{ ONEWIRE_FAMILY_DS1822, "Temperature" },
	{ ONEWIRE_FAMILY_DS2433, "4kb EEPROM" },
	{ ONEWIRE_FAMILY_DS2415, "RTC" },
	{ ONEWIRE_FAMILY_DS2438, "Smart Battery Monitor" },
	{ ONEWIRE_FAMILY_DS2417, "RTC with interrupt" },
	{ ONEWIRE_FAMILY_DS18B20, "Temperature" },
	{ ONEWIRE_FAMILY_DS2408, "8-channel Addressable Switch" },
	{ ONEWIRE_FAMILY_DS2890, "Digital Potentiometer" },
	{ ONEWIRE_FAMILY_DS2431, "1kb EEPROM" },
	{ ONEWIRE_FAMILY_DS2760, "Lithium Battery Monitor" },
	{ ONEWIRE_FAMILY_DS2413, "2-channel Addressable Switch" },
	{ ONEWIRE_FAMILY_DS2422, "Temperature + 8kb datalog" },
	{ ONEWIRE_FAMILY_DS28EA00, "Temperature" },
	{ ONEWIRE_FAMILY_DS28EC20, "20kb EEPROM" },
	{ 0, NULL }
};
