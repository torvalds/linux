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
#include <linux/slab.h>
#include <linux/pinctrl/consumer.h>
#include <mach/am_regs.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <sound/tas57xx.h>
#include "aml_audio_codec_probe.h"


extern struct i2c_client * i2c_new_device(struct i2c_adapter *adap,
            struct i2c_board_info const *info);


static struct platform_device* audio_codec_pdev = NULL;
static int regist_codec_info(struct device_node* p_node, aml_audio_codec_info_t* audio_codec_dev)
{
    int ret = 0;
    ret = of_property_read_string(p_node, "codec_name", &audio_codec_dev->name);
    if (ret) {
        printk("get audio codec name failed!\n");
    }
    ret = of_property_read_string(p_node, "status", &audio_codec_dev->status);
    if(ret){
        printk("%s:this audio codec is disabled!\n",audio_codec_dev->name);
    }

    return 0;
}


static int get_audio_codec_i2c_info(struct device_node* p_node, aml_audio_codec_info_t* audio_codec_dev)
{
    const char* str;
    int ret = 0;
    unsigned i2c_addr;
    struct i2c_adapter *adapter;

    ret = of_property_read_string(p_node, "codec_name", &audio_codec_dev->name);
    if (ret) {
        printk("get audio codec name failed!\n");
        goto err_out;
    }

    ret = of_property_match_string(p_node,"status","okay");
    if(ret){
        printk("%s:this audio codec is disabled!\n",audio_codec_dev->name);
        goto err_out;
    }
    printk("use audio codec %s\n",audio_codec_dev->name);

    ret = of_property_read_u32(p_node,"capless",&audio_codec_dev->capless);
    if(ret){
        printk("don't find audio codec capless mode!\n");
    }

    ret = of_property_read_string(p_node, "i2c_bus", &str);
    if (ret) {
        printk("%s: faild to get i2c_bus str,use default i2c bus!\n", audio_codec_dev->name);
        audio_codec_dev->i2c_bus_type = AML_I2C_BUS_D;
    } else {
        if (!strncmp(str, "i2c_bus_a", 9))
            audio_codec_dev->i2c_bus_type = AML_I2C_BUS_A;
        else if (!strncmp(str, "i2c_bus_b", 9))
            audio_codec_dev->i2c_bus_type = AML_I2C_BUS_B;
        else if (!strncmp(str, "i2c_bus_c", 9))
            audio_codec_dev->i2c_bus_type = AML_I2C_BUS_C;
        else if (!strncmp(str, "i2c_bus_d", 9))
            audio_codec_dev->i2c_bus_type = AML_I2C_BUS_D;
        else if (!strncmp(str, "i2c_bus_ao", 10))
            audio_codec_dev->i2c_bus_type = AML_I2C_BUS_AO;
        else
            audio_codec_dev->i2c_bus_type = AML_I2C_BUS_D;
    }

    ret = of_property_read_u32(p_node,"i2c_addr",&i2c_addr);
    if(ret){
        printk("don't find i2c adress capless,use default!\n");
        audio_codec_dev->i2c_addr = 0x1B;
    }else{
        audio_codec_dev->i2c_addr = i2c_addr;
    }
    printk("audio codec addr: 0x%x\n", audio_codec_dev->i2c_addr);
    printk("audio codec i2c bus: %d\n", audio_codec_dev->i2c_bus_type);

    /* test if the camera is exist */
    adapter = i2c_get_adapter(audio_codec_dev->i2c_bus_type);
    if (!adapter) {
        printk("can not do probe function\n");
        ret = -1;
        goto err_out;
    }
    ret = 0;

err_out:
    return ret;
}

#ifdef CONFIG_SND_AML_M6TV_AUDIO_CODEC
codec_info_t codec_info;
static struct codec_probe_priv prob_priv;
struct codec_probe_priv{
	int num_eq;
	struct tas57xx_eq_cfg *eq_configs;
	char *sub_bq_table;
	char *drc1_table;
	char *drc1_tko_table;
};

static int of_get_eq_pdata(struct tas57xx_platform_data *pdata, struct device_node* p_node)
{
	int i, ret = 0, length = 0;
	const char *str = NULL;
	char *regs = NULL;

	prob_priv.num_eq = of_property_count_strings(p_node,"eq_name");
	if(prob_priv.num_eq <= 0){
		printk("no of eq_name config\n");
		ret = -ENODEV;
		goto exit;
	}

	pdata->num_eq_cfgs = prob_priv.num_eq;

	prob_priv.eq_configs = kzalloc(prob_priv.num_eq * sizeof(struct tas57xx_eq_cfg), GFP_KERNEL);

	for(i = 0; i < prob_priv.num_eq; i++){
		ret = of_property_read_string_index(p_node, "eq_name", i , &str);

		if(of_find_property(p_node, "eq_table", &length) == NULL){
			printk("%s fail to get of eq_table\n", __func__);
			goto exit1;
		}

		regs = kzalloc(length * sizeof(char *), GFP_KERNEL);
		if (!regs) {
			printk("ERROR, NO enough mem for eq_table!\n");
			return -ENOMEM;
		}

		ret = of_property_read_u8_array(p_node, "eq_table", regs, length);

		strncpy(prob_priv.eq_configs[i].name, str, NAME_SIZE);
		prob_priv.eq_configs[i].regs = regs;
	}

	pdata->eq_cfgs = prob_priv.eq_configs;

	return 0;
exit1:
	kfree(prob_priv.eq_configs);
exit:
	return ret;
}
static void *alloc_and_get_data_array(struct device_node *p_node, char *str, int *lenp)
{
	int ret = 0, length = 0;
	char *p = NULL;

	if(of_find_property(p_node, str, &length) == NULL){
		printk("DT of %s not found!\n", str);
		goto exit;
	}
	printk("prop=%s,length=%d\n",str,length);
	p = kzalloc(length * sizeof(char *), GFP_KERNEL);
	if (p == NULL) {
		printk("ERROR, NO enough mem for %s!\n", str);
		length = 0;
		goto exit;
	}

	ret = of_property_read_u8_array(p_node, str, p, length);
	if (ret) {
	printk("no of property %s!\n", str);
		kfree(p);
		p = NULL;
		goto exit;
	}

	*lenp = length;

exit:
	return p;
}

static int of_get_subwoofer_pdata(struct tas57xx_platform_data *pdata, struct device_node *p_node)
{
	int length = 0;
	char *pd = NULL;

	pd = alloc_and_get_data_array(p_node, "sub_bq_table", &length);

	if(pd == NULL){
		return -1;
	}

	pdata->custom_sub_bq_table_len = length;
	pdata->custom_sub_bq_table = pd;

	return 0;
}

static int of_get_drc_pdata(struct tas57xx_platform_data *pdata, struct device_node* p_node)
{
	int length = 0;
	char *pd = NULL;

	//get drc1 table
	pd = alloc_and_get_data_array(p_node, "drc1_table", &length);
	if(pd == NULL){
		return -1;
	}
	pdata->custom_drc1_table_len = length;
	pdata->custom_drc1_table = pd;

	//get drc1 tko table
	length = 0;
	pd = NULL;

	pd = alloc_and_get_data_array(p_node, "drc1_tko_table", &length);
	if(pd == NULL){
		return -1;
	}
	pdata->custom_drc1_tko_table_len = length;
	pdata->custom_drc1_tko_table = pd;
	pdata->enable_ch1_drc = 1;

	//get drc2 table
	length = 0;
	pd = NULL;
	pd = alloc_and_get_data_array(p_node, "drc2_table", &length);
	if(pd == NULL){
		return -1;
	}
	pdata->custom_drc2_table_len = length;
	pdata->custom_drc2_table = pd;

	//get drc2 tko table
	length = 0;
	pd = NULL;
	pd = alloc_and_get_data_array(p_node, "drc2_tko_table", &length);
	if(pd == NULL){
		return -1;
	}
	pdata->custom_drc2_tko_table_len = length;
	pdata->custom_drc2_tko_table = pd;
	pdata->enable_ch2_drc = 1;

	return 0;
}

static int of_get_init_pdata(struct tas57xx_platform_data *pdata, struct device_node* p_node)
{
	int length = 0;
	char *pd = NULL;

	pd = alloc_and_get_data_array(p_node, "input_mux_reg_buf", &length);
	if(pd == NULL){
		printk("%s : can't get input_mux_reg_buf \n", __func__);
		return -1;
	}

	/*Now only support 0x20 input mux init*/
	pdata->num_init_regs = length;
	pdata->init_regs = pd;

	if(of_property_read_u32(p_node,"master_vol", &pdata->custom_master_vol)){
		printk("%s fail to get master volume\n", __func__);
		return -1;
	}

	return 0;

}
static int codec_get_of_pdata(struct tas57xx_platform_data *pdata, struct device_node* p_node)
{
	int ret = 0;

	ret = of_get_eq_pdata(pdata, p_node);
	if(ret){
		printk("no platform codec EQ config found\n");
	}

	ret = of_get_subwoofer_pdata(pdata, p_node);
	if(ret){
		printk("no platform codec subwoofer config found\n");
	}

	ret = of_get_drc_pdata(pdata, p_node);
	if(ret){
		printk("no platform codec drc config found\n");
	}

	ret = of_get_init_pdata(pdata, p_node);
	if(ret){
		printk("no platform codec init config found\n");
	}

	return ret;
}
#endif
static int aml_audio_codec_probe(struct platform_device *pdev)
{
	struct device_node* audio_codec_node = pdev->dev.of_node;
	struct device_node* child;
	struct i2c_board_info board_info;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	aml_audio_codec_info_t temp_audio_codec;
#ifdef CONFIG_SND_AML_M6TV_AUDIO_CODEC
	struct tas57xx_platform_data *pdata;
	char tmp[NAME_SIZE];

	pdata = kzalloc(sizeof(struct tas57xx_platform_data), GFP_KERNEL);
	if (!pdata) {
		printk("ERROR, NO enough mem for tas57xx_platform_data!\n");
		return -ENOMEM;
    }
#endif
	memset(&board_info, 0, sizeof(board_info));

	audio_codec_pdev = pdev;

	for_each_child_of_node(audio_codec_node, child) {

        memset(&temp_audio_codec, 0, sizeof(aml_audio_codec_info_t));
        regist_codec_info(child,&temp_audio_codec);
        if (get_audio_codec_i2c_info(child, &temp_audio_codec)) {
            continue;
        }
        memset(&board_info, 0, sizeof(board_info));
		strncpy(board_info.type, temp_audio_codec.name, I2C_NAME_SIZE);
        adapter = i2c_get_adapter(temp_audio_codec.i2c_bus_type);
        board_info.addr = temp_audio_codec.i2c_addr;
        board_info.platform_data = &temp_audio_codec;
        client = i2c_new_device(adapter, &board_info);
#ifdef CONFIG_SND_AML_M6TV_AUDIO_CODEC
	snprintf(tmp, I2C_NAME_SIZE, "%s", temp_audio_codec.name);
	strlcpy(codec_info.name, tmp, I2C_NAME_SIZE);
	snprintf(tmp, I2C_NAME_SIZE, "%s.%s", temp_audio_codec.name, dev_name(&client->dev));
	strlcpy(codec_info.name_bus, tmp, I2C_NAME_SIZE);

	codec_get_of_pdata(pdata, child);
	client->dev.platform_data = pdata;
#endif
    }
    return 0;
}


static int aml_audio_codec_remove(struct platform_device *pdev)
{
#ifdef CONFIG_SND_AML_M6TV_AUDIO_CODEC
	int i;
	for(i = 0; i < prob_priv.num_eq; i++){
		if(prob_priv.eq_configs[i].regs)
			kfree(prob_priv.eq_configs[i].regs);
	}

	if(prob_priv.eq_configs)
		kfree(prob_priv.eq_configs);

	if(prob_priv.sub_bq_table)
		kfree(prob_priv.sub_bq_table);
#endif

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

