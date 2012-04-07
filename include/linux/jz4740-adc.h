
#ifndef __LINUX_JZ4740_ADC
#define __LINUX_JZ4740_ADC

struct device;

/*
 * jz4740_adc_set_config - Configure a JZ4740 adc device
 * @dev: Pointer to a jz4740-adc device
 * @mask: Mask for the config value to be set
 * @val: Value to be set
 *
 * This function can be used by the JZ4740 ADC mfd cells to configure their
 * options in the shared config register.
*/
int jz4740_adc_set_config(struct device *dev, uint32_t mask, uint32_t val);

#define JZ_ADC_CONFIG_SPZZ		BIT(31)
#define JZ_ADC_CONFIG_EX_IN		BIT(30)
#define JZ_ADC_CONFIG_DNUM_MASK		(0x7 << 16)
#define JZ_ADC_CONFIG_DMA_ENABLE	BIT(15)
#define JZ_ADC_CONFIG_XYZ_MASK		(0x2 << 13)
#define JZ_ADC_CONFIG_SAMPLE_NUM_MASK	(0x7 << 10)
#define JZ_ADC_CONFIG_CLKDIV_MASK	(0xf << 5)
#define JZ_ADC_CONFIG_BAT_MB		BIT(4)

#define JZ_ADC_CONFIG_DNUM(dnum)	((dnum) << 16)
#define JZ_ADC_CONFIG_XYZ_OFFSET(dnum)	((xyz) << 13)
#define JZ_ADC_CONFIG_SAMPLE_NUM(x)	((x) << 10)
#define JZ_ADC_CONFIG_CLKDIV(div)	((div) << 5)

#endif
