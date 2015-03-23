/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *
 *******************************************************************/
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/pinctrl/consumer.h>
#include <mach/am_regs.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <linux/amlogic/aml_audio_codec_probe.h>
#include <linux/amlogic/aml_gpio_consumer.h>

codec_info_t codec_info;
int ext_codec = 0;

EXPORT_SYMBOL(ext_codec);


static const struct regmap_config codec_regmaps[] = {
	{
		.name = "rt5616",
		.reg_bits =		8,
		.val_bits =		16,
		.max_register =		0xff,
	},
	{
		.name = "rt5631",
		.reg_bits = 	8,
		.val_bits = 	16,
		.max_register = 	0x7e,
	},
	{
		.name = "wm8960",
		.reg_bits = 	7,
		.val_bits = 	9,
		.max_register = 	0x37,
	},
};

static int test_codec_of_node(struct device_node* p_node, aml_audio_codec_info_t* audio_codec_dev)
{
	int ret = 0, val = 0;
	const char* str;
	struct i2c_board_info board_info;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	struct regmap *regmap;
	
	memset(&board_info, 0, sizeof(board_info));

	ret = of_property_read_string(p_node, "codec_name", &audio_codec_dev->name);
	if (ret) {
        printk("no of property codec_name!\n");
		goto exit;
    }
	printk("test codec %s\n", audio_codec_dev->name);
	ret = of_property_read_string(p_node, "status", &audio_codec_dev->status);
	if(ret){
		printk("%s:can't get status info!\n",audio_codec_dev->name);
		goto exit;
	}

	if (strcmp(audio_codec_dev->status, "okay") && strcmp(audio_codec_dev->status, "ok")){
		printk("test_codec_of_node, node %s disable\n", audio_codec_dev->name);
		ret = -ENODEV;
		goto exit;
	}
	
	/* if aml pmu codec, do not test i2c for it was done in power domain */
	if (!strcmp(audio_codec_dev->name, "amlpmu3"))
		goto exit;
	if (!strcmp(audio_codec_dev->name, "dummy_codec"))
		goto exit;
	if (!strcmp(audio_codec_dev->name, "pcm5102"))
		goto exit;

	ret = of_property_read_u32(p_node,"i2c_addr", &audio_codec_dev->i2c_addr);
	if(ret){
		printk("%s fail to get i2c_addr\n", __func__);
		goto exit;
	}

	ret = of_property_read_u32(p_node,"id_reg", &audio_codec_dev->id_reg);
	if(ret){
		printk("%s fail to get id_reg\n", __func__);
		goto exit;
	}

	ret = of_property_read_u32(p_node,"id_val", &audio_codec_dev->id_val);
	if(ret){
		printk("%s fail to get id_val\n", __func__);
		goto exit;
	}

	ret = of_property_read_string(p_node, "i2c_bus", &str);
	if(ret){
		printk("%s fail to get i2c_bus\n", __func__);
		goto exit;
	}
	
	if (!strncmp(str, "i2c_bus_ao", 10))
        audio_codec_dev->i2c_bus_type = AML_I2C_BUS_AO;
	else if (!strncmp(str, "i2c_bus_a", 9))
        audio_codec_dev->i2c_bus_type = AML_I2C_BUS_A;
    else if (!strncmp(str, "i2c_bus_b", 9))
        audio_codec_dev->i2c_bus_type = AML_I2C_BUS_B;
    else if (!strncmp(str, "i2c_bus_c", 9))
        audio_codec_dev->i2c_bus_type = AML_I2C_BUS_C;
    else if (!strncmp(str, "i2c_bus_d", 9))
        audio_codec_dev->i2c_bus_type = AML_I2C_BUS_D;
    else
		printk("ERR, unsupported i2c bus addr: %s \n", str);
	
	adapter = i2c_get_adapter(audio_codec_dev->i2c_bus_type);
	if (!adapter){
		ret = -ENODEV;
		goto exit;
	}
	
	strncpy(board_info.type, "codec_i2c", I2C_NAME_SIZE);
	board_info.addr = audio_codec_dev->i2c_addr;
	client = i2c_new_device(adapter, &board_info);
	if (!client) {
		/* I2C device registration failed, continue with the next */
		printk("Unable to add I2C device for 0x%x\n",
			 board_info.addr);
		ret = -ENODEV;
		goto err2;
	}

	regmap = devm_regmap_init_i2c(client, &codec_regmaps[codec_info.codec_index]);
	if (IS_ERR(regmap)){
		ret = PTR_ERR(regmap);
		goto err1;
	}
	
	ret = regmap_read(regmap, audio_codec_dev->id_reg, &val);
	if (ret){
		printk("try regmap_read err, so %s disabled\n", audio_codec_dev->name);
		ret = -ENODEV;
		goto err1;
	}

	if (val != audio_codec_dev->id_val){
		printk("ID value mismatch, so %s disabled!\n", audio_codec_dev->name);
		ret = -ENODEV;
	}
	
err1:
	i2c_unregister_device(client);
err2:
	i2c_put_adapter(adapter);
exit:
	return ret;
}

static int register_i2c_codec_device(aml_audio_codec_info_t* audio_codec_dev)
{
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	struct i2c_board_info board_info;
	char tmp[NAME_SIZE];

	strncpy(board_info.type, audio_codec_dev->name, I2C_NAME_SIZE);
	board_info.addr = audio_codec_dev->i2c_addr;
	
	adapter = i2c_get_adapter(audio_codec_dev->i2c_bus_type);
	client = i2c_new_device(adapter, &board_info);
	snprintf(tmp, NAME_SIZE, "%s", audio_codec_dev->name);
	strlcpy(codec_info.name, tmp, NAME_SIZE);
	snprintf(tmp, NAME_SIZE, "%s.%s", audio_codec_dev->name, dev_name(&client->dev));
	strlcpy(codec_info.name_bus, tmp, NAME_SIZE);

	return 0;
}

static int aml_audio_codec_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device_node* audio_codec_node = pdev->dev.of_node;
    struct device_node* child;
    aml_audio_codec_info_t *audio_codec_dev;

	audio_codec_dev = kzalloc(sizeof(aml_audio_codec_info_t), GFP_KERNEL);
	if (!audio_codec_dev) {
		printk("ERROR, temp_audio_codec device create fail.\n");
		ret = -ENOMEM;
		goto exit;
    }
	
	memset(&codec_info, 0, sizeof(codec_info));

    for_each_child_of_node(audio_codec_node, child) {
        memset(audio_codec_dev, 0, sizeof(aml_audio_codec_info_t));
		ret = test_codec_of_node(child, audio_codec_dev);
		codec_info.codec_index++;
		
		if (ret == 0){
			ext_codec = 1;
			printk("using external codec, index = %d\n", codec_info.codec_index);
			break;
		}
    }
	
	if (ext_codec &&(!strcmp(audio_codec_dev->name, "amlpmu3"))){
		printk("using aml pmu3 codec\n");
		strlcpy(codec_info.name_bus, "aml_pmu3_codec.0", NAME_SIZE);
		strlcpy(codec_info.name, "amlpmu3", NAME_SIZE);
		goto exit;
	}
    
	if (ext_codec &&(!strcmp(audio_codec_dev->name, "dummy_codec"))){
		printk("using external dummy codec\n");
		strlcpy(codec_info.name_bus, "dummy_codec.0", NAME_SIZE);
		strlcpy(codec_info.name, "dummy", NAME_SIZE);
		goto exit;
	}

	if (ext_codec &&(!strcmp(audio_codec_dev->name, "pcm5102"))){
		printk("using pcm5102 codec\n");
		strlcpy(codec_info.name_bus, "pcm5102.0", NAME_SIZE);
		strlcpy(codec_info.name, "pcm5102", NAME_SIZE);
		goto exit;
	}
		
	if (!ext_codec){
		printk("no external codec, using aml default codec\n");
		strlcpy(codec_info.name_bus, "aml_m8_codec.0", NAME_SIZE);
		strlcpy(codec_info.name, "amlm8", NAME_SIZE);
		codec_info.codec_index = aml_codec;
		ret = 0;
		goto exit;
	}

	ret = register_i2c_codec_device(audio_codec_dev);
	if (ret)
        dev_err(&pdev->dev, "register_codec_device failed (%d)\n", ret);

exit:
	kfree(audio_codec_dev);
    return ret;
}

static int aml_audio_codec_remove(struct platform_device *pdev)
{
    return 0;
}

static const struct of_device_id aml_audio_codec_probe_dt_match[]={
    {
        .compatible = "amlogic,audio_codec",
    },
    {},
};

static  struct platform_driver aml_audio_codec_probe_driver = {
    .probe      = aml_audio_codec_probe,
    .remove     = aml_audio_codec_remove,
    .driver     = {
        .name   = "aml_audio_codec_probe",
        .owner  = THIS_MODULE,
        .of_match_table = aml_audio_codec_probe_dt_match,
    },
};

static int __init aml_audio_codec_probe_init(void)
{
    int ret;

    ret = platform_driver_register(&aml_audio_codec_probe_driver);
    if (ret){
        printk(KERN_ERR"aml_audio_codec_probre_driver register failed\n");
        return ret;
    }

    return ret;
}

static void __exit aml_audio_codec_probe_exit(void)
{
    platform_driver_unregister(&aml_audio_codec_probe_driver);
}

module_init(aml_audio_codec_probe_init);
module_exit(aml_audio_codec_probe_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Amlogic Audio Codec prober driver");

