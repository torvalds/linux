/**
 * struct syno9629_codec_platform_data - platform-specific data
 */

#ifndef _AML_M6TV_AUDIO_H_
#define _AML_M6TV_AUDIO_H_

struct m6tv_audio_codec_platform_data {
    void (*device_init)(void);
    void (*device_uninit)(void);
};

#endif
