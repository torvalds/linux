/**
 * struct rt5631_platform_data - platform-specific RT5631 data
 */

#ifndef _RT5631_H_
#define _RT5631_H_

/* platform speaker watt */
#define RT5631_SPK_1_0W     0
#define RT5631_SPK_0_5W     1
#define RT5631_SPK_1_5W     2

/* platform speaker mode */
#define RT5631_SPK_STEREO   0
#define RT5631_SPK_LEFT     1
#define RT5631_SPK_RIGHT    2

/* platform mic input mode */
#define RT5631_MIC_DIFFERENTIAL     0
#define RT5631_MIC_SINGLEENDED      1

struct rt5631_platform_data {
    int (*hp_detect)(void);
    void (*device_init)(void);
    void (*device_uninit)(void); 

    int  spk_watt;
    int  spk_output;
    int  mic_input;
};

#endif
