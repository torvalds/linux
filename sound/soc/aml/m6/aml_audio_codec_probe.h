#ifndef __AML_AUDIO_CODEC_DEV__
#define __AML_AUDIO_CODEC_DEV__
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/of.h>

#define AML_I2C_BUS_AO 0
#define AML_I2C_BUS_A 1
#define AML_I2C_BUS_B 2
#define AML_I2C_BUS_C 3
#define AML_I2C_BUS_D 4

//#define NAME_SIZE 32

typedef int(*aml_audio_codec_probe_fun_t)(struct i2c_adapter *);

typedef struct {
	const char* name;
	const char* status;
	struct device_node* p_node;
	unsigned i2c_bus_type;
	unsigned i2c_addr;
	unsigned id_reg;
	unsigned id_val;
    unsigned capless;
}aml_audio_codec_info_t;

enum codecs_enum{
	aml_codec = 0,
	rt5616,
	rt5631,
	wm8960,
};

typedef struct {
	enum codecs_enum codec_index;
	char name[I2C_NAME_SIZE];
	char name_bus[I2C_NAME_SIZE];
}codec_info_t;

extern codec_info_t codec_info;

#endif
