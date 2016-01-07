/*
 * Amlogic Meson HDMI Transmitter Driver
 * HDMI CEC Driver-----------HDMI_TX
 * Copyright (C) 2011 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef _TV_CEC_H_
#define _TV_CEC_H_
#include <linux/amlogic/hdmi_tx/hdmi_tx_module.h> 
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#include <mach/hdmi_parameter.h>
#endif

#define CEC0_LOG_ADDR 4 // MBX logical address
#define TV_CEC_INTERVAL     (HZ*3)

#define CEC_VERSION     "v1.3"
#define _RX_DATA_BUF_SIZE_ 16

#define _RX_CEC_DBG_ON_
#define _TX_CEC_DBG_ON_

#ifdef  _RX_CEC_DBG_ON_
#define hdmirx_cec_dbg_print(fmt, args...) printk(KERN_WARNING fmt, ## args)//hdmi_print
#else
#define hdmirx_cec_dbg_print(fmt, args...)
#endif

#ifdef  _TX_CEC_DBG_ON_
#define hdmitx_cec_dbg_print(fmt, args...) printk(KERN_WARNING fmt, ## args)//hdmi_print
#else
#define hdmitx_cec_dbg_print(fmt, args...)
#endif
#define HDMI_CEC_DEBUG()  printk("HDMI CEC DEBUG: %s [%d]\n", __FUNCTION__, __LINE__)

extern unsigned int cec_tx_irq_flag;
extern unsigned int cec_tx_irq_syn_flag;
void fiq_gpio_test(unsigned int cmd);


#define MSG_P0( init, follow, opcode )    {                \
    gbl_msg[0] = (init)<<4 | (follow);                    \
    gbl_msg[1] = (opcode);                                \
}

#define MSG_P1( i, f, o, p1 )    { \
    gbl_msg[2]=(p1); MSG_P0((i),(f),(o)); }
#define MSG_P2( i, f, o, p1, p2 )    { \
    gbl_msg[3]=(p2); MSG_P1((i),(f),(o),(p1)); }
#define MSG_P3( i, f, o, p1, p2, p3 )    { \
    gbl_msg[4]=(p3); MSG_P2((i),(f),(o),(p1),(p2)); }
#define MSG_P4( i, f, o, p1, p2, p3, p4 )    { \
    gbl_msg[5]=(p4); MSG_P3((i),(f),(o),(p1),(p2),(p3)); }
#define MSG_P5( i, f, o, p1, p2, p3, p4, p5 )    { \
    gbl_msg[6]=(p5); MSG_P4((i),(f),(o),(p1),(p2),(p3),(p4)); }
#define MSG_P6( i, f, o, p1, p2, p3, p4, p5, p6 )    { \
    gbl_msg[7]=(p6); MSG_P5((i),(f),(o),(p1),(p2),(p3),(p4),(p5)); }
#define MSG_P7( i, f, o, p1, p2, p3, p4, p5, p6, p7 )    { \
    gbl_msg[8]=(p7); MSG_P6((i),(f),(o),(p1),(p2),(p3),(p4),(p5),(p6)); }
#define MSG_P8( i, f, o, p1, p2, p3, p4, p5, p6, p7, p8 )    { \
    gbl_msg[9]=(p8); MSG_P7((i),(f),(o),(p1),(p2),(p3),(p4),(p5),(p6),(p7)); }
#define MSG_P9( i, f, o, p1, p2, p3, p4, p5, p6, p7, p8, p9 )    { \
    gbl_msg[10]=(p9); MSG_P8((i),(f),(o),(p1),(p2),(p3),(p4),(p5),(p6),(p7),(p8)); }
#define MSG_P10( i, f, o, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10 )    { \
    gbl_msg[11]=(p10); MSG_P9((i),(f),(o),(p1),(p2),(p3),(p4),(p5),(p6),(p7),(p8),(p9)); }
#define MSG_P11( i, f, o, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11 )    { \
    gbl_msg[12]=(p11); MSG_P10((i),(f),(o),(p1),(p2),(p3),(p4),(p5),(p6),(p7),(p8),(p9),(p10)); }
#define MSG_P12( i, f, o, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12 )    { \
    gbl_msg[13]=(p12); MSG_P11((i),(f),(o),(p1),(p2),(p3),(p4),(p5),(p6),(p7),(p8),(p9),(p10),(p11)); }
#define MSG_P13( i, f, o, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13 )    { \
    gbl_msg[14]=(p13); MSG_P12((i),(f),(o),(p1),(p2),(p3),(p4),(p5),(p6),(p7),(p8),(p9),(p10),(p11),(p12)); }
#define MSG_P14( i, f, o, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14 )    { \
    gbl_msg[15]=(p14); MSG_P13((i),(f),(o),(p1),(p2),(p3),(p4),(p5),(p6),(p7),(p8),(p9),(p10),(p11),(p12),(p13)); }


#define MAX_MSG        16
#define MAX_NUM_OF_DEV    16

enum _cec_ping_e {
    CEC_PING_REPLY = 0,
    CEC_PING_FAIL
};

enum _cec_log_dev_addr_e {
    CEC_TV_ADDR                    = 0x00,
    CEC_RECORDING_DEVICE_1_ADDR,
    CEC_RECORDING_DEVICE_2_ADDR,
    CEC_TUNER_1_ADDR,
    CEC_PLAYBACK_DEVICE_1_ADDR,
    CEC_AUDIO_SYSTEM_ADDR,
    CEC_TUNER_2_ADDR,
    CEC_TUNER_3_ADDR,
    CEC_PLAYBACK_DEVICE_2_ADDR,
    CEC_RECORDING_DEVICE_3_ADDR,
    CEC_TUNER_4_ADDR,
    CEC_PLAYBACK_DEVICE_3_ADDR,
    CEC_RESERVED_1_ADDR,
    CEC_RESERVED_2_ADDR,
    CEC_FREE_USE_ADDR,
    CEC_UNREGISTERED_ADDR
};

#define CEC_BROADCAST_ADDR CEC_UNREGISTERED_ADDR

#define CEC_TV                        ( 1 << CEC_TV_ADDR )
#define CEC_RECORDING_DEVICE_1        ( 1 << CEC_RECORDING_DEVICE_1_ADDR )
#define CEC_RECORDING_DEVICE_2        ( 1 << CEC_RECORDING_DEVICE_2_ADDR )
#define CEC_TUNER_1                    ( 1 << CEC_TUNER_1_ADDR )
#define CEC_PLAYBACK_DEVICE_1        ( 1 << CEC_PLAYBACK_DEVICE_1_ADDR )
#define CEC_AUDIO_SYSTEM            ( 1 << CEC_AUDIO_SYSTEM_ADDR )
#define CEC_TUNER_2                    ( 1 << CEC_TUNER_2_ADDR )
#define CEC_TUNER_3                    ( 1 << CEC_TUNER_3_ADDR )
#define CEC_PLAYBACK_DEVICE_2        ( 1 << CEC_PLAYBACK_DEVICE_2_ADDR )
#define CEC_RECORDING_DEVICE_3        ( 1 << CEC_RECORDING_DEVICE_3_ADDR )
#define CEC_TUNER_4                    ( 1 << CEC_TUNER_4_ADDR )
#define CEC_PLAYBACK_DEVICE_3        ( 1 << CEC_PLAYBACK_DEVICE_3_ADDR )
#define CEC_RESERVED_1                ( 1 << CEC_RESERVED_1_ADDR )
#define CEC_RESERVED_2                ( 1 << CEC_RESERVED_2_ADDR )
#define CEC_FREE_USE                ( 1 << CEC_FREE_USE_ADDR )
#define CEC_UNREGISTERED            ( 1 << CEC_UNREGISTERED_ADDR )

#define CEC_DISPLAY_DEVICE            ( CEC_TV | CEC_FREE_USE )
#define    CEC_RECORDING_DEVICE        ( CEC_RECORDING_DEVICE_1 | CEC_RECORDING_DEVICE_2 | CEC_RECORDING_DEVICE_3 )
#define    CEC_PLAYBACK_DEVICE            ( CEC_PLAYBACK_DEVICE_1 | CEC_PLAYBACK_DEVICE_2 | CEC_PLAYBACK_DEVICE_3 )
#define    CEC_TUNER_DEVICE            ( CEC_TUNER_1 | CEC_TUNER_2 | CEC_TUNER_3 | CEC_TUNER_4 )
#define    CEC_AUDIO_SYSTEM_DEVICE        ( CEC_AUDIO_SYSTEM )

enum cec_dev_type_addr {
    CEC_DISPLAY_DEVICE_TYPE = 0x0,
    CEC_RECORDING_DEVICE_TYPE,
    CEC_RESERVED_DEVICE_TYPE,
    CEC_TUNER_DEVICE_TYPE,
    CEC_PLAYBACK_DEVICE_TYPE,
    CEC_AUDIO_SYSTEM_DEVICE_TYPE,
    CEC_UNREGISTERED_DEVICE_TYPE,
};

typedef struct _context_t {
    unsigned char state;
} context_t;

typedef enum  {
    CEC_UNRECONIZED_OPCODE = 0x0,
    CEC_NOT_CORRECT_MODE_TO_RESPOND,
    CEC_CANNOT_PROVIDE_SOURCE,
    CEC_INVALID_OPERAND,
    CEC_REFUSED,
    CEC_UNABLE_TO_DETERMINE,
} cec_feature_abort_e;

/*
 * CEC OPCODES
 */
#define    CEC_OC_ABORT_MESSAGE                     0xFF
#define    CEC_OC_ACTIVE_SOURCE                     0x82
#define    CEC_OC_CEC_VERSION                         0x9E
#define    CEC_OC_CLEAR_ANALOGUE_TIMER             0x33
#define    CEC_OC_CLEAR_DIGITAL_TIMER                 0x99
#define    CEC_OC_CLEAR_EXTERNAL_TIMER             0xA1
#define    CEC_OC_DECK_CONTROL                     0x42
#define    CEC_OC_DECK_STATUS                         0x1B
#define    CEC_OC_DEVICE_VENDOR_ID                 0x87
#define    CEC_OC_FEATURE_ABORT                     0x00
#define    CEC_OC_GET_CEC_VERSION                     0x9F
#define    CEC_OC_GET_MENU_LANGUAGE                 0x91
#define    CEC_OC_GIVE_AUDIO_STATUS                 0x71
#define    CEC_OC_GIVE_DECK_STATUS                 0x1A
#define    CEC_OC_GIVE_DEVICE_POWER_STATUS         0x8F
#define    CEC_OC_GIVE_DEVICE_VENDOR_ID             0x8C
#define    CEC_OC_GIVE_OSD_NAME                     0x46
#define    CEC_OC_GIVE_PHYSICAL_ADDRESS             0x83
#define    CEC_OC_GIVE_SYSTEM_AUDIO_MODE_STATUS     0x7D
#define    CEC_OC_GIVE_TUNER_DEVICE_STATUS         0x08
#define    CEC_OC_IMAGE_VIEW_ON                     0x04
#define    CEC_OC_INACTIVE_SOURCE                     0x9D
#define    CEC_OC_MENU_REQUEST                     0x8D
#define    CEC_OC_MENU_STATUS                         0x8E
#define    CEC_OC_PLAY                             0x41
#define    CEC_OC_POLLING_MESSAGE                     0xFC    /* Fake Code - <Poll Message> has no OP Code and requires only the header byte */
#define    CEC_OC_RECORD_OFF                         0x0B
#define    CEC_OC_RECORD_ON                         0x09
#define    CEC_OC_RECORD_STATUS                     0x0A
#define    CEC_OC_RECORD_TV_SCREEN                 0x0F
#define    CEC_OC_REPORT_AUDIO_STATUS                 0x7A
#define    CEC_OC_REPORT_PHYSICAL_ADDRESS             0x84
#define    CEC_OC_REPORT_POWER_STATUS                 0x90
#define    CEC_OC_REQUEST_ACTIVE_SOURCE             0x85
#define    CEC_OC_ROUTING_CHANGE                     0x80
#define    CEC_OC_ROUTING_INFORMATION                 0x81
#define    CEC_OC_SELECT_ANALOGUE_SERVICE             0x92
#define    CEC_OC_SELECT_DIGITAL_SERVICE             0x93
#define    CEC_OC_SET_ANALOGUE_TIMER                 0x34
#define    CEC_OC_SET_AUDIO_RATE                     0x9A
#define    CEC_OC_SET_DIGITAL_TIMER                 0x97
#define    CEC_OC_SET_EXTERNAL_TIMER                 0xA2
#define    CEC_OC_SET_MENU_LANGUAGE                 0x32
#define    CEC_OC_SET_OSD_NAME                     0x47
#define    CEC_OC_SET_OSD_STRING                     0x64
#define    CEC_OC_SET_STREAM_PATH                     0x86
#define    CEC_OC_SET_SYSTEM_AUDIO_MODE             0x72
#define    CEC_OC_SET_TIMER_PROGRAM_TITLE             0x67
#define    CEC_OC_STANDBY                             0x36
#define    CEC_OC_SYSTEM_AUDIO_MODE_REQUEST         0x70
#define    CEC_OC_SYSTEM_AUDIO_MODE_STATUS         0x7E
#define    CEC_OC_TEXT_VIEW_ON                     0x0D
#define    CEC_OC_TIMER_CLEARED_STATUS             0x43
#define    CEC_OC_TIMER_STATUS                     0x35
#define    CEC_OC_TUNER_DEVICE_STATUS                 0x07
#define    CEC_OC_TUNER_STEP_DECREMENT             0x06
#define    CEC_OC_TUNER_STEP_INCREMENT             0x05
#define    CEC_OC_USER_CONTROL_PRESSED             0x44
#define    CEC_OC_USER_CONTROL_RELEASED             0x45
#define    CEC_OC_VENDOR_COMMAND                     0x89
#define    CEC_OC_VENDOR_COMMAND_WITH_ID             0xA0
#define    CEC_OC_VENDOR_REMOTE_BUTTON_DOWN         0x8A
#define    CEC_OC_VENDOR_REMOTE_BUTTON_UP             0x8B

/* cec message structure */
typedef struct {
    union {
        unsigned char buffer[16];          // message memory
        struct {
            unsigned char header;          // 4bit Initiator logical address + 4bit Destination logical address
            unsigned char opcode;          // message opcode
            unsigned char operands[14];    // the maximun operand is 14
            //unsigned int  flag;            // flag = 1 ,cec key pressed;flag = 0, cec key released
        }msg;                              // message struct
    }content;                              // message content
    unsigned char operand_num;             // number of operand
    unsigned char msg_length;
} cec_rx_message_t;

typedef struct {
    cec_rx_message_t cec_message;
    struct list_head list;
} cec_rx_message_list_t;

typedef struct {
    unsigned char msg[MAX_MSG];
    unsigned char length;
    struct list_head list;
} cec_tx_message_list_t;

/* cec global struct */

typedef enum {
    STATE_UNKNOWN = 0x00,
    STATE_START,
    STATE_STOP
} cec_node_status_e;

typedef enum {
    POWER_ON = 0x00,
    POWER_STANDBY,
    TRANS_STANDBY_TO_ON,
    TRANS_ON_TO_STANDBY,
} cec_power_status_e;

typedef enum {
    STATUS_REQ_ON = 1,
    STATUS_REQ_OFF,
    STATUS_REQ_ONCE,
}status_req_mode_e;

typedef enum {
    DECK_UNKNOWN_STATUS = 0,
    DECK_PLAY = 0X11,
    DECK_RECORD,
    DECK_PLAY_REVERSE,
    DECK_STILL,
    DECK_SLOW,
    DECK_SLOW_REVERSE,
    DECK_FAST_FORWARD,
    DECK_FAST_REVERSE,
    DECK_NO_MEDIA,
    DECK_STOP,
    DECK_SKIP_FORWARD_WIND,
    DECK_SKIP_REVERSE_REWIND,
    DECK_INDEX_SEARCH_FORWARD,
    DECK_INDEX_SEARCH_REVERSE,
    DECK_OTHER_STATUS,
} deck_info_e;

typedef enum {
    DECK_CNT_SKIP_FORWARD_WIND = 1,
    DECK_CNT_SKIP_REVERSE_REWIND,
    DECK_CNT_STOP,
    DECK_CNT_EJECT,
} deck_cnt_mode_e;

typedef enum {
    PLAY_FORWARD = 0X24,
    PLAY_REVERSE = 0X20,
    PLAY_STILL = 0X25,
    FAST_FORWARD_MIN_SPEED = 0X05,
    FAST_FORWARD_MEDIUM_SPEED = 0X06,
    FAST_FORWARD_MAX_SPEED = 0X07,
    FAST_REVERSE_MIN_SPEED = 0X09,
    FAST_REVERSE_MEDIUM_SPEED = 0X0A,
    FAST_REVERSE_MAX_SPEED = 0X0B,
    SLOW_FORWARD_MIN_SPEED = 0X15,
    SLOW_FORWARD_MEDIUM_SPEED = 0X16,
    SLOW_FORWARD_MAX_SPEED = 0X17,
    SLOW_REVERSE_MIN_SPEED = 0X19,
    SLOW_REVERSE_MEDIUM_SPEED = 0X1A,
    SLOW_REVERSE_MAX_SPEED = 0X1B,
} play_mode_e;

typedef enum {
    MENU_REQ_ACTIVATE = 0,
    MENU_REQ_DEACTIVATE,
    MENU_REQ_QUERY,
} menu_req_type_e;

typedef enum {
    MENU_STATE_ACTIVATED = 0,
    MENU_STATE_DEACTIVATED,
} menu_state_e;

typedef enum {
    CEC_VERSION_11 = 0,
    CEC_VERSION_12,
    CEC_VERSION_12A,
    CEC_VERSION_13,
    CEC_VERSION_13A,
    CEC_VERSION_14A,
} cec_version_e;

typedef enum {
    CEC_HDMI_PORT_UKNOWN = 0,
    CEC_HDMI_PORT_1,
    CEC_HDMI_PORT_2,
    CEC_HDMI_PORT_3,
} cec_hdmi_port_e;

typedef enum {
    OFF = 0,
    ON,
} system_audio_status_e;
typedef unsigned long cec_info_mask;

#define INFO_MASK_CEC_VERSION                (1<<0)
#define INFO_MASK_VENDOR_ID                  (1<<1)
#define INFO_MASK_DEVICE_TYPE                (1<<2)
#define INFO_MASK_POWER_STATUS               (1<<3)
#define INFO_MASK_PHYSICAL_ADDRESS           (1<<4)
#define INFO_MASK_LOGIC_ADDRESS              (1<<5)
#define INFO_MASK_OSD_NAME                   (1<<6)
#define INFO_MASK_MENU_STATE                 (1<<7)
#define INFO_MASK_MENU_LANGUAGE              (1<<8)
#define INFO_MASK_DECK_INfO                  (1<<9)
#define INFO_MASK_PLAY_MODE                  (1<<10)

/*CEC UI MASK*/
#define CEC_FUNC_MSAK                        0
#define ONE_TOUCH_PLAY_MASK                  1
#define ONE_TOUCH_STANDBY_MASK               2
#define AUTO_POWER_ON_MASK                   3


typedef struct {
    cec_hdmi_port_e hdmi_port;
    cec_info_mask real_info_mask;
    unsigned char cec_version;
    unsigned int vendor_id:24;
    unsigned char dev_type;
    unsigned char menu_status;
    cec_power_status_e power_status;
    union {
        unsigned short phy_addr_4;
        struct {
            unsigned char cd;
            unsigned char ab;
        } phy_addr_2;
        struct {
            unsigned char d:4;
            unsigned char c:4;
            unsigned char b:4;
            unsigned char a:4;
        }phy_addr_1;
    }phy_addr;
    unsigned char log_addr;
    unsigned char osd_name[16];
    unsigned char osd_name_def[16];
    menu_state_e menu_state;
    int menu_lang;
    union {
        struct {
        } display;
        struct {
        } recording;
        struct {
            deck_cnt_mode_e deck_cnt_mode;
            deck_info_e deck_info;
            play_mode_e play_mode;
        } playback;
        struct {
        } tuner;
        struct {
            system_audio_status_e sys_audio_mode;
            struct {
                unsigned char audio_mute_status : 1;
                unsigned char audio_volume_status : 7;
            } audio_status;
        } audio;
    }specific_info;
    
} cec_node_info_t;

typedef struct {
    cec_rx_message_t cec_rx_message[_RX_DATA_BUF_SIZE_];
    unsigned char rx_write_pos;
    unsigned char rx_read_pos;
    unsigned char rx_buf_size;
} cec_rx_msg_buf_t;

typedef struct {
    unsigned int cec_key_flag;
    unsigned int cec_init_flag;
    int cec_fiq_flag;
    int cec_repeat_flag;
} cec_flag_t;

typedef struct {
    unsigned short dev_mask;
    unsigned char active_log_dev;
    unsigned char my_node_index;
    cec_flag_t cec_flag;
    struct input_dev *remote_cec_dev; //cec input device
    cec_node_info_t cec_node_info[MAX_NUM_OF_DEV];
    cec_rx_msg_buf_t cec_rx_msg_buf;
    hdmitx_dev_t* hdmitx_device;
    cec_power_status_e tv_power_status;
} cec_global_info_t;

typedef struct {
    unsigned char dev_number;
    unsigned char active_log_dev;
    cec_node_info_t cec_node_info_online[];
} cec_global_info_to_usr_t;

typedef enum {
    GET_CEC_VERSION = 0,
    GET_POWER_STATUS,
    GET_DEV_POWER_STATUS,
    GET_DEV_VENDOR_ID,
    GET_OSD_NAME,
    GET_OSD_STRING,
    GET_PHYSICAL_ADDR,
    GET_SYSTEM_AUDIO_MODE,
    GET_TUNER_DEV_STATUS,       //0x8
    GIVE_DECK_STATUS,
    SET_DECK_CONTROL_MODE,
    SET_PLAY_MODE,
    GET_AUDIO_STATUS,
    SET_STANDBY,
    SET_IMAGEVIEW_ON,
    GET_MENU_STATE,
    SET_MENU_STATE,             //0x10
    SET_MENU_LANGAGE,
    GET_MENU_LANGUAGE,
    GET_ACTIVE_SOURCE,
    SET_ACTIVE_SOURCE,
    SET_DEACTIVE_SOURCE,
    CLR_NODE_DEV_REAL_INFO_MASK,
    REPORT_PHYSICAL_ADDRESS,    //0x17
    SET_TEXT_VIEW_ON,
    POLLING_ONLINE_DEV, //0x19
    PING_TV,
    USR_CMD_MAX,
} usr_cmd_type_e;

typedef enum {
    TV_CEC_PENDING_OFF = 0,
    TV_CEC_PENDING_ON,
} tv_cec_pending_e;

typedef enum {
    TV_CEC_POLLING_OFF = 0,
    TV_CEC_POLLING_ON,
} tv_cec_polling_state_e;

typedef enum {
    DEVICE_MENU_ACTIVE = 0,
    DEVICE_MENU_INACTIVE,    
} cec_device_menu_state_e;

void cec_enable_irq(void);
void cec_disable_irq(void);

int cec_ll_tx_polling(const unsigned char *msg, unsigned char len);

int cec_ll_tx(const unsigned char *msg, unsigned char len);
int cec_ll_rx( unsigned char *msg, unsigned char *len);

void cec_test_function(unsigned char* arg, unsigned char arg_cnt);
void cec_node_init(hdmitx_dev_t* hdmitx_device);
void cec_node_uninit(hdmitx_dev_t* hdmitx_device);

void cec_hw_reset(void);

void register_cec_rx_msg(unsigned char *msg, unsigned char len );
void cec_handle_message(cec_rx_message_t* pcec_message);
void unregister_cec_tx_msg(cec_tx_message_list_t* cec_usr_message_list);
void register_cec_tx_msg(unsigned char *msg, unsigned char len);
void cec_usr_cmd_post_process(void);
void cec_set_pending(tv_cec_pending_e on_off);
void cec_polling_online_dev(int log_addr, int *bool);
unsigned short cec_log_addr_to_dev_type(unsigned char log_addr);

void cec_routing_information(cec_rx_message_t* pcec_message);
void cec_routing_change(cec_rx_message_t* pcec_message);
void cec_usrcmd_set_osd_name(cec_rx_message_t* pcec_message);
void cec_usrcmd_set_device_vendor_id(void);
void cec_usrcmd_get_cec_version(unsigned char log_addr);
void cec_usrcmd_get_audio_status(unsigned char log_addr);
void cec_usrcmd_get_deck_status(unsigned char log_addr);
void cec_usrcmd_get_device_power_status(unsigned char log_addr);
void cec_usrcmd_get_device_vendor_id(unsigned char log_addr);
void cec_usrcmd_get_osd_name(unsigned char log_addr);
void cec_usrcmd_get_physical_address(unsigned char log_addr);
void cec_usrcmd_get_system_audio_mode_status(unsigned char log_addr);
void cec_usrcmd_get_tuner_device_status(unsigned char log_addr);
void cec_usrcmd_parse_all_dev_online(void);
void cec_usrcmd_set_deck_cnt_mode(unsigned char log_addr, deck_cnt_mode_e deck_cnt_mode);
void cec_usrcmd_set_standby(unsigned char log_addr);
void cec_usrcmd_set_imageview_on(unsigned char log_addr);
void cec_usrcmd_set_play_mode(unsigned char log_addr, play_mode_e play_mode);
void cec_usrcmd_get_menu_state(unsigned char log_addr);
void cec_usrcmd_set_menu_state(unsigned char log_addr, menu_req_type_e menu_req_type);
void cec_usrcmd_get_menu_language(unsigned char log_addr);
void cec_usrcmd_get_active_source(void);
void cec_usrcmd_set_active_source(void);
void cec_usrcmd_set_deactive_source(unsigned char log_addr);
void cec_usrcmd_clear_node_dev_real_info_mask(unsigned char log_addr, cec_info_mask mask);
void cec_usrcmd_set_report_physical_address(void);
void cec_usrcmd_text_view_on(unsigned char log_addr);
void cec_polling_online_dev(int log_addr, int *bool);
void cec_device_vendor_id(cec_rx_message_t* pcec_message);
void cec_report_power_status(cec_rx_message_t* pcec_message);
void cec_active_source(cec_rx_message_t* pcec_message);
void cec_set_stream_path(cec_rx_message_t* pcec_message);
void cec_set_osd_name(cec_rx_message_t* pcec_message);
void cec_set_osd_name_init(void);
void cec_inactive_source_rx(cec_rx_message_t* pcec_message);
void cec_set_system_audio_mode(void);
void cec_system_audio_mode_request(void);
void cec_report_audio_status(void);
void cec_get_menu_language_smp(void);
void cec_device_vendor_id_smp(void);
void cec_menu_status_smp(cec_device_menu_state_e status);
void cec_set_imageview_on_irq(void);
void cec_active_source_irq(void);

void cec_report_physical_address_smp(void);
void cec_imageview_on_smp(void);
void cec_active_source_smp(void);
void cec_active_source_rx(cec_rx_message_t* pcec_message);

size_t cec_usrcmd_get_global_info(char * buf);
void cec_usrcmd_set_dispatch(const char * buf, size_t count);
void cec_usrcmd_set_config(const char * buf, size_t count);
void cec_usrcmd_set_lang_config(const char * buf, size_t count); 
void cec_input_handle_message(void);
void cec_send_event_irq(void);
void cec_standby_irq(void);
void cec_user_control_released_irq(void);
void cec_user_control_pressed_irq(void);
void cec_inactive_source(void);
void cec_set_standby(void);
void cec_isr_post_process(void);
void cec_clear_buf(unsigned int flag);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
void cec_arbit_bit_time_set(unsigned bit_set, unsigned time_set, unsigned flag);
void tx_irq_handle(void);
void cec_arbit_bit_time_read(void);
void tx_irq_handle(void);
void ao_cec_init(void);
#endif

#ifdef CONFIG_ARCH_MESON6
void cec_gpi_init(void);
#endif

unsigned char check_cec_msg_valid(const cec_rx_message_t* pcec_message);
void cec_send_event(cec_rx_message_t* pcec_message);
void cec_user_control_pressed(cec_rx_message_t* pcec_message);
void cec_user_control_released(cec_rx_message_t* pcec_message);  
void cec_standby(cec_rx_message_t* pcec_message);
void cec_send_simplink_alive(cec_rx_message_t *pcec_message);
void cec_send_simplink_ack(cec_rx_message_t *pcec_message);
extern void cec_key_init(void);

extern __u16 cec_key_map[];
extern cec_global_info_t cec_global_info;

#endif

