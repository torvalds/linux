/**
 * struct dummy_codec_platform_data - platform-specific data
 */

#ifndef _DUMMY_CODEC_H_
#define _DUMMY_CODEC_H_

struct dummy_codec_platform_data {
    void (*device_init)(void);
    void (*device_uninit)(void);
    void (*mute_spk)(int);
};

#endif
