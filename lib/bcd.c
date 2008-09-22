#include <linux/bcd.h>
#include <linux/module.h>

unsigned bcd2bin(unsigned char val)
{
	return (val & 0x0f) + (val >> 4) * 10;
}
EXPORT_SYMBOL(bcd2bin);

unsigned char bin2bcd(unsigned val)
{
	return ((val / 10) << 4) + val % 10;
}
EXPORT_SYMBOL(bin2bcd);
