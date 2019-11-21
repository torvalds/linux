/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ALSA SoC CX2072x Solana codec driver
 *
 * Copyright:   (C) 2016 Conexant Systems, Inc.
 */

#define NUM_OF_DAI 1
#define CX2072X_MCLK_PLL 1
#define CX2072X_MCLK_EXTERNAL_PLL 1
#define CX2072X_MCLK_INTERNAL_OSC 2

#define CX2072X_RATES		SNDRV_PCM_RATE_8000_192000
#define CX2072X_RATES_DSP	SNDRV_PCM_RATE_48000
#define CX2072X_RATES_MCLK	12288000

#define CX2072X_REG_MAX 0x8a3c
#define AUDDRV_VERSION(major0, major1, minor, build) \
		((major0) << 24 | (major1) << 16 | (minor) << 8 | (build))

#define CX2072X_VENDOR_ID                                0x0200
#define CX2072X_REVISION_ID                              0x0208
#define CX2072X_CURRENT_BCLK_FREQUENCY                   0x00dc
#define CX2072X_AFG_POWER_STATE                          0x0414
#define CX2072X_UM_RESPONSE                              0x0420
#define CX2072X_GPIO_DATA                                0x0454
#define CX2072X_GPIO_ENABLE                              0x0458
#define CX2072X_GPIO_DIRECTION                           0x045c
#define CX2072X_GPIO_WAKE                                0x0460
#define CX2072X_GPIO_UM_ENABLE                           0x0464
#define CX2072X_GPIO_STICKY_MASK                         0x0468
#define CX2072X_AFG_FUNCTION_RESET                       0x07FC
#define CX2072X_DAC1_CONVERTER_FORMAT                    0x43c8
#define CX2072X_DAC1_AMP_GAIN_RIGHT                      0x41c0
#define CX2072X_DAC1_AMP_GAIN_LEFT                       0x41e0
#define CX2072X_DAC1_POWER_STATE                         0x4014
#define CX2072X_DAC1_CONVERTER_STREAM_CHANNEL            0x4018
#define CX2072X_DAC1_EAPD_ENABLE                         0x4030
#define CX2072X_DAC2_CONVERTER_FORMAT                    0x47c8
#define CX2072X_DAC2_AMP_GAIN_RIGHT                      0x45c0
#define CX2072X_DAC2_AMP_GAIN_LEFT                       0x45e0
#define CX2072X_DAC2_POWER_STATE                         0x4414
#define CX2072X_DAC2_CONVERTER_STREAM_CHANNEL            0x4418
#define CX2072X_ADC1_CONVERTER_FORMAT                    0x4fc8
#define CX2072X_ADC1_AMP_GAIN_RIGHT_0                    0x4d80
#define CX2072X_ADC1_AMP_GAIN_LEFT_0                     0x4da0
#define CX2072X_ADC1_AMP_GAIN_RIGHT_1                    0x4d84
#define CX2072X_ADC1_AMP_GAIN_LEFT_1                     0x4da4
#define CX2072X_ADC1_AMP_GAIN_RIGHT_2                    0x4d88
#define CX2072X_ADC1_AMP_GAIN_LEFT_2                     0x4da8
#define CX2072X_ADC1_AMP_GAIN_RIGHT_3                    0x4d8c
#define CX2072X_ADC1_AMP_GAIN_LEFT_3                     0x4dac
#define CX2072X_ADC1_AMP_GAIN_RIGHT_4                    0x4d90
#define CX2072X_ADC1_AMP_GAIN_LEFT_4                     0x4db0
#define CX2072X_ADC1_AMP_GAIN_RIGHT_5                    0x4d94
#define CX2072X_ADC1_AMP_GAIN_LEFT_5                     0x4db4
#define CX2072X_ADC1_AMP_GAIN_RIGHT_6                    0x4d98
#define CX2072X_ADC1_AMP_GAIN_LEFT_6                     0x4db8
#define CX2072X_ADC1_CONNECTION_SELECT_CONTROL           0x4c04
#define CX2072X_ADC1_POWER_STATE                         0x4c14
#define CX2072X_ADC1_CONVERTER_STREAM_CHANNEL            0x4c18
#define CX2072X_ADC2_CONVERTER_FORMAT                    0x53c8
#define CX2072X_ADC2_AMP_GAIN_RIGHT_0                    0x5180
#define CX2072X_ADC2_AMP_GAIN_LEFT_0                     0x51a0
#define CX2072X_ADC2_AMP_GAIN_RIGHT_1                    0x5184
#define CX2072X_ADC2_AMP_GAIN_LEFT_1                     0x51a4
#define CX2072X_ADC2_AMP_GAIN_RIGHT_2                    0x5188
#define CX2072X_ADC2_AMP_GAIN_LEFT_2                     0x51a8
#define CX2072X_ADC2_CONNECTION_SELECT_CONTROL           0x5004
#define CX2072X_ADC2_POWER_STATE                         0x5014
#define CX2072X_ADC2_CONVERTER_STREAM_CHANNEL            0x5018
#define CX2072X_PORTA_CONNECTION_SELECT_CTRL             0x5804
#define CX2072X_PORTA_POWER_STATE                        0x5814
#define CX2072X_PORTA_PIN_CTRL                           0x581c
#define CX2072X_PORTA_UNSOLICITED_RESPONSE               0x5820
#define CX2072X_PORTA_PIN_SENSE                          0x5824
#define CX2072X_PORTA_EAPD_BTL                           0x5830
#define CX2072X_PORTB_POWER_STATE                        0x6014
#define CX2072X_PORTB_PIN_CTRL                           0x601c
#define CX2072X_PORTB_UNSOLICITED_RESPONSE               0x6020
#define CX2072X_PORTB_PIN_SENSE                          0x6024
#define CX2072X_PORTB_EAPD_BTL                           0x6030
#define CX2072X_PORTB_GAIN_RIGHT                         0x6180
#define CX2072X_PORTB_GAIN_LEFT                          0x61a0
#define CX2072X_PORTC_POWER_STATE                        0x6814
#define CX2072X_PORTC_PIN_CTRL                           0x681c
#define CX2072X_PORTC_GAIN_RIGHT                         0x6980
#define CX2072X_PORTC_GAIN_LEFT                          0x69a0
#define CX2072X_PORTD_POWER_STATE                        0x6414
#define CX2072X_PORTD_PIN_CTRL                           0x641c
#define CX2072X_PORTD_UNSOLICITED_RESPONSE               0x6420
#define CX2072X_PORTD_PIN_SENSE                          0x6424
#define CX2072X_PORTD_GAIN_RIGHT                         0x6580
#define CX2072X_PORTD_GAIN_LEFT                          0x65a0
#define CX2072X_PORTE_CONNECTION_SELECT_CTRL             0x7404
#define CX2072X_PORTE_POWER_STATE                        0x7414
#define CX2072X_PORTE_PIN_CTRL                           0x741c
#define CX2072X_PORTE_UNSOLICITED_RESPONSE               0x7420
#define CX2072X_PORTE_PIN_SENSE                          0x7424
#define CX2072X_PORTE_EAPD_BTL                           0x7430
#define CX2072X_PORTE_GAIN_RIGHT                         0x7580
#define CX2072X_PORTE_GAIN_LEFT                          0x75a0
#define CX2072X_PORTF_POWER_STATE                        0x7814
#define CX2072X_PORTF_PIN_CTRL                           0x781c
#define CX2072X_PORTF_UNSOLICITED_RESPONSE               0x7820
#define CX2072X_PORTF_PIN_SENSE                          0x7824
#define CX2072X_PORTF_GAIN_RIGHT                         0x7980
#define CX2072X_PORTF_GAIN_LEFT                          0x79a0
#define CX2072X_PORTG_POWER_STATE                        0x5c14
#define CX2072X_PORTG_PIN_CTRL                           0x5c1c
#define CX2072X_PORTG_CONNECTION_SELECT_CTRL             0x5c04
#define CX2072X_PORTG_EAPD_BTL                           0x5c30
#define CX2072X_PORTM_POWER_STATE                        0x8814
#define CX2072X_PORTM_PIN_CTRL                           0x881c
#define CX2072X_PORTM_CONNECTION_SELECT_CTRL             0x8804
#define CX2072X_PORTM_EAPD_BTL                           0x8830
#define CX2072X_MIXER_POWER_STATE                        0x5414
#define CX2072X_MIXER_GAIN_RIGHT_0                       0x5580
#define CX2072X_MIXER_GAIN_LEFT_0                        0x55a0
#define CX2072X_MIXER_GAIN_RIGHT_1                       0x5584
#define CX2072X_MIXER_GAIN_LEFT_1                        0x55a4
#define CX2072X_EQ_ENABLE_BYPASS                         0x6d00
#define CX2072X_EQ_B0_COEFF                              0x6d02
#define CX2072X_EQ_B1_COEFF                              0x6d04
#define CX2072X_EQ_B2_COEFF                              0x6d06
#define CX2072X_EQ_A1_COEFF                              0x6d08
#define CX2072X_EQ_A2_COEFF                              0x6d0a
#define CX2072X_EQ_G_COEFF                               0x6d0c
#define CX2072X_EQ_BAND                                  0x6d0d
#define CX2072X_SPKR_DRC_ENABLE_STEP                     0x6d10
#define CX2072X_SPKR_DRC_CONTROL                         0x6d14
#define CX2072X_SPKR_DRC_TEST                            0X6D18
#define CX2072X_DIGITAL_BIOS_TEST0                       0x6d80
#define CX2072X_DIGITAL_BIOS_TEST2                       0x6d84
#define CX2072X_I2SPCM_CONTROL1                          0x6e00
#define CX2072X_I2SPCM_CONTROL2                          0x6e04
#define CX2072X_I2SPCM_CONTROL3                          0x6e08
#define CX2072X_I2SPCM_CONTROL4                          0x6e0c
#define CX2072X_I2SPCM_CONTROL5                          0x6e10
#define CX2072X_I2SPCM_CONTROL6                          0x6e18
#define CX2072X_UM_INTERRUPT_CRTL_E                      0x6e14
#define CX2072X_CODEC_TEST2                              0x7108
#define CX2072X_CODEC_TEST9                              0x7124
#define CX2072X_CODEC_TEST20                             0x7310
#define CX2072X_CODEC_TEST26                             0x7328
#define CX2072X_ANALOG_TEST3                             0x718c
#define CX2072X_ANALOG_TEST4                             0x7190
#define CX2072X_ANALOG_TEST5                             0x7194
#define CX2072X_ANALOG_TEST6                             0x7198
#define CX2072X_ANALOG_TEST7                             0x719c
#define CX2072X_ANALOG_TEST8                             0x71a0
#define CX2072X_ANALOG_TEST9                             0x71a4
#define CX2072X_ANALOG_TEST10                            0x71a8
#define CX2072X_ANALOG_TEST11                            0x71ac
#define CX2072X_ANALOG_TEST12                            0x71b0
#define CX2072X_ANALOG_TEST13                            0x71b4
#define CX2072X_DIGITAL_TEST0                            0x7200
#define CX2072X_DIGITAL_TEST1                            0x7204
#define CX2072X_DIGITAL_TEST11                           0x722c
#define CX2072X_DIGITAL_TEST12                           0x7230
#define CX2072X_DIGITAL_TEST15                           0x723c
#define CX2072X_DIGITAL_TEST16                           0x7080
#define CX2072X_DIGITAL_TEST17                           0x7084
#define CX2072X_DIGITAL_TEST18                           0x7088
#define CX2072X_DIGITAL_TEST19                           0x708c
#define CX2072X_DIGITAL_TEST20                           0x7090

#define INVALID_GPIO -1
#define MAX_EQ_BAND 7
#define MAC_EQ_COEFF 11
#define MAX_DRC_REGS 9
#define MIC_EQ_COEFF 10
/* DAI interfae type*/

#define CX2072X_DAI_HIFI 1
#define CX2072X_DAI_DSP  2
/*4 ch, including mic and aec*/
#define CX2072X_DAI_DSP_PWM 3

enum cx2072x_jack_types {
	CX_JACK_NONE = 0x0000,
	CX_JACK_HEADPHONE = 0x0001,
	CX_JACK_APPLE_HEADSET = 0x0002,
	CX_JACK_NOKIE_HEADSET = 0x0003,
};

int cx2072x_hs_jack_report(struct snd_soc_component *component);

enum REG_SAMPLE_SIZE {
	SAMPLE_SIZE_8_BITS = 0,
	SAMPLE_SIZE_16_BITS = 1,
	SAMPLE_SIZE_24_BITS = 2,
	SAMPLE_SIZE_RESERVED = 3,
};

union REG_I2SPCM_CTRL_REG1 {
	struct {
		u32 rx_data_one_line :1;
		u32 rx_ws_pol        :1;
		u32 rx_ws_wid       :7;
		u32 rx_frm_len      :5;
		u32 rx_sa_size     :2;
		u32 tx_data_one_line :1;
		u32 tx_ws_pol        :1;
		u32 tx_ws_wid       :7;
		u32 tx_frm_len      :5;
		u32 tx_sa_size     :2;
	} r;
	u32 ulval;
};

union REG_I2SPCM_CTRL_REG2 {
	struct {
		u32 tx_en_ch1		:1;
		u32 tx_en_ch2		:1;
		u32 tx_en_ch3		:1;
		u32 tx_en_ch4		:1;
		u32 tx_en_ch5		:1;
		u32 tx_en_ch6		:1;
		u32 tx_slot_1         :5;
		u32 tx_slot_2         :5;
		u32 tx_slot_3         :5;
		u32 tx_slot_4         :5;
		u32  res               :1;
		u32  tx_data_neg_bclk  :1;
		u32  tx_master         :1;
		u32  tx_tri_n          :1;
		u32  tx_endian_sel     :1;
		u32  tx_dstart_dly     :1;
	} r;
	u32 ulval;
};

union REG_I2SPCM_CTRL_REG3 {
	struct {
		u32 rx_en_ch1		:1;
		u32 rx_en_ch2		:1;
		u32 rx_en_ch3		:1;
		u32 rx_en_ch4		:1;
		u32 rx_en_ch5		:1;
		u32 rx_en_ch6		:1;
		u32 rx_slot_1         :5;
		u32 rx_slot_2         :5;
		u32 rx_slot_3         :5;
		u32 rx_slot_4         :5;
		u32  res               :1;
		u32  rx_data_neg_bclk  :1;
		u32  rx_master         :1;
		u32  rx_tri_n          :1;
		u32  rx_endian_sel     :1;
		u32  rx_dstart_dly     :1;
	} r;
	u32 ulval;
};

union REG_I2SPCM_CTRL_REG4 {
	struct {
		u32 rx_mute		:1;
		u32 tx_mute		:1;
		u32 reserved		:1;
		u32 dac_34_independent	:1;
		u32 dac_bclk_lrck_share:1;
		u32 bclk_lrck_share_en :1;
		u32 reserved2          :2;
		u32 rx_last_dac_ch_en  :1;
		u32 rx_last_dac_ch     :3;
		u32 tx_last_adc_ch_en  :1;
		u32 tx_last_adc_ch     :3;
		u32 rx_slot_5         :5;
		u32 rx_slot_6		:5;
		u32 reserved3		:6;
	} r;
	u32 ulval;
};

union REG_I2SPCM_CTRL_REG5 {
	struct {
		u32 tx_slot_5         :5;
		u32 reserved          :3;
		u32 tx_slot_6         :5;
		u32 reserved2         :3;
		u32 reserved3         :8;
		u32 i2s_pcm_clk_div   :7;
		u32 i2s_pcm_clk_div_chan_en :1;
	} r;
	u32 ulval;
};

union REG_I2SPCM_CTRL_REG6 {
	struct {
		u32 reserved		:5;
		u32 rx_pause_cycles	:3;
		u32 rx_pause_start_pos	:8;
		u32 reserved2		:5;
		u32 tx_pause_cycles	:3;
		u32 tx_pause_start_pos	:8;
	} r;
	u32 ulval;
};

union REG_DIGITAL_BIOS_TEST2 {
	struct {
		u32 pull_down_eapd :2;
		u32  input_en_eapd_pad :1;
		u32  push_pull_mode    :1;
		u32 eapd_pad_output_driver :2;
		u32  pll_source        :1;
		u32  i2s_bclk_en       :1;
		u32  i2s_bclk_invert   :1;
		u32  pll_ref_clock     :1;
		u32  class_d_shield_clk:1;
		u32  audio_pll_bypass_mode:1;
		u32  reserved          :4;
	} r;
	u32 ulval;
};
