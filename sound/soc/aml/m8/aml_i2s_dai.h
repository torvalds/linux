#ifndef __AML_I2S_DAI_H__
#define __AML_I2S_DAI_H__

extern struct snd_soc_dai_driver aml_dai[];
struct aml_i2s
{
	int mpll;
	int old_samplerate;
};

#endif
