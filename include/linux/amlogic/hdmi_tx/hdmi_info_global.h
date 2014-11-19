#ifndef _HDMI_INFO_GLOBAL_H
#define _HDMI_INFO_GLOBAL_H

typedef enum HDMI_Video_Type_ {
    HDMI_Unkown = 0 ,
    HDMI_640x480p60 = 1 ,
    HDMI_480p60,
    HDMI_480p60_16x9,
    HDMI_720p60,
    HDMI_1080i60,
    HDMI_480i60,
    HDMI_480i60_16x9,
    HDMI_480i60_16x9_rpt = 11,
    HDMI_1440x480p60 = 14 ,
    HDMI_1440x480p60_16x9 = 15 ,
    HDMI_1080p60 = 16,
    HDMI_576p50,
    HDMI_576p50_16x9,
    HDMI_720p50,
    HDMI_1080i50,
    HDMI_576i50,
    HDMI_576i50_16x9,
    HDMI_576i50_16x9_rpt = 26,
    HDMI_1080p50 = 31,
    HDMI_1080p24,
    HDMI_1080p25,
    HDMI_1080p30,
    HDMI_480p60_16x9_rpt = 36,
    HDMI_576p50_16x9_rpt = 38,
    HDMI_1280x1024 = 71,
    HDMI_1920x1200 = 100,
    HDMI_4k2k_24 = 93,      // CEA-861-F
    HDMI_4k2k_25,
    HDMI_4k2k_30,
    HDMI_4k2k_50,
    HDMI_4k2k_60,
    HDMI_4k2k_smpte_24 = 98,   //24 Hz
    HDMI_4k2k_smpte_50 = 101,
    HDMI_4k2k_smpte_60,
} HDMI_Video_Codes_t ;


//-------------------HDMI VIDEO--------------------------------
#define TYPE_AVI_INFOFRAMES       0x82
#define AVI_INFOFRAMES_VERSION    0x02
#define AVI_INFOFRAMES_LENGTH     0x0D

typedef enum
{
    COLOR_SPACE_RGB444 = 0,
    COLOR_SPACE_YUV422 = 1,
    COLOR_SPACE_YUV444 = 2,
}color_space_type_t;

typedef enum
{
		STATE_VIDEO__POWERDOWN  =    0,
		STATE_VIDEO__MUTED      =    1,
		STATE_VIDEO__UNMUTE     =    2,
		STATE_VIDEO__ON         =    3,
}hdmi_rx_video_state_t;


typedef struct
{
     short H; // Number of horizontal pixels
     short V; // Number of vertical pixels
}pixs_type_t;


typedef enum
{
    ASPECT_RATIO_SAME_AS_SOURCE= 0,
    TV_ASPECT_RATIO_4_3,
    TV_ASPECT_RATIO_16_9,
    TV_ASPECT_RATIO_14_9,
    ASPECT_RATIO_MAX
} hdmi_aspect_ratio_t;

typedef enum
{
    COLOR_24BIT = 0,
    COLOR_30BIT,
    COLOR_36BIT,
    COLOR_48BIT
} hdmi_color_depth_t;

typedef enum
{
    NO_REPEAT = 0,
    HDMI_2_TIMES_REPEAT,
    HDMI_3_TIMES_REPEAT,
    HDMI_4_TIMES_REPEAT,
    HDMI_5_TIMES_REPEAT,
    HDMI_6_TIMES_REPEAT,
    HDMI_7_TIMES_REPEAT,
    HDMI_8_TIMES_REPEAT,
    HDMI_9_TIMES_REPEAT,
    HDMI_10_TIMES_REPEAT,
    MAX_TIMES_REPEAT,
} hdmi_pixel_repeat_t;

typedef enum
{
        SS_NO_DATA = 0,
        SS_SCAN_OVER,           // where some active pixelsand lines at the edges are not  displayed.
        SS_SCAN_UNDER,          // where all active pixels&lines are displayed, with or withouta border.
        SS_RSV
} hdmi_scan_t;


typedef enum
{
        B_UNVALID = 0,
        B_BAR_VERT,           // Vert. Bar Infovalid
        B_BAR_HORIZ,          // Horiz. Bar Infovalid
        B_BAR_VERT_HORIZ,       //Vert.and Horiz. Bar Info valid
} hdmi_bar_info_t;


typedef enum
{
        CC_NO_DATA = 0,
        CC_ITU601,
        CC_ITU709,
        CC_XVYCC601,
        CC_XVYCC709,
} hdmi_colorimetry_t;


typedef enum
{
        SC_NO_UINFORM = 0,
        SC_SCALE_HORIZ,     //Picture has been scaled horizontally
        SC_SCALE_VERT,     //Picture has been scaled verticallv
        SC_SCALE_HORIZ_VERT,   //Picture has been scaled horizontally & SC_SCALE_H_V
} hdmi_slacing_t;



typedef struct {
        HDMI_Video_Codes_t VIC;
        color_space_type_t color;
        hdmi_color_depth_t color_depth;
        hdmi_bar_info_t     bar_info;
        hdmi_pixel_repeat_t repeat_time;
        hdmi_aspect_ratio_t aspect_ratio;
        hdmi_colorimetry_t cc;
        hdmi_scan_t            ss;
        hdmi_slacing_t          sc;
}Hdmi_rx_video_info_t;
//-----------------------HDMI VIDEO END----------------------------------------



//-------------------HDMI AUDIO--------------------------------
#define TYPE_AUDIO_INFOFRAMES       0x84
#define AUDIO_INFOFRAMES_VERSION    0x01
#define AUDIO_INFOFRAMES_LENGTH     0x0A


typedef enum
{
    CT_REFER_TO_STREAM = 0,
    CT_PCM,
    CT_AC_3,
    CT_MPEG1,
    CT_MP3,
    CT_MPEG2,
    CT_AAC,
    CT_DTS,
    CT_ATRAC,
    CT_ONE_BIT_AUDIO,
    CT_DOLBY_D,
    CT_DTS_HD,
    CT_MAT,
    CT_DST,
    CT_WMA,
    CT_MAX,
} audio_type_t;


typedef enum
{
    CC_REFER_TO_STREAM = 0,
    CC_2CH,
    CC_3CH,
    CC_4CH,
    CC_5CH,
    CC_6CH,
    CC_7CH,
    CC_8CH,
    CC_MAX_CH
} audio_channel_t;

typedef enum
{
    AF_SPDIF = 0,
    AF_I2S,
    AF_DSD,
    AF_HBR,
    AT_MAX
} audio_format_t;


//Sampling Freq Fs:0 - Refer to Stream Header; 1 - 32KHz; 2 - 44.1KHz; 3 - 48KHz; 4 - 88.2KHz...
typedef enum {
    FS_REFER_TO_STREAM = 0,
    FS_32K   = 1,
    FS_44K1  = 2,
    FS_48K   = 3,
    FS_88K2  = 4,
    FS_96K   = 5,
    FS_176K4 = 6,
    FS_192K  = 7,
    FS_MAX,
}audio_fs_t;

struct rate_map_fs {
    unsigned int rate;
    audio_fs_t fs;
};

typedef enum {
    SS_REFER_TO_STREAM = 0,
    SS_16BITS,
    SS_20BITS,
    SS_24BITS,
    SS_MAX
}audio_sample_size_t;

struct size_map_ss {
    unsigned int sample_bits;
    audio_sample_size_t ss;
};

//FL-- Front Left
//FC --Front Center
//FR --Front Right
//FLC-- Front Left Center
//FRC-- Front RiQhtCenter
//RL-- Rear Left
//RC --Rear Center
//RR-- Rear Right
//RLC-- Rear Left Center
//RRC --Rear RiQhtCenter
//LFE-- Low Frequency Effect
typedef enum {
    CA_FR_FL = 0,
    CA_LFE_FR_FL,
    CA_FC_FR_FL,
    CA_FC_LFE_FR_FL,

    CA_RC_FR_FL,
    CA_RC_LFE_FR_FL,
    CA_RC_FC_FR_FL,
    CA_RC_FC_LFE_FR_FL,

    CA_RR_RL_FR_FL,
    CA_RR_RL_LFE_FR_FL,
    CA_RR_RL_FC_FR_FL,
    CA_RR_RL_FC_LFE_FR_FL,

    CA_RC_RR_RL_FR_FL,
    CA_RC_RR_RL_LFE_FR_FL,
    CA_RC_RR_RL_FC_FR_FL,
    CA_RC_RR_RL_FC_LFE_FR_FL,

    CA_RRC_RC_RR_RL_FR_FL,
    CA_RRC_RC_RR_RL_LFE_FR_FL,
    CA_RRC_RC_RR_RL_FC_FR_FL,
    CA_RRC_RC_RR_RL_FC_LFE_FR_FL,

    CA_FRC_RLC_FR_FL,
    CA_FRC_RLC_LFE_FR_FL,
    CA_FRC_RLC_FC_FR_FL,
    CA_FRC_RLC_FC_LFE_FR_FL,

    CA_FRC_RLC_RC_FR_FL,
    CA_FRC_RLC_RC_LFE_FR_FL,
    CA_FRC_RLC_RC_FC_FR_FL,
    CA_FRC_RLC_RC_FC_LFE_FR_FL,

    CA_FRC_RLC_RR_RL_FR_FL,
    CA_FRC_RLC_RR_RL_LFE_FR_FL,
    CA_FRC_RLC_RR_RL_FC_FR_FL,
    CA_FRC_RLC_RR_RL_FC_LFE_FR_FL,

}speak_location_t;

typedef enum {
	LSV_0DB = 0,
        LSV_1DB,
        LSV_2DB,
        LSV_3DB,
        LSV_4DB,
        LSV_5DB,
        LSV_6DB,
        LSV_7DB,
        LSV_8DB,
        LSV_9DB,
        LSV_10DB,
        LSV_11DB,
        LSV_12DB,
        LSV_13DB,
        LSV_14DB,
        LSV_15DB,

}audio_down_mix_t;

typedef enum
{
	 STATE_AUDIO__MUTED         =  0,
	 STATE_AUDIO__REQUEST_AUDIO = 1,
	 STATE_AUDIO__AUDIO_READY   = 2,
	 STATE_AUDIO__ON            = 3,
}hdmi_rx_audio_state_t;

typedef struct
{
    audio_type_t		            type ;     //!< Signal decoding type -- TvAudioType
    audio_format_t                      format;
     audio_channel_t			 channels ; //!< active audio channels bit mask.
    audio_fs_t				        fs;     //!< Signal sample rate in Hz
    audio_sample_size_t                 ss;
    speak_location_t                        speak_loc;
    audio_down_mix_t                       lsv;
    unsigned                                    N_value;
    unsigned                                    CTS;
} Hdmi_rx_audio_info_t;

#define HDMI_E_NONE         0x0
// HPD Event & Status
#define E_HPD_PULG_IN       0x1
#define E_HPD_PLUG_OUT      0x2
#define S_HPD_PLUG_IN       0x1
#define S_HPD_PLUG_OUT      0x0

#define E_HDCP_CHK_BKSV      0x1

typedef struct {
    unsigned int event;
    unsigned int stat;
}hdmi_mo_s;

//-----------------------HDMI AUDIO END----------------------------------------


//-------------------HDCP--------------------------------
// HDCP keys from Efuse are encrypted by default, in this test HDCP keys are written by CPU with encryption manually added
#define ENCRYPT_KEY                                 0xbe

typedef enum {
     HDCP_NO_AUTH						         = 0,
     HDCP_NO_DEVICE_WITH_SLAVE_ADDR	,
     HDCP_BCAP_ERROR						    ,
     HDCP_BKSV_ERROR						    ,
     HDCP_R0S_ARE_MISSMATCH				  ,
     HDCP_RIS_ARE_MISSMATCH				   ,
     HDCP_REAUTHENTATION_REQ				 ,
     HDCP_REQ_AUTHENTICATION				 ,
     HDCP_NO_ACK_FROM_DEV			       ,
     HDCP_NO_RSEN							       ,
     HDCP_AUTHENTICATED					     ,
     HDCP_REPEATER_AUTH_REQ				   ,
     HDCP_REQ_SHA_CALC					     ,
     HDCP_REQ_SHA_HW_CALC					   ,
     HDCP_FAILED_ViERROR					    ,
     HDCP_MAX
} hdcp_auth_state_t;



//-----------------------HDCP END----------------------------------------




//-------------------CEC-------------------------------------
#define CEC_MAX_CMD_SIZE 16 // defnes number of operands

typedef enum
{
    CEC_LogAddr_TV          = 0x00,
    CEC_LogAddr_RecDev1     = 0x01,
    CEC_LogAddr_RecDev2     = 0x02,
    CEC_LogAddr_STB1        = 0x03,
    CEC_LogAddr_DVD1        = 0x04,
    CEC_LogAddr_AudSys      = 0x05,
    CEC_LogAddr_STB2        = 0x06,
    CEC_LogAddr_STB3        = 0x07,
    CEC_LogAddr_DVD2        = 0x08,
    CEC_LogAddr_RecDev3     = 0x09,
    CEC_LogAddr_Res1        = 0x0A,
    CEC_LogAddr_Res2        = 0x0B,
    CEC_LogAddr_Res3        = 0x0C,
    CEC_LogAddr_Res4        = 0x0D,
    CEC_LogAddr_FreeUse     = 0x0E,
    LOGADDR_UNREGORBC_MSG   = 0x0F

} CEC_LogAddr_t;

typedef enum {
    CEC_TV = 0,
    CEC_DVD,
    CEC_STB,
    CEC_SW
} CECDev_t;

typedef enum {
    CEC_EnumTV = 0,
    CEC_EnumDVD
} CEC_EnumType;

typedef enum {
    CEC_Idle = 0,
    CEC_ReqPing,
    CEC_ReqCmd1,
    CEC_ReqCmd2,
    CEC_ReqCmd3,
    CEC_Enumiration
} CECOp_t;

typedef enum {
    CEC_TXWaitCmd,
    CEC_TXSending,
    CEC_TXSendAcked,
    CEC_TXFailedToSend
} CEC_TXState;

typedef enum
{
    CEC_POWERSTATUS_ON              = 0x00,
    CEC_POWERSTATUS_STANDBY         = 0x01,
    CEC_POWERSTATUS_STANDBY_TO_ON   = 0x02,
    CEC_POWERSTATUS_ON_TO_STANDBY   = 0x03
 } POWER_STATUS_ET;


typedef struct {
    unsigned char bRXState;
    CEC_TXState bTXState;
    unsigned char bCECErrors;

} CEC_Int_t;


typedef struct
{
    unsigned char bCount;
    unsigned char bRXNextCount;
    unsigned char bDestOrRXHeader;
    unsigned char bOpcode;
    unsigned char bOperand[ CEC_MAX_CMD_SIZE ];

} CEC_t;
//-----------------------CEC END----------------------------------------



//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//----------------------------------------------HDMI TX----------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
typedef enum {
      CABLE_UNPLUG                      = 0,
      CABLE_PLUGIN_CHECK_EDID_I2C_ERROR           ,
      CABLE_PLUGIN_CHECK_EDID_HEAD_ERROR,
      CABLE_PLUGIN_CHECK_EDID_CHECKSUM_ERROR,
      CABLE_PLUGIN_DVI_OUT                 ,
      CABLE_PLUGIN_HDMI_OUT                ,
      CABLE_MAX
}hdmi_tx_display_type_t;



         //0b000 = MCLK is 128*Fs
        //0b001 = MCLK is 256*Fs  (recommanded)
        //0b010 = MCLK is 384*Fs
        //0b011 = MCLK is 512*Fs
        //0b100 = MCLK is 768*Fs
        //0b101 = MCLK is 1024*Fs
        //0b110 = MCLK is 1152*Fs
        //0b111 = MCLK is 192*Fs
typedef enum {
      MCLK_128_Fs                      = 0,
       MCLK_256_Fs   ,
       MCLK_384_Fs                ,
        MCLK_512_Fs              ,
        MCLK_768_Fs,
        MCLK_1024_Fs,
        MCLK_1152_Fs
} hdmi_tx_audio_mclk_t;

typedef struct{
	int     hpd_state            : 1;
	int     support_480i         : 1;
	int     support_576i         : 1;
  int     support_480p         : 1;
  int     support_576p         : 1;
  int     support_720p_60hz    : 1;
  int     support_720p_50hz    : 1;
  int     support_1080i_60hz   : 1;
  int     support_1080i_50hz   : 1;
  int     support_1080p_60hz   : 1;
  int     support_1080p_50hz   : 1;
  int     support_1080p_24hz   : 1;
  int     support_1080p_25hz   : 1;
  int     support_1080p_30hz   : 1;
}hdmi_tx_sup_status_t;



typedef struct{
	int    support_flag     : 1;
	int    max_channel_num  : 3;
	int    _192k            : 1;
	int    _176k            : 1;
	int    _96k             : 1;
	int    _88k             : 1;
	int    _48k             : 1;
	int    _44k             : 1;
	int    _32k             : 1;
	int    _24bit           : 1;
	int    _20bit           : 1;
	int    _16bit           : 1;
}hdmi_tx_sup_lpcm_info_t;


typedef struct{
	int    support_flag     : 1;
	int    max_channel_num  : 3;
	int    _192k            : 1;
	int    _176k            : 1;
	int    _96k             : 1;
	int    _88k             : 1;
	int    _48k             : 1;
	int    _44k             : 1;
	int    _32k             : 1;
	int     _max_bit	 : 10;
}hdmi_tx_sup_compressed_info_t;

typedef struct{
	int    rlc_rrc : 1;
	int    flc_frc : 1;
  int    rc      : 1;
  int    rl_rr   : 1;
  int    fc      : 1;
  int    lfe     : 1;
  int    fl_fr   : 1;
}hdmi_tx_sup_speaker_format_t;


typedef struct{
	hdmi_tx_sup_lpcm_info_t			_60958_PCM;
	hdmi_tx_sup_compressed_info_t	_AC3;
	hdmi_tx_sup_compressed_info_t	_MPEG1;
	hdmi_tx_sup_compressed_info_t	_MP3;
	hdmi_tx_sup_compressed_info_t	_MPEG2;
	hdmi_tx_sup_compressed_info_t	_AAC;
	hdmi_tx_sup_compressed_info_t	_DTS;
	hdmi_tx_sup_compressed_info_t	_ATRAC;
	hdmi_tx_sup_compressed_info_t	_One_Bit_Audio;
	hdmi_tx_sup_compressed_info_t	_Dolby;
	hdmi_tx_sup_compressed_info_t	_DTS_HD;
	hdmi_tx_sup_compressed_info_t	_MAT;
	hdmi_tx_sup_compressed_info_t	_DST;
	hdmi_tx_sup_compressed_info_t	_WMA;
	hdmi_tx_sup_speaker_format_t		speaker_allocation;
}hdmi_tx_sup_audio_info_t;


typedef struct {
        unsigned char VIC;
        color_space_type_t color_prefer;
        color_space_type_t color;
        hdmi_color_depth_t color_depth;
        hdmi_bar_info_t     bar_info;
        hdmi_pixel_repeat_t repeat_time;
        hdmi_aspect_ratio_t aspect_ratio;
        hdmi_colorimetry_t cc;
        hdmi_scan_t            ss;
        hdmi_slacing_t          sc;
}Hdmi_tx_video_para_t;

typedef struct {
    audio_type_t type;  
    audio_channel_t channel_num;
    audio_fs_t sample_rate; 
    audio_sample_size_t sample_size; 
}Hdmi_tx_audio_para_t;    

// ACR packet CTS parameters have 3 types:
// 1. HW auto calculated
// 2. Fixed values defined by Spec
// 3. Calculated by clock meter
typedef enum {
    AUD_CTS_AUTO = 0,
    AUD_CTS_FIXED,
    AUD_CTS_CALC,
}Hdmi_tx_audio_cts_t;

typedef struct
{
    audio_type_t		            type ;     //!< Signal decoding type -- TvAudioType
    audio_format_t                      format;
     audio_channel_t			 channels ; //!< active audio channels bit mask.
    audio_fs_t				        fs;     //!< Signal sample rate in Hz
    audio_sample_size_t                 ss;
    hdmi_tx_audio_mclk_t            audio_mclk;

} Hdmi_tx_audio_info_t;

//-----------------Source Physical Address---------------
typedef struct {
	unsigned char a:4;
	unsigned char b:4;
	unsigned char c:4;
	unsigned char d:4;
	unsigned char valid;
}vsdb_phy_addr_t;


typedef struct {
        //Hdmi_tx_video_info_t            video_info;
        Hdmi_tx_audio_info_t            audio_info;
        hdmi_tx_sup_audio_info_t      tv_audio_info;
        hdcp_auth_state_t                  auth_state;
        hdmi_tx_display_type_t          output_state;
//-----------------Source Physical Address---------------
	vsdb_phy_addr_t vsdb_phy_addr;
//-------------------------------------------------------
        unsigned    video_out_changing_flag : 1;
        unsigned    support_underscan_flag : 1;
        unsigned    support_ycbcr444_flag : 1;
        unsigned    support_ycbcr422_flag : 1;
        unsigned    tx_video_input_stable_flag : 1;
        unsigned    auto_hdcp_ri_flag  : 1;   // If == 1, turn on Auto Ri Checking, user control
        unsigned    hw_sha_calculator_flag : 1;  // If  == 1, use the HW SHA calculator, otherwise, use SW SHA calculator, user control
        unsigned    need_sup_cec : 1;     //, user control

//-------------------------------------------------------
        unsigned    audio_out_changing_flag : 1;
        unsigned    audio_flag              : 1;        // 1 - enable hdmi audio;  0 - display hdmi audio, user control
        unsigned    support_basic_audio_flag : 1;
        unsigned        audio_fifo_overflow : 1;
        unsigned        audio_fifo_underflow : 1;
        unsigned        audio_cts_status_err_flag : 1;
        unsigned    support_ai_flag : 1;
        unsigned    hdmi_sup_480i : 1;


//-------------------------------------------------------
        unsigned	    hdmi_sup_576i : 1;
        unsigned        hdmi_sup_480p : 1;
        unsigned	        hdmi_sup_576p : 1;
        unsigned	        hdmi_sup_720p_60hz : 1;
        unsigned	        hdmi_sup_720p_50hz : 1;
        unsigned	        hdmi_sup_1080i_60hz : 1;
        unsigned	        hdmi_sup_1080i_50hz : 1;
        unsigned	        hdmi_sup_1080p_60hz : 1;



//-------------------------------------------------------
        unsigned	        hdmi_sup_1080p_50hz : 1;
        unsigned    hdmi_sup_1080p_24hz : 1;
        unsigned    hdmi_sup_1080p_25hz : 1;
        unsigned    hdmi_sup_1080p_30hz : 1;

//-------------------------------------------------------




}HDMI_TX_INFO_t;


#endif  // _HDMI_RX_GLOBAL_H
