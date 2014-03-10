/*
* ALSA SoC CX2070X codec driver
*
* Copyright:   (C) 2009/2010 Conexant Systems
*
* Based on sound/soc/codecs/tlv320aic2x.c by Vladimir Barinov
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* 
*      
*************************************************************************
*  Modified Date:  09/14/12
*  File Version:   3.1.10.13
*************************************************************************
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/gpio.h>
#include <sound/jack.h>
#include <linux/slab.h>

#include "cx2070x.h"

#define CX2070X_DRIVER_VERSION AUDDRV_VERSION( 3, 1 ,0x10 ,0x13) 

#ifdef USING_I2C
#include <linux/i2c.h>
#endif 

#ifdef USING_SPI 
#include <linux/spi/spi.h>
#endif 

#if defined(CONFIG_SND_CXLIFEGUARD)
#include "cxdebug.h"
#endif 

#ifdef CONFIG_SND_CX2070X_LOAD_FW
#ifdef CONFIG_SND_CX2070X_USE_FW_H
#include "cx2070x_fw.h"
#else
#include <linux/firmware.h>
#endif
#include "cxpump.h" 
#endif


#define CX2070X_TRISTATE_EEPROM	0
#define CX2070X_REG_NAMES	1
#define CX2070X_REG_WIDE	1


#define AUDIO_NAME	"cx2070x"


#define CX2070X_RATES	( \
       SNDRV_PCM_RATE_8000  \
    | SNDRV_PCM_RATE_11025 \
    | SNDRV_PCM_RATE_16000 \
    | SNDRV_PCM_RATE_22050 \
    | SNDRV_PCM_RATE_32000 \
    | SNDRV_PCM_RATE_44100 \
    | SNDRV_PCM_RATE_48000 \
    | SNDRV_PCM_RATE_88200 \
    | SNDRV_PCM_RATE_96000 )
    
#if defined(CONFIG_SND_DIGICOLOR_SOC_CHANNEL_VER_4_30F)

#define CX2070X_FORMATS ( SNDRV_PCM_FMTBIT_S16_LE \
    | SNDRV_PCM_FMTBIT_S16_BE \
    | SNDRV_PCM_FMTBIT_MU_LAW \
    | SNDRV_PCM_FMTBIT_A_LAW )
#else
#define CX2070X_FORMATS ( SNDRV_PCM_FMTBIT_S16_LE \
    | SNDRV_PCM_FMTBIT_S16_BE )
#endif



#define noof(a) (sizeof(a)/sizeof(a[0]))
#define NOINLINE __attribute__((__noinline__))
//#ifdef DEBUG
#if 1
# define INFO(fmt,...)	printk(KERN_INFO fmt, ##__VA_ARGS__)
# define _INFO(fmt,...)	printk(KERN_INFO fmt, ##__VA_ARGS__)
# define _INFO_		1
#else
# define INFO(fmt,...)	
# define _INFO(fmt,...)
# define _INFO_		1
#endif
#define MSG(fmt,...)	printk(KERN_INFO fmt, ##__VA_ARGS__)
#define ERROR(fmt,...)	printk(KERN_ERR fmt, ##__VA_ARGS__)

enum {
    b_00000000,b_00000001,b_00000010,b_00000011, b_00000100,b_00000101,b_00000110,b_00000111,
    b_00001000,b_00001001,b_00001010,b_00001011, b_00001100,b_00001101,b_00001110,b_00001111,
    b_00010000,b_00010001,b_00010010,b_00010011, b_00010100,b_00010101,b_00010110,b_00010111,
    b_00011000,b_00011001,b_00011010,b_00011011, b_00011100,b_00011101,b_00011110,b_00011111,
    b_00100000,b_00100001,b_00100010,b_00100011, b_00100100,b_00100101,b_00100110,b_00100111,
    b_00101000,b_00101001,b_00101010,b_00101011, b_00101100,b_00101101,b_00101110,b_00101111,
    b_00110000,b_00110001,b_00110010,b_00110011, b_00110100,b_00110101,b_00110110,b_00110111,
    b_00111000,b_00111001,b_00111010,b_00111011, b_00111100,b_00111101,b_00111110,b_00111111,
    b_01000000,b_01000001,b_01000010,b_01000011, b_01000100,b_01000101,b_01000110,b_01000111,
    b_01001000,b_01001001,b_01001010,b_01001011, b_01001100,b_01001101,b_01001110,b_01001111,
    b_01010000,b_01010001,b_01010010,b_01010011, b_01010100,b_01010101,b_01010110,b_01010111,
    b_01011000,b_01011001,b_01011010,b_01011011, b_01011100,b_01011101,b_01011110,b_01011111,
    b_01100000,b_01100001,b_01100010,b_01100011, b_01100100,b_01100101,b_01100110,b_01100111,
    b_01101000,b_01101001,b_01101010,b_01101011, b_01101100,b_01101101,b_01101110,b_01101111,
    b_01110000,b_01110001,b_01110010,b_01110011, b_01110100,b_01110101,b_01110110,b_01110111,
    b_01111000,b_01111001,b_01111010,b_01111011, b_01111100,b_01111101,b_01111110,b_01111111,
    b_10000000,b_10000001,b_10000010,b_10000011, b_10000100,b_10000101,b_10000110,b_10000111,
    b_10001000,b_10001001,b_10001010,b_10001011, b_10001100,b_10001101,b_10001110,b_10001111,
    b_10010000,b_10010001,b_10010010,b_10010011, b_10010100,b_10010101,b_10010110,b_10010111,
    b_10011000,b_10011001,b_10011010,b_10011011, b_10011100,b_10011101,b_10011110,b_10011111,
    b_10100000,b_10100001,b_10100010,b_10100011, b_10100100,b_10100101,b_10100110,b_10100111,
    b_10101000,b_10101001,b_10101010,b_10101011, b_10101100,b_10101101,b_10101110,b_10101111,
    b_10110000,b_10110001,b_10110010,b_10110011, b_10110100,b_10110101,b_10110110,b_10110111,
    b_10111000,b_10111001,b_10111010,b_10111011, b_10111100,b_10111101,b_10111110,b_10111111,
    b_11000000,b_11000001,b_11000010,b_11000011, b_11000100,b_11000101,b_11000110,b_11000111,
    b_11001000,b_11001001,b_11001010,b_11001011, b_11001100,b_11001101,b_11001110,b_11001111,
    b_11010000,b_11010001,b_11010010,b_11010011, b_11010100,b_11010101,b_11010110,b_11010111,
    b_11011000,b_11011001,b_11011010,b_11011011, b_11011100,b_11011101,b_11011110,b_11011111,
    b_11100000,b_11100001,b_11100010,b_11100011, b_11100100,b_11100101,b_11100110,b_11100111,
    b_11101000,b_11101001,b_11101010,b_11101011, b_11101100,b_11101101,b_11101110,b_11101111,
    b_11110000,b_11110001,b_11110010,b_11110011, b_11110100,b_11110101,b_11110110,b_11110111,
    b_11111000,b_11111001,b_11111010,b_11111011, b_11111100,b_11111101,b_11111110,b_11111111,
};

enum {
    NO_INPUT = 0,
    I2S_ONLY,
    USB_ONLY,
    I2S_USB_MIXING,
};

#define REG_TYPE_RO	0	// read only,  read during initialization
#define REG_TYPE_RW	1	// read/write, read during initialization
#define REG_TYPE_WI	2	// write only, written during initialization
#define REG_TYPE_WC	3	// write/init, needs NEWC to be set when written
#define REG_TYPE_DM	4	// dummy register, read/write to cache only
#if CX2070X_REG_WIDE
# define REG_TYPE_MASK	0x0F
# define REG_WIDTH_B	0x00	//  8-bit data
# define REG_WIDTH_W	0x10	// 16-bit data
# define REG_WIDTH_MASK	0xF0
#endif
enum {
#define __REG(a,b2,b1,c,d,e,f) a,
#include "cx2070x-i2c.h"
#undef __REG
};

#if CX2070X_REG_WIDE
typedef u16 cx2070x_reg_t;
#else
typedef u8 cx2070x_reg_t;
#endif
static const cx2070x_reg_t cx2070x_data[]=
{
#define __REG(a,b2,b1,c,d,e,f) c,
#include "cx2070x-i2c.h"
#undef __REG
};

struct cx2070x_reg
{
#if CX2070X_REG_NAMES
    char *name;
#endif
    u16   addr;
    u8    bias;
    u8    type;
};

static const struct cx2070x_reg cx2070x_regs[]=
{
#if defined(CONFIG_SND_DIGICOLOR_SOC_CHANNEL_VER_3_13E)
# if CX2070X_REG_NAMES
#  define __REG(a,b2,b1,c,d,e,f) { #a,b1,d,REG_TYPE_##e|REG_WIDTH_##f },
# else
#  define __REG(a,b2,b1,c,d,e,f) { b1,d,REG_TYPE_##e|REG_WIDTH_##f },
# endif
#elif defined(CONFIG_SND_DIGICOLOR_SOC_CHANNEL_VER_4_30F)
# if CX2070X_REG_NAMES
#  define __REG(a,b2,b1,c,d,e,f) { #a,b2,d,REG_TYPE_##e|REG_WIDTH_##f },
# else
#  define __REG(a,b2,b1,c,d,e,f) { b2,d,REG_TYPE_##e|REG_WIDTH_##f },
# endif
#else
# if CX2070X_REG_NAMES
#  define __REG(a,b2,b1,c,d,e,f) { #a,b2,d,REG_TYPE_##e|REG_WIDTH_##f },
# else
#  define __REG(a,b2,b1,c,d,e,f) { b2,d,REG_TYPE_##e|REG_WIDTH_##f },
# endif
#endif
#include "cx2070x-i2c.h"
#undef __REG
};

// codec private data
struct cx2070x_priv
{
    enum snd_soc_control_type control_type;
    void *control_data;
    unsigned int sysclk;
    int	       master;
    enum Cx_INPUT_SEL input_sel;
    enum Cx_OUTPUT_SEL output_sel;
	unsigned int mute;
    long int playback_path;
	long int capture_path;
};

#define get_cx2070x_priv(_codec_) ((struct cx2070x_priv *)snd_soc_codec_get_drvdata(codec))

#if defined(CONFIG_CXNT_SOFTWOARE_SIMULATION)
static int bNoHW = 1;
#else
static int bNoHW = 0;
#endif 


/*
 * Playback Volume 
 *
 * max : 0x00 : 0 dB
 *       ( 1 dB step )
 * min : 0xB6 : -74 dB
 */
static const DECLARE_TLV_DB_SCALE(dac_tlv, -7400 , 100, 0);


/*
 * Capture Volume 
 *
 * max : 0x00 : 0 dB
 *       ( 1 dB step )
 * min : 0xB6 : -74 dB
 */
static const DECLARE_TLV_DB_SCALE(adc_tlv, -7400 , 100, 0);


#if defined (CONFIG_SND_CX2070X_GPIO_JACKSENSE)
// TODO : the jack sensing code should be moved to machine layer.
static struct snd_soc_jack hs_jack ;
/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin hs_jack_pins[] = {
    {
        /*.list_head list*/{},
            /*.pin*/"Headphone",
            /*.mask*/SND_JACK_HEADPHONE,
            /*.invert*/1
    },
    {
        /*.list_head list*/{},
            /*.pin*/"INT SPK",
            /*.mask*/SND_JACK_HEADPHONE,
            /*.invert*/0
    }
};

/* Headset jack detection gpios */
static struct snd_soc_jack_gpio hs_jack_gpios[] = {
    {
        /*.gpio*/ JACK_SENSE_GPIO_PIN,
            /*.name*/ "hsdet-gpio",
            /*.report*/ SND_JACK_HEADSET,
            /*.invert*/ 0,
            /*.debounce_time*/ 200,
            /*.jack*/ NULL,
            /*.work*/ NULL,
    },
};

#endif //CONFIG_SND_CX2070X_GPIO_JACKSENSE

#if defined(CONFIG_SND_CX2070X_LOAD_FW)
int I2cWrite( struct snd_soc_codec *codec, unsigned char ChipAddr, unsigned long cbBuf, unsigned char* pBuf);
int I2cWriteThenRead( struct snd_soc_codec *codec, unsigned char ChipAddr, unsigned long cbBuf,
    unsigned char* pBuf, unsigned long cbReadBuf, unsigned char*pReadBuf);
#endif 

#define GET_REG_CACHE(_codec_) (cx2070x_reg_t *) (_codec_)->reg_cache
static inline unsigned int cx2070x_read_reg_cache(struct snd_soc_codec *codec, unsigned int reg)
{
    cx2070x_reg_t *reg_cache;
    if (reg >= noof(cx2070x_regs))
        return (unsigned int)0;
    reg_cache  =  GET_REG_CACHE(codec);
    return reg_cache[reg];
}

static inline void cx2070x_write_reg_cache(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
    cx2070x_reg_t *reg_cache;
    if (reg >= noof(cx2070x_regs))
        return;
    reg_cache=GET_REG_CACHE(codec);
    reg_cache[reg] = value;
}

#ifdef USING_SPI
static int NOINLINE cx2070x_real_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{  /* SPI bus */
    int ret;
    u8                  data[4];
    struct spi_device * spi =   (struct spi_device *) codec->control_data;
    int len=0;
    const struct cx2070x_reg *ri;

    ri=&cx2070x_regs[reg];


    switch(ri->type&REG_TYPE_MASK)
    {
    case REG_TYPE_RO:		// read only,  read during initialization
#if CX2070X_REG_NAMES
        ERROR("%s(): write to Read-only register '%s'\n",__func__,ri->name);
#endif
        break;

    case REG_TYPE_RW:		// read/write, read during initialization
    case REG_TYPE_WI:		// write only, written during initialization
    case REG_TYPE_WC:		// write/init, needs NEWC to be set when written
        // msg[0].addr  = client->addr;
        //      msg[0].flags = client->flags & I2C_M_TEN;
        data[0]=(u8)(ri->addr>>8);
        data[1]=(u8)(ri->addr>>0);
        switch(ri->type&REG_WIDTH_MASK)
        {
        case REG_WIDTH_B:
            data[2]=(u8)(value-ri->bias);
            len=3;
            break;
        case REG_WIDTH_W:
            data[2]=(u8)((value-ri->bias)>>0)&0xFF;
            data[3]=(u8)((value-ri->bias)>>8)&0xFF;
            len=4;
            break;
        default:
            return -EIO;
        }
        data[0] |= 0x80; //Write flag.
#ifdef DBG_MONITOR_REG 
        printk(KERN_ERR "Write REG %02x%02x  %02x\n",data[0],data[1],data[2]);
#endif 
        spi_write(spi, data, len);
        break;

#if defined(REG_TYPE_DM)
    case REG_TYPE_DM:		// dummy register, no I2C transfers
        break;
#endif
    }

    cx2070x_write_reg_cache(codec,reg,value);
    return 0;
}

static int NOINLINE cx2070x_real_read(struct snd_soc_codec *codec, unsigned int reg)
{
    struct spi_device * spi =   (struct spi_device *) codec->control_data;
    int len=0;

    u8			    data[4];
    const struct cx2070x_reg *ri;
    int			    dat;
    int ret;
    ri=&cx2070x_regs[reg];

    if ((ri->type&REG_TYPE_MASK)==REG_TYPE_DM)
        return cx2070x_read_reg_cache(codec,reg);

    data[0]=(u8)(ri->addr>>8);
    data[1]=(u8)(ri->addr>>0);
    len   = ((ri->type&REG_WIDTH_MASK)==REG_WIDTH_W)?2:1;
    data[2] = 0;
    if (spi_write_then_read(spi, &data[0], 3, &data[2],len))
    {

    } 
    switch(ri->type&REG_WIDTH_MASK)
    {
    case REG_WIDTH_B:
        dat=ri->bias+data[2];
        break;
    case REG_WIDTH_W:
        dat=ri->bias+(data[2]<<0)+(data[3]<<8);
        break;
    default:
        return -EIO;
    }
    cx2070x_write_reg_cache(codec,reg,dat);
    return dat;
}
#else
static int NOINLINE cx2070x_real_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
    struct i2c_client  *client = (struct i2c_client  *) codec->control_data;
    struct i2c_adapter *adap   = client->adapter;
    struct i2c_msg      msg[2];
    u8                  data[4];
    const struct cx2070x_reg *ri;
    if(reg == MIC_CONTROL)
        printk(">>>>>>>>>>>>>%s value = %0x\n", __func__, value);
    if(reg == MIC_CONTROL)
        dump_stack();

    ri=&cx2070x_regs[reg];

    switch(ri->type&REG_TYPE_MASK)
    {
    case REG_TYPE_RO:		// read only,  read during initialization
#if CX2070X_REG_NAMES
        ERROR("%s(): write to Read-only register '%s'\n",__func__,ri->name);
#endif
        break;

    case REG_TYPE_RW:		// read/write, read during initialization
    case REG_TYPE_WI:		// write only, written during initialization
    case REG_TYPE_WC:		// write/init, needs NEWC to be set when written
        msg[0].addr  = client->addr;
        msg[0].flags = client->flags & I2C_M_TEN;
        msg[0].buf   = &data[0];
        msg[0].scl_rate = 200 * 1000;
        data[0]=(u8)(ri->addr>>8);
        data[1]=(u8)(ri->addr>>0);
        switch(ri->type&REG_WIDTH_MASK)
        {
        case REG_WIDTH_B:
            data[2]=(u8)(value-ri->bias);
            msg[0].len=3;
            break;
        case REG_WIDTH_W:
            data[2]=(u8)((value-ri->bias)>>0)&0xFF;
            data[3]=(u8)((value-ri->bias)>>8)&0xFF;
            msg[0].len=4;
            break;
        default:
            return -EIO;
        }
#ifdef DBG_MONITOR_REG 
        printk(KERN_ERR "Write REG %02x%02x  %02x\n",data[0],data[1],data[2]);
#endif 

        if (i2c_transfer(adap,msg,1)!=1)
            return -EIO;
        break;

#if defined(REG_TYPE_DM)
    case REG_TYPE_DM:		// dummy register, no I2C transfers
        break;
#endif
    }

    cx2070x_write_reg_cache(codec,reg,value);
    return 0;
}

static int NOINLINE cx2070x_real_read(struct snd_soc_codec *codec, unsigned int reg)
{
    struct i2c_client	   *client =(struct i2c_client  *) codec->control_data;
    struct i2c_adapter	   *adap   = client->adapter;
    struct i2c_msg	    msg[2];
    u8			    data[4];
    const struct cx2070x_reg *ri;
    int			    dat;

    ri=&cx2070x_regs[reg];

    if ((ri->type&REG_TYPE_MASK)==REG_TYPE_DM)
        return cx2070x_read_reg_cache(codec,reg);

    data[0]=(u8)(ri->addr>>8);
    data[1]=(u8)(ri->addr>>0);

    msg[0].addr  = client->addr;
    msg[0].flags = client->flags & I2C_M_TEN;
    msg[0].len   = 2;
    msg[0].buf   = &data[0];
    msg[0].scl_rate = 200 * 1000;

    msg[1].addr  = client->addr;
    msg[1].flags = (client->flags & I2C_M_TEN) | I2C_M_RD;
    msg[1].len   = ((ri->type&REG_WIDTH_MASK)==REG_WIDTH_W)?2:1;
    msg[1].buf   = &data[2];
    msg[1].scl_rate = 200 * 1000;

    if (i2c_transfer(adap,msg,2)!=2)
        return -EIO;

    switch(ri->type&REG_WIDTH_MASK)
    {
    case REG_WIDTH_B:
        dat=ri->bias+data[2];
        break;
    case REG_WIDTH_W:
        dat=ri->bias+(data[2]<<0)+(data[3]<<8);
        break;
    default:
        return -EIO;
    }
    cx2070x_write_reg_cache(codec,reg,dat);
    return dat;
}
#endif //#!ENABLE_SPI

// reset codec via gpio pin.
#if defined(CONFIG_SND_CX2070X_GPIO_RESET)
static int cx2070x_reset_device(void)
{

    int err = 0;
    int reset_pin = CODEC_RESET_GPIO_PIN;
    INFO("%lu: %s() called\n",jiffies,__func__);
    if (gpio_is_valid(reset_pin)) {
        if (gpio_request(reset_pin, "reset_pin")) {
            printk( KERN_ERR "cx2070x: reset pin %d not available\n",reset_pin);
            err = -ENODEV;
        } else {
            gpio_direction_output(reset_pin, 1);
            mdelay(3);
            gpio_set_value(reset_pin, 0);
            //udelay(1);// simon :need to re-check the reset timing.
            mdelay(3);
            gpio_set_value(reset_pin, 1);
            gpio_free(reset_pin); 
            mdelay(200); //simon :not sure how long the device become ready.
        }
    }
    else
    {
        printk( KERN_ERR "cx2070x: reset pin %d is not valid\n",reset_pin);
        err = -ENODEV;
    }
    return err;
}
#endif //#if defined(CONFIG_SND_CX2070X_GPIO_RESET)


static int cx2070x_dsp_init(struct snd_soc_codec *codec,unsigned mode)
{
    unsigned r;
    cx2070x_real_write(codec,DSP_INIT,mode);
    printk("******************%s mode = %0x\n",__func__, mode);
    // maximum time for the NEWC to clear is about 2ms.
    for(r=1000;;) 
        if (!(cx2070x_real_read(codec,DSP_INIT)&DSP_INIT_NEWC))
            return 0;
        else if (--r==0)
            return -EIO;
        else
            msleep(1);
}

static int NOINLINE cx2070x_write(struct snd_soc_codec *codec, unsigned int reg, unsigned int value)
{
    int err = 0;

    if ((err=cx2070x_real_write(codec,reg,value))<0)
        return err;

    switch(cx2070x_regs[reg].type&REG_TYPE_MASK)
    {
    case REG_TYPE_WC:
        printk("^^^^^^^^^%0x\n",cx2070x_read_reg_cache(codec,DSP_INIT));
        printk("^^^^^^^^^%0x\n",cx2070x_read_reg_cache(codec,DSP_INIT)|DSP_INIT_NEWC);
        return cx2070x_dsp_init(codec,cx2070x_read_reg_cache(codec,DSP_INIT)|DSP_INIT_NEWC);
    default:
        return err;
    }
}


static int output_select_event_set(struct snd_kcontrol *kcontrol,
struct snd_ctl_elem_value *ucontrol)
{

    int changed = 0;
    struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);
    struct soc_enum *control = (struct soc_enum *)kcontrol->private_value;
    struct cx2070x_priv  *channel = get_cx2070x_priv(codec);

   // unsigned short sel;

    /* Refuse any mode changes if we are not able to control the codec. */
    if (!codec->control_data)
        return -EUNATCH;

    if (ucontrol->value.enumerated.item[0] >= control->max)
        return -EINVAL;

    mutex_lock(&codec->mutex);

    /* Translate selection to bitmap */
    channel->output_sel = (enum Cx_OUTPUT_SEL) ucontrol->value.enumerated.item[0];

    
    switch(ucontrol->value.enumerated.item[0])
    {
        case Cx_OUTPUT_SEL_BY_GPIO:
            {
                //disable BT output.
                snd_soc_dapm_disable_pin(&codec->dapm, "BT OUT");
                //snd_soc_dapm_disable_pin(codec, "Headphone");
                snd_soc_dapm_disable_pin(&codec->dapm, "BT OUT");
                //enable analog pin 
#if defined(CONFIG_SND_CX2070X_GPIO_JACKSENSE)
							#ifdef CONFIG_GPIOLIB
               // snd_soc_jack_gpio_detect(&hs_jack_gpios[0]);
                //snd_soc_dapm_enable_pin(codec, "INT SPK");
              #else
                snd_soc_dapm_enable_pin(&codec->dapm, "INT SPK");
              #endif
#else
                snd_soc_dapm_enable_pin(&codec->dapm, "INT SPK");
#endif //if defined(CONFIG_SND_CX2070X_GPIO_JACKSENSE)
                break;
            }
        case Cx_OUTPUT_SEL_SPK:
        case Cx_OUTPUT_SEL_LINE:
            {
                snd_soc_dapm_disable_pin(&codec->dapm, "BT OUT");
                snd_soc_dapm_disable_pin(&codec->dapm, "Headphone");

                snd_soc_dapm_enable_pin(&codec->dapm, "INT SPK");
                break;
            }
        case Cx_OUTPUT_SEL_HP:
            {
                snd_soc_dapm_disable_pin(&codec->dapm, "BT OUT");
                snd_soc_dapm_disable_pin(&codec->dapm, "INT SPK");
                snd_soc_dapm_enable_pin(&codec->dapm, "Headphone");
                break;
            }
        case Cx_OUTPUT_SEL_DPORT2:
            {
                snd_soc_dapm_disable_pin(&codec->dapm, "INT SPK");
                snd_soc_dapm_disable_pin(&codec->dapm, "Headphone");
                snd_soc_dapm_enable_pin(&codec->dapm, "BT OUT");
                break;
            }
        default:;
            printk( KERN_ERR "output mode is not valid\n");
    }

    snd_soc_dapm_sync(&codec->dapm);
    mutex_unlock(&codec->mutex);
    return changed;
}

static int output_select_event_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);
    struct cx2070x_priv  *channel = get_cx2070x_priv(codec);
    ucontrol->value.enumerated.item[0] = channel->output_sel;
    return 0;
}


static const char *output_select_mode[] =
{"AUTO",  "SPK" ,"LINE",  "HP" ,"PCM2"};

static const struct soc_enum output_select_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(output_select_mode),
    output_select_mode),
};

static const char *input_select_mode[] =
{"MIC",  "PCM" };
    
static const struct soc_enum input_select_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(input_select_mode),
    input_select_mode),
};

static const struct snd_kcontrol_new input_select_controls =
SOC_DAPM_ENUM("Route", input_select_enum);

static const struct snd_kcontrol_new cx2070x_snd_controls[]=
{
    // Output
    SOC_DOUBLE_R_TLV("Master Playback Volume", DAC1_GAIN_LEFT,		DAC2_GAIN_RIGHT, 0, 74, 0,dac_tlv),
    SOC_SINGLE(  "EQ Switch",				DSP_PROCESSING_ENABLE_2,			0, 0x01, 0),
    SOC_SINGLE(  "SWC Switch",				DSP_PROCESSING_ENABLE_2,			1, 0x01, 0),
    SOC_SINGLE(  "DRC Switch",				DSP_PROCESSING_ENABLE_2,			2, 0x01, 0),
    SOC_SINGLE(  "Stage Enhancer Switch",			DSP_PROCESSING_ENABLE_2,			3, 0x01, 0),
    SOC_SINGLE(  "Loudness Switch",			DSP_PROCESSING_ENABLE_2,			4, 0x01, 0),
    SOC_SINGLE(  "DSP Mono Out Switch",			DSP_PROCESSING_ENABLE_2,			5, 0x01, 0),

    //// Input

    SOC_DOUBLE_R_TLV("Mic Pga Volume", ADC2_GAIN_LEFT,		ADC2_GAIN_RIGHT, 0, 74, 0,adc_tlv),
    SOC_SINGLE(  "Right Microphone Switch",		DSP_PROCESSING_ENABLE_1,			6, 0x01, 0),
    SOC_SINGLE(  "Inbound Noice Reduction Switch",	DSP_PROCESSING_ENABLE_1,			5, 0x01, 0),
    SOC_SINGLE(  "Mic AGC Switch",			DSP_PROCESSING_ENABLE_1,			4, 0x01, 0),
    SOC_SINGLE(  "Beam Forming Switch",			DSP_PROCESSING_ENABLE_1,			3, 0x01, 0),
    SOC_SINGLE(  "Noise Reduction Switch",		DSP_PROCESSING_ENABLE_1,			2, 0x01, 0),
    SOC_SINGLE(  "LEC Switch",				DSP_PROCESSING_ENABLE_1,			1, 0x01, 0),
    SOC_SINGLE(  "AEC Switch",				DSP_PROCESSING_ENABLE_1,			0, 0x01, 0),
    SOC_ENUM_EXT("Master Playback Switch", output_select_enum[0], output_select_event_get, output_select_event_set),
};

//For tiny alsa playback/capture/voice call path
static const char *cx2070x_playback_path_mode[] = {"OFF", "RCV", "SPK", "HP", "HP_NO_MIC", "BT", "SPK_HP", //0-6
		"RING_SPK", "RING_HP", "RING_HP_NO_MIC", "RING_SPK_HP"};//7-10

static const char *cx2070x_capture_path_mode[] = {"MIC OFF", "Main Mic", "Hands Free Mic", "BT Sco Mic"};

static const SOC_ENUM_SINGLE_DECL(cx2070x_playback_path_type, 0, 0, cx2070x_playback_path_mode);

static const SOC_ENUM_SINGLE_DECL(cx2070x_capture_path_type, 0, 0, cx2070x_capture_path_mode);


static int cx2070x_playback_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
    struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);
	struct cx2070x_priv  *cx2070x = get_cx2070x_priv(codec);

	if (!cx2070x) {
		printk("%s : cx2070x_priv is NULL\n", __func__);
		return -EINVAL;
	}

	printk("%s : playback_path %ld\n",__func__,ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = cx2070x->playback_path;

	return 0;
}

static int cx2070x_playback_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);
    struct cx2070x_priv  *cx2070x = get_cx2070x_priv(codec);
	long int pre_path;

	if (!cx2070x) {
		printk("%s : cx2070x_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (cx2070x->playback_path == ucontrol->value.integer.value[0]){
		printk("%s : playback_path is not changed!\n",__func__);
		return 0;
	}

	pre_path = cx2070x->playback_path;
	cx2070x->playback_path = ucontrol->value.integer.value[0];

	printk("%s : set playback_path %ld, pre_path %ld\n", __func__,
		cx2070x->playback_path, pre_path);

	switch (cx2070x->playback_path) {
	case OFF:
		if (pre_path != OFF) {
            cx2070x_real_read(codec,DSP_INIT);
            cx2070x_write(codec,DSP_INIT,(cx2070x_read_reg_cache(codec,DSP_INIT)| DSP_INIT_NEWC) & ~(1 << 3));
        }			
		break;
	case RCV:
		break;
	case SPK_PATH:
	case RING_SPK:
        printk("%s : >>>>>>>>>>>>>>>PUT SPK_PATH\n",__func__);
		if (pre_path == OFF) {
			cx2070x_real_read(codec,DSP_INIT);
            cx2070x_write(codec,DSP_INIT,cx2070x_read_reg_cache(codec,DSP_INIT)| DSP_INIT_NEWC | DSP_INIT_STREAM_3);
        }
		break;
	case HP_PATH:
	case HP_NO_MIC:
	case RING_HP:
	case RING_HP_NO_MIC:
		if (pre_path == OFF) {
			cx2070x_real_read(codec,DSP_INIT);
            cx2070x_write(codec,DSP_INIT,cx2070x_read_reg_cache(codec,DSP_INIT)| DSP_INIT_NEWC | DSP_INIT_STREAM_3);
        }
		break;
	case BT:
		break;
	case SPK_HP:
	case RING_SPK_HP:
	    if (pre_path == OFF) {
			cx2070x_real_read(codec,DSP_INIT);
            cx2070x_write(codec,DSP_INIT,cx2070x_read_reg_cache(codec,DSP_INIT)| DSP_INIT_NEWC | DSP_INIT_STREAM_3);
        }
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cx2070x_capture_path_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);
    struct cx2070x_priv  *cx2070x = get_cx2070x_priv(codec);

	if (!cx2070x) {
		printk("%s : cx2070x_priv is NULL\n", __func__);
		return -EINVAL;
	}

	printk("%s : capture_path %ld\n", __func__,
		ucontrol->value.integer.value[0]);

	ucontrol->value.integer.value[0] = cx2070x->capture_path;

	return 0;
}

static int cx2070x_capture_path_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);
    struct cx2070x_priv  *cx2070x = get_cx2070x_priv(codec);
	long int pre_path;

	if (!cx2070x) {
		printk("%s : cx2070x_priv is NULL\n", __func__);
		return -EINVAL;
	}

	if (cx2070x->capture_path == ucontrol->value.integer.value[0]){
		printk("%s : capture_path is not changed!\n", __func__);
		return 0;
	}

	pre_path = cx2070x->capture_path;
	cx2070x->capture_path = ucontrol->value.integer.value[0];

	printk("%s : set capture_path %ld, pre_path %ld\n", __func__,
		cx2070x->capture_path, pre_path);

	switch (cx2070x->capture_path) {
	case MIC_OFF:
		if (pre_path != MIC_OFF) {
            cx2070x_real_read(codec,DSP_INIT);
            cx2070x_write(codec,DSP_INIT,(cx2070x_read_reg_cache(codec,DSP_INIT)| DSP_INIT_NEWC) & ~(DSP_INIT_STREAM_5));
        }
		break;
	case Main_Mic:
    printk("%s : >>>>>>>>>>>>>>>PUT MAIN_MIC_PATH\n",__func__);
        if (pre_path == MIC_OFF) {
            cx2070x_real_read(codec,DSP_INIT);
            cx2070x_write(codec,DSP_INIT,cx2070x_read_reg_cache(codec,DSP_INIT)| DSP_INIT_NEWC | DSP_INIT_STREAM_5);
        }
		break;
	case Hands_Free_Mic:
		if (pre_path == MIC_OFF) {
            cx2070x_real_read(codec,DSP_INIT);
            cx2070x_write(codec,DSP_INIT,cx2070x_read_reg_cache(codec,DSP_INIT)| DSP_INIT_NEWC | DSP_INIT_STREAM_5);
        }
		break;
	case BT_Sco_Mic:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_kcontrol_new cx2070x_snd_path_controls[] = {
	SOC_ENUM_EXT("Playback Path", cx2070x_playback_path_type,
		cx2070x_playback_path_get, cx2070x_playback_path_put),

	SOC_ENUM_EXT("Capture MIC Path", cx2070x_capture_path_type,
		cx2070x_capture_path_get, cx2070x_capture_path_put),
};


// add non dapm controls
static int cx2070x_add_controls(struct snd_soc_codec *codec)
{
    INFO("%lu: %s() called\n",jiffies,__func__);

    return (snd_soc_add_codec_controls(codec, cx2070x_snd_controls, ARRAY_SIZE(cx2070x_snd_controls)));
}

static int hpportpga_event(struct snd_soc_dapm_widget *w,
struct snd_kcontrol *kcontrol, int event)
{
    unsigned int old;
    struct snd_soc_codec * codec = w->codec;
    unsigned int on = 0x1;
    old = cx2070x_read_reg_cache(codec,OUTPUT_CONTROL);
    old &=~ on;
    old &= 0xFF;
    switch (event) {
    case SND_SOC_DAPM_POST_PMU:
        cx2070x_write(codec,OUTPUT_CONTROL,old|on );
        break;
    case SND_SOC_DAPM_POST_PMD:
        cx2070x_write(codec,OUTPUT_CONTROL,old);
        break;
    }
    return 0;

}

static int lineoutpga_event(struct snd_soc_dapm_widget *w,
struct snd_kcontrol *kcontrol, int event)
{
    unsigned int val;
    struct snd_soc_codec * codec = w->codec;
    unsigned int on = (unsigned int)1<<1;
    val = cx2070x_read_reg_cache(codec,OUTPUT_CONTROL);
    val &=~ on;
    val &= 0xFF;
    switch (event) {
    case SND_SOC_DAPM_POST_PMU:
        cx2070x_write(codec,OUTPUT_CONTROL,val|on);
        break;
    case SND_SOC_DAPM_POST_PMD:
        cx2070x_write(codec,OUTPUT_CONTROL,val);
        break;
    }
    return 0;
}
static int clsdportpga_event(struct snd_soc_dapm_widget *w,
struct snd_kcontrol *kcontrol, int event)
{
    unsigned int val;
    struct snd_soc_codec * codec = w->codec;
    unsigned int on = (unsigned int)1<<2;
    val = cx2070x_read_reg_cache(codec,OUTPUT_CONTROL);
    val &=~ on;
    val &= 0xFF;
    switch (event) {
    case SND_SOC_DAPM_POST_PMU:
        cx2070x_write(codec,OUTPUT_CONTROL,val|on);
        break;
    case SND_SOC_DAPM_POST_PMD:
        cx2070x_write(codec,OUTPUT_CONTROL,val);
        break;
    }
    return 0;
}

static int lineinpga_event(struct snd_soc_dapm_widget *w,
struct snd_kcontrol *kcontrol, int event)
{
    //unsigned int val;
    //struct snd_soc_codec * codec = w->codec;
    //unsigned int on = (unsigned int)1<<2;
    //val = cx2070x_read_reg_cache(codec,OUTPUT_CONTROL);
    //val &=~ on;
    //val &= 0xFF;
    //switch (event) {
    //case SND_SOC_DAPM_POST_PMU:
    //    cx2070x_write(codec,OUTPUT_CONTROL,val|on);
    //    break;
    //case SND_SOC_DAPM_POST_PMD:
    //    cx2070x_write(codec,OUTPUT_CONTROL,val);
    //    break;
    //}
    return 0;
}

static int micportpga_event(struct snd_soc_dapm_widget *w,
struct snd_kcontrol *kcontrol, int event)
{
    //unsigned int val;
    //struct snd_soc_codec * codec = w->codec;
    //unsigned int on = (unsigned int)1<<1;
    //val = cx2070x_read_reg_cache(codec,OUTPUT_CONTROL);
    //val &=~ on;
    //val &= 0xFF;
    //switch (event) {
    //case SND_SOC_DAPM_POST_PMU:
    //    cx2070x_write(codec,OUTPUT_CONTROL,val|on);
    //    break;
    //case SND_SOC_DAPM_POST_PMD:
    //    cx2070x_write(codec,OUTPUT_CONTROL,val);
    //    break;
    //}
    return 0;
}

static int aout_pga_event(struct snd_soc_dapm_widget *w,
struct snd_kcontrol *kcontrol, int event)
{
    unsigned int val;
    struct snd_soc_codec * codec = w->codec;
    unsigned int on   = (unsigned int)1<<7; //steam 7
    unsigned int reg  = DSP_INIT;
    val = cx2070x_read_reg_cache(codec,reg);
    val &=~ on;
    val &= 0xFF;
    switch (event) {
    case SND_SOC_DAPM_POST_PMU:
        cx2070x_write(codec,STREAMOP_ROUTING,0x60 );  // scal_out
        cx2070x_write(codec,STREAM7_SOURCE,5 );  //Scale_out
        cx2070x_write(codec,reg,val|on );
        break;
    case SND_SOC_DAPM_POST_PMD:
        cx2070x_write(codec,STREAM7_SOURCE,0 );  //disconnect
        cx2070x_write(codec,reg,val);
        break;
    }
    return 0;
}

static int dout_pga_event(struct snd_soc_dapm_widget *w,
struct snd_kcontrol *kcontrol, int event)
{
    unsigned int val;
    struct snd_soc_codec * codec = w->codec;
    unsigned int on   = (unsigned int)1<<4;
    unsigned int reg  = DSP_INIT;
    val = cx2070x_read_reg_cache(codec,reg);
    val &=~ on;
    val &= 0xFF;
    switch (event) {
    case SND_SOC_DAPM_POST_PMU:
        cx2070x_write(codec,STREAMOP_ROUTING,0x60 );  // scal_out
        snd_soc_update_bits(codec, MIC_CONTROL, 3, 1);
        //cx2070x_write(codec,STREAM6_ROUTING,5 );  // scal_out
        cx2070x_write(codec,reg,val|on );
        break;
    case SND_SOC_DAPM_POST_PMD:
        cx2070x_write(codec,STREAMOP_ROUTING,0x62 );  // scal_out
        cx2070x_write(codec,STREAM6_ROUTING,0 );  //disconnect
        cx2070x_write(codec,reg,val);
        break;
    }
    return 0;
}

static int ain_pga_event(struct snd_soc_dapm_widget *w,
struct snd_kcontrol *kcontrol, int event)
{
    unsigned int val;
    struct snd_soc_codec * codec = w->codec;
    unsigned int on   = (unsigned int)1<<2; //steam 2
    unsigned int reg  = DSP_INIT;
    val = cx2070x_read_reg_cache(codec,reg);
    val &=~ on;
    val &= 0xFF;
    switch (event) {
    case SND_SOC_DAPM_POST_PMU:
        //cx2070x_write(codec,VOICE_IN_SOURCE, 0x2 );  //stream 2 -> Voice In
        cx2070x_write(codec,reg,val|on );
        break;
    case SND_SOC_DAPM_POST_PMD:
        cx2070x_write(codec,reg,val);
        break;
    }
    return 0;
}

static int din_pga_event(struct snd_soc_dapm_widget *w,
struct snd_kcontrol *kcontrol, int event)
{
    unsigned int val;
    struct snd_soc_codec * codec = w->codec;
    unsigned int on   = (unsigned int)1<<4; //stream 4
    unsigned int reg  = DSP_INIT;
    val = cx2070x_read_reg_cache(codec,reg);
    val &=~ on;
    val &= 0xFF;
    switch (event) {
    case SND_SOC_DAPM_POST_PMU:
        cx2070x_write(codec,VOICE_IN_SOURCE, 0x4 ); //stream 4 -> Voice In
        cx2070x_write(codec,reg,val|on );
        break;
    case SND_SOC_DAPM_POST_PMD:
        cx2070x_write(codec,reg,val);
        break;
    }
    return 0;
}

static int adc_pga_event(struct snd_soc_dapm_widget *w,
struct snd_kcontrol *kcontrol, int event)
{
    unsigned int val;
    struct snd_soc_codec * codec = w->codec;
    unsigned int on   = (unsigned int)1<<5; //stream 5
    unsigned int reg  = DSP_INIT;
    val = cx2070x_read_reg_cache(codec,reg);
    val &=~ on;
    val &= 0xFF;
    switch (event) {
    case SND_SOC_DAPM_POST_PMU:
        cx2070x_write(codec,reg,val|on );
        break;
    case SND_SOC_DAPM_POST_PMD:
        cx2070x_write(codec,reg,val);
        break;
    }
    return 0;
}

static const struct snd_soc_dapm_widget cx2070x_dapm_widgets[]=
{

    //Playback 
    SND_SOC_DAPM_DAC(	"DAC",		"Playback",	DSP_INIT,3,0), //stream 3

    SND_SOC_DAPM_PGA_E("AOUT PGA", SND_SOC_NOPM,
    0, 0, NULL, 0, aout_pga_event,
    SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),

    SND_SOC_DAPM_PGA_E("DOUT PGA", SND_SOC_NOPM,
    0, 0, NULL, 0, dout_pga_event,
    SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),

    SND_SOC_DAPM_PGA_E("HP Port", SND_SOC_NOPM,
    0, 0, NULL, 0, hpportpga_event,
    SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),

    SND_SOC_DAPM_PGA_E("CLASSD Port", SND_SOC_NOPM,
    0, 0, NULL, 0, clsdportpga_event,
    SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),

    SND_SOC_DAPM_PGA_E("LINEOUT Port", SND_SOC_NOPM,
    0, 0, NULL, 0, lineoutpga_event,
    SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),

    //Output Pin.
    SND_SOC_DAPM_OUTPUT("SPK OUT"),
    SND_SOC_DAPM_OUTPUT("LINE OUT"),
    SND_SOC_DAPM_OUTPUT("HP OUT"),
    SND_SOC_DAPM_OUTPUT("PCM OUT"),

    //
    // Captuer
    //
    SND_SOC_DAPM_ADC_E("ADC", "Capture", SND_SOC_NOPM,
	0, 0, adc_pga_event,
	SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),


    SND_SOC_DAPM_PGA_E("AIN PGA", SND_SOC_NOPM,
    0, 0, NULL, 0, ain_pga_event,
    SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),

    SND_SOC_DAPM_PGA_E("DIN PGA", SND_SOC_NOPM,
    0, 0, NULL, 0, din_pga_event,
    SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),

    SND_SOC_DAPM_PGA_E("MIC Port", SND_SOC_NOPM,
    0, 0, NULL, 0, micportpga_event,
    SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),

    SND_SOC_DAPM_PGA_E("LINEIN Port", SND_SOC_NOPM,
    0, 0, NULL, 0, lineinpga_event,
    SND_SOC_DAPM_POST_PMU|SND_SOC_DAPM_POST_PMD),

    //DECLARE_TLV_DB_SCALE

    SND_SOC_DAPM_INPUT("MIC IN"),
    SND_SOC_DAPM_INPUT("PCM IN"),
    SND_SOC_DAPM_INPUT("LINE IN"),

    SND_SOC_DAPM_MUX("Capture Source", SND_SOC_NOPM, 0, 0,
        &input_select_controls),

    SND_SOC_DAPM_MICBIAS("Mic Bias",MIC_CONTROL,3,0),
#if 0
    //machina layer.
    SND_SOC_DAPM_MIC("INT MIC", NULL),
    SND_SOC_DAPM_MIC("BT IN", NULL),
    SND_SOC_DAPM_HP("BT OUT", NULL),
    SND_SOC_DAPM_SPK("INT SPK", NULL),
    SND_SOC_DAPM_HP("Headphone", NULL),
#endif
};

static const struct snd_soc_dapm_route cx2070x_routes[] = 
{
    //playback.
    { "AOUT PGA",           NULL,"DAC"          },
    { "DOUT PGA",           NULL,"DAC"          },

    { "HP Port",            NULL,"AOUT PGA"     },
    { "LINEOUT Port",       NULL,"AOUT PGA"     },
    { "CLASSD Port",        NULL,"AOUT PGA"     },

    { "HP OUT",             NULL,"HP Port"      },
    { "LINE OUT",           NULL,"LINEOUT Port" },
    { "SPK OUT",            NULL,"CLASSD Port"  },
    { "PCM OUT",            NULL,"DOUT PGA"     },

    //capture. 
    { "ADC",                NULL,"Capture Source"       },

    { "Capture Source",             "MIC","AIN PGA"     },
    { "Capture Source",             "PCM","DIN PGA"     },

    { "AIN PGA",            NULL,"MIC Port"     },
   // { "MIC Port",           NULL,"Mic Bias"     },
    { "MIC Port",           NULL,"MIC IN"     },

  //  { "Mic Bias",           NULL,"MIC IN"       },
    { "DIN PGA",            NULL,"PCM IN"       },
#if 0
    //machina layer.
    { "Headphone",          NULL,"HP OUT"       },
    { "INT SPK",            NULL,"SPK OUT"      },
    { "BT OUT",             NULL,"PCM OUT"      },
    { "MIC IN",             NULL,"INT MIC"       },
    { "PCM IN",             NULL,"BT IN"         },
#endif
};

static int cx2070x_add_widgets(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	
    INFO("%lu: %s() called\n",jiffies,__func__);

    snd_soc_dapm_new_controls(dapm, cx2070x_dapm_widgets, ARRAY_SIZE(cx2070x_dapm_widgets));
    snd_soc_dapm_add_routes(dapm, cx2070x_routes, ARRAY_SIZE(cx2070x_routes));
    return 0;
}

static int cx2070x_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)

{
    struct snd_soc_codec *codec  = dai->codec;
    unsigned int s5,s3,i2s,dsp;
#define _3_3_f_f_5 1
#define _1_1_7_7_0 0
    int err =0;

    ERROR("%lu: %s() called\n",jiffies,__func__);
    ERROR("\tformat:%u speed:%u\n",params_format(params),params_rate(params));
    
    switch(params_format(params))
    {
    case SNDRV_PCM_FORMAT_S16_LE: s5=STREAM5_SAMPLE_16_LIN; s3=STREAM5_SAMPLE_16_LIN; i2s=_3_3_f_f_5; break;
    case SNDRV_PCM_FORMAT_S16_BE: s5=STREAM5_SAMPLE_16_LIN; s3=STREAM5_SAMPLE_16_LIN; i2s=_3_3_f_f_5; break;
    case SNDRV_PCM_FORMAT_MU_LAW: s5=STREAM5_SAMPLE_U_LAW;  s3=STREAM5_SAMPLE_U_LAW;  i2s=_1_1_7_7_0; break;
    case SNDRV_PCM_FORMAT_A_LAW:  s5=STREAM5_SAMPLE_A_LAW;  s3=STREAM5_SAMPLE_A_LAW;  i2s=_1_1_7_7_0; break;
    default:
        printk(KERN_ERR "cx2070x: unsupported PCM format 0x%u\n",params_format(params));
        return -EINVAL;
    }

    switch(params_rate(params))
    {
    case  8000:	s5|=			  STREAM5_RATE_8000;
        s3|=STREAM3_STREAM_STEREO|STREAM3_RATE_8000;  break;
    case 11025:	s5|=			  STREAM5_RATE_11025;
        s3|=STREAM3_STREAM_STEREO|STREAM3_RATE_11025; break;
    case 16000:	s5|=			  STREAM5_RATE_16000;
        s3|=STREAM3_STREAM_STEREO|STREAM3_RATE_16000; break;
    case 22050:	s5|=			  STREAM5_RATE_22050;
        s3|=STREAM3_STREAM_STEREO|STREAM3_RATE_22050; break;
    case 24000:	s5|=			  STREAM5_RATE_24000;
        s3|=STREAM3_STREAM_STEREO|STREAM3_RATE_24000; break;
    case 32000:	s5|=			  STREAM5_RATE_32000;
        s3|=STREAM3_STREAM_STEREO|STREAM3_RATE_32000; break;
    case 44100:	s5|=			  STREAM5_RATE_44100;
        s3|=STREAM3_STREAM_STEREO|STREAM3_RATE_44100; break;
    case 48000:	s5|=			  STREAM5_RATE_48000;
        s3|=STREAM3_STREAM_STEREO|STREAM3_RATE_48000; break;
    case 88200:	s5|=			  STREAM5_RATE_88200;
        s3|=STREAM3_STREAM_STEREO|STREAM3_RATE_88200; break;
    case 96000:	s5|=			  STREAM5_RATE_96000;
        s3|=STREAM3_STREAM_STEREO|STREAM3_RATE_96000; break;
    default:
        printk(KERN_ERR "cx2070x: unsupported rate  %d\n",params_rate(params));
	 return -EINVAL;
    }

    cx2070x_real_write(codec,PORT1_TX_CLOCKS_PER_FRAME_PHASE,i2s?0x07:0x01);
    cx2070x_real_write(codec,PORT1_RX_CLOCKS_PER_FRAME_PHASE,i2s?0x07:0x01);
    cx2070x_real_write(codec,PORT1_TX_SYNC_WIDTH,            i2s?0x0f:0x07);
    cx2070x_real_write(codec,PORT1_RX_SYNC_WIDTH,            i2s?0x0f:0x07);
    cx2070x_real_write(codec,PORT1_CONTROL_2,                i2s?0x05:0x00);
    /*params for port2 by showy.zhang*/
    cx2070x_real_write(codec,STREAM5_RATE,s5);
    cx2070x_real_write(codec,STREAM3_RATE,s3);// cause by incorrect parameter
#if 0
    cx2070x_real_read(codec,DSP_INIT);
    dsp=cx2070x_read_reg_cache(codec,DSP_INIT);
    printk(">>>>>>>>>>>> dsp = %0x", dsp);
    if ((err=cx2070x_dsp_init(codec,dsp|DSP_INIT_NEWC))<0)
        return err;
#endif
    return 0;
}

static int cx2070x_mute(struct snd_soc_dai *dai, int mute)
{
    struct snd_soc_codec *codec = dai->codec;

    ERROR("%lu: %s(%d) called\n",jiffies,__func__,mute);
#if 0
    cx2070x_real_write(codec,VOLUME_MUTE,mute?VOLUME_MUTE_ALL:b_00000000);

    if( mute)
    {
//          cx2070x_real_write(codec,DSP_POWER,0xe0); // deep sleep mode
//          cx2070x_dsp_init(codec,DSP_INIT_STREAM_OFF);
        /*mute I2S output*/
        cx2070x_real_read(codec,DSP_INIT);
        cx2070x_write(codec,DSP_INIT,(cx2070x_read_reg_cache(codec,DSP_INIT)| DSP_INIT_NEWC) & ~(1 << 3));
    }
    else
    {
        /*unmute I2S output*/
        cx2070x_real_read(codec,DSP_INIT);
        cx2070x_write(codec,DSP_INIT,cx2070x_read_reg_cache(codec,DSP_INIT)| DSP_INIT_NEWC | DSP_INIT_STREAM_3);
        //cx2070x_dsp_init(codec,DSP_INIT_NEWC | 0xff);
    }
#endif
    return 0;
}

static int cx2070x_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id, unsigned int freq, int dir)
{
    struct snd_soc_codec *codec = dai->codec;
    struct cx2070x_priv  *channel = get_cx2070x_priv(codec);

    INFO("%lu: %s() called\n",jiffies,__func__);

    // sysclk is not used where, but store it anyway
    channel->sysclk = freq;
    return 0;
}

static int cx2070x_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
    struct snd_soc_codec *codec = dai->codec;
    struct cx2070x_priv *channel = get_cx2070x_priv(codec);

    INFO("%lu: %s() called\n",jiffies,__func__);

    // set master/slave audio interface
    switch (fmt & SND_SOC_DAIFMT_MASTER_MASK)
    {
    case SND_SOC_DAIFMT_CBS_CFS:	// This design only supports slave mode
        channel->master = 0;
        break;
    default:
	printk(KERN_ERR "unsupport DAI format, driver only supports slave mode\n");
        return -EINVAL;
    }

    switch (fmt & SND_SOC_DAIFMT_INV_MASK)
    {
    case SND_SOC_DAIFMT_NB_NF:		// This design only supports normal bclk + frm
        break;
    default:
	printk(KERN_ERR "unsupport DAI format, driver only supports normal bclk+ frm\n");
        return -EINVAL;
    }

    // interface format
    switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK)
    {
    case SND_SOC_DAIFMT_I2S:		// This design only supports I2S
        break;
    default:
	printk( KERN_ERR "unspoort DAI format, driver only supports I2S interface.\n");
        return -EINVAL;
    }

    return 0;
}

struct snd_soc_dai_ops cx2070x_dai_ops = 
{
    .set_sysclk=cx2070x_set_dai_sysclk,
    .set_fmt = cx2070x_set_dai_fmt,
    .digital_mute=cx2070x_mute,
    .hw_params = cx2070x_hw_params,
};

struct snd_soc_dai_driver soc_codec_cx2070x_dai = 
{
    .name = "cx2070x-hifi",
    .ops = &cx2070x_dai_ops,
    .capture = {
        .stream_name="Capture",
            .formats = CX2070X_FORMATS,
            .rates = CX2070X_RATES,
            .channels_min = 1,
            .channels_max = 2,
    },
    .playback= {
	  .stream_name="Playback",
            .formats = CX2070X_FORMATS,
            .rates = CX2070X_RATES,
            .channels_min = 1,
            .channels_max = 2,
        },
	.symmetric_rates = 1,
};
EXPORT_SYMBOL_GPL(soc_codec_cx2070x_dai);

static int cx2070x_set_bias_level(struct snd_soc_codec *codec, enum snd_soc_bias_level level)
{
    INFO("%lu: %s(,%d) called\n",jiffies,__func__,level);

    switch (level)
    {
        // Fully on
    case SND_SOC_BIAS_ON:
        cx2070x_real_write(codec,DSP_POWER,0x20);
        // all power is driven by DAPM system
        break;

        // Partial on
    case SND_SOC_BIAS_PREPARE:
        break;

        // Off, with power
    case SND_SOC_BIAS_STANDBY:
        //cx2070x_real_write(codec,VOLUME_MUTE,VOLUME_MUTE_ALL); // mute all
        //cx2070x_real_write(codec,DSP_POWER,0xe0); // deep sleep mode
        //cx2070x_dsp_init(codec,DSP_INIT_STREAM_OFF);
       
        // TODO: power down channel
        break;

        // Off, without power
    case SND_SOC_BIAS_OFF:
        cx2070x_real_write(codec,VOLUME_MUTE,VOLUME_MUTE_ALL); // mute all
        cx2070x_real_write(codec,DSP_POWER,0xe0); // deep sleep mode
        cx2070x_dsp_init(codec,DSP_INIT_STREAM_OFF);
        // TODO: put channel into deep-sleep
        break;
    }

    return 0;
}

int I2cWrite( struct snd_soc_codec *codec, unsigned char ChipAddr, unsigned long cbBuf, unsigned char* pBuf)
{
#ifdef USING_SPI
    struct spi_device * spi =   (struct spi_device *) codec->control_data;
    pBuf[0] |= 0x80; //SPI_WRITE
    spi_write(spi,pBuf,cbBuf);
    return true;
#else //#ifdef ENABLE_SPI
    struct i2c_client  *client = (struct i2c_client  *)codec->control_data;
    struct i2c_adapter *adap   = client->adapter;
    struct i2c_msg      msg[1];


    msg[0].addr  = client->addr;
    msg[0].flags = client->flags & I2C_M_TEN;
    msg[0].buf   = pBuf;
    msg[0].len   = cbBuf;
    if (i2c_transfer(adap,msg,1)!=1)
    {
        printk(KERN_ERR "cx2070x: I2cWriteThenRead failed.\n");

        return 0;
    }
    else
    {
        return 1;
    }
#endif //#!ENABLE_SPI
}

int I2cWriteThenRead( struct snd_soc_codec *codec, unsigned char ChipAddr, unsigned long cbBuf,
    unsigned char* pBuf, unsigned long cbReadBuf, unsigned char*pReadBuf)
{
    
#ifdef USING_SPI
    u8  reg[3];
    struct spi_device * spi =   (struct spi_device *) codec->control_data;
    reg[0] = pBuf[0];
    reg[1] = pBuf[1];
    reg[2] = 0;
    spi_write_then_read(spi, reg, 3, pReadBuf,cbReadBuf);
    return true;
#else //#ifdef USING_SPI
    struct i2c_client  *client = (struct i2c_client  *)codec->control_data;
    struct i2c_adapter *adap   = client->adapter;
    struct i2c_msg      msg[2];

    msg[0].addr  = client->addr;
    msg[0].flags = client->flags & I2C_M_TEN;
    msg[0].len   = cbBuf;
    msg[0].buf   = pBuf;

    msg[1].addr  = client->addr;
    msg[1].flags = (client->flags & I2C_M_TEN) | I2C_M_RD;
    msg[1].len   = cbReadBuf;
    msg[1].buf   = pReadBuf;

    if (i2c_transfer(adap,msg,2)!=2)
    {
        printk(KERN_ERR "cx2070x: I2cWriteThenRead failed.\n");
        return 0;
    }
    else 
    {
        return 1;
    }
#endif //#!ENABLE_SPI
}


#if defined(CONFIG_SND_CX2070X_LOAD_FW)
static int cx2070x_apply_firmware_patch(struct snd_soc_codec *codec)
{
	int ret = 0;
    const struct firmware *fw = NULL;
    const unsigned char *dsp_code  = NULL;
    struct device *dev = codec->dev;	

#if defined(CONFIG_SND_CX2070X_USE_FW_H)
    // load firmware from c head file.
    dsp_code = ChannelFW;
#else
    // load firmware from file.
    ret = request_firmware(&fw, CX2070X_FIRMWARE_FILENAME, dev); 
    if (ret < 0) {
        printk( KERN_ERR "%s(): Firmware %s not available %d",
					__func__, CX2070X_FIRMWARE_FILENAME, ret);
		return ret;
    }
    dsp_code = fw->data;
#endif
    
    ret = ApplyDSPChanges(dsp_code);
    if (ret) {
        printk(KERN_ERR "cx2070x: patch firmware failed, Error %d\n", ret);
    } else {
        printk(KERN_INFO "cx2070x: patch firmware successfully.\n");	
    }
    
	return ret;
}

static int cx2070x_download_firmware(struct snd_soc_codec        *codec)
{
    int 			ret 	   = 0;
    char 			*buf       = NULL;
    const struct firmware       *fw        = NULL;
    const unsigned char         *dsp_code  = NULL;
#if !defined(CONFIG_SND_CX2070X_USE_FW_H)
    struct device	        *dev       = codec->dev;	
#endif 

    // load firmware to memory.
#if defined(CONFIG_SND_CX2070X_USE_FW_H)
    // load firmware from c head file.
    dsp_code = ChannelFW;
#else
    // load firmware from file.
    ret = request_firmware(&fw, CX2070X_FIRMWARE_FILENAME,dev); 
    if( ret < 0)
    {
        printk( KERN_ERR "%s(): Firmware %s not available %d",__func__,CX2070X_FIRMWARE_FILENAME,ret);
	goto LEAVE;
    }
    dsp_code = fw->data;
#endif // #if defined(CONFIG_SND_CX2070X_USE_FW_H)
    //
    // Load rom data from a array.
    //
    buf = (char*)kzalloc(0x200,GFP_KERNEL);
    if (buf  == NULL)
    {
        printk(KERN_ERR "cx2070x: out of memory .\n");
        ret = -ENOMEM;
        goto LEAVE;
    }

    //
    // Setup the i2c callback function.
    //
    SetupI2cWriteCallback( (void *) codec, (fun_I2cWrite) I2cWrite,32);
    SetupI2cWriteThenReadCallback( (void *) codec, (fun_I2cWriteThenRead) I2cWriteThenRead); 

    // download
    SetupMemoryBuffer(buf);
    
    ret = DownloadFW(dsp_code);
    if(ret)
    {
        printk(KERN_ERR "cx2070x: download firmware failed, Error %d\n",ret);
    }
    else
    {
        printk(KERN_INFO "cx2070x: download firmware successfully.\n");	
    }
    if (buf)
    {
        kfree(buf);
    }
LEAVE:

#if defined(CONFIG_SND_CX2070X_LOAD_FW) && !defined(CONFIG_SND_CX2070X_USE_FW_H)
    if(fw)
    {
        release_firmware(fw);
    }
#endif 
    return ret;

}
#endif

unsigned int cx2070x_hw_read( struct snd_soc_codec *codec, unsigned int regaddr)
{
    unsigned char data;
    unsigned char chipaddr = 0;
    unsigned char reg[2];
#ifdef USING_I2C
    struct i2c_client  *client = (struct i2c_client  *) codec->control_data;
    chipaddr = client->addr;
#endif
    reg[0] = regaddr>>8;
    reg[1] = regaddr&0xff;
    I2cWriteThenRead(codec,chipaddr, 2, reg, 1,&data);
    return (unsigned int)data;
}

unsigned int (*hw_read)(struct snd_soc_codec *, unsigned int);

//
// Initialise the cx2070x  driver
// Register the mixer and dsp interfaces with the kernel
//
static int NOINLINE cx2070x_init(struct snd_soc_codec* codec)
{
    struct cx2070x_priv     *cx2070x = get_cx2070x_priv(codec);
    int                   n,vl,vh,vm,fh, fl,ret = 0;
    cx2070x_reg_t	       *reg_cache;

    printk(">>>>>>>%s",__func__);
    INFO("%lu: %s() called\n",jiffies,__func__);

    codec->control_data = cx2070x->control_data;


#if defined(CONFIG_SND_CX2070X_GPIO_RESET)
    cx2070x_reset_device();
#endif 

#if CX2070X_TRISTATE_EEPROM
    // If CX2070X  has to float the pins to the NVRAM, enable this code
    for(;;)
    {
        int pad,pbd;
        pad=cx2070x_real_read(codec,PAD);
        pbd=cx2070x_real_read(codec,PBD);
        printk("%s(): PAD/PBD=%02x/%02x\n",__func__,pad,pbd);

        _cx2070x_real_write(codec,PAD,pad&~(1<<4));
        _cx2070x_real_write(codec,PBD,pbd&~(1<<6));
        msleep(1000);
    }
#endif


#if defined(CONFIG_SND_CX2070X_LOAD_FW)
    ret = cx2070x_download_firmware(codec);
    if( ret < 0)
    {
	printk(KERN_ERR "%s: failed to download firmware\n",__func__);
	return ret;
    }

#endif
    // Verify that Channel/Balboa is ready.
    // May have to wait for ~5sec becore Channel/Balboa comes out of reset
    for(n=5000;!bNoHW;)
    {
        int abcode=cx2070x_real_read(codec,ABORT_CODE);
     //   int abcode=cx2070x_real_read(codec,CHIP_VERSION);
        printk(">>>>>>>>>>>>>>>%s abcode = %d",__func__, abcode);
        if (abcode==0x01)
            break;  // initialization done!
        if (--n==0)
        {
            printk(KERN_ERR "Timeout waiting for cx2070x to come out of reset!\n");
            return -EIO;
        }
        msleep(1);
    }

    cx2070x_real_read(codec,FIRMWARE_VERSION);
    cx2070x_real_read(codec,PATCH_VERSION);
    cx2070x_real_read(codec,CHIP_VERSION);
    cx2070x_real_read(codec,RELEASE_TYPE);

    reg_cache = GET_REG_CACHE(codec);
    fl=(reg_cache[FIRMWARE_VERSION]>>0)&0xFF;
    fl=(fl>>4)*10+(fl&0xf);
    fh=(reg_cache[FIRMWARE_VERSION]>>8)&0xFF;
    
    // determine whether the codec is ROM version or not.
    if( fh == 5)
    {   //firmware 5.x
	//shows the firmware patch version.
        cx2070x_real_read(codec,ROM_PATCH_VER_HB);
        cx2070x_real_read(codec,ROM_PATCH_VER_MB);
        cx2070x_real_read(codec,ROM_PATCH_VER_LB);
        vh =  reg_cache[ROM_PATCH_VER_HB];
        vm =  reg_cache[ROM_PATCH_VER_MB];
        vl =  reg_cache[ROM_PATCH_VER_LB];
        printk("cx2070x: firmware version %u.%u, patch %u.%u.%u, chip CX2070%u (ROM)\n",fh,fl,vh,vm,vl,reg_cache[CHIP_VERSION]);
    }
    else if( fh == 4)
    {
        //firmware 4.x
        printk("cx2070x: firmware version %u.%u,  chip CX2070%u (RAM), ",fh,fl,reg_cache[CHIP_VERSION]);
        // shows the firmware release type.
    	switch(reg_cache[RELEASE_TYPE])
    	{
    	case 12: printk("Custom Release\n"); break;
    	case 14: printk("Engineering Release\n"); break;
    	case 15: printk("Field Release\n"); break;
    	default: printk("Release %u?\n",reg_cache[RELEASE_TYPE]); break;
    	}
    }
    else
    {
        printk("cx2070x: Unsupported firmware version %u.%u!!!\n",fh,fl);
        ret = -EINVAL;
	goto card_err;
    }
    

    if (reg_cache[PATCH_VERSION])
    {
      	vl=(reg_cache[PATCH_VERSION]>>0)&0xFF;
      	vh=(reg_cache[PATCH_VERSION]>>8)&0xFF;
       	printk("%s(): CX2070X patch version %u.%u\n",__func__,vh,vl);
    }

    // Initialize the CX2070X regisers and/or read them as needed.
    for(n=0;n<noof(cx2070x_regs);n++)
        switch(cx2070x_regs[n].type&REG_TYPE_MASK)
    {
        case REG_TYPE_RO:
        case REG_TYPE_RW:
            cx2070x_real_read(codec,n);
            break;
        case REG_TYPE_WI:
        case REG_TYPE_WC:
            cx2070x_real_write(codec,n,cx2070x_data[n]);
            break;
#if defined(REG_TYPE_DM)
        case REG_TYPE_DM:
            break;
#endif
        default:
            snd_BUG();
    }
    
#if defined(CONFIG_SND_CX2070X_LOAD_FW)
    cx2070x_apply_firmware_patch(codec);
#endif

    //cx2070x_add_controls(codec);
    //cx2070x_add_widgets(codec);
    snd_soc_add_codec_controls(codec, cx2070x_snd_path_controls,
				ARRAY_SIZE(cx2070x_snd_path_controls));

    //snd_soc_dapm_nc_pin(&codec->dapm, "LINE IN");
    //snd_soc_dapm_nc_pin( &codec->dapm, "LINE OUT");
    //snd_soc_dapm_enable_pin( &codec->dapm, "INT MIC");
    //snd_soc_dapm_enable_pin( &codec->dapm, "INT SPK");
    //snd_soc_dapm_disable_pin( &codec->dapm, "BT IN");
    //snd_soc_dapm_enable_pin( &codec->dapm, "Headphone");
    //snd_soc_dapm_disable_pin( &codec->dapm, "BT OUT");


#if defined(CONFIG_SND_CX2070X_GPIO_JACKSENSE)
    /* Headset jack detection */
    ret = snd_soc_jack_new(codec, "Headset Jack",
        SND_JACK_HEADSET, &hs_jack);
    if (ret)    
    {
        printk(KERN_ERR "CX2070X: failed to register Headset Jack\n");
        goto card_err;
    }

    ret = snd_soc_jack_add_gpios(&hs_jack, ARRAY_SIZE(hs_jack_gpios),
        hs_jack_gpios);
    if (ret)    
    {
        printk(KERN_ERR "CX2070X: failed to add jack gpios.\n");
        goto card_err;
    }
    
    ret = snd_soc_jack_add_pins(&hs_jack, ARRAY_SIZE(hs_jack_pins),
        hs_jack_pins);
    if (ret)    
    {
        printk(KERN_ERR "CX2070X: failed to add soc jack pin\n");
        goto card_err;
    }
#else
     snd_soc_dapm_sync( &codec->dapm);
#endif //#if defined(CONFIG_SND_CX2070X_GPIO_JACKSENSE)

#if defined(CONFIG_SND_CXLIFEGUARD)
    cxdbg_dev_init(codec);
#endif 

    cx2070x_real_write(codec, USB_LOCAL_VOLUME, 0x42);

    if( ret == 0)
    {
        printk(KERN_INFO "CX2070X: codec is ready.\n");
    }
    return ret;

card_err:
    return ret;
}

static int cx2070x_hw_reset(void)
{
	int err;
	/* Reset */
	err = gpio_request(CODEC_RESET_GPIO_PIN, "nCX2070X_Reset");
        printk(KERN_ERR "channel reset gpio=%d\n", CODEC_RESET_GPIO_PIN);
	if (err)
		printk(KERN_ERR "#### failed to request GPH3_2 for Audio Reset\n");

	gpio_direction_output(CODEC_RESET_GPIO_PIN, 0);
	msleep(150);
	gpio_direction_output(CODEC_RESET_GPIO_PIN, 1);
	gpio_free(CODEC_RESET_GPIO_PIN);
	msleep(150);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
static struct snd_soc_codec *g_cx2070x_codec;

static int cx2070x_dbg_show_regs(struct seq_file *s, void *unused)
{
	
	int reg_no = (int)s->private;
	int i = 0;
    int source_switch = 0;
    if(reg_no == 0x4321) {
        cx2070x_real_read(g_cx2070x_codec, DSP_INIT); 
        source_switch = cx2070x_read_reg_cache(g_cx2070x_codec,DSP_INIT) & DSP_ENABLE_STREAM_3_4;
        printk(">>>>>>>>>>>>source_switch = %0x",source_switch);
        switch (source_switch) {
			case DSP_NO_SOURCE:
				seq_printf(s, "NO_INPUT\t");
				break;
			case DSP_ENABLE_STREAM_3:
				seq_printf(s, "I2S_ONLY\t");
				break;
			case DSP_ENABLE_STREAM_4:
				seq_printf(s, "USB_ONLY\t");
				break;
			case DSP_ENABLE_STREAM_3_4:
				seq_printf(s, "I2S_USB_MIXING\t");
				break;
			default:
				seq_printf(s, "UNKNOWN\t");
			}
        return 0;
    }
	
	if (reg_no == noof(cx2070x_regs)) {
		seq_printf(s, "Offset\tType\tValue\tName\n");
		for (i = 0; i < reg_no; i++) {
			seq_printf(s, "0x%02X\t", cx2070x_regs[i].addr);
			switch (cx2070x_regs[i].type) {
			case REG_TYPE_RO:
				seq_printf(s, "R");
				break;
			case REG_TYPE_RW:
				seq_printf(s, "RW");
				break;
			case REG_TYPE_WC:
				seq_printf(s, "WC");
				break;
			case REG_TYPE_WI:
				seq_printf(s, "WI");
				break;
			default:
				seq_printf(s, "UNKNOWN\t");
			}
			seq_printf(s, "\t0x%02X\t%s\n", cx2070x_read_reg_cache(g_cx2070x_codec, i),
											cx2070x_regs[i].name);
		}
		return 0;
	}
	
	seq_printf(s, "Offset:\t0x%02X\n", cx2070x_regs[reg_no].addr);
	
	seq_printf(s, "Type:\t");
	switch (cx2070x_regs[reg_no].type) {
	case REG_TYPE_RO:
		seq_printf(s, "R");
		break;
	case REG_TYPE_RW:
		seq_printf(s, "RW");
		break;
	case REG_TYPE_WC:
		seq_printf(s, "WC");
		break;
	case REG_TYPE_WI:
		seq_printf(s, "WI");
		break;
	default:
		seq_printf(s, "UNKNOWN");
	}
	seq_printf(s, "\n");
	
	seq_printf(s, "Value:\t0x%02X\n", cx2070x_read_reg_cache(g_cx2070x_codec, reg_no));

	return 0;
}

static int cx2070x_dbg_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file, cx2070x_dbg_show_regs, inode->i_private);
}

static ssize_t cx2070x_dbg_reg_write(struct file *file,
								const char __user *ubuf,                                                                        
                                size_t count, loff_t *off)
{
	struct seq_file *seq = file->private_data;
	char buf[8];
	unsigned long val;
	int reg_no = (int)seq->private;
	int ret = 0;

	if (count >= sizeof(buf)) {
		ERROR("%s, The buffer is not enough.\n", __func__);
		return -EINVAL;
	} if (copy_from_user(buf, ubuf, count)) {
		ERROR("%s, Faied to copy data from user space.\n", __func__);
		return -EFAULT;
	}
		
	buf[count] = 0;
	
	ret = strict_strtoul(buf, 16, &val);
	if (ret < 0) {
		ERROR("%s, Failed to convert a string to an unsinged long integer.\n", __func__);
		return ret;
	}

    if(reg_no == 0x4321) {
         printk(">>>>>>>>>>>>>>>>>>>>>>val = %d",val);
         cx2070x_real_read(g_cx2070x_codec, DSP_INIT); 
         switch (val) {
			case NO_INPUT:
                cx2070x_write(g_cx2070x_codec,DSP_INIT,(cx2070x_read_reg_cache(g_cx2070x_codec,DSP_INIT)|
                    DSP_INIT_NEWC) & ~DSP_ENABLE_STREAM_3_4);
                break;
			case I2S_ONLY:
                cx2070x_write(g_cx2070x_codec,DSP_INIT,(cx2070x_read_reg_cache(g_cx2070x_codec,DSP_INIT)| 
                    DSP_INIT_NEWC | DSP_ENABLE_STREAM_3) & ~DSP_ENABLE_STREAM_4);
				break;
			case USB_ONLY:
                cx2070x_write(g_cx2070x_codec,DSP_INIT,(cx2070x_read_reg_cache(g_cx2070x_codec,DSP_INIT)| 
                    DSP_INIT_NEWC | DSP_ENABLE_STREAM_4) & ~DSP_ENABLE_STREAM_3);
				break;
			case I2S_USB_MIXING:
                cx2070x_write(g_cx2070x_codec,DSP_INIT,cx2070x_read_reg_cache(g_cx2070x_codec,DSP_INIT)| 
                    DSP_INIT_NEWC | DSP_ENABLE_STREAM_3_4);
				break;
			default:
                return count;
			}
        return count;
    }
	switch (cx2070x_regs[reg_no].type) {
	case REG_TYPE_RO:
		ERROR("%s, A read-only register 0x%02x cannot be written.\n",
				__func__, cx2070x_regs[reg_no].addr);
		return -EINVAL; 
	case REG_TYPE_WI:
	case REG_TYPE_WC:
	case REG_TYPE_RW:
		ret = cx2070x_write(g_cx2070x_codec, reg_no, (u8)val); 
		if (ret) {
			ERROR("%s, Failed to write register 0x%02x.\n", __func__,
					cx2070x_regs[reg_no].addr);
			return ret;
		}
		break;
	default:
		ERROR("%s, Unknown type register\n", __func__);
		return -EINVAL;
	}
	
	return count;
}          

static const struct file_operations cx2070x_debug_reg_fops = {
	.open           = cx2070x_dbg_reg_open,
	.read           = seq_read,
	.write			= cx2070x_dbg_reg_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};
#endif

static int cx2070x_probe(struct snd_soc_codec *codec)
{
#ifdef CONFIG_DEBUG_FS
    struct dentry *d, *regs;
    int n = 0;
    int m = 0x4321;
#endif

    INFO("%lu: %s() called\n",jiffies,__func__);
    printk(KERN_INFO "cx2070x codec driver version: %02x,%02x,%02x,%02x\n",(u8)((CX2070X_DRIVER_VERSION)>>24), 
      (u8)((CX2070X_DRIVER_VERSION)>>16),
      (u8)((CX2070X_DRIVER_VERSION)>>8),
      (u8)((CX2070X_DRIVER_VERSION)));
    
#ifdef CONFIG_DEBUG_FS
	g_cx2070x_codec = codec;
    d = debugfs_create_dir("cx2070x", NULL);
	if (IS_ERR(d))
		return PTR_ERR(d);

    debugfs_create_file("SOURCE_SWITCH", 0644, d, (void *)m,
							&cx2070x_debug_reg_fops);
		
    regs = debugfs_create_dir("regs", d);
	if (IS_ERR(regs))
		return PTR_ERR(regs);
	
	for (n = 0; n < noof(cx2070x_regs); n++)	
		debugfs_create_file(cx2070x_regs[n].name, 0644, regs, (void *)n,
								&cx2070x_debug_reg_fops);
								
	debugfs_create_file("ALL", 0644, regs, (void *)n,
							&cx2070x_debug_reg_fops);
#endif

    return cx2070x_init(codec);
}

static int cx2070x_remove(struct snd_soc_codec *codec)
{
    INFO("%lu: %s() called\n",jiffies,__func__);
    // power down chip
    cx2070x_set_bias_level(codec, SND_SOC_BIAS_OFF);

#if defined (CONFIG_SND_CX2070X_GPIO_JACKSENSE)
    snd_soc_jack_free_gpios(&hs_jack, ARRAY_SIZE(hs_jack_gpios),
        hs_jack_gpios);
#endif//#if defined (CONFIG_SND_CX2070X_GPIO_JACKSENSE)
    return 0;
}

static int cx2070x_suspend(struct snd_soc_codec *codec)
{
    INFO("%lu: %s() called\n",jiffies,__func__);
    cx2070x_set_bias_level(codec, SND_SOC_BIAS_OFF);
    return 0;
}

static int cx2070x_resume(struct snd_soc_codec *codec)
{
    int                       n;
    INFO("%lu: %s() called\n",jiffies,__func__);

    // Sync reg_cache with the hardware
    for(n=0;n<noof(cx2070x_regs);n++)
        switch(cx2070x_regs[n].type&REG_TYPE_MASK)
    {
        case REG_TYPE_RO:
            break;
        case REG_TYPE_RW:
        case REG_TYPE_WI:
        case REG_TYPE_WC:
            cx2070x_real_write(codec,n,cx2070x_read_reg_cache(codec,n));
            break;
#if defined(REG_TYPE_DM)
        case REG_TYPE_DM:
            break;
#endif
        default: 
            snd_BUG();
    }
    cx2070x_dsp_init(codec,cx2070x_read_reg_cache(codec,DSP_INIT)|DSP_INIT_NEWC);
    cx2070x_set_bias_level(codec, SND_SOC_BIAS_ON);
    return 0;
}

static inline unsigned int cx2070x_read(struct snd_soc_codec *codec,
	unsigned int reg)
{
    return 0;
}

struct snd_soc_codec_driver soc_codec_dev_cx2070x=
{
    .probe =cx2070x_probe,
    .remove = cx2070x_remove,
    .suspend = cx2070x_suspend,
    .resume = cx2070x_resume,
    .read = cx2070x_read,
    .write = cx2070x_write,
    .reg_cache_size = sizeof(cx2070x_data),
    .reg_cache_step = 1,
    .reg_word_size = sizeof(u8),
    .reg_cache_default = cx2070x_data,
    .set_bias_level = cx2070x_set_bias_level,

};

#if defined(USING_I2C)
static int cx2070x_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
    struct cx2070x_priv      *cx2070x;
    int     ret = 0;

    INFO("%lu: %s() called\n", jiffies, __func__);
    
    cx2070x = (struct cx2070x_priv*)kzalloc(sizeof(struct cx2070x_priv), GFP_KERNEL);
    if (cx2070x == NULL)
    {
        return -ENOMEM;
    }

    i2c_set_clientdata(i2c, cx2070x);

    cx2070x->control_data = (void*)i2c;
    cx2070x->control_type =  SND_SOC_I2C;

    cx2070x->input_sel = Cx_INPUT_SEL_BY_GPIO;
    cx2070x->output_sel = Cx_OUTPUT_SEL_BY_GPIO;

    ret =  snd_soc_register_codec(&i2c->dev,
        &soc_codec_dev_cx2070x, &soc_codec_cx2070x_dai, 1);
    printk(">>>>>>>%s ret = %d ",__func__,ret);

    if (ret < 0)
        INFO("%s() failed ret = %d\n", __func__, ret);
    return ret;
}

static int cx2070x_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
#if defined(CONFIG_SND_CXLIFEGUARD)
    cxdbg_dev_exit();
#endif 
	kfree(i2c_get_clientdata(client));
    return 0;
}

static const struct i2c_device_id cx2070x_i2c_id[] = 
{
    { "cx2070x", 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, cx2070x_i2c_id);
 
static struct i2c_driver cx2070x_i2c_driver = {
    .driver = {
	.name = "cx2070x",
	.owner = THIS_MODULE,
     },
    .probe=cx2070x_i2c_probe,
    .remove=__devexit_p(cx2070x_i2c_remove),
    .id_table=cx2070x_i2c_id,
};

#elif defined(USING_SPI)
static int cx2070x_spi_probe(struct spi_device *spi)
{
    INFO("%lu: %s() called\n", jiffies, __func__);

    struct cx2070x_priv      *cx2070x;
    int     ret = 0;

    //printk(KERN_INFO "Channel Audio Codec %08x\n", CX2070X_DRIVER_VERSION);

    cx2070x = (struct cx2070x_priv      *)kzalloc(sizeof(struct cx2070x_priv), GFP_KERNEL);
    if (cx2070x == NULL)
    {
        return -ENOMEM;
    }

    spi_set_drvdata(spi, cx2070x);

    cx2070x->control_data = (void*)spi;
    cx2070x->control_type =  SND_SOC_SPI;

    cx2070x->input_sel = Cx_INPUT_SEL_BY_GPIO;
    cx2070x->output_sel = Cx_OUTPUT_SEL_BY_GPIO;

    ret =  snd_soc_register_codec(&spi->dev,
        &soc_codec_dev_cx2070x, &soc_codec_cx2070x_dai, 1);

    if (ret < 0)
        INFO("%s() failed ret = %d\n", __func__, ret);
    return ret;
}

static int cx2070x_spi_remove(struct spi_device *client)
{
    struct snd_soc_codec *codec = (struct snd_soc_codec *)spi_get_drvdata(client);

    kfree(codec->reg_cache);
    return 0;
}

static const struct spi_device_id cx2070x_spi_id[]={
    { CX2070X_SPI_DRIVER_NAME,NULL},
};

static struct spi_driver cx2070x_spi_driver = {
	.driver = {
		.name	= "cx2070x-codec",
		.owner	= THIS_MODULE,
	},
	.probe		= cx2070x_spi_probe,
	.remove		= __devexit_p(cx2070x_spi_remove),
};
#endif

static  int __init cx2070x_modinit(void)
{
    int ret;
    INFO("%lu: %s() called\n",jiffies,__func__);
#if defined(USING_I2C)
	ret = i2c_add_driver(&cx2070x_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register CX2070X I2C driver: %d\n",
		       ret);
	}
#elif defined(USING_SPI)
	ret = spi_register_driver(&cx2070x_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register CX2070X SPI driver: %d\n",
		       ret);
	}
#endif
    return ret;
}
module_init(cx2070x_modinit);

static void __exit cx2070x_exit(void)
{
    INFO("%lu: %s() called\n",jiffies,__func__);
#if defined(USING_I2C)
	i2c_del_driver(&cx2070x_i2c_driver);
#elif defined(USING_SPI)
	spi_unregister_driver(&cx2070x_spi_driver);
#endif
}
module_exit(cx2070x_exit);

int CX_AUDDRV_VERSION = CX2070X_DRIVER_VERSION;
EXPORT_SYMBOL_GPL(CX_AUDDRV_VERSION);
EXPORT_SYMBOL_GPL(soc_codec_dev_cx2070x);
MODULE_DESCRIPTION("ASoC cx2070x Codec Driver");
MODULE_LICENSE("GPL");

