#ifndef WM8993_H
#define WM8993_H

#define WM8993_SYSCLK_MCLK     1
#define WM8993_SYSCLK_FLL      2

#define WM8993_FLL_MCLK  1
#define WM8993_FLL_BCLK  2
#define WM8993_FLL_LRCLK 3

/*
 * Register values.
 */
#define WM8993_SOFTWARE_RESET                   0x00
#define WM8993_POWER_MANAGEMENT_1               0x01
#define WM8993_POWER_MANAGEMENT_2               0x02
#define WM8993_POWER_MANAGEMENT_3               0x03
#define WM8993_AUDIO_INTERFACE_1                0x04
#define WM8993_AUDIO_INTERFACE_2                0x05
#define WM8993_CLOCKING_1                       0x06
#define WM8993_CLOCKING_2                       0x07
#define WM8993_AUDIO_INTERFACE_3                0x08
#define WM8993_AUDIO_INTERFACE_4                0x09
#define WM8993_DAC_CTRL                         0x0A
#define WM8993_LEFT_DAC_DIGITAL_VOLUME          0x0B
#define WM8993_RIGHT_DAC_DIGITAL_VOLUME         0x0C
#define WM8993_DIGITAL_SIDE_TONE                0x0D
#define WM8993_ADC_CTRL                         0x0E
#define WM8993_LEFT_ADC_DIGITAL_VOLUME          0x0F
#define WM8993_RIGHT_ADC_DIGITAL_VOLUME         0x10
#define WM8993_GPIO_CTRL_1                      0x12
#define WM8993_GPIO1                            0x13
#define WM8993_IRQ_DEBOUNCE                     0x14
#define WM8993_INPUTS_CLAMP_REG			0x15
#define WM8993_GPIOCTRL_2                       0x16
#define WM8993_GPIO_POL                         0x17
#define WM8993_LEFT_LINE_INPUT_1_2_VOLUME       0x18
#define WM8993_LEFT_LINE_INPUT_3_4_VOLUME       0x19
#define WM8993_RIGHT_LINE_INPUT_1_2_VOLUME      0x1A
#define WM8993_RIGHT_LINE_INPUT_3_4_VOLUME      0x1B
#define WM8993_LEFT_OUTPUT_VOLUME               0x1C
#define WM8993_RIGHT_OUTPUT_VOLUME              0x1D
#define WM8993_LINE_OUTPUTS_VOLUME              0x1E
#define WM8993_HPOUT2_VOLUME                    0x1F
#define WM8993_LEFT_OPGA_VOLUME                 0x20
#define WM8993_RIGHT_OPGA_VOLUME                0x21
#define WM8993_SPKMIXL_ATTENUATION              0x22
#define WM8993_SPKMIXR_ATTENUATION              0x23
#define WM8993_SPKOUT_MIXERS                    0x24
#define WM8993_SPKOUT_BOOST                     0x25
#define WM8993_SPEAKER_VOLUME_LEFT              0x26
#define WM8993_SPEAKER_VOLUME_RIGHT             0x27
#define WM8993_INPUT_MIXER2                     0x28
#define WM8993_INPUT_MIXER3                     0x29
#define WM8993_INPUT_MIXER4                     0x2A
#define WM8993_INPUT_MIXER5                     0x2B
#define WM8993_INPUT_MIXER6                     0x2C
#define WM8993_OUTPUT_MIXER1                    0x2D
#define WM8993_OUTPUT_MIXER2                    0x2E
#define WM8993_OUTPUT_MIXER3                    0x2F
#define WM8993_OUTPUT_MIXER4                    0x30
#define WM8993_OUTPUT_MIXER5                    0x31
#define WM8993_OUTPUT_MIXER6                    0x32
#define WM8993_HPOUT2_MIXER                     0x33
#define WM8993_LINE_MIXER1                      0x34
#define WM8993_LINE_MIXER2                      0x35
#define WM8993_SPEAKER_MIXER                    0x36
#define WM8993_ADDITIONAL_CONTROL               0x37
#define WM8993_ANTIPOP1                         0x38
#define WM8993_ANTIPOP2                         0x39
#define WM8993_MICBIAS                          0x3A
#define WM8993_FLL_CONTROL_1                    0x3C
#define WM8993_FLL_CONTROL_2                    0x3D
#define WM8993_FLL_CONTROL_3                    0x3E
#define WM8993_FLL_CONTROL_4                    0x3F
#define WM8993_FLL_CONTROL_5                    0x40
#define WM8993_CLOCKING_3                       0x41
#define WM8993_CLOCKING_4                       0x42
#define WM8993_MW_SLAVE_CONTROL                 0x43
#define WM8993_BUS_CONTROL_1                    0x45
#define WM8993_WRITE_SEQUENCER_0                0x46
#define WM8993_WRITE_SEQUENCER_1                0x47
#define WM8993_WRITE_SEQUENCER_2                0x48
#define WM8993_WRITE_SEQUENCER_3                0x49
#define WM8993_WRITE_SEQUENCER_4                0x4A
#define WM8993_WRITE_SEQUENCER_5                0x4B
#define WM8993_CHARGE_PUMP_1                    0x4C
#define WM8993_CLASS_W_0                        0x51
#define WM8993_DC_SERVO_0                       0x54
#define WM8993_DC_SERVO_1                       0x55
#define WM8993_DC_SERVO_3                       0x57
#define WM8993_DC_SERVO_READBACK_0              0x58
#define WM8993_DC_SERVO_READBACK_1              0x59
#define WM8993_DC_SERVO_READBACK_2              0x5A
#define WM8993_ANALOGUE_HP_0                    0x60
#define WM8993_EQ1                              0x62
#define WM8993_EQ2                              0x63
#define WM8993_EQ3                              0x64
#define WM8993_EQ4                              0x65
#define WM8993_EQ5                              0x66
#define WM8993_EQ6                              0x67
#define WM8993_EQ7                              0x68
#define WM8993_EQ8                              0x69
#define WM8993_EQ9                              0x6A
#define WM8993_EQ10                             0x6B
#define WM8993_EQ11                             0x6C
#define WM8993_EQ12                             0x6D
#define WM8993_EQ13                             0x6E
#define WM8993_EQ14                             0x6F
#define WM8993_EQ15                             0x70
#define WM8993_EQ16                             0x71
#define WM8993_EQ17                             0x72
#define WM8993_EQ18                             0x73
#define WM8993_EQ19                             0x74
#define WM8993_EQ20                             0x75
#define WM8993_EQ21                             0x76
#define WM8993_EQ22                             0x77
#define WM8993_EQ23                             0x78
#define WM8993_EQ24                             0x79
#define WM8993_DIGITAL_PULLS                    0x7A
#define WM8993_DRC_CONTROL_1                    0x7B
#define WM8993_DRC_CONTROL_2                    0x7C
#define WM8993_DRC_CONTROL_3                    0x7D
#define WM8993_DRC_CONTROL_4                    0x7E

#define WM8993_REGISTER_COUNT                   0x7F
#define WM8993_MAX_REGISTER                     0x7E

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - Software Reset
 */
#define WM8993_SW_RESET_MASK                    0xFFFF  /* SW_RESET - [15:0] */
#define WM8993_SW_RESET_SHIFT                        0  /* SW_RESET - [15:0] */
#define WM8993_SW_RESET_WIDTH                       16  /* SW_RESET - [15:0] */

/*
 * R1 (0x01) - Power Management (1)
 */
#define WM8993_SPKOUTR_ENA                      0x2000  /* SPKOUTR_ENA */
#define WM8993_SPKOUTR_ENA_MASK                 0x2000  /* SPKOUTR_ENA */
#define WM8993_SPKOUTR_ENA_SHIFT                    13  /* SPKOUTR_ENA */
#define WM8993_SPKOUTR_ENA_WIDTH                     1  /* SPKOUTR_ENA */
#define WM8993_SPKOUTL_ENA                      0x1000  /* SPKOUTL_ENA */
#define WM8993_SPKOUTL_ENA_MASK                 0x1000  /* SPKOUTL_ENA */
#define WM8993_SPKOUTL_ENA_SHIFT                    12  /* SPKOUTL_ENA */
#define WM8993_SPKOUTL_ENA_WIDTH                     1  /* SPKOUTL_ENA */
#define WM8993_HPOUT2_ENA                       0x0800  /* HPOUT2_ENA */
#define WM8993_HPOUT2_ENA_MASK                  0x0800  /* HPOUT2_ENA */
#define WM8993_HPOUT2_ENA_SHIFT                     11  /* HPOUT2_ENA */
#define WM8993_HPOUT2_ENA_WIDTH                      1  /* HPOUT2_ENA */
#define WM8993_HPOUT1L_ENA                      0x0200  /* HPOUT1L_ENA */
#define WM8993_HPOUT1L_ENA_MASK                 0x0200  /* HPOUT1L_ENA */
#define WM8993_HPOUT1L_ENA_SHIFT                     9  /* HPOUT1L_ENA */
#define WM8993_HPOUT1L_ENA_WIDTH                     1  /* HPOUT1L_ENA */
#define WM8993_HPOUT1R_ENA                      0x0100  /* HPOUT1R_ENA */
#define WM8993_HPOUT1R_ENA_MASK                 0x0100  /* HPOUT1R_ENA */
#define WM8993_HPOUT1R_ENA_SHIFT                     8  /* HPOUT1R_ENA */
#define WM8993_HPOUT1R_ENA_WIDTH                     1  /* HPOUT1R_ENA */
#define WM8993_MICB2_ENA                        0x0020  /* MICB2_ENA */
#define WM8993_MICB2_ENA_MASK                   0x0020  /* MICB2_ENA */
#define WM8993_MICB2_ENA_SHIFT                       5  /* MICB2_ENA */
#define WM8993_MICB2_ENA_WIDTH                       1  /* MICB2_ENA */
#define WM8993_MICB1_ENA                        0x0010  /* MICB1_ENA */
#define WM8993_MICB1_ENA_MASK                   0x0010  /* MICB1_ENA */
#define WM8993_MICB1_ENA_SHIFT                       4  /* MICB1_ENA */
#define WM8993_MICB1_ENA_WIDTH                       1  /* MICB1_ENA */
#define WM8993_VMID_SEL_MASK                    0x0006  /* VMID_SEL - [2:1] */
#define WM8993_VMID_SEL_SHIFT                        1  /* VMID_SEL - [2:1] */
#define WM8993_VMID_SEL_WIDTH                        2  /* VMID_SEL - [2:1] */
#define WM8993_BIAS_ENA                         0x0001  /* BIAS_ENA */
#define WM8993_BIAS_ENA_MASK                    0x0001  /* BIAS_ENA */
#define WM8993_BIAS_ENA_SHIFT                        0  /* BIAS_ENA */
#define WM8993_BIAS_ENA_WIDTH                        1  /* BIAS_ENA */

/*
 * R2 (0x02) - Power Management (2)
 */
#define WM8993_TSHUT_ENA                        0x4000  /* TSHUT_ENA */
#define WM8993_TSHUT_ENA_MASK                   0x4000  /* TSHUT_ENA */
#define WM8993_TSHUT_ENA_SHIFT                      14  /* TSHUT_ENA */
#define WM8993_TSHUT_ENA_WIDTH                       1  /* TSHUT_ENA */
#define WM8993_TSHUT_OPDIS                      0x2000  /* TSHUT_OPDIS */
#define WM8993_TSHUT_OPDIS_MASK                 0x2000  /* TSHUT_OPDIS */
#define WM8993_TSHUT_OPDIS_SHIFT                    13  /* TSHUT_OPDIS */
#define WM8993_TSHUT_OPDIS_WIDTH                     1  /* TSHUT_OPDIS */
#define WM8993_OPCLK_ENA                        0x0800  /* OPCLK_ENA */
#define WM8993_OPCLK_ENA_MASK                   0x0800  /* OPCLK_ENA */
#define WM8993_OPCLK_ENA_SHIFT                      11  /* OPCLK_ENA */
#define WM8993_OPCLK_ENA_WIDTH                       1  /* OPCLK_ENA */
#define WM8993_MIXINL_ENA                       0x0200  /* MIXINL_ENA */
#define WM8993_MIXINL_ENA_MASK                  0x0200  /* MIXINL_ENA */
#define WM8993_MIXINL_ENA_SHIFT                      9  /* MIXINL_ENA */
#define WM8993_MIXINL_ENA_WIDTH                      1  /* MIXINL_ENA */
#define WM8993_MIXINR_ENA                       0x0100  /* MIXINR_ENA */
#define WM8993_MIXINR_ENA_MASK                  0x0100  /* MIXINR_ENA */
#define WM8993_MIXINR_ENA_SHIFT                      8  /* MIXINR_ENA */
#define WM8993_MIXINR_ENA_WIDTH                      1  /* MIXINR_ENA */
#define WM8993_IN2L_ENA                         0x0080  /* IN2L_ENA */
#define WM8993_IN2L_ENA_MASK                    0x0080  /* IN2L_ENA */
#define WM8993_IN2L_ENA_SHIFT                        7  /* IN2L_ENA */
#define WM8993_IN2L_ENA_WIDTH                        1  /* IN2L_ENA */
#define WM8993_IN1L_ENA                         0x0040  /* IN1L_ENA */
#define WM8993_IN1L_ENA_MASK                    0x0040  /* IN1L_ENA */
#define WM8993_IN1L_ENA_SHIFT                        6  /* IN1L_ENA */
#define WM8993_IN1L_ENA_WIDTH                        1  /* IN1L_ENA */
#define WM8993_IN2R_ENA                         0x0020  /* IN2R_ENA */
#define WM8993_IN2R_ENA_MASK                    0x0020  /* IN2R_ENA */
#define WM8993_IN2R_ENA_SHIFT                        5  /* IN2R_ENA */
#define WM8993_IN2R_ENA_WIDTH                        1  /* IN2R_ENA */
#define WM8993_IN1R_ENA                         0x0010  /* IN1R_ENA */
#define WM8993_IN1R_ENA_MASK                    0x0010  /* IN1R_ENA */
#define WM8993_IN1R_ENA_SHIFT                        4  /* IN1R_ENA */
#define WM8993_IN1R_ENA_WIDTH                        1  /* IN1R_ENA */
#define WM8993_ADCL_ENA                         0x0002  /* ADCL_ENA */
#define WM8993_ADCL_ENA_MASK                    0x0002  /* ADCL_ENA */
#define WM8993_ADCL_ENA_SHIFT                        1  /* ADCL_ENA */
#define WM8993_ADCL_ENA_WIDTH                        1  /* ADCL_ENA */
#define WM8993_ADCR_ENA                         0x0001  /* ADCR_ENA */
#define WM8993_ADCR_ENA_MASK                    0x0001  /* ADCR_ENA */
#define WM8993_ADCR_ENA_SHIFT                        0  /* ADCR_ENA */
#define WM8993_ADCR_ENA_WIDTH                        1  /* ADCR_ENA */

/*
 * R3 (0x03) - Power Management (3)
 */
#define WM8993_LINEOUT1N_ENA                    0x2000  /* LINEOUT1N_ENA */
#define WM8993_LINEOUT1N_ENA_MASK               0x2000  /* LINEOUT1N_ENA */
#define WM8993_LINEOUT1N_ENA_SHIFT                  13  /* LINEOUT1N_ENA */
#define WM8993_LINEOUT1N_ENA_WIDTH                   1  /* LINEOUT1N_ENA */
#define WM8993_LINEOUT1P_ENA                    0x1000  /* LINEOUT1P_ENA */
#define WM8993_LINEOUT1P_ENA_MASK               0x1000  /* LINEOUT1P_ENA */
#define WM8993_LINEOUT1P_ENA_SHIFT                  12  /* LINEOUT1P_ENA */
#define WM8993_LINEOUT1P_ENA_WIDTH                   1  /* LINEOUT1P_ENA */
#define WM8993_LINEOUT2N_ENA                    0x0800  /* LINEOUT2N_ENA */
#define WM8993_LINEOUT2N_ENA_MASK               0x0800  /* LINEOUT2N_ENA */
#define WM8993_LINEOUT2N_ENA_SHIFT                  11  /* LINEOUT2N_ENA */
#define WM8993_LINEOUT2N_ENA_WIDTH                   1  /* LINEOUT2N_ENA */
#define WM8993_LINEOUT2P_ENA                    0x0400  /* LINEOUT2P_ENA */
#define WM8993_LINEOUT2P_ENA_MASK               0x0400  /* LINEOUT2P_ENA */
#define WM8993_LINEOUT2P_ENA_SHIFT                  10  /* LINEOUT2P_ENA */
#define WM8993_LINEOUT2P_ENA_WIDTH                   1  /* LINEOUT2P_ENA */
#define WM8993_SPKRVOL_ENA                      0x0200  /* SPKRVOL_ENA */
#define WM8993_SPKRVOL_ENA_MASK                 0x0200  /* SPKRVOL_ENA */
#define WM8993_SPKRVOL_ENA_SHIFT                     9  /* SPKRVOL_ENA */
#define WM8993_SPKRVOL_ENA_WIDTH                     1  /* SPKRVOL_ENA */
#define WM8993_SPKLVOL_ENA                      0x0100  /* SPKLVOL_ENA */
#define WM8993_SPKLVOL_ENA_MASK                 0x0100  /* SPKLVOL_ENA */
#define WM8993_SPKLVOL_ENA_SHIFT                     8  /* SPKLVOL_ENA */
#define WM8993_SPKLVOL_ENA_WIDTH                     1  /* SPKLVOL_ENA */
#define WM8993_MIXOUTLVOL_ENA                   0x0080  /* MIXOUTLVOL_ENA */
#define WM8993_MIXOUTLVOL_ENA_MASK              0x0080  /* MIXOUTLVOL_ENA */
#define WM8993_MIXOUTLVOL_ENA_SHIFT                  7  /* MIXOUTLVOL_ENA */
#define WM8993_MIXOUTLVOL_ENA_WIDTH                  1  /* MIXOUTLVOL_ENA */
#define WM8993_MIXOUTRVOL_ENA                   0x0040  /* MIXOUTRVOL_ENA */
#define WM8993_MIXOUTRVOL_ENA_MASK              0x0040  /* MIXOUTRVOL_ENA */
#define WM8993_MIXOUTRVOL_ENA_SHIFT                  6  /* MIXOUTRVOL_ENA */
#define WM8993_MIXOUTRVOL_ENA_WIDTH                  1  /* MIXOUTRVOL_ENA */
#define WM8993_MIXOUTL_ENA                      0x0020  /* MIXOUTL_ENA */
#define WM8993_MIXOUTL_ENA_MASK                 0x0020  /* MIXOUTL_ENA */
#define WM8993_MIXOUTL_ENA_SHIFT                     5  /* MIXOUTL_ENA */
#define WM8993_MIXOUTL_ENA_WIDTH                     1  /* MIXOUTL_ENA */
#define WM8993_MIXOUTR_ENA                      0x0010  /* MIXOUTR_ENA */
#define WM8993_MIXOUTR_ENA_MASK                 0x0010  /* MIXOUTR_ENA */
#define WM8993_MIXOUTR_ENA_SHIFT                     4  /* MIXOUTR_ENA */
#define WM8993_MIXOUTR_ENA_WIDTH                     1  /* MIXOUTR_ENA */
#define WM8993_DACL_ENA                         0x0002  /* DACL_ENA */
#define WM8993_DACL_ENA_MASK                    0x0002  /* DACL_ENA */
#define WM8993_DACL_ENA_SHIFT                        1  /* DACL_ENA */
#define WM8993_DACL_ENA_WIDTH                        1  /* DACL_ENA */
#define WM8993_DACR_ENA                         0x0001  /* DACR_ENA */
#define WM8993_DACR_ENA_MASK                    0x0001  /* DACR_ENA */
#define WM8993_DACR_ENA_SHIFT                        0  /* DACR_ENA */
#define WM8993_DACR_ENA_WIDTH                        1  /* DACR_ENA */

/*
 * R4 (0x04) - Audio Interface (1)
 */
#define WM8993_AIFADCL_SRC                      0x8000  /* AIFADCL_SRC */
#define WM8993_AIFADCL_SRC_MASK                 0x8000  /* AIFADCL_SRC */
#define WM8993_AIFADCL_SRC_SHIFT                    15  /* AIFADCL_SRC */
#define WM8993_AIFADCL_SRC_WIDTH                     1  /* AIFADCL_SRC */
#define WM8993_AIFADCR_SRC                      0x4000  /* AIFADCR_SRC */
#define WM8993_AIFADCR_SRC_MASK                 0x4000  /* AIFADCR_SRC */
#define WM8993_AIFADCR_SRC_SHIFT                    14  /* AIFADCR_SRC */
#define WM8993_AIFADCR_SRC_WIDTH                     1  /* AIFADCR_SRC */
#define WM8993_AIFADC_TDM                       0x2000  /* AIFADC_TDM */
#define WM8993_AIFADC_TDM_MASK                  0x2000  /* AIFADC_TDM */
#define WM8993_AIFADC_TDM_SHIFT                     13  /* AIFADC_TDM */
#define WM8993_AIFADC_TDM_WIDTH                      1  /* AIFADC_TDM */
#define WM8993_AIFADC_TDM_CHAN                  0x1000  /* AIFADC_TDM_CHAN */
#define WM8993_AIFADC_TDM_CHAN_MASK             0x1000  /* AIFADC_TDM_CHAN */
#define WM8993_AIFADC_TDM_CHAN_SHIFT                12  /* AIFADC_TDM_CHAN */
#define WM8993_AIFADC_TDM_CHAN_WIDTH                 1  /* AIFADC_TDM_CHAN */
#define WM8993_BCLK_DIR                         0x0200  /* BCLK_DIR */
#define WM8993_BCLK_DIR_MASK                    0x0200  /* BCLK_DIR */
#define WM8993_BCLK_DIR_SHIFT                        9  /* BCLK_DIR */
#define WM8993_BCLK_DIR_WIDTH                        1  /* BCLK_DIR */
#define WM8993_AIF_BCLK_INV                     0x0100  /* AIF_BCLK_INV */
#define WM8993_AIF_BCLK_INV_MASK                0x0100  /* AIF_BCLK_INV */
#define WM8993_AIF_BCLK_INV_SHIFT                    8  /* AIF_BCLK_INV */
#define WM8993_AIF_BCLK_INV_WIDTH                    1  /* AIF_BCLK_INV */
#define WM8993_AIF_LRCLK_INV                    0x0080  /* AIF_LRCLK_INV */
#define WM8993_AIF_LRCLK_INV_MASK               0x0080  /* AIF_LRCLK_INV */
#define WM8993_AIF_LRCLK_INV_SHIFT                   7  /* AIF_LRCLK_INV */
#define WM8993_AIF_LRCLK_INV_WIDTH                   1  /* AIF_LRCLK_INV */
#define WM8993_AIF_WL_MASK                      0x0060  /* AIF_WL - [6:5] */
#define WM8993_AIF_WL_SHIFT                          5  /* AIF_WL - [6:5] */
#define WM8993_AIF_WL_WIDTH                          2  /* AIF_WL - [6:5] */
#define WM8993_AIF_FMT_MASK                     0x0018  /* AIF_FMT - [4:3] */
#define WM8993_AIF_FMT_SHIFT                         3  /* AIF_FMT - [4:3] */
#define WM8993_AIF_FMT_WIDTH                         2  /* AIF_FMT - [4:3] */

/*
 * R5 (0x05) - Audio Interface (2)
 */
#define WM8993_AIFDACL_SRC                      0x8000  /* AIFDACL_SRC */
#define WM8993_AIFDACL_SRC_MASK                 0x8000  /* AIFDACL_SRC */
#define WM8993_AIFDACL_SRC_SHIFT                    15  /* AIFDACL_SRC */
#define WM8993_AIFDACL_SRC_WIDTH                     1  /* AIFDACL_SRC */
#define WM8993_AIFDACR_SRC                      0x4000  /* AIFDACR_SRC */
#define WM8993_AIFDACR_SRC_MASK                 0x4000  /* AIFDACR_SRC */
#define WM8993_AIFDACR_SRC_SHIFT                    14  /* AIFDACR_SRC */
#define WM8993_AIFDACR_SRC_WIDTH                     1  /* AIFDACR_SRC */
#define WM8993_AIFDAC_TDM                       0x2000  /* AIFDAC_TDM */
#define WM8993_AIFDAC_TDM_MASK                  0x2000  /* AIFDAC_TDM */
#define WM8993_AIFDAC_TDM_SHIFT                     13  /* AIFDAC_TDM */
#define WM8993_AIFDAC_TDM_WIDTH                      1  /* AIFDAC_TDM */
#define WM8993_AIFDAC_TDM_CHAN                  0x1000  /* AIFDAC_TDM_CHAN */
#define WM8993_AIFDAC_TDM_CHAN_MASK             0x1000  /* AIFDAC_TDM_CHAN */
#define WM8993_AIFDAC_TDM_CHAN_SHIFT                12  /* AIFDAC_TDM_CHAN */
#define WM8993_AIFDAC_TDM_CHAN_WIDTH                 1  /* AIFDAC_TDM_CHAN */
#define WM8993_DAC_BOOST_MASK                   0x0C00  /* DAC_BOOST - [11:10] */
#define WM8993_DAC_BOOST_SHIFT                      10  /* DAC_BOOST - [11:10] */
#define WM8993_DAC_BOOST_WIDTH                       2  /* DAC_BOOST - [11:10] */
#define WM8993_DAC_COMP                         0x0010  /* DAC_COMP */
#define WM8993_DAC_COMP_MASK                    0x0010  /* DAC_COMP */
#define WM8993_DAC_COMP_SHIFT                        4  /* DAC_COMP */
#define WM8993_DAC_COMP_WIDTH                        1  /* DAC_COMP */
#define WM8993_DAC_COMPMODE                     0x0008  /* DAC_COMPMODE */
#define WM8993_DAC_COMPMODE_MASK                0x0008  /* DAC_COMPMODE */
#define WM8993_DAC_COMPMODE_SHIFT                    3  /* DAC_COMPMODE */
#define WM8993_DAC_COMPMODE_WIDTH                    1  /* DAC_COMPMODE */
#define WM8993_ADC_COMP                         0x0004  /* ADC_COMP */
#define WM8993_ADC_COMP_MASK                    0x0004  /* ADC_COMP */
#define WM8993_ADC_COMP_SHIFT                        2  /* ADC_COMP */
#define WM8993_ADC_COMP_WIDTH                        1  /* ADC_COMP */
#define WM8993_ADC_COMPMODE                     0x0002  /* ADC_COMPMODE */
#define WM8993_ADC_COMPMODE_MASK                0x0002  /* ADC_COMPMODE */
#define WM8993_ADC_COMPMODE_SHIFT                    1  /* ADC_COMPMODE */
#define WM8993_ADC_COMPMODE_WIDTH                    1  /* ADC_COMPMODE */
#define WM8993_LOOPBACK                         0x0001  /* LOOPBACK */
#define WM8993_LOOPBACK_MASK                    0x0001  /* LOOPBACK */
#define WM8993_LOOPBACK_SHIFT                        0  /* LOOPBACK */
#define WM8993_LOOPBACK_WIDTH                        1  /* LOOPBACK */

/*
 * R6 (0x06) - Clocking 1
 */
#define WM8993_TOCLK_RATE                       0x8000  /* TOCLK_RATE */
#define WM8993_TOCLK_RATE_MASK                  0x8000  /* TOCLK_RATE */
#define WM8993_TOCLK_RATE_SHIFT                     15  /* TOCLK_RATE */
#define WM8993_TOCLK_RATE_WIDTH                      1  /* TOCLK_RATE */
#define WM8993_TOCLK_ENA                        0x4000  /* TOCLK_ENA */
#define WM8993_TOCLK_ENA_MASK                   0x4000  /* TOCLK_ENA */
#define WM8993_TOCLK_ENA_SHIFT                      14  /* TOCLK_ENA */
#define WM8993_TOCLK_ENA_WIDTH                       1  /* TOCLK_ENA */
#define WM8993_OPCLK_DIV_MASK                   0x1E00  /* OPCLK_DIV - [12:9] */
#define WM8993_OPCLK_DIV_SHIFT                       9  /* OPCLK_DIV - [12:9] */
#define WM8993_OPCLK_DIV_WIDTH                       4  /* OPCLK_DIV - [12:9] */
#define WM8993_DCLK_DIV_MASK                    0x01C0  /* DCLK_DIV - [8:6] */
#define WM8993_DCLK_DIV_SHIFT                        6  /* DCLK_DIV - [8:6] */
#define WM8993_DCLK_DIV_WIDTH                        3  /* DCLK_DIV - [8:6] */
#define WM8993_BCLK_DIV_MASK                    0x001E  /* BCLK_DIV - [4:1] */
#define WM8993_BCLK_DIV_SHIFT                        1  /* BCLK_DIV - [4:1] */
#define WM8993_BCLK_DIV_WIDTH                        4  /* BCLK_DIV - [4:1] */

/*
 * R7 (0x07) - Clocking 2
 */
#define WM8993_MCLK_SRC                         0x8000  /* MCLK_SRC */
#define WM8993_MCLK_SRC_MASK                    0x8000  /* MCLK_SRC */
#define WM8993_MCLK_SRC_SHIFT                       15  /* MCLK_SRC */
#define WM8993_MCLK_SRC_WIDTH                        1  /* MCLK_SRC */
#define WM8993_SYSCLK_SRC                       0x4000  /* SYSCLK_SRC */
#define WM8993_SYSCLK_SRC_MASK                  0x4000  /* SYSCLK_SRC */
#define WM8993_SYSCLK_SRC_SHIFT                     14  /* SYSCLK_SRC */
#define WM8993_SYSCLK_SRC_WIDTH                      1  /* SYSCLK_SRC */
#define WM8993_MCLK_DIV                         0x1000  /* MCLK_DIV */
#define WM8993_MCLK_DIV_MASK                    0x1000  /* MCLK_DIV */
#define WM8993_MCLK_DIV_SHIFT                       12  /* MCLK_DIV */
#define WM8993_MCLK_DIV_WIDTH                        1  /* MCLK_DIV */
#define WM8993_MCLK_INV                         0x0400  /* MCLK_INV */
#define WM8993_MCLK_INV_MASK                    0x0400  /* MCLK_INV */
#define WM8993_MCLK_INV_SHIFT                       10  /* MCLK_INV */
#define WM8993_MCLK_INV_WIDTH                        1  /* MCLK_INV */
#define WM8993_ADC_DIV_MASK                     0x00E0  /* ADC_DIV - [7:5] */
#define WM8993_ADC_DIV_SHIFT                         5  /* ADC_DIV - [7:5] */
#define WM8993_ADC_DIV_WIDTH                         3  /* ADC_DIV - [7:5] */
#define WM8993_DAC_DIV_MASK                     0x001C  /* DAC_DIV - [4:2] */
#define WM8993_DAC_DIV_SHIFT                         2  /* DAC_DIV - [4:2] */
#define WM8993_DAC_DIV_WIDTH                         3  /* DAC_DIV - [4:2] */

/*
 * R8 (0x08) - Audio Interface (3)
 */
#define WM8993_AIF_MSTR1                        0x8000  /* AIF_MSTR1 */
#define WM8993_AIF_MSTR1_MASK                   0x8000  /* AIF_MSTR1 */
#define WM8993_AIF_MSTR1_SHIFT                      15  /* AIF_MSTR1 */
#define WM8993_AIF_MSTR1_WIDTH                       1  /* AIF_MSTR1 */

/*
 * R9 (0x09) - Audio Interface (4)
 */
#define WM8993_AIF_TRIS                         0x2000  /* AIF_TRIS */
#define WM8993_AIF_TRIS_MASK                    0x2000  /* AIF_TRIS */
#define WM8993_AIF_TRIS_SHIFT                       13  /* AIF_TRIS */
#define WM8993_AIF_TRIS_WIDTH                        1  /* AIF_TRIS */
#define WM8993_LRCLK_DIR                        0x0800  /* LRCLK_DIR */
#define WM8993_LRCLK_DIR_MASK                   0x0800  /* LRCLK_DIR */
#define WM8993_LRCLK_DIR_SHIFT                      11  /* LRCLK_DIR */
#define WM8993_LRCLK_DIR_WIDTH                       1  /* LRCLK_DIR */
#define WM8993_LRCLK_RATE_MASK                  0x07FF  /* LRCLK_RATE - [10:0] */
#define WM8993_LRCLK_RATE_SHIFT                      0  /* LRCLK_RATE - [10:0] */
#define WM8993_LRCLK_RATE_WIDTH                     11  /* LRCLK_RATE - [10:0] */

/*
 * R10 (0x0A) - DAC CTRL
 */
#define WM8993_DAC_OSR128                       0x2000  /* DAC_OSR128 */
#define WM8993_DAC_OSR128_MASK                  0x2000  /* DAC_OSR128 */
#define WM8993_DAC_OSR128_SHIFT                     13  /* DAC_OSR128 */
#define WM8993_DAC_OSR128_WIDTH                      1  /* DAC_OSR128 */
#define WM8993_DAC_MONO                         0x0200  /* DAC_MONO */
#define WM8993_DAC_MONO_MASK                    0x0200  /* DAC_MONO */
#define WM8993_DAC_MONO_SHIFT                        9  /* DAC_MONO */
#define WM8993_DAC_MONO_WIDTH                        1  /* DAC_MONO */
#define WM8993_DAC_SB_FILT                      0x0100  /* DAC_SB_FILT */
#define WM8993_DAC_SB_FILT_MASK                 0x0100  /* DAC_SB_FILT */
#define WM8993_DAC_SB_FILT_SHIFT                     8  /* DAC_SB_FILT */
#define WM8993_DAC_SB_FILT_WIDTH                     1  /* DAC_SB_FILT */
#define WM8993_DAC_MUTERATE                     0x0080  /* DAC_MUTERATE */
#define WM8993_DAC_MUTERATE_MASK                0x0080  /* DAC_MUTERATE */
#define WM8993_DAC_MUTERATE_SHIFT                    7  /* DAC_MUTERATE */
#define WM8993_DAC_MUTERATE_WIDTH                    1  /* DAC_MUTERATE */
#define WM8993_DAC_UNMUTE_RAMP                  0x0040  /* DAC_UNMUTE_RAMP */
#define WM8993_DAC_UNMUTE_RAMP_MASK             0x0040  /* DAC_UNMUTE_RAMP */
#define WM8993_DAC_UNMUTE_RAMP_SHIFT                 6  /* DAC_UNMUTE_RAMP */
#define WM8993_DAC_UNMUTE_RAMP_WIDTH                 1  /* DAC_UNMUTE_RAMP */
#define WM8993_DEEMPH_MASK                      0x0030  /* DEEMPH - [5:4] */
#define WM8993_DEEMPH_SHIFT                          4  /* DEEMPH - [5:4] */
#define WM8993_DEEMPH_WIDTH                          2  /* DEEMPH - [5:4] */
#define WM8993_DAC_MUTE                         0x0004  /* DAC_MUTE */
#define WM8993_DAC_MUTE_MASK                    0x0004  /* DAC_MUTE */
#define WM8993_DAC_MUTE_SHIFT                        2  /* DAC_MUTE */
#define WM8993_DAC_MUTE_WIDTH                        1  /* DAC_MUTE */
#define WM8993_DACL_DATINV                      0x0002  /* DACL_DATINV */
#define WM8993_DACL_DATINV_MASK                 0x0002  /* DACL_DATINV */
#define WM8993_DACL_DATINV_SHIFT                     1  /* DACL_DATINV */
#define WM8993_DACL_DATINV_WIDTH                     1  /* DACL_DATINV */
#define WM8993_DACR_DATINV                      0x0001  /* DACR_DATINV */
#define WM8993_DACR_DATINV_MASK                 0x0001  /* DACR_DATINV */
#define WM8993_DACR_DATINV_SHIFT                     0  /* DACR_DATINV */
#define WM8993_DACR_DATINV_WIDTH                     1  /* DACR_DATINV */

/*
 * R11 (0x0B) - Left DAC Digital Volume
 */
#define WM8993_DAC_VU                           0x0100  /* DAC_VU */
#define WM8993_DAC_VU_MASK                      0x0100  /* DAC_VU */
#define WM8993_DAC_VU_SHIFT                          8  /* DAC_VU */
#define WM8993_DAC_VU_WIDTH                          1  /* DAC_VU */
#define WM8993_DACL_VOL_MASK                    0x00FF  /* DACL_VOL - [7:0] */
#define WM8993_DACL_VOL_SHIFT                        0  /* DACL_VOL - [7:0] */
#define WM8993_DACL_VOL_WIDTH                        8  /* DACL_VOL - [7:0] */

/*
 * R12 (0x0C) - Right DAC Digital Volume
 */
#define WM8993_DAC_VU                           0x0100  /* DAC_VU */
#define WM8993_DAC_VU_MASK                      0x0100  /* DAC_VU */
#define WM8993_DAC_VU_SHIFT                          8  /* DAC_VU */
#define WM8993_DAC_VU_WIDTH                          1  /* DAC_VU */
#define WM8993_DACR_VOL_MASK                    0x00FF  /* DACR_VOL - [7:0] */
#define WM8993_DACR_VOL_SHIFT                        0  /* DACR_VOL - [7:0] */
#define WM8993_DACR_VOL_WIDTH                        8  /* DACR_VOL - [7:0] */

/*
 * R13 (0x0D) - Digital Side Tone
 */
#define WM8993_ADCL_DAC_SVOL_MASK               0x1E00  /* ADCL_DAC_SVOL - [12:9] */
#define WM8993_ADCL_DAC_SVOL_SHIFT                   9  /* ADCL_DAC_SVOL - [12:9] */
#define WM8993_ADCL_DAC_SVOL_WIDTH                   4  /* ADCL_DAC_SVOL - [12:9] */
#define WM8993_ADCR_DAC_SVOL_MASK               0x01E0  /* ADCR_DAC_SVOL - [8:5] */
#define WM8993_ADCR_DAC_SVOL_SHIFT                   5  /* ADCR_DAC_SVOL - [8:5] */
#define WM8993_ADCR_DAC_SVOL_WIDTH                   4  /* ADCR_DAC_SVOL - [8:5] */
#define WM8993_ADC_TO_DACL_MASK                 0x000C  /* ADC_TO_DACL - [3:2] */
#define WM8993_ADC_TO_DACL_SHIFT                     2  /* ADC_TO_DACL - [3:2] */
#define WM8993_ADC_TO_DACL_WIDTH                     2  /* ADC_TO_DACL - [3:2] */
#define WM8993_ADC_TO_DACR_MASK                 0x0003  /* ADC_TO_DACR - [1:0] */
#define WM8993_ADC_TO_DACR_SHIFT                     0  /* ADC_TO_DACR - [1:0] */
#define WM8993_ADC_TO_DACR_WIDTH                     2  /* ADC_TO_DACR - [1:0] */

/*
 * R14 (0x0E) - ADC CTRL
 */
#define WM8993_ADC_OSR128                       0x0200  /* ADC_OSR128 */
#define WM8993_ADC_OSR128_MASK                  0x0200  /* ADC_OSR128 */
#define WM8993_ADC_OSR128_SHIFT                      9  /* ADC_OSR128 */
#define WM8993_ADC_OSR128_WIDTH                      1  /* ADC_OSR128 */
#define WM8993_ADC_HPF                          0x0100  /* ADC_HPF */
#define WM8993_ADC_HPF_MASK                     0x0100  /* ADC_HPF */
#define WM8993_ADC_HPF_SHIFT                         8  /* ADC_HPF */
#define WM8993_ADC_HPF_WIDTH                         1  /* ADC_HPF */
#define WM8993_ADC_HPF_CUT_MASK                 0x0060  /* ADC_HPF_CUT - [6:5] */
#define WM8993_ADC_HPF_CUT_SHIFT                     5  /* ADC_HPF_CUT - [6:5] */
#define WM8993_ADC_HPF_CUT_WIDTH                     2  /* ADC_HPF_CUT - [6:5] */
#define WM8993_ADCL_DATINV                      0x0002  /* ADCL_DATINV */
#define WM8993_ADCL_DATINV_MASK                 0x0002  /* ADCL_DATINV */
#define WM8993_ADCL_DATINV_SHIFT                     1  /* ADCL_DATINV */
#define WM8993_ADCL_DATINV_WIDTH                     1  /* ADCL_DATINV */
#define WM8993_ADCR_DATINV                      0x0001  /* ADCR_DATINV */
#define WM8993_ADCR_DATINV_MASK                 0x0001  /* ADCR_DATINV */
#define WM8993_ADCR_DATINV_SHIFT                     0  /* ADCR_DATINV */
#define WM8993_ADCR_DATINV_WIDTH                     1  /* ADCR_DATINV */

/*
 * R15 (0x0F) - Left ADC Digital Volume
 */
#define WM8993_ADC_VU                           0x0100  /* ADC_VU */
#define WM8993_ADC_VU_MASK                      0x0100  /* ADC_VU */
#define WM8993_ADC_VU_SHIFT                          8  /* ADC_VU */
#define WM8993_ADC_VU_WIDTH                          1  /* ADC_VU */
#define WM8993_ADCL_VOL_MASK                    0x00FF  /* ADCL_VOL - [7:0] */
#define WM8993_ADCL_VOL_SHIFT                        0  /* ADCL_VOL - [7:0] */
#define WM8993_ADCL_VOL_WIDTH                        8  /* ADCL_VOL - [7:0] */

/*
 * R16 (0x10) - Right ADC Digital Volume
 */
#define WM8993_ADC_VU                           0x0100  /* ADC_VU */
#define WM8993_ADC_VU_MASK                      0x0100  /* ADC_VU */
#define WM8993_ADC_VU_SHIFT                          8  /* ADC_VU */
#define WM8993_ADC_VU_WIDTH                          1  /* ADC_VU */
#define WM8993_ADCR_VOL_MASK                    0x00FF  /* ADCR_VOL - [7:0] */
#define WM8993_ADCR_VOL_SHIFT                        0  /* ADCR_VOL - [7:0] */
#define WM8993_ADCR_VOL_WIDTH                        8  /* ADCR_VOL - [7:0] */

/*
 * R18 (0x12) - GPIO CTRL 1
 */
#define WM8993_JD2_SC_EINT                      0x8000  /* JD2_SC_EINT */
#define WM8993_JD2_SC_EINT_MASK                 0x8000  /* JD2_SC_EINT */
#define WM8993_JD2_SC_EINT_SHIFT                    15  /* JD2_SC_EINT */
#define WM8993_JD2_SC_EINT_WIDTH                     1  /* JD2_SC_EINT */
#define WM8993_JD2_EINT                         0x4000  /* JD2_EINT */
#define WM8993_JD2_EINT_MASK                    0x4000  /* JD2_EINT */
#define WM8993_JD2_EINT_SHIFT                       14  /* JD2_EINT */
#define WM8993_JD2_EINT_WIDTH                        1  /* JD2_EINT */
#define WM8993_WSEQ_EINT                        0x2000  /* WSEQ_EINT */
#define WM8993_WSEQ_EINT_MASK                   0x2000  /* WSEQ_EINT */
#define WM8993_WSEQ_EINT_SHIFT                      13  /* WSEQ_EINT */
#define WM8993_WSEQ_EINT_WIDTH                       1  /* WSEQ_EINT */
#define WM8993_IRQ                              0x1000  /* IRQ */
#define WM8993_IRQ_MASK                         0x1000  /* IRQ */
#define WM8993_IRQ_SHIFT                            12  /* IRQ */
#define WM8993_IRQ_WIDTH                             1  /* IRQ */
#define WM8993_TEMPOK_EINT                      0x0800  /* TEMPOK_EINT */
#define WM8993_TEMPOK_EINT_MASK                 0x0800  /* TEMPOK_EINT */
#define WM8993_TEMPOK_EINT_SHIFT                    11  /* TEMPOK_EINT */
#define WM8993_TEMPOK_EINT_WIDTH                     1  /* TEMPOK_EINT */
#define WM8993_JD1_SC_EINT                      0x0400  /* JD1_SC_EINT */
#define WM8993_JD1_SC_EINT_MASK                 0x0400  /* JD1_SC_EINT */
#define WM8993_JD1_SC_EINT_SHIFT                    10  /* JD1_SC_EINT */
#define WM8993_JD1_SC_EINT_WIDTH                     1  /* JD1_SC_EINT */
#define WM8993_JD1_EINT                         0x0200  /* JD1_EINT */
#define WM8993_JD1_EINT_MASK                    0x0200  /* JD1_EINT */
#define WM8993_JD1_EINT_SHIFT                        9  /* JD1_EINT */
#define WM8993_JD1_EINT_WIDTH                        1  /* JD1_EINT */
#define WM8993_FLL_LOCK_EINT                    0x0100  /* FLL_LOCK_EINT */
#define WM8993_FLL_LOCK_EINT_MASK               0x0100  /* FLL_LOCK_EINT */
#define WM8993_FLL_LOCK_EINT_SHIFT                   8  /* FLL_LOCK_EINT */
#define WM8993_FLL_LOCK_EINT_WIDTH                   1  /* FLL_LOCK_EINT */
#define WM8993_GPI8_EINT                        0x0080  /* GPI8_EINT */
#define WM8993_GPI8_EINT_MASK                   0x0080  /* GPI8_EINT */
#define WM8993_GPI8_EINT_SHIFT                       7  /* GPI8_EINT */
#define WM8993_GPI8_EINT_WIDTH                       1  /* GPI8_EINT */
#define WM8993_GPI7_EINT                        0x0040  /* GPI7_EINT */
#define WM8993_GPI7_EINT_MASK                   0x0040  /* GPI7_EINT */
#define WM8993_GPI7_EINT_SHIFT                       6  /* GPI7_EINT */
#define WM8993_GPI7_EINT_WIDTH                       1  /* GPI7_EINT */
#define WM8993_GPIO1_EINT                       0x0001  /* GPIO1_EINT */
#define WM8993_GPIO1_EINT_MASK                  0x0001  /* GPIO1_EINT */
#define WM8993_GPIO1_EINT_SHIFT                      0  /* GPIO1_EINT */
#define WM8993_GPIO1_EINT_WIDTH                      1  /* GPIO1_EINT */

/*
 * R19 (0x13) - GPIO1
 */
#define WM8993_GPIO1_PU                         0x0020  /* GPIO1_PU */
#define WM8993_GPIO1_PU_MASK                    0x0020  /* GPIO1_PU */
#define WM8993_GPIO1_PU_SHIFT                        5  /* GPIO1_PU */
#define WM8993_GPIO1_PU_WIDTH                        1  /* GPIO1_PU */
#define WM8993_GPIO1_PD                         0x0010  /* GPIO1_PD */
#define WM8993_GPIO1_PD_MASK                    0x0010  /* GPIO1_PD */
#define WM8993_GPIO1_PD_SHIFT                        4  /* GPIO1_PD */
#define WM8993_GPIO1_PD_WIDTH                        1  /* GPIO1_PD */
#define WM8993_GPIO1_SEL_MASK                   0x000F  /* GPIO1_SEL - [3:0] */
#define WM8993_GPIO1_SEL_SHIFT                       0  /* GPIO1_SEL - [3:0] */
#define WM8993_GPIO1_SEL_WIDTH                       4  /* GPIO1_SEL - [3:0] */

/*
 * R20 (0x14) - IRQ_DEBOUNCE
 */
#define WM8993_JD2_SC_DB                        0x8000  /* JD2_SC_DB */
#define WM8993_JD2_SC_DB_MASK                   0x8000  /* JD2_SC_DB */
#define WM8993_JD2_SC_DB_SHIFT                      15  /* JD2_SC_DB */
#define WM8993_JD2_SC_DB_WIDTH                       1  /* JD2_SC_DB */
#define WM8993_JD2_DB                           0x4000  /* JD2_DB */
#define WM8993_JD2_DB_MASK                      0x4000  /* JD2_DB */
#define WM8993_JD2_DB_SHIFT                         14  /* JD2_DB */
#define WM8993_JD2_DB_WIDTH                          1  /* JD2_DB */
#define WM8993_WSEQ_DB                          0x2000  /* WSEQ_DB */
#define WM8993_WSEQ_DB_MASK                     0x2000  /* WSEQ_DB */
#define WM8993_WSEQ_DB_SHIFT                        13  /* WSEQ_DB */
#define WM8993_WSEQ_DB_WIDTH                         1  /* WSEQ_DB */
#define WM8993_TEMPOK_DB                        0x0800  /* TEMPOK_DB */
#define WM8993_TEMPOK_DB_MASK                   0x0800  /* TEMPOK_DB */
#define WM8993_TEMPOK_DB_SHIFT                      11  /* TEMPOK_DB */
#define WM8993_TEMPOK_DB_WIDTH                       1  /* TEMPOK_DB */
#define WM8993_JD1_SC_DB                        0x0400  /* JD1_SC_DB */
#define WM8993_JD1_SC_DB_MASK                   0x0400  /* JD1_SC_DB */
#define WM8993_JD1_SC_DB_SHIFT                      10  /* JD1_SC_DB */
#define WM8993_JD1_SC_DB_WIDTH                       1  /* JD1_SC_DB */
#define WM8993_JD1_DB                           0x0200  /* JD1_DB */
#define WM8993_JD1_DB_MASK                      0x0200  /* JD1_DB */
#define WM8993_JD1_DB_SHIFT                          9  /* JD1_DB */
#define WM8993_JD1_DB_WIDTH                          1  /* JD1_DB */
#define WM8993_FLL_LOCK_DB                      0x0100  /* FLL_LOCK_DB */
#define WM8993_FLL_LOCK_DB_MASK                 0x0100  /* FLL_LOCK_DB */
#define WM8993_FLL_LOCK_DB_SHIFT                     8  /* FLL_LOCK_DB */
#define WM8993_FLL_LOCK_DB_WIDTH                     1  /* FLL_LOCK_DB */
#define WM8993_GPI8_DB                          0x0080  /* GPI8_DB */
#define WM8993_GPI8_DB_MASK                     0x0080  /* GPI8_DB */
#define WM8993_GPI8_DB_SHIFT                         7  /* GPI8_DB */
#define WM8993_GPI8_DB_WIDTH                         1  /* GPI8_DB */
#define WM8993_GPI7_DB                          0x0008  /* GPI7_DB */
#define WM8993_GPI7_DB_MASK                     0x0008  /* GPI7_DB */
#define WM8993_GPI7_DB_SHIFT                         3  /* GPI7_DB */
#define WM8993_GPI7_DB_WIDTH                         1  /* GPI7_DB */
#define WM8993_GPIO1_DB                         0x0001  /* GPIO1_DB */
#define WM8993_GPIO1_DB_MASK                    0x0001  /* GPIO1_DB */
#define WM8993_GPIO1_DB_SHIFT                        0  /* GPIO1_DB */
#define WM8993_GPIO1_DB_WIDTH                        1  /* GPIO1_DB */

/*
 * R21 (0x15) - Inputs Clamp
 */
#define WM8993_INPUTS_CLAMP                     0x0040  /* INPUTS_CLAMP */
#define WM8993_INPUTS_CLAMP_MASK                0x0040  /* INPUTS_CLAMP */
#define WM8993_INPUTS_CLAMP_SHIFT                    7  /* INPUTS_CLAMP */
#define WM8993_INPUTS_CLAMP_WIDTH                    1  /* INPUTS_CLAMP */

/*
 * R22 (0x16) - GPIOCTRL 2
 */
#define WM8993_IM_JD2_EINT                      0x2000  /* IM_JD2_EINT */
#define WM8993_IM_JD2_EINT_MASK                 0x2000  /* IM_JD2_EINT */
#define WM8993_IM_JD2_EINT_SHIFT                    13  /* IM_JD2_EINT */
#define WM8993_IM_JD2_EINT_WIDTH                     1  /* IM_JD2_EINT */
#define WM8993_IM_JD2_SC_EINT                   0x1000  /* IM_JD2_SC_EINT */
#define WM8993_IM_JD2_SC_EINT_MASK              0x1000  /* IM_JD2_SC_EINT */
#define WM8993_IM_JD2_SC_EINT_SHIFT                 12  /* IM_JD2_SC_EINT */
#define WM8993_IM_JD2_SC_EINT_WIDTH                  1  /* IM_JD2_SC_EINT */
#define WM8993_IM_TEMPOK_EINT                   0x0800  /* IM_TEMPOK_EINT */
#define WM8993_IM_TEMPOK_EINT_MASK              0x0800  /* IM_TEMPOK_EINT */
#define WM8993_IM_TEMPOK_EINT_SHIFT                 11  /* IM_TEMPOK_EINT */
#define WM8993_IM_TEMPOK_EINT_WIDTH                  1  /* IM_TEMPOK_EINT */
#define WM8993_IM_JD1_SC_EINT                   0x0400  /* IM_JD1_SC_EINT */
#define WM8993_IM_JD1_SC_EINT_MASK              0x0400  /* IM_JD1_SC_EINT */
#define WM8993_IM_JD1_SC_EINT_SHIFT                 10  /* IM_JD1_SC_EINT */
#define WM8993_IM_JD1_SC_EINT_WIDTH                  1  /* IM_JD1_SC_EINT */
#define WM8993_IM_JD1_EINT                      0x0200  /* IM_JD1_EINT */
#define WM8993_IM_JD1_EINT_MASK                 0x0200  /* IM_JD1_EINT */
#define WM8993_IM_JD1_EINT_SHIFT                     9  /* IM_JD1_EINT */
#define WM8993_IM_JD1_EINT_WIDTH                     1  /* IM_JD1_EINT */
#define WM8993_IM_FLL_LOCK_EINT                 0x0100  /* IM_FLL_LOCK_EINT */
#define WM8993_IM_FLL_LOCK_EINT_MASK            0x0100  /* IM_FLL_LOCK_EINT */
#define WM8993_IM_FLL_LOCK_EINT_SHIFT                8  /* IM_FLL_LOCK_EINT */
#define WM8993_IM_FLL_LOCK_EINT_WIDTH                1  /* IM_FLL_LOCK_EINT */
#define WM8993_IM_GPI8_EINT                     0x0040  /* IM_GPI8_EINT */
#define WM8993_IM_GPI8_EINT_MASK                0x0040  /* IM_GPI8_EINT */
#define WM8993_IM_GPI8_EINT_SHIFT                    6  /* IM_GPI8_EINT */
#define WM8993_IM_GPI8_EINT_WIDTH                    1  /* IM_GPI8_EINT */
#define WM8993_IM_GPIO1_EINT                    0x0020  /* IM_GPIO1_EINT */
#define WM8993_IM_GPIO1_EINT_MASK               0x0020  /* IM_GPIO1_EINT */
#define WM8993_IM_GPIO1_EINT_SHIFT                   5  /* IM_GPIO1_EINT */
#define WM8993_IM_GPIO1_EINT_WIDTH                   1  /* IM_GPIO1_EINT */
#define WM8993_GPI8_ENA                         0x0010  /* GPI8_ENA */
#define WM8993_GPI8_ENA_MASK                    0x0010  /* GPI8_ENA */
#define WM8993_GPI8_ENA_SHIFT                        4  /* GPI8_ENA */
#define WM8993_GPI8_ENA_WIDTH                        1  /* GPI8_ENA */
#define WM8993_IM_GPI7_EINT                     0x0004  /* IM_GPI7_EINT */
#define WM8993_IM_GPI7_EINT_MASK                0x0004  /* IM_GPI7_EINT */
#define WM8993_IM_GPI7_EINT_SHIFT                    2  /* IM_GPI7_EINT */
#define WM8993_IM_GPI7_EINT_WIDTH                    1  /* IM_GPI7_EINT */
#define WM8993_IM_WSEQ_EINT                     0x0002  /* IM_WSEQ_EINT */
#define WM8993_IM_WSEQ_EINT_MASK                0x0002  /* IM_WSEQ_EINT */
#define WM8993_IM_WSEQ_EINT_SHIFT                    1  /* IM_WSEQ_EINT */
#define WM8993_IM_WSEQ_EINT_WIDTH                    1  /* IM_WSEQ_EINT */
#define WM8993_GPI7_ENA                         0x0001  /* GPI7_ENA */
#define WM8993_GPI7_ENA_MASK                    0x0001  /* GPI7_ENA */
#define WM8993_GPI7_ENA_SHIFT                        0  /* GPI7_ENA */
#define WM8993_GPI7_ENA_WIDTH                        1  /* GPI7_ENA */

/*
 * R23 (0x17) - GPIO_POL
 */
#define WM8993_JD2_SC_POL                       0x8000  /* JD2_SC_POL */
#define WM8993_JD2_SC_POL_MASK                  0x8000  /* JD2_SC_POL */
#define WM8993_JD2_SC_POL_SHIFT                     15  /* JD2_SC_POL */
#define WM8993_JD2_SC_POL_WIDTH                      1  /* JD2_SC_POL */
#define WM8993_JD2_POL                          0x4000  /* JD2_POL */
#define WM8993_JD2_POL_MASK                     0x4000  /* JD2_POL */
#define WM8993_JD2_POL_SHIFT                        14  /* JD2_POL */
#define WM8993_JD2_POL_WIDTH                         1  /* JD2_POL */
#define WM8993_WSEQ_POL                         0x2000  /* WSEQ_POL */
#define WM8993_WSEQ_POL_MASK                    0x2000  /* WSEQ_POL */
#define WM8993_WSEQ_POL_SHIFT                       13  /* WSEQ_POL */
#define WM8993_WSEQ_POL_WIDTH                        1  /* WSEQ_POL */
#define WM8993_IRQ_POL                          0x1000  /* IRQ_POL */
#define WM8993_IRQ_POL_MASK                     0x1000  /* IRQ_POL */
#define WM8993_IRQ_POL_SHIFT                        12  /* IRQ_POL */
#define WM8993_IRQ_POL_WIDTH                         1  /* IRQ_POL */
#define WM8993_TEMPOK_POL                       0x0800  /* TEMPOK_POL */
#define WM8993_TEMPOK_POL_MASK                  0x0800  /* TEMPOK_POL */
#define WM8993_TEMPOK_POL_SHIFT                     11  /* TEMPOK_POL */
#define WM8993_TEMPOK_POL_WIDTH                      1  /* TEMPOK_POL */
#define WM8993_JD1_SC_POL                       0x0400  /* JD1_SC_POL */
#define WM8993_JD1_SC_POL_MASK                  0x0400  /* JD1_SC_POL */
#define WM8993_JD1_SC_POL_SHIFT                     10  /* JD1_SC_POL */
#define WM8993_JD1_SC_POL_WIDTH                      1  /* JD1_SC_POL */
#define WM8993_JD1_POL                          0x0200  /* JD1_POL */
#define WM8993_JD1_POL_MASK                     0x0200  /* JD1_POL */
#define WM8993_JD1_POL_SHIFT                         9  /* JD1_POL */
#define WM8993_JD1_POL_WIDTH                         1  /* JD1_POL */
#define WM8993_FLL_LOCK_POL                     0x0100  /* FLL_LOCK_POL */
#define WM8993_FLL_LOCK_POL_MASK                0x0100  /* FLL_LOCK_POL */
#define WM8993_FLL_LOCK_POL_SHIFT                    8  /* FLL_LOCK_POL */
#define WM8993_FLL_LOCK_POL_WIDTH                    1  /* FLL_LOCK_POL */
#define WM8993_GPI8_POL                         0x0080  /* GPI8_POL */
#define WM8993_GPI8_POL_MASK                    0x0080  /* GPI8_POL */
#define WM8993_GPI8_POL_SHIFT                        7  /* GPI8_POL */
#define WM8993_GPI8_POL_WIDTH                        1  /* GPI8_POL */
#define WM8993_GPI7_POL                         0x0040  /* GPI7_POL */
#define WM8993_GPI7_POL_MASK                    0x0040  /* GPI7_POL */
#define WM8993_GPI7_POL_SHIFT                        6  /* GPI7_POL */
#define WM8993_GPI7_POL_WIDTH                        1  /* GPI7_POL */
#define WM8993_GPIO1_POL                        0x0001  /* GPIO1_POL */
#define WM8993_GPIO1_POL_MASK                   0x0001  /* GPIO1_POL */
#define WM8993_GPIO1_POL_SHIFT                       0  /* GPIO1_POL */
#define WM8993_GPIO1_POL_WIDTH                       1  /* GPIO1_POL */

/*
 * R24 (0x18) - Left Line Input 1&2 Volume
 */
#define WM8993_IN1_VU                           0x0100  /* IN1_VU */
#define WM8993_IN1_VU_MASK                      0x0100  /* IN1_VU */
#define WM8993_IN1_VU_SHIFT                          8  /* IN1_VU */
#define WM8993_IN1_VU_WIDTH                          1  /* IN1_VU */
#define WM8993_IN1L_MUTE                        0x0080  /* IN1L_MUTE */
#define WM8993_IN1L_MUTE_MASK                   0x0080  /* IN1L_MUTE */
#define WM8993_IN1L_MUTE_SHIFT                       7  /* IN1L_MUTE */
#define WM8993_IN1L_MUTE_WIDTH                       1  /* IN1L_MUTE */
#define WM8993_IN1L_ZC                          0x0040  /* IN1L_ZC */
#define WM8993_IN1L_ZC_MASK                     0x0040  /* IN1L_ZC */
#define WM8993_IN1L_ZC_SHIFT                         6  /* IN1L_ZC */
#define WM8993_IN1L_ZC_WIDTH                         1  /* IN1L_ZC */
#define WM8993_IN1L_VOL_MASK                    0x001F  /* IN1L_VOL - [4:0] */
#define WM8993_IN1L_VOL_SHIFT                        0  /* IN1L_VOL - [4:0] */
#define WM8993_IN1L_VOL_WIDTH                        5  /* IN1L_VOL - [4:0] */

/*
 * R25 (0x19) - Left Line Input 3&4 Volume
 */
#define WM8993_IN2_VU                           0x0100  /* IN2_VU */
#define WM8993_IN2_VU_MASK                      0x0100  /* IN2_VU */
#define WM8993_IN2_VU_SHIFT                          8  /* IN2_VU */
#define WM8993_IN2_VU_WIDTH                          1  /* IN2_VU */
#define WM8993_IN2L_MUTE                        0x0080  /* IN2L_MUTE */
#define WM8993_IN2L_MUTE_MASK                   0x0080  /* IN2L_MUTE */
#define WM8993_IN2L_MUTE_SHIFT                       7  /* IN2L_MUTE */
#define WM8993_IN2L_MUTE_WIDTH                       1  /* IN2L_MUTE */
#define WM8993_IN2L_ZC                          0x0040  /* IN2L_ZC */
#define WM8993_IN2L_ZC_MASK                     0x0040  /* IN2L_ZC */
#define WM8993_IN2L_ZC_SHIFT                         6  /* IN2L_ZC */
#define WM8993_IN2L_ZC_WIDTH                         1  /* IN2L_ZC */
#define WM8993_IN2L_VOL_MASK                    0x001F  /* IN2L_VOL - [4:0] */
#define WM8993_IN2L_VOL_SHIFT                        0  /* IN2L_VOL - [4:0] */
#define WM8993_IN2L_VOL_WIDTH                        5  /* IN2L_VOL - [4:0] */

/*
 * R26 (0x1A) - Right Line Input 1&2 Volume
 */
#define WM8993_IN1_VU                           0x0100  /* IN1_VU */
#define WM8993_IN1_VU_MASK                      0x0100  /* IN1_VU */
#define WM8993_IN1_VU_SHIFT                          8  /* IN1_VU */
#define WM8993_IN1_VU_WIDTH                          1  /* IN1_VU */
#define WM8993_IN1R_MUTE                        0x0080  /* IN1R_MUTE */
#define WM8993_IN1R_MUTE_MASK                   0x0080  /* IN1R_MUTE */
#define WM8993_IN1R_MUTE_SHIFT                       7  /* IN1R_MUTE */
#define WM8993_IN1R_MUTE_WIDTH                       1  /* IN1R_MUTE */
#define WM8993_IN1R_ZC                          0x0040  /* IN1R_ZC */
#define WM8993_IN1R_ZC_MASK                     0x0040  /* IN1R_ZC */
#define WM8993_IN1R_ZC_SHIFT                         6  /* IN1R_ZC */
#define WM8993_IN1R_ZC_WIDTH                         1  /* IN1R_ZC */
#define WM8993_IN1R_VOL_MASK                    0x001F  /* IN1R_VOL - [4:0] */
#define WM8993_IN1R_VOL_SHIFT                        0  /* IN1R_VOL - [4:0] */
#define WM8993_IN1R_VOL_WIDTH                        5  /* IN1R_VOL - [4:0] */

/*
 * R27 (0x1B) - Right Line Input 3&4 Volume
 */
#define WM8993_IN2_VU                           0x0100  /* IN2_VU */
#define WM8993_IN2_VU_MASK                      0x0100  /* IN2_VU */
#define WM8993_IN2_VU_SHIFT                          8  /* IN2_VU */
#define WM8993_IN2_VU_WIDTH                          1  /* IN2_VU */
#define WM8993_IN2R_MUTE                        0x0080  /* IN2R_MUTE */
#define WM8993_IN2R_MUTE_MASK                   0x0080  /* IN2R_MUTE */
#define WM8993_IN2R_MUTE_SHIFT                       7  /* IN2R_MUTE */
#define WM8993_IN2R_MUTE_WIDTH                       1  /* IN2R_MUTE */
#define WM8993_IN2R_ZC                          0x0040  /* IN2R_ZC */
#define WM8993_IN2R_ZC_MASK                     0x0040  /* IN2R_ZC */
#define WM8993_IN2R_ZC_SHIFT                         6  /* IN2R_ZC */
#define WM8993_IN2R_ZC_WIDTH                         1  /* IN2R_ZC */
#define WM8993_IN2R_VOL_MASK                    0x001F  /* IN2R_VOL - [4:0] */
#define WM8993_IN2R_VOL_SHIFT                        0  /* IN2R_VOL - [4:0] */
#define WM8993_IN2R_VOL_WIDTH                        5  /* IN2R_VOL - [4:0] */

/*
 * R28 (0x1C) - Left Output Volume
 */
#define WM8993_HPOUT1_VU                        0x0100  /* HPOUT1_VU */
#define WM8993_HPOUT1_VU_MASK                   0x0100  /* HPOUT1_VU */
#define WM8993_HPOUT1_VU_SHIFT                       8  /* HPOUT1_VU */
#define WM8993_HPOUT1_VU_WIDTH                       1  /* HPOUT1_VU */
#define WM8993_HPOUT1L_ZC                       0x0080  /* HPOUT1L_ZC */
#define WM8993_HPOUT1L_ZC_MASK                  0x0080  /* HPOUT1L_ZC */
#define WM8993_HPOUT1L_ZC_SHIFT                      7  /* HPOUT1L_ZC */
#define WM8993_HPOUT1L_ZC_WIDTH                      1  /* HPOUT1L_ZC */
#define WM8993_HPOUT1L_MUTE_N                   0x0040  /* HPOUT1L_MUTE_N */
#define WM8993_HPOUT1L_MUTE_N_MASK              0x0040  /* HPOUT1L_MUTE_N */
#define WM8993_HPOUT1L_MUTE_N_SHIFT                  6  /* HPOUT1L_MUTE_N */
#define WM8993_HPOUT1L_MUTE_N_WIDTH                  1  /* HPOUT1L_MUTE_N */
#define WM8993_HPOUT1L_VOL_MASK                 0x003F  /* HPOUT1L_VOL - [5:0] */
#define WM8993_HPOUT1L_VOL_SHIFT                     0  /* HPOUT1L_VOL - [5:0] */
#define WM8993_HPOUT1L_VOL_WIDTH                     6  /* HPOUT1L_VOL - [5:0] */

/*
 * R29 (0x1D) - Right Output Volume
 */
#define WM8993_HPOUT1_VU                        0x0100  /* HPOUT1_VU */
#define WM8993_HPOUT1_VU_MASK                   0x0100  /* HPOUT1_VU */
#define WM8993_HPOUT1_VU_SHIFT                       8  /* HPOUT1_VU */
#define WM8993_HPOUT1_VU_WIDTH                       1  /* HPOUT1_VU */
#define WM8993_HPOUT1R_ZC                       0x0080  /* HPOUT1R_ZC */
#define WM8993_HPOUT1R_ZC_MASK                  0x0080  /* HPOUT1R_ZC */
#define WM8993_HPOUT1R_ZC_SHIFT                      7  /* HPOUT1R_ZC */
#define WM8993_HPOUT1R_ZC_WIDTH                      1  /* HPOUT1R_ZC */
#define WM8993_HPOUT1R_MUTE_N                   0x0040  /* HPOUT1R_MUTE_N */
#define WM8993_HPOUT1R_MUTE_N_MASK              0x0040  /* HPOUT1R_MUTE_N */
#define WM8993_HPOUT1R_MUTE_N_SHIFT                  6  /* HPOUT1R_MUTE_N */
#define WM8993_HPOUT1R_MUTE_N_WIDTH                  1  /* HPOUT1R_MUTE_N */
#define WM8993_HPOUT1R_VOL_MASK                 0x003F  /* HPOUT1R_VOL - [5:0] */
#define WM8993_HPOUT1R_VOL_SHIFT                     0  /* HPOUT1R_VOL - [5:0] */
#define WM8993_HPOUT1R_VOL_WIDTH                     6  /* HPOUT1R_VOL - [5:0] */

/*
 * R30 (0x1E) - Line Outputs Volume
 */
#define WM8993_LINEOUT1N_MUTE                   0x0040  /* LINEOUT1N_MUTE */
#define WM8993_LINEOUT1N_MUTE_MASK              0x0040  /* LINEOUT1N_MUTE */
#define WM8993_LINEOUT1N_MUTE_SHIFT                  6  /* LINEOUT1N_MUTE */
#define WM8993_LINEOUT1N_MUTE_WIDTH                  1  /* LINEOUT1N_MUTE */
#define WM8993_LINEOUT1P_MUTE                   0x0020  /* LINEOUT1P_MUTE */
#define WM8993_LINEOUT1P_MUTE_MASK              0x0020  /* LINEOUT1P_MUTE */
#define WM8993_LINEOUT1P_MUTE_SHIFT                  5  /* LINEOUT1P_MUTE */
#define WM8993_LINEOUT1P_MUTE_WIDTH                  1  /* LINEOUT1P_MUTE */
#define WM8993_LINEOUT1_VOL                     0x0010  /* LINEOUT1_VOL */
#define WM8993_LINEOUT1_VOL_MASK                0x0010  /* LINEOUT1_VOL */
#define WM8993_LINEOUT1_VOL_SHIFT                    4  /* LINEOUT1_VOL */
#define WM8993_LINEOUT1_VOL_WIDTH                    1  /* LINEOUT1_VOL */
#define WM8993_LINEOUT2N_MUTE                   0x0004  /* LINEOUT2N_MUTE */
#define WM8993_LINEOUT2N_MUTE_MASK              0x0004  /* LINEOUT2N_MUTE */
#define WM8993_LINEOUT2N_MUTE_SHIFT                  2  /* LINEOUT2N_MUTE */
#define WM8993_LINEOUT2N_MUTE_WIDTH                  1  /* LINEOUT2N_MUTE */
#define WM8993_LINEOUT2P_MUTE                   0x0002  /* LINEOUT2P_MUTE */
#define WM8993_LINEOUT2P_MUTE_MASK              0x0002  /* LINEOUT2P_MUTE */
#define WM8993_LINEOUT2P_MUTE_SHIFT                  1  /* LINEOUT2P_MUTE */
#define WM8993_LINEOUT2P_MUTE_WIDTH                  1  /* LINEOUT2P_MUTE */
#define WM8993_LINEOUT2_VOL                     0x0001  /* LINEOUT2_VOL */
#define WM8993_LINEOUT2_VOL_MASK                0x0001  /* LINEOUT2_VOL */
#define WM8993_LINEOUT2_VOL_SHIFT                    0  /* LINEOUT2_VOL */
#define WM8993_LINEOUT2_VOL_WIDTH                    1  /* LINEOUT2_VOL */

/*
 * R31 (0x1F) - HPOUT2 Volume
 */
#define WM8993_HPOUT2_MUTE                      0x0020  /* HPOUT2_MUTE */
#define WM8993_HPOUT2_MUTE_MASK                 0x0020  /* HPOUT2_MUTE */
#define WM8993_HPOUT2_MUTE_SHIFT                     5  /* HPOUT2_MUTE */
#define WM8993_HPOUT2_MUTE_WIDTH                     1  /* HPOUT2_MUTE */
#define WM8993_HPOUT2_VOL                       0x0010  /* HPOUT2_VOL */
#define WM8993_HPOUT2_VOL_MASK                  0x0010  /* HPOUT2_VOL */
#define WM8993_HPOUT2_VOL_SHIFT                      4  /* HPOUT2_VOL */
#define WM8993_HPOUT2_VOL_WIDTH                      1  /* HPOUT2_VOL */

/*
 * R32 (0x20) - Left OPGA Volume
 */
#define WM8993_MIXOUT_VU                        0x0100  /* MIXOUT_VU */
#define WM8993_MIXOUT_VU_MASK                   0x0100  /* MIXOUT_VU */
#define WM8993_MIXOUT_VU_SHIFT                       8  /* MIXOUT_VU */
#define WM8993_MIXOUT_VU_WIDTH                       1  /* MIXOUT_VU */
#define WM8993_MIXOUTL_ZC                       0x0080  /* MIXOUTL_ZC */
#define WM8993_MIXOUTL_ZC_MASK                  0x0080  /* MIXOUTL_ZC */
#define WM8993_MIXOUTL_ZC_SHIFT                      7  /* MIXOUTL_ZC */
#define WM8993_MIXOUTL_ZC_WIDTH                      1  /* MIXOUTL_ZC */
#define WM8993_MIXOUTL_MUTE_N                   0x0040  /* MIXOUTL_MUTE_N */
#define WM8993_MIXOUTL_MUTE_N_MASK              0x0040  /* MIXOUTL_MUTE_N */
#define WM8993_MIXOUTL_MUTE_N_SHIFT                  6  /* MIXOUTL_MUTE_N */
#define WM8993_MIXOUTL_MUTE_N_WIDTH                  1  /* MIXOUTL_MUTE_N */
#define WM8993_MIXOUTL_VOL_MASK                 0x003F  /* MIXOUTL_VOL - [5:0] */
#define WM8993_MIXOUTL_VOL_SHIFT                     0  /* MIXOUTL_VOL - [5:0] */
#define WM8993_MIXOUTL_VOL_WIDTH                     6  /* MIXOUTL_VOL - [5:0] */

/*
 * R33 (0x21) - Right OPGA Volume
 */
#define WM8993_MIXOUT_VU                        0x0100  /* MIXOUT_VU */
#define WM8993_MIXOUT_VU_MASK                   0x0100  /* MIXOUT_VU */
#define WM8993_MIXOUT_VU_SHIFT                       8  /* MIXOUT_VU */
#define WM8993_MIXOUT_VU_WIDTH                       1  /* MIXOUT_VU */
#define WM8993_MIXOUTR_ZC                       0x0080  /* MIXOUTR_ZC */
#define WM8993_MIXOUTR_ZC_MASK                  0x0080  /* MIXOUTR_ZC */
#define WM8993_MIXOUTR_ZC_SHIFT                      7  /* MIXOUTR_ZC */
#define WM8993_MIXOUTR_ZC_WIDTH                      1  /* MIXOUTR_ZC */
#define WM8993_MIXOUTR_MUTE_N                   0x0040  /* MIXOUTR_MUTE_N */
#define WM8993_MIXOUTR_MUTE_N_MASK              0x0040  /* MIXOUTR_MUTE_N */
#define WM8993_MIXOUTR_MUTE_N_SHIFT                  6  /* MIXOUTR_MUTE_N */
#define WM8993_MIXOUTR_MUTE_N_WIDTH                  1  /* MIXOUTR_MUTE_N */
#define WM8993_MIXOUTR_VOL_MASK                 0x003F  /* MIXOUTR_VOL - [5:0] */
#define WM8993_MIXOUTR_VOL_SHIFT                     0  /* MIXOUTR_VOL - [5:0] */
#define WM8993_MIXOUTR_VOL_WIDTH                     6  /* MIXOUTR_VOL - [5:0] */

/*
 * R34 (0x22) - SPKMIXL Attenuation
 */
#define WM8993_MIXINL_SPKMIXL_VOL               0x0020  /* MIXINL_SPKMIXL_VOL */
#define WM8993_MIXINL_SPKMIXL_VOL_MASK          0x0020  /* MIXINL_SPKMIXL_VOL */
#define WM8993_MIXINL_SPKMIXL_VOL_SHIFT              5  /* MIXINL_SPKMIXL_VOL */
#define WM8993_MIXINL_SPKMIXL_VOL_WIDTH              1  /* MIXINL_SPKMIXL_VOL */
#define WM8993_IN1LP_SPKMIXL_VOL                0x0010  /* IN1LP_SPKMIXL_VOL */
#define WM8993_IN1LP_SPKMIXL_VOL_MASK           0x0010  /* IN1LP_SPKMIXL_VOL */
#define WM8993_IN1LP_SPKMIXL_VOL_SHIFT               4  /* IN1LP_SPKMIXL_VOL */
#define WM8993_IN1LP_SPKMIXL_VOL_WIDTH               1  /* IN1LP_SPKMIXL_VOL */
#define WM8993_MIXOUTL_SPKMIXL_VOL              0x0008  /* MIXOUTL_SPKMIXL_VOL */
#define WM8993_MIXOUTL_SPKMIXL_VOL_MASK         0x0008  /* MIXOUTL_SPKMIXL_VOL */
#define WM8993_MIXOUTL_SPKMIXL_VOL_SHIFT             3  /* MIXOUTL_SPKMIXL_VOL */
#define WM8993_MIXOUTL_SPKMIXL_VOL_WIDTH             1  /* MIXOUTL_SPKMIXL_VOL */
#define WM8993_DACL_SPKMIXL_VOL                 0x0004  /* DACL_SPKMIXL_VOL */
#define WM8993_DACL_SPKMIXL_VOL_MASK            0x0004  /* DACL_SPKMIXL_VOL */
#define WM8993_DACL_SPKMIXL_VOL_SHIFT                2  /* DACL_SPKMIXL_VOL */
#define WM8993_DACL_SPKMIXL_VOL_WIDTH                1  /* DACL_SPKMIXL_VOL */
#define WM8993_SPKMIXL_VOL_MASK                 0x0003  /* SPKMIXL_VOL - [1:0] */
#define WM8993_SPKMIXL_VOL_SHIFT                     0  /* SPKMIXL_VOL - [1:0] */
#define WM8993_SPKMIXL_VOL_WIDTH                     2  /* SPKMIXL_VOL - [1:0] */

/*
 * R35 (0x23) - SPKMIXR Attenuation
 */
#define WM8993_SPKOUT_CLASSAB_MODE              0x0100  /* SPKOUT_CLASSAB_MODE */
#define WM8993_SPKOUT_CLASSAB_MODE_MASK         0x0100  /* SPKOUT_CLASSAB_MODE */
#define WM8993_SPKOUT_CLASSAB_MODE_SHIFT             8  /* SPKOUT_CLASSAB_MODE */
#define WM8993_SPKOUT_CLASSAB_MODE_WIDTH             1  /* SPKOUT_CLASSAB_MODE */
#define WM8993_MIXINR_SPKMIXR_VOL               0x0020  /* MIXINR_SPKMIXR_VOL */
#define WM8993_MIXINR_SPKMIXR_VOL_MASK          0x0020  /* MIXINR_SPKMIXR_VOL */
#define WM8993_MIXINR_SPKMIXR_VOL_SHIFT              5  /* MIXINR_SPKMIXR_VOL */
#define WM8993_MIXINR_SPKMIXR_VOL_WIDTH              1  /* MIXINR_SPKMIXR_VOL */
#define WM8993_IN1RP_SPKMIXR_VOL                0x0010  /* IN1RP_SPKMIXR_VOL */
#define WM8993_IN1RP_SPKMIXR_VOL_MASK           0x0010  /* IN1RP_SPKMIXR_VOL */
#define WM8993_IN1RP_SPKMIXR_VOL_SHIFT               4  /* IN1RP_SPKMIXR_VOL */
#define WM8993_IN1RP_SPKMIXR_VOL_WIDTH               1  /* IN1RP_SPKMIXR_VOL */
#define WM8993_MIXOUTR_SPKMIXR_VOL              0x0008  /* MIXOUTR_SPKMIXR_VOL */
#define WM8993_MIXOUTR_SPKMIXR_VOL_MASK         0x0008  /* MIXOUTR_SPKMIXR_VOL */
#define WM8993_MIXOUTR_SPKMIXR_VOL_SHIFT             3  /* MIXOUTR_SPKMIXR_VOL */
#define WM8993_MIXOUTR_SPKMIXR_VOL_WIDTH             1  /* MIXOUTR_SPKMIXR_VOL */
#define WM8993_DACR_SPKMIXR_VOL                 0x0004  /* DACR_SPKMIXR_VOL */
#define WM8993_DACR_SPKMIXR_VOL_MASK            0x0004  /* DACR_SPKMIXR_VOL */
#define WM8993_DACR_SPKMIXR_VOL_SHIFT                2  /* DACR_SPKMIXR_VOL */
#define WM8993_DACR_SPKMIXR_VOL_WIDTH                1  /* DACR_SPKMIXR_VOL */
#define WM8993_SPKMIXR_VOL_MASK                 0x0003  /* SPKMIXR_VOL - [1:0] */
#define WM8993_SPKMIXR_VOL_SHIFT                     0  /* SPKMIXR_VOL - [1:0] */
#define WM8993_SPKMIXR_VOL_WIDTH                     2  /* SPKMIXR_VOL - [1:0] */

/*
 * R36 (0x24) - SPKOUT Mixers
 */
#define WM8993_VRX_TO_SPKOUTL                   0x0020  /* VRX_TO_SPKOUTL */
#define WM8993_VRX_TO_SPKOUTL_MASK              0x0020  /* VRX_TO_SPKOUTL */
#define WM8993_VRX_TO_SPKOUTL_SHIFT                  5  /* VRX_TO_SPKOUTL */
#define WM8993_VRX_TO_SPKOUTL_WIDTH                  1  /* VRX_TO_SPKOUTL */
#define WM8993_SPKMIXL_TO_SPKOUTL               0x0010  /* SPKMIXL_TO_SPKOUTL */
#define WM8993_SPKMIXL_TO_SPKOUTL_MASK          0x0010  /* SPKMIXL_TO_SPKOUTL */
#define WM8993_SPKMIXL_TO_SPKOUTL_SHIFT              4  /* SPKMIXL_TO_SPKOUTL */
#define WM8993_SPKMIXL_TO_SPKOUTL_WIDTH              1  /* SPKMIXL_TO_SPKOUTL */
#define WM8993_SPKMIXR_TO_SPKOUTL               0x0008  /* SPKMIXR_TO_SPKOUTL */
#define WM8993_SPKMIXR_TO_SPKOUTL_MASK          0x0008  /* SPKMIXR_TO_SPKOUTL */
#define WM8993_SPKMIXR_TO_SPKOUTL_SHIFT              3  /* SPKMIXR_TO_SPKOUTL */
#define WM8993_SPKMIXR_TO_SPKOUTL_WIDTH              1  /* SPKMIXR_TO_SPKOUTL */
#define WM8993_VRX_TO_SPKOUTR                   0x0004  /* VRX_TO_SPKOUTR */
#define WM8993_VRX_TO_SPKOUTR_MASK              0x0004  /* VRX_TO_SPKOUTR */
#define WM8993_VRX_TO_SPKOUTR_SHIFT                  2  /* VRX_TO_SPKOUTR */
#define WM8993_VRX_TO_SPKOUTR_WIDTH                  1  /* VRX_TO_SPKOUTR */
#define WM8993_SPKMIXL_TO_SPKOUTR               0x0002  /* SPKMIXL_TO_SPKOUTR */
#define WM8993_SPKMIXL_TO_SPKOUTR_MASK          0x0002  /* SPKMIXL_TO_SPKOUTR */
#define WM8993_SPKMIXL_TO_SPKOUTR_SHIFT              1  /* SPKMIXL_TO_SPKOUTR */
#define WM8993_SPKMIXL_TO_SPKOUTR_WIDTH              1  /* SPKMIXL_TO_SPKOUTR */
#define WM8993_SPKMIXR_TO_SPKOUTR               0x0001  /* SPKMIXR_TO_SPKOUTR */
#define WM8993_SPKMIXR_TO_SPKOUTR_MASK          0x0001  /* SPKMIXR_TO_SPKOUTR */
#define WM8993_SPKMIXR_TO_SPKOUTR_SHIFT              0  /* SPKMIXR_TO_SPKOUTR */
#define WM8993_SPKMIXR_TO_SPKOUTR_WIDTH              1  /* SPKMIXR_TO_SPKOUTR */

/*
 * R37 (0x25) - SPKOUT Boost
 */
#define WM8993_SPKOUTL_BOOST_MASK               0x0038  /* SPKOUTL_BOOST - [5:3] */
#define WM8993_SPKOUTL_BOOST_SHIFT                   3  /* SPKOUTL_BOOST - [5:3] */
#define WM8993_SPKOUTL_BOOST_WIDTH                   3  /* SPKOUTL_BOOST - [5:3] */
#define WM8993_SPKOUTR_BOOST_MASK               0x0007  /* SPKOUTR_BOOST - [2:0] */
#define WM8993_SPKOUTR_BOOST_SHIFT                   0  /* SPKOUTR_BOOST - [2:0] */
#define WM8993_SPKOUTR_BOOST_WIDTH                   3  /* SPKOUTR_BOOST - [2:0] */

/*
 * R38 (0x26) - Speaker Volume Left
 */
#define WM8993_SPKOUT_VU                        0x0100  /* SPKOUT_VU */
#define WM8993_SPKOUT_VU_MASK                   0x0100  /* SPKOUT_VU */
#define WM8993_SPKOUT_VU_SHIFT                       8  /* SPKOUT_VU */
#define WM8993_SPKOUT_VU_WIDTH                       1  /* SPKOUT_VU */
#define WM8993_SPKOUTL_ZC                       0x0080  /* SPKOUTL_ZC */
#define WM8993_SPKOUTL_ZC_MASK                  0x0080  /* SPKOUTL_ZC */
#define WM8993_SPKOUTL_ZC_SHIFT                      7  /* SPKOUTL_ZC */
#define WM8993_SPKOUTL_ZC_WIDTH                      1  /* SPKOUTL_ZC */
#define WM8993_SPKOUTL_MUTE_N                   0x0040  /* SPKOUTL_MUTE_N */
#define WM8993_SPKOUTL_MUTE_N_MASK              0x0040  /* SPKOUTL_MUTE_N */
#define WM8993_SPKOUTL_MUTE_N_SHIFT                  6  /* SPKOUTL_MUTE_N */
#define WM8993_SPKOUTL_MUTE_N_WIDTH                  1  /* SPKOUTL_MUTE_N */
#define WM8993_SPKOUTL_VOL_MASK                 0x003F  /* SPKOUTL_VOL - [5:0] */
#define WM8993_SPKOUTL_VOL_SHIFT                     0  /* SPKOUTL_VOL - [5:0] */
#define WM8993_SPKOUTL_VOL_WIDTH                     6  /* SPKOUTL_VOL - [5:0] */

/*
 * R39 (0x27) - Speaker Volume Right
 */
#define WM8993_SPKOUT_VU                        0x0100  /* SPKOUT_VU */
#define WM8993_SPKOUT_VU_MASK                   0x0100  /* SPKOUT_VU */
#define WM8993_SPKOUT_VU_SHIFT                       8  /* SPKOUT_VU */
#define WM8993_SPKOUT_VU_WIDTH                       1  /* SPKOUT_VU */
#define WM8993_SPKOUTR_ZC                       0x0080  /* SPKOUTR_ZC */
#define WM8993_SPKOUTR_ZC_MASK                  0x0080  /* SPKOUTR_ZC */
#define WM8993_SPKOUTR_ZC_SHIFT                      7  /* SPKOUTR_ZC */
#define WM8993_SPKOUTR_ZC_WIDTH                      1  /* SPKOUTR_ZC */
#define WM8993_SPKOUTR_MUTE_N                   0x0040  /* SPKOUTR_MUTE_N */
#define WM8993_SPKOUTR_MUTE_N_MASK              0x0040  /* SPKOUTR_MUTE_N */
#define WM8993_SPKOUTR_MUTE_N_SHIFT                  6  /* SPKOUTR_MUTE_N */
#define WM8993_SPKOUTR_MUTE_N_WIDTH                  1  /* SPKOUTR_MUTE_N */
#define WM8993_SPKOUTR_VOL_MASK                 0x003F  /* SPKOUTR_VOL - [5:0] */
#define WM8993_SPKOUTR_VOL_SHIFT                     0  /* SPKOUTR_VOL - [5:0] */
#define WM8993_SPKOUTR_VOL_WIDTH                     6  /* SPKOUTR_VOL - [5:0] */

/*
 * R40 (0x28) - Input Mixer2
 */
#define WM8993_IN2LP_TO_IN2L                    0x0080  /* IN2LP_TO_IN2L */
#define WM8993_IN2LP_TO_IN2L_MASK               0x0080  /* IN2LP_TO_IN2L */
#define WM8993_IN2LP_TO_IN2L_SHIFT                   7  /* IN2LP_TO_IN2L */
#define WM8993_IN2LP_TO_IN2L_WIDTH                   1  /* IN2LP_TO_IN2L */
#define WM8993_IN2LN_TO_IN2L                    0x0040  /* IN2LN_TO_IN2L */
#define WM8993_IN2LN_TO_IN2L_MASK               0x0040  /* IN2LN_TO_IN2L */
#define WM8993_IN2LN_TO_IN2L_SHIFT                   6  /* IN2LN_TO_IN2L */
#define WM8993_IN2LN_TO_IN2L_WIDTH                   1  /* IN2LN_TO_IN2L */
#define WM8993_IN1LP_TO_IN1L                    0x0020  /* IN1LP_TO_IN1L */
#define WM8993_IN1LP_TO_IN1L_MASK               0x0020  /* IN1LP_TO_IN1L */
#define WM8993_IN1LP_TO_IN1L_SHIFT                   5  /* IN1LP_TO_IN1L */
#define WM8993_IN1LP_TO_IN1L_WIDTH                   1  /* IN1LP_TO_IN1L */
#define WM8993_IN1LN_TO_IN1L                    0x0010  /* IN1LN_TO_IN1L */
#define WM8993_IN1LN_TO_IN1L_MASK               0x0010  /* IN1LN_TO_IN1L */
#define WM8993_IN1LN_TO_IN1L_SHIFT                   4  /* IN1LN_TO_IN1L */
#define WM8993_IN1LN_TO_IN1L_WIDTH                   1  /* IN1LN_TO_IN1L */
#define WM8993_IN2RP_TO_IN2R                    0x0008  /* IN2RP_TO_IN2R */
#define WM8993_IN2RP_TO_IN2R_MASK               0x0008  /* IN2RP_TO_IN2R */
#define WM8993_IN2RP_TO_IN2R_SHIFT                   3  /* IN2RP_TO_IN2R */
#define WM8993_IN2RP_TO_IN2R_WIDTH                   1  /* IN2RP_TO_IN2R */
#define WM8993_IN2RN_TO_IN2R                    0x0004  /* IN2RN_TO_IN2R */
#define WM8993_IN2RN_TO_IN2R_MASK               0x0004  /* IN2RN_TO_IN2R */
#define WM8993_IN2RN_TO_IN2R_SHIFT                   2  /* IN2RN_TO_IN2R */
#define WM8993_IN2RN_TO_IN2R_WIDTH                   1  /* IN2RN_TO_IN2R */
#define WM8993_IN1RP_TO_IN1R                    0x0002  /* IN1RP_TO_IN1R */
#define WM8993_IN1RP_TO_IN1R_MASK               0x0002  /* IN1RP_TO_IN1R */
#define WM8993_IN1RP_TO_IN1R_SHIFT                   1  /* IN1RP_TO_IN1R */
#define WM8993_IN1RP_TO_IN1R_WIDTH                   1  /* IN1RP_TO_IN1R */
#define WM8993_IN1RN_TO_IN1R                    0x0001  /* IN1RN_TO_IN1R */
#define WM8993_IN1RN_TO_IN1R_MASK               0x0001  /* IN1RN_TO_IN1R */
#define WM8993_IN1RN_TO_IN1R_SHIFT                   0  /* IN1RN_TO_IN1R */
#define WM8993_IN1RN_TO_IN1R_WIDTH                   1  /* IN1RN_TO_IN1R */

/*
 * R41 (0x29) - Input Mixer3
 */
#define WM8993_IN2L_TO_MIXINL                   0x0100  /* IN2L_TO_MIXINL */
#define WM8993_IN2L_TO_MIXINL_MASK              0x0100  /* IN2L_TO_MIXINL */
#define WM8993_IN2L_TO_MIXINL_SHIFT                  8  /* IN2L_TO_MIXINL */
#define WM8993_IN2L_TO_MIXINL_WIDTH                  1  /* IN2L_TO_MIXINL */
#define WM8993_IN2L_MIXINL_VOL                  0x0080  /* IN2L_MIXINL_VOL */
#define WM8993_IN2L_MIXINL_VOL_MASK             0x0080  /* IN2L_MIXINL_VOL */
#define WM8993_IN2L_MIXINL_VOL_SHIFT                 7  /* IN2L_MIXINL_VOL */
#define WM8993_IN2L_MIXINL_VOL_WIDTH                 1  /* IN2L_MIXINL_VOL */
#define WM8993_IN1L_TO_MIXINL                   0x0020  /* IN1L_TO_MIXINL */
#define WM8993_IN1L_TO_MIXINL_MASK              0x0020  /* IN1L_TO_MIXINL */
#define WM8993_IN1L_TO_MIXINL_SHIFT                  5  /* IN1L_TO_MIXINL */
#define WM8993_IN1L_TO_MIXINL_WIDTH                  1  /* IN1L_TO_MIXINL */
#define WM8993_IN1L_MIXINL_VOL                  0x0010  /* IN1L_MIXINL_VOL */
#define WM8993_IN1L_MIXINL_VOL_MASK             0x0010  /* IN1L_MIXINL_VOL */
#define WM8993_IN1L_MIXINL_VOL_SHIFT                 4  /* IN1L_MIXINL_VOL */
#define WM8993_IN1L_MIXINL_VOL_WIDTH                 1  /* IN1L_MIXINL_VOL */
#define WM8993_MIXOUTL_MIXINL_VOL_MASK          0x0007  /* MIXOUTL_MIXINL_VOL - [2:0] */
#define WM8993_MIXOUTL_MIXINL_VOL_SHIFT              0  /* MIXOUTL_MIXINL_VOL - [2:0] */
#define WM8993_MIXOUTL_MIXINL_VOL_WIDTH              3  /* MIXOUTL_MIXINL_VOL - [2:0] */

/*
 * R42 (0x2A) - Input Mixer4
 */
#define WM8993_IN2R_TO_MIXINR                   0x0100  /* IN2R_TO_MIXINR */
#define WM8993_IN2R_TO_MIXINR_MASK              0x0100  /* IN2R_TO_MIXINR */
#define WM8993_IN2R_TO_MIXINR_SHIFT                  8  /* IN2R_TO_MIXINR */
#define WM8993_IN2R_TO_MIXINR_WIDTH                  1  /* IN2R_TO_MIXINR */
#define WM8993_IN2R_MIXINR_VOL                  0x0080  /* IN2R_MIXINR_VOL */
#define WM8993_IN2R_MIXINR_VOL_MASK             0x0080  /* IN2R_MIXINR_VOL */
#define WM8993_IN2R_MIXINR_VOL_SHIFT                 7  /* IN2R_MIXINR_VOL */
#define WM8993_IN2R_MIXINR_VOL_WIDTH                 1  /* IN2R_MIXINR_VOL */
#define WM8993_IN1R_TO_MIXINR                   0x0020  /* IN1R_TO_MIXINR */
#define WM8993_IN1R_TO_MIXINR_MASK              0x0020  /* IN1R_TO_MIXINR */
#define WM8993_IN1R_TO_MIXINR_SHIFT                  5  /* IN1R_TO_MIXINR */
#define WM8993_IN1R_TO_MIXINR_WIDTH                  1  /* IN1R_TO_MIXINR */
#define WM8993_IN1R_MIXINR_VOL                  0x0010  /* IN1R_MIXINR_VOL */
#define WM8993_IN1R_MIXINR_VOL_MASK             0x0010  /* IN1R_MIXINR_VOL */
#define WM8993_IN1R_MIXINR_VOL_SHIFT                 4  /* IN1R_MIXINR_VOL */
#define WM8993_IN1R_MIXINR_VOL_WIDTH                 1  /* IN1R_MIXINR_VOL */
#define WM8993_MIXOUTR_MIXINR_VOL_MASK          0x0007  /* MIXOUTR_MIXINR_VOL - [2:0] */
#define WM8993_MIXOUTR_MIXINR_VOL_SHIFT              0  /* MIXOUTR_MIXINR_VOL - [2:0] */
#define WM8993_MIXOUTR_MIXINR_VOL_WIDTH              3  /* MIXOUTR_MIXINR_VOL - [2:0] */

/*
 * R43 (0x2B) - Input Mixer5
 */
#define WM8993_IN1LP_MIXINL_VOL_MASK            0x01C0  /* IN1LP_MIXINL_VOL - [8:6] */
#define WM8993_IN1LP_MIXINL_VOL_SHIFT                6  /* IN1LP_MIXINL_VOL - [8:6] */
#define WM8993_IN1LP_MIXINL_VOL_WIDTH                3  /* IN1LP_MIXINL_VOL - [8:6] */
#define WM8993_VRX_MIXINL_VOL_MASK              0x0007  /* VRX_MIXINL_VOL - [2:0] */
#define WM8993_VRX_MIXINL_VOL_SHIFT                  0  /* VRX_MIXINL_VOL - [2:0] */
#define WM8993_VRX_MIXINL_VOL_WIDTH                  3  /* VRX_MIXINL_VOL - [2:0] */

/*
 * R44 (0x2C) - Input Mixer6
 */
#define WM8993_IN1RP_MIXINR_VOL_MASK            0x01C0  /* IN1RP_MIXINR_VOL - [8:6] */
#define WM8993_IN1RP_MIXINR_VOL_SHIFT                6  /* IN1RP_MIXINR_VOL - [8:6] */
#define WM8993_IN1RP_MIXINR_VOL_WIDTH                3  /* IN1RP_MIXINR_VOL - [8:6] */
#define WM8993_VRX_MIXINR_VOL_MASK              0x0007  /* VRX_MIXINR_VOL - [2:0] */
#define WM8993_VRX_MIXINR_VOL_SHIFT                  0  /* VRX_MIXINR_VOL - [2:0] */
#define WM8993_VRX_MIXINR_VOL_WIDTH                  3  /* VRX_MIXINR_VOL - [2:0] */

/*
 * R45 (0x2D) - Output Mixer1
 */
#define WM8993_DACL_TO_HPOUT1L                  0x0100  /* DACL_TO_HPOUT1L */
#define WM8993_DACL_TO_HPOUT1L_MASK             0x0100  /* DACL_TO_HPOUT1L */
#define WM8993_DACL_TO_HPOUT1L_SHIFT                 8  /* DACL_TO_HPOUT1L */
#define WM8993_DACL_TO_HPOUT1L_WIDTH                 1  /* DACL_TO_HPOUT1L */
#define WM8993_MIXINR_TO_MIXOUTL                0x0080  /* MIXINR_TO_MIXOUTL */
#define WM8993_MIXINR_TO_MIXOUTL_MASK           0x0080  /* MIXINR_TO_MIXOUTL */
#define WM8993_MIXINR_TO_MIXOUTL_SHIFT               7  /* MIXINR_TO_MIXOUTL */
#define WM8993_MIXINR_TO_MIXOUTL_WIDTH               1  /* MIXINR_TO_MIXOUTL */
#define WM8993_MIXINL_TO_MIXOUTL                0x0040  /* MIXINL_TO_MIXOUTL */
#define WM8993_MIXINL_TO_MIXOUTL_MASK           0x0040  /* MIXINL_TO_MIXOUTL */
#define WM8993_MIXINL_TO_MIXOUTL_SHIFT               6  /* MIXINL_TO_MIXOUTL */
#define WM8993_MIXINL_TO_MIXOUTL_WIDTH               1  /* MIXINL_TO_MIXOUTL */
#define WM8993_IN2RN_TO_MIXOUTL                 0x0020  /* IN2RN_TO_MIXOUTL */
#define WM8993_IN2RN_TO_MIXOUTL_MASK            0x0020  /* IN2RN_TO_MIXOUTL */
#define WM8993_IN2RN_TO_MIXOUTL_SHIFT                5  /* IN2RN_TO_MIXOUTL */
#define WM8993_IN2RN_TO_MIXOUTL_WIDTH                1  /* IN2RN_TO_MIXOUTL */
#define WM8993_IN2LN_TO_MIXOUTL                 0x0010  /* IN2LN_TO_MIXOUTL */
#define WM8993_IN2LN_TO_MIXOUTL_MASK            0x0010  /* IN2LN_TO_MIXOUTL */
#define WM8993_IN2LN_TO_MIXOUTL_SHIFT                4  /* IN2LN_TO_MIXOUTL */
#define WM8993_IN2LN_TO_MIXOUTL_WIDTH                1  /* IN2LN_TO_MIXOUTL */
#define WM8993_IN1R_TO_MIXOUTL                  0x0008  /* IN1R_TO_MIXOUTL */
#define WM8993_IN1R_TO_MIXOUTL_MASK             0x0008  /* IN1R_TO_MIXOUTL */
#define WM8993_IN1R_TO_MIXOUTL_SHIFT                 3  /* IN1R_TO_MIXOUTL */
#define WM8993_IN1R_TO_MIXOUTL_WIDTH                 1  /* IN1R_TO_MIXOUTL */
#define WM8993_IN1L_TO_MIXOUTL                  0x0004  /* IN1L_TO_MIXOUTL */
#define WM8993_IN1L_TO_MIXOUTL_MASK             0x0004  /* IN1L_TO_MIXOUTL */
#define WM8993_IN1L_TO_MIXOUTL_SHIFT                 2  /* IN1L_TO_MIXOUTL */
#define WM8993_IN1L_TO_MIXOUTL_WIDTH                 1  /* IN1L_TO_MIXOUTL */
#define WM8993_IN2LP_TO_MIXOUTL                 0x0002  /* IN2LP_TO_MIXOUTL */
#define WM8993_IN2LP_TO_MIXOUTL_MASK            0x0002  /* IN2LP_TO_MIXOUTL */
#define WM8993_IN2LP_TO_MIXOUTL_SHIFT                1  /* IN2LP_TO_MIXOUTL */
#define WM8993_IN2LP_TO_MIXOUTL_WIDTH                1  /* IN2LP_TO_MIXOUTL */
#define WM8993_DACL_TO_MIXOUTL                  0x0001  /* DACL_TO_MIXOUTL */
#define WM8993_DACL_TO_MIXOUTL_MASK             0x0001  /* DACL_TO_MIXOUTL */
#define WM8993_DACL_TO_MIXOUTL_SHIFT                 0  /* DACL_TO_MIXOUTL */
#define WM8993_DACL_TO_MIXOUTL_WIDTH                 1  /* DACL_TO_MIXOUTL */

/*
 * R46 (0x2E) - Output Mixer2
 */
#define WM8993_DACR_TO_HPOUT1R                  0x0100  /* DACR_TO_HPOUT1R */
#define WM8993_DACR_TO_HPOUT1R_MASK             0x0100  /* DACR_TO_HPOUT1R */
#define WM8993_DACR_TO_HPOUT1R_SHIFT                 8  /* DACR_TO_HPOUT1R */
#define WM8993_DACR_TO_HPOUT1R_WIDTH                 1  /* DACR_TO_HPOUT1R */
#define WM8993_MIXINL_TO_MIXOUTR                0x0080  /* MIXINL_TO_MIXOUTR */
#define WM8993_MIXINL_TO_MIXOUTR_MASK           0x0080  /* MIXINL_TO_MIXOUTR */
#define WM8993_MIXINL_TO_MIXOUTR_SHIFT               7  /* MIXINL_TO_MIXOUTR */
#define WM8993_MIXINL_TO_MIXOUTR_WIDTH               1  /* MIXINL_TO_MIXOUTR */
#define WM8993_MIXINR_TO_MIXOUTR                0x0040  /* MIXINR_TO_MIXOUTR */
#define WM8993_MIXINR_TO_MIXOUTR_MASK           0x0040  /* MIXINR_TO_MIXOUTR */
#define WM8993_MIXINR_TO_MIXOUTR_SHIFT               6  /* MIXINR_TO_MIXOUTR */
#define WM8993_MIXINR_TO_MIXOUTR_WIDTH               1  /* MIXINR_TO_MIXOUTR */
#define WM8993_IN2LN_TO_MIXOUTR                 0x0020  /* IN2LN_TO_MIXOUTR */
#define WM8993_IN2LN_TO_MIXOUTR_MASK            0x0020  /* IN2LN_TO_MIXOUTR */
#define WM8993_IN2LN_TO_MIXOUTR_SHIFT                5  /* IN2LN_TO_MIXOUTR */
#define WM8993_IN2LN_TO_MIXOUTR_WIDTH                1  /* IN2LN_TO_MIXOUTR */
#define WM8993_IN2RN_TO_MIXOUTR                 0x0010  /* IN2RN_TO_MIXOUTR */
#define WM8993_IN2RN_TO_MIXOUTR_MASK            0x0010  /* IN2RN_TO_MIXOUTR */
#define WM8993_IN2RN_TO_MIXOUTR_SHIFT                4  /* IN2RN_TO_MIXOUTR */
#define WM8993_IN2RN_TO_MIXOUTR_WIDTH                1  /* IN2RN_TO_MIXOUTR */
#define WM8993_IN1L_TO_MIXOUTR                  0x0008  /* IN1L_TO_MIXOUTR */
#define WM8993_IN1L_TO_MIXOUTR_MASK             0x0008  /* IN1L_TO_MIXOUTR */
#define WM8993_IN1L_TO_MIXOUTR_SHIFT                 3  /* IN1L_TO_MIXOUTR */
#define WM8993_IN1L_TO_MIXOUTR_WIDTH                 1  /* IN1L_TO_MIXOUTR */
#define WM8993_IN1R_TO_MIXOUTR                  0x0004  /* IN1R_TO_MIXOUTR */
#define WM8993_IN1R_TO_MIXOUTR_MASK             0x0004  /* IN1R_TO_MIXOUTR */
#define WM8993_IN1R_TO_MIXOUTR_SHIFT                 2  /* IN1R_TO_MIXOUTR */
#define WM8993_IN1R_TO_MIXOUTR_WIDTH                 1  /* IN1R_TO_MIXOUTR */
#define WM8993_IN2RP_TO_MIXOUTR                 0x0002  /* IN2RP_TO_MIXOUTR */
#define WM8993_IN2RP_TO_MIXOUTR_MASK            0x0002  /* IN2RP_TO_MIXOUTR */
#define WM8993_IN2RP_TO_MIXOUTR_SHIFT                1  /* IN2RP_TO_MIXOUTR */
#define WM8993_IN2RP_TO_MIXOUTR_WIDTH                1  /* IN2RP_TO_MIXOUTR */
#define WM8993_DACR_TO_MIXOUTR                  0x0001  /* DACR_TO_MIXOUTR */
#define WM8993_DACR_TO_MIXOUTR_MASK             0x0001  /* DACR_TO_MIXOUTR */
#define WM8993_DACR_TO_MIXOUTR_SHIFT                 0  /* DACR_TO_MIXOUTR */
#define WM8993_DACR_TO_MIXOUTR_WIDTH                 1  /* DACR_TO_MIXOUTR */

/*
 * R47 (0x2F) - Output Mixer3
 */
#define WM8993_IN2LP_MIXOUTL_VOL_MASK           0x0E00  /* IN2LP_MIXOUTL_VOL - [11:9] */
#define WM8993_IN2LP_MIXOUTL_VOL_SHIFT               9  /* IN2LP_MIXOUTL_VOL - [11:9] */
#define WM8993_IN2LP_MIXOUTL_VOL_WIDTH               3  /* IN2LP_MIXOUTL_VOL - [11:9] */
#define WM8993_IN2LN_MIXOUTL_VOL_MASK           0x01C0  /* IN2LN_MIXOUTL_VOL - [8:6] */
#define WM8993_IN2LN_MIXOUTL_VOL_SHIFT               6  /* IN2LN_MIXOUTL_VOL - [8:6] */
#define WM8993_IN2LN_MIXOUTL_VOL_WIDTH               3  /* IN2LN_MIXOUTL_VOL - [8:6] */
#define WM8993_IN1R_MIXOUTL_VOL_MASK            0x0038  /* IN1R_MIXOUTL_VOL - [5:3] */
#define WM8993_IN1R_MIXOUTL_VOL_SHIFT                3  /* IN1R_MIXOUTL_VOL - [5:3] */
#define WM8993_IN1R_MIXOUTL_VOL_WIDTH                3  /* IN1R_MIXOUTL_VOL - [5:3] */
#define WM8993_IN1L_MIXOUTL_VOL_MASK            0x0007  /* IN1L_MIXOUTL_VOL - [2:0] */
#define WM8993_IN1L_MIXOUTL_VOL_SHIFT                0  /* IN1L_MIXOUTL_VOL - [2:0] */
#define WM8993_IN1L_MIXOUTL_VOL_WIDTH                3  /* IN1L_MIXOUTL_VOL - [2:0] */

/*
 * R48 (0x30) - Output Mixer4
 */
#define WM8993_IN2RP_MIXOUTR_VOL_MASK           0x0E00  /* IN2RP_MIXOUTR_VOL - [11:9] */
#define WM8993_IN2RP_MIXOUTR_VOL_SHIFT               9  /* IN2RP_MIXOUTR_VOL - [11:9] */
#define WM8993_IN2RP_MIXOUTR_VOL_WIDTH               3  /* IN2RP_MIXOUTR_VOL - [11:9] */
#define WM8993_IN2RN_MIXOUTR_VOL_MASK           0x01C0  /* IN2RN_MIXOUTR_VOL - [8:6] */
#define WM8993_IN2RN_MIXOUTR_VOL_SHIFT               6  /* IN2RN_MIXOUTR_VOL - [8:6] */
#define WM8993_IN2RN_MIXOUTR_VOL_WIDTH               3  /* IN2RN_MIXOUTR_VOL - [8:6] */
#define WM8993_IN1L_MIXOUTR_VOL_MASK            0x0038  /* IN1L_MIXOUTR_VOL - [5:3] */
#define WM8993_IN1L_MIXOUTR_VOL_SHIFT                3  /* IN1L_MIXOUTR_VOL - [5:3] */
#define WM8993_IN1L_MIXOUTR_VOL_WIDTH                3  /* IN1L_MIXOUTR_VOL - [5:3] */
#define WM8993_IN1R_MIXOUTR_VOL_MASK            0x0007  /* IN1R_MIXOUTR_VOL - [2:0] */
#define WM8993_IN1R_MIXOUTR_VOL_SHIFT                0  /* IN1R_MIXOUTR_VOL - [2:0] */
#define WM8993_IN1R_MIXOUTR_VOL_WIDTH                3  /* IN1R_MIXOUTR_VOL - [2:0] */

/*
 * R49 (0x31) - Output Mixer5
 */
#define WM8993_DACL_MIXOUTL_VOL_MASK            0x0E00  /* DACL_MIXOUTL_VOL - [11:9] */
#define WM8993_DACL_MIXOUTL_VOL_SHIFT                9  /* DACL_MIXOUTL_VOL - [11:9] */
#define WM8993_DACL_MIXOUTL_VOL_WIDTH                3  /* DACL_MIXOUTL_VOL - [11:9] */
#define WM8993_IN2RN_MIXOUTL_VOL_MASK           0x01C0  /* IN2RN_MIXOUTL_VOL - [8:6] */
#define WM8993_IN2RN_MIXOUTL_VOL_SHIFT               6  /* IN2RN_MIXOUTL_VOL - [8:6] */
#define WM8993_IN2RN_MIXOUTL_VOL_WIDTH               3  /* IN2RN_MIXOUTL_VOL - [8:6] */
#define WM8993_MIXINR_MIXOUTL_VOL_MASK          0x0038  /* MIXINR_MIXOUTL_VOL - [5:3] */
#define WM8993_MIXINR_MIXOUTL_VOL_SHIFT              3  /* MIXINR_MIXOUTL_VOL - [5:3] */
#define WM8993_MIXINR_MIXOUTL_VOL_WIDTH              3  /* MIXINR_MIXOUTL_VOL - [5:3] */
#define WM8993_MIXINL_MIXOUTL_VOL_MASK          0x0007  /* MIXINL_MIXOUTL_VOL - [2:0] */
#define WM8993_MIXINL_MIXOUTL_VOL_SHIFT              0  /* MIXINL_MIXOUTL_VOL - [2:0] */
#define WM8993_MIXINL_MIXOUTL_VOL_WIDTH              3  /* MIXINL_MIXOUTL_VOL - [2:0] */

/*
 * R50 (0x32) - Output Mixer6
 */
#define WM8993_DACR_MIXOUTR_VOL_MASK            0x0E00  /* DACR_MIXOUTR_VOL - [11:9] */
#define WM8993_DACR_MIXOUTR_VOL_SHIFT                9  /* DACR_MIXOUTR_VOL - [11:9] */
#define WM8993_DACR_MIXOUTR_VOL_WIDTH                3  /* DACR_MIXOUTR_VOL - [11:9] */
#define WM8993_IN2LN_MIXOUTR_VOL_MASK           0x01C0  /* IN2LN_MIXOUTR_VOL - [8:6] */
#define WM8993_IN2LN_MIXOUTR_VOL_SHIFT               6  /* IN2LN_MIXOUTR_VOL - [8:6] */
#define WM8993_IN2LN_MIXOUTR_VOL_WIDTH               3  /* IN2LN_MIXOUTR_VOL - [8:6] */
#define WM8993_MIXINL_MIXOUTR_VOL_MASK          0x0038  /* MIXINL_MIXOUTR_VOL - [5:3] */
#define WM8993_MIXINL_MIXOUTR_VOL_SHIFT              3  /* MIXINL_MIXOUTR_VOL - [5:3] */
#define WM8993_MIXINL_MIXOUTR_VOL_WIDTH              3  /* MIXINL_MIXOUTR_VOL - [5:3] */
#define WM8993_MIXINR_MIXOUTR_VOL_MASK          0x0007  /* MIXINR_MIXOUTR_VOL - [2:0] */
#define WM8993_MIXINR_MIXOUTR_VOL_SHIFT              0  /* MIXINR_MIXOUTR_VOL - [2:0] */
#define WM8993_MIXINR_MIXOUTR_VOL_WIDTH              3  /* MIXINR_MIXOUTR_VOL - [2:0] */

/*
 * R51 (0x33) - HPOUT2 Mixer
 */
#define WM8993_VRX_TO_HPOUT2                    0x0020  /* VRX_TO_HPOUT2 */
#define WM8993_VRX_TO_HPOUT2_MASK               0x0020  /* VRX_TO_HPOUT2 */
#define WM8993_VRX_TO_HPOUT2_SHIFT                   5  /* VRX_TO_HPOUT2 */
#define WM8993_VRX_TO_HPOUT2_WIDTH                   1  /* VRX_TO_HPOUT2 */
#define WM8993_MIXOUTLVOL_TO_HPOUT2             0x0010  /* MIXOUTLVOL_TO_HPOUT2 */
#define WM8993_MIXOUTLVOL_TO_HPOUT2_MASK        0x0010  /* MIXOUTLVOL_TO_HPOUT2 */
#define WM8993_MIXOUTLVOL_TO_HPOUT2_SHIFT            4  /* MIXOUTLVOL_TO_HPOUT2 */
#define WM8993_MIXOUTLVOL_TO_HPOUT2_WIDTH            1  /* MIXOUTLVOL_TO_HPOUT2 */
#define WM8993_MIXOUTRVOL_TO_HPOUT2             0x0008  /* MIXOUTRVOL_TO_HPOUT2 */
#define WM8993_MIXOUTRVOL_TO_HPOUT2_MASK        0x0008  /* MIXOUTRVOL_TO_HPOUT2 */
#define WM8993_MIXOUTRVOL_TO_HPOUT2_SHIFT            3  /* MIXOUTRVOL_TO_HPOUT2 */
#define WM8993_MIXOUTRVOL_TO_HPOUT2_WIDTH            1  /* MIXOUTRVOL_TO_HPOUT2 */

/*
 * R52 (0x34) - Line Mixer1
 */
#define WM8993_MIXOUTL_TO_LINEOUT1N             0x0040  /* MIXOUTL_TO_LINEOUT1N */
#define WM8993_MIXOUTL_TO_LINEOUT1N_MASK        0x0040  /* MIXOUTL_TO_LINEOUT1N */
#define WM8993_MIXOUTL_TO_LINEOUT1N_SHIFT            6  /* MIXOUTL_TO_LINEOUT1N */
#define WM8993_MIXOUTL_TO_LINEOUT1N_WIDTH            1  /* MIXOUTL_TO_LINEOUT1N */
#define WM8993_MIXOUTR_TO_LINEOUT1N             0x0020  /* MIXOUTR_TO_LINEOUT1N */
#define WM8993_MIXOUTR_TO_LINEOUT1N_MASK        0x0020  /* MIXOUTR_TO_LINEOUT1N */
#define WM8993_MIXOUTR_TO_LINEOUT1N_SHIFT            5  /* MIXOUTR_TO_LINEOUT1N */
#define WM8993_MIXOUTR_TO_LINEOUT1N_WIDTH            1  /* MIXOUTR_TO_LINEOUT1N */
#define WM8993_LINEOUT1_MODE                    0x0010  /* LINEOUT1_MODE */
#define WM8993_LINEOUT1_MODE_MASK               0x0010  /* LINEOUT1_MODE */
#define WM8993_LINEOUT1_MODE_SHIFT                   4  /* LINEOUT1_MODE */
#define WM8993_LINEOUT1_MODE_WIDTH                   1  /* LINEOUT1_MODE */
#define WM8993_IN1R_TO_LINEOUT1P                0x0004  /* IN1R_TO_LINEOUT1P */
#define WM8993_IN1R_TO_LINEOUT1P_MASK           0x0004  /* IN1R_TO_LINEOUT1P */
#define WM8993_IN1R_TO_LINEOUT1P_SHIFT               2  /* IN1R_TO_LINEOUT1P */
#define WM8993_IN1R_TO_LINEOUT1P_WIDTH               1  /* IN1R_TO_LINEOUT1P */
#define WM8993_IN1L_TO_LINEOUT1P                0x0002  /* IN1L_TO_LINEOUT1P */
#define WM8993_IN1L_TO_LINEOUT1P_MASK           0x0002  /* IN1L_TO_LINEOUT1P */
#define WM8993_IN1L_TO_LINEOUT1P_SHIFT               1  /* IN1L_TO_LINEOUT1P */
#define WM8993_IN1L_TO_LINEOUT1P_WIDTH               1  /* IN1L_TO_LINEOUT1P */
#define WM8993_MIXOUTL_TO_LINEOUT1P             0x0001  /* MIXOUTL_TO_LINEOUT1P */
#define WM8993_MIXOUTL_TO_LINEOUT1P_MASK        0x0001  /* MIXOUTL_TO_LINEOUT1P */
#define WM8993_MIXOUTL_TO_LINEOUT1P_SHIFT            0  /* MIXOUTL_TO_LINEOUT1P */
#define WM8993_MIXOUTL_TO_LINEOUT1P_WIDTH            1  /* MIXOUTL_TO_LINEOUT1P */

/*
 * R53 (0x35) - Line Mixer2
 */
#define WM8993_MIXOUTR_TO_LINEOUT2N             0x0040  /* MIXOUTR_TO_LINEOUT2N */
#define WM8993_MIXOUTR_TO_LINEOUT2N_MASK        0x0040  /* MIXOUTR_TO_LINEOUT2N */
#define WM8993_MIXOUTR_TO_LINEOUT2N_SHIFT            6  /* MIXOUTR_TO_LINEOUT2N */
#define WM8993_MIXOUTR_TO_LINEOUT2N_WIDTH            1  /* MIXOUTR_TO_LINEOUT2N */
#define WM8993_MIXOUTL_TO_LINEOUT2N             0x0020  /* MIXOUTL_TO_LINEOUT2N */
#define WM8993_MIXOUTL_TO_LINEOUT2N_MASK        0x0020  /* MIXOUTL_TO_LINEOUT2N */
#define WM8993_MIXOUTL_TO_LINEOUT2N_SHIFT            5  /* MIXOUTL_TO_LINEOUT2N */
#define WM8993_MIXOUTL_TO_LINEOUT2N_WIDTH            1  /* MIXOUTL_TO_LINEOUT2N */
#define WM8993_LINEOUT2_MODE                    0x0010  /* LINEOUT2_MODE */
#define WM8993_LINEOUT2_MODE_MASK               0x0010  /* LINEOUT2_MODE */
#define WM8993_LINEOUT2_MODE_SHIFT                   4  /* LINEOUT2_MODE */
#define WM8993_LINEOUT2_MODE_WIDTH                   1  /* LINEOUT2_MODE */
#define WM8993_IN1L_TO_LINEOUT2P                0x0004  /* IN1L_TO_LINEOUT2P */
#define WM8993_IN1L_TO_LINEOUT2P_MASK           0x0004  /* IN1L_TO_LINEOUT2P */
#define WM8993_IN1L_TO_LINEOUT2P_SHIFT               2  /* IN1L_TO_LINEOUT2P */
#define WM8993_IN1L_TO_LINEOUT2P_WIDTH               1  /* IN1L_TO_LINEOUT2P */
#define WM8993_IN1R_TO_LINEOUT2P                0x0002  /* IN1R_TO_LINEOUT2P */
#define WM8993_IN1R_TO_LINEOUT2P_MASK           0x0002  /* IN1R_TO_LINEOUT2P */
#define WM8993_IN1R_TO_LINEOUT2P_SHIFT               1  /* IN1R_TO_LINEOUT2P */
#define WM8993_IN1R_TO_LINEOUT2P_WIDTH               1  /* IN1R_TO_LINEOUT2P */
#define WM8993_MIXOUTR_TO_LINEOUT2P             0x0001  /* MIXOUTR_TO_LINEOUT2P */
#define WM8993_MIXOUTR_TO_LINEOUT2P_MASK        0x0001  /* MIXOUTR_TO_LINEOUT2P */
#define WM8993_MIXOUTR_TO_LINEOUT2P_SHIFT            0  /* MIXOUTR_TO_LINEOUT2P */
#define WM8993_MIXOUTR_TO_LINEOUT2P_WIDTH            1  /* MIXOUTR_TO_LINEOUT2P */

/*
 * R54 (0x36) - Speaker Mixer
 */
#define WM8993_SPKAB_REF_SEL                    0x0100  /* SPKAB_REF_SEL */
#define WM8993_SPKAB_REF_SEL_MASK               0x0100  /* SPKAB_REF_SEL */
#define WM8993_SPKAB_REF_SEL_SHIFT                   8  /* SPKAB_REF_SEL */
#define WM8993_SPKAB_REF_SEL_WIDTH                   1  /* SPKAB_REF_SEL */
#define WM8993_MIXINL_TO_SPKMIXL                0x0080  /* MIXINL_TO_SPKMIXL */
#define WM8993_MIXINL_TO_SPKMIXL_MASK           0x0080  /* MIXINL_TO_SPKMIXL */
#define WM8993_MIXINL_TO_SPKMIXL_SHIFT               7  /* MIXINL_TO_SPKMIXL */
#define WM8993_MIXINL_TO_SPKMIXL_WIDTH               1  /* MIXINL_TO_SPKMIXL */
#define WM8993_MIXINR_TO_SPKMIXR                0x0040  /* MIXINR_TO_SPKMIXR */
#define WM8993_MIXINR_TO_SPKMIXR_MASK           0x0040  /* MIXINR_TO_SPKMIXR */
#define WM8993_MIXINR_TO_SPKMIXR_SHIFT               6  /* MIXINR_TO_SPKMIXR */
#define WM8993_MIXINR_TO_SPKMIXR_WIDTH               1  /* MIXINR_TO_SPKMIXR */
#define WM8993_IN1LP_TO_SPKMIXL                 0x0020  /* IN1LP_TO_SPKMIXL */
#define WM8993_IN1LP_TO_SPKMIXL_MASK            0x0020  /* IN1LP_TO_SPKMIXL */
#define WM8993_IN1LP_TO_SPKMIXL_SHIFT                5  /* IN1LP_TO_SPKMIXL */
#define WM8993_IN1LP_TO_SPKMIXL_WIDTH                1  /* IN1LP_TO_SPKMIXL */
#define WM8993_IN1RP_TO_SPKMIXR                 0x0010  /* IN1RP_TO_SPKMIXR */
#define WM8993_IN1RP_TO_SPKMIXR_MASK            0x0010  /* IN1RP_TO_SPKMIXR */
#define WM8993_IN1RP_TO_SPKMIXR_SHIFT                4  /* IN1RP_TO_SPKMIXR */
#define WM8993_IN1RP_TO_SPKMIXR_WIDTH                1  /* IN1RP_TO_SPKMIXR */
#define WM8993_MIXOUTL_TO_SPKMIXL               0x0008  /* MIXOUTL_TO_SPKMIXL */
#define WM8993_MIXOUTL_TO_SPKMIXL_MASK          0x0008  /* MIXOUTL_TO_SPKMIXL */
#define WM8993_MIXOUTL_TO_SPKMIXL_SHIFT              3  /* MIXOUTL_TO_SPKMIXL */
#define WM8993_MIXOUTL_TO_SPKMIXL_WIDTH              1  /* MIXOUTL_TO_SPKMIXL */
#define WM8993_MIXOUTR_TO_SPKMIXR               0x0004  /* MIXOUTR_TO_SPKMIXR */
#define WM8993_MIXOUTR_TO_SPKMIXR_MASK          0x0004  /* MIXOUTR_TO_SPKMIXR */
#define WM8993_MIXOUTR_TO_SPKMIXR_SHIFT              2  /* MIXOUTR_TO_SPKMIXR */
#define WM8993_MIXOUTR_TO_SPKMIXR_WIDTH              1  /* MIXOUTR_TO_SPKMIXR */
#define WM8993_DACL_TO_SPKMIXL                  0x0002  /* DACL_TO_SPKMIXL */
#define WM8993_DACL_TO_SPKMIXL_MASK             0x0002  /* DACL_TO_SPKMIXL */
#define WM8993_DACL_TO_SPKMIXL_SHIFT                 1  /* DACL_TO_SPKMIXL */
#define WM8993_DACL_TO_SPKMIXL_WIDTH                 1  /* DACL_TO_SPKMIXL */
#define WM8993_DACR_TO_SPKMIXR                  0x0001  /* DACR_TO_SPKMIXR */
#define WM8993_DACR_TO_SPKMIXR_MASK             0x0001  /* DACR_TO_SPKMIXR */
#define WM8993_DACR_TO_SPKMIXR_SHIFT                 0  /* DACR_TO_SPKMIXR */
#define WM8993_DACR_TO_SPKMIXR_WIDTH                 1  /* DACR_TO_SPKMIXR */

/*
 * R55 (0x37) - Additional Control
 */
#define WM8993_LINEOUT1_FB                      0x0080  /* LINEOUT1_FB */
#define WM8993_LINEOUT1_FB_MASK                 0x0080  /* LINEOUT1_FB */
#define WM8993_LINEOUT1_FB_SHIFT                     7  /* LINEOUT1_FB */
#define WM8993_LINEOUT1_FB_WIDTH                     1  /* LINEOUT1_FB */
#define WM8993_LINEOUT2_FB                      0x0040  /* LINEOUT2_FB */
#define WM8993_LINEOUT2_FB_MASK                 0x0040  /* LINEOUT2_FB */
#define WM8993_LINEOUT2_FB_SHIFT                     6  /* LINEOUT2_FB */
#define WM8993_LINEOUT2_FB_WIDTH                     1  /* LINEOUT2_FB */
#define WM8993_VROI                             0x0001  /* VROI */
#define WM8993_VROI_MASK                        0x0001  /* VROI */
#define WM8993_VROI_SHIFT                            0  /* VROI */
#define WM8993_VROI_WIDTH                            1  /* VROI */

/*
 * R56 (0x38) - AntiPOP1
 */
#define WM8993_LINEOUT_VMID_BUF_ENA             0x0080  /* LINEOUT_VMID_BUF_ENA */
#define WM8993_LINEOUT_VMID_BUF_ENA_MASK        0x0080  /* LINEOUT_VMID_BUF_ENA */
#define WM8993_LINEOUT_VMID_BUF_ENA_SHIFT            7  /* LINEOUT_VMID_BUF_ENA */
#define WM8993_LINEOUT_VMID_BUF_ENA_WIDTH            1  /* LINEOUT_VMID_BUF_ENA */
#define WM8993_HPOUT2_IN_ENA                    0x0040  /* HPOUT2_IN_ENA */
#define WM8993_HPOUT2_IN_ENA_MASK               0x0040  /* HPOUT2_IN_ENA */
#define WM8993_HPOUT2_IN_ENA_SHIFT                   6  /* HPOUT2_IN_ENA */
#define WM8993_HPOUT2_IN_ENA_WIDTH                   1  /* HPOUT2_IN_ENA */
#define WM8993_LINEOUT1_DISCH                   0x0020  /* LINEOUT1_DISCH */
#define WM8993_LINEOUT1_DISCH_MASK              0x0020  /* LINEOUT1_DISCH */
#define WM8993_LINEOUT1_DISCH_SHIFT                  5  /* LINEOUT1_DISCH */
#define WM8993_LINEOUT1_DISCH_WIDTH                  1  /* LINEOUT1_DISCH */
#define WM8993_LINEOUT2_DISCH                   0x0010  /* LINEOUT2_DISCH */
#define WM8993_LINEOUT2_DISCH_MASK              0x0010  /* LINEOUT2_DISCH */
#define WM8993_LINEOUT2_DISCH_SHIFT                  4  /* LINEOUT2_DISCH */
#define WM8993_LINEOUT2_DISCH_WIDTH                  1  /* LINEOUT2_DISCH */

/*
 * R57 (0x39) - AntiPOP2
 */
#define WM8993_VMID_RAMP_MASK                   0x0060  /* VMID_RAMP - [6:5] */
#define WM8993_VMID_RAMP_SHIFT                       5  /* VMID_RAMP - [6:5] */
#define WM8993_VMID_RAMP_WIDTH                       2  /* VMID_RAMP - [6:5] */
#define WM8993_VMID_BUF_ENA                     0x0008  /* VMID_BUF_ENA */
#define WM8993_VMID_BUF_ENA_MASK                0x0008  /* VMID_BUF_ENA */
#define WM8993_VMID_BUF_ENA_SHIFT                    3  /* VMID_BUF_ENA */
#define WM8993_VMID_BUF_ENA_WIDTH                    1  /* VMID_BUF_ENA */
#define WM8993_STARTUP_BIAS_ENA                 0x0004  /* STARTUP_BIAS_ENA */
#define WM8993_STARTUP_BIAS_ENA_MASK            0x0004  /* STARTUP_BIAS_ENA */
#define WM8993_STARTUP_BIAS_ENA_SHIFT                2  /* STARTUP_BIAS_ENA */
#define WM8993_STARTUP_BIAS_ENA_WIDTH                1  /* STARTUP_BIAS_ENA */
#define WM8993_BIAS_SRC                         0x0002  /* BIAS_SRC */
#define WM8993_BIAS_SRC_MASK                    0x0002  /* BIAS_SRC */
#define WM8993_BIAS_SRC_SHIFT                        1  /* BIAS_SRC */
#define WM8993_BIAS_SRC_WIDTH                        1  /* BIAS_SRC */
#define WM8993_VMID_DISCH                       0x0001  /* VMID_DISCH */
#define WM8993_VMID_DISCH_MASK                  0x0001  /* VMID_DISCH */
#define WM8993_VMID_DISCH_SHIFT                      0  /* VMID_DISCH */
#define WM8993_VMID_DISCH_WIDTH                      1  /* VMID_DISCH */

/*
 * R58 (0x3A) - MICBIAS
 */
#define WM8993_JD_SCTHR_MASK                    0x00C0  /* JD_SCTHR - [7:6] */
#define WM8993_JD_SCTHR_SHIFT                        6  /* JD_SCTHR - [7:6] */
#define WM8993_JD_SCTHR_WIDTH                        2  /* JD_SCTHR - [7:6] */
#define WM8993_JD_THR_MASK                      0x0030  /* JD_THR - [5:4] */
#define WM8993_JD_THR_SHIFT                          4  /* JD_THR - [5:4] */
#define WM8993_JD_THR_WIDTH                          2  /* JD_THR - [5:4] */
#define WM8993_JD_ENA                           0x0004  /* JD_ENA */
#define WM8993_JD_ENA_MASK                      0x0004  /* JD_ENA */
#define WM8993_JD_ENA_SHIFT                          2  /* JD_ENA */
#define WM8993_JD_ENA_WIDTH                          1  /* JD_ENA */
#define WM8993_MICB2_LVL                        0x0002  /* MICB2_LVL */
#define WM8993_MICB2_LVL_MASK                   0x0002  /* MICB2_LVL */
#define WM8993_MICB2_LVL_SHIFT                       1  /* MICB2_LVL */
#define WM8993_MICB2_LVL_WIDTH                       1  /* MICB2_LVL */
#define WM8993_MICB1_LVL                        0x0001  /* MICB1_LVL */
#define WM8993_MICB1_LVL_MASK                   0x0001  /* MICB1_LVL */
#define WM8993_MICB1_LVL_SHIFT                       0  /* MICB1_LVL */
#define WM8993_MICB1_LVL_WIDTH                       1  /* MICB1_LVL */

/*
 * R60 (0x3C) - FLL Control 1
 */
#define WM8993_FLL_FRAC                         0x0004  /* FLL_FRAC */
#define WM8993_FLL_FRAC_MASK                    0x0004  /* FLL_FRAC */
#define WM8993_FLL_FRAC_SHIFT                        2  /* FLL_FRAC */
#define WM8993_FLL_FRAC_WIDTH                        1  /* FLL_FRAC */
#define WM8993_FLL_OSC_ENA                      0x0002  /* FLL_OSC_ENA */
#define WM8993_FLL_OSC_ENA_MASK                 0x0002  /* FLL_OSC_ENA */
#define WM8993_FLL_OSC_ENA_SHIFT                     1  /* FLL_OSC_ENA */
#define WM8993_FLL_OSC_ENA_WIDTH                     1  /* FLL_OSC_ENA */
#define WM8993_FLL_ENA                          0x0001  /* FLL_ENA */
#define WM8993_FLL_ENA_MASK                     0x0001  /* FLL_ENA */
#define WM8993_FLL_ENA_SHIFT                         0  /* FLL_ENA */
#define WM8993_FLL_ENA_WIDTH                         1  /* FLL_ENA */

/*
 * R61 (0x3D) - FLL Control 2
 */
#define WM8993_FLL_OUTDIV_MASK                  0x0700  /* FLL_OUTDIV - [10:8] */
#define WM8993_FLL_OUTDIV_SHIFT                      8  /* FLL_OUTDIV - [10:8] */
#define WM8993_FLL_OUTDIV_WIDTH                      3  /* FLL_OUTDIV - [10:8] */
#define WM8993_FLL_CTRL_RATE_MASK               0x0070  /* FLL_CTRL_RATE - [6:4] */
#define WM8993_FLL_CTRL_RATE_SHIFT                   4  /* FLL_CTRL_RATE - [6:4] */
#define WM8993_FLL_CTRL_RATE_WIDTH                   3  /* FLL_CTRL_RATE - [6:4] */
#define WM8993_FLL_FRATIO_MASK                  0x0007  /* FLL_FRATIO - [2:0] */
#define WM8993_FLL_FRATIO_SHIFT                      0  /* FLL_FRATIO - [2:0] */
#define WM8993_FLL_FRATIO_WIDTH                      3  /* FLL_FRATIO - [2:0] */

/*
 * R62 (0x3E) - FLL Control 3
 */
#define WM8993_FLL_K_MASK                       0xFFFF  /* FLL_K - [15:0] */
#define WM8993_FLL_K_SHIFT                           0  /* FLL_K - [15:0] */
#define WM8993_FLL_K_WIDTH                          16  /* FLL_K - [15:0] */

/*
 * R63 (0x3F) - FLL Control 4
 */
#define WM8993_FLL_N_MASK                       0x7FE0  /* FLL_N - [14:5] */
#define WM8993_FLL_N_SHIFT                           5  /* FLL_N - [14:5] */
#define WM8993_FLL_N_WIDTH                          10  /* FLL_N - [14:5] */
#define WM8993_FLL_GAIN_MASK                    0x000F  /* FLL_GAIN - [3:0] */
#define WM8993_FLL_GAIN_SHIFT                        0  /* FLL_GAIN - [3:0] */
#define WM8993_FLL_GAIN_WIDTH                        4  /* FLL_GAIN - [3:0] */

/*
 * R64 (0x40) - FLL Control 5
 */
#define WM8993_FLL_FRC_NCO_VAL_MASK             0x1F80  /* FLL_FRC_NCO_VAL - [12:7] */
#define WM8993_FLL_FRC_NCO_VAL_SHIFT                 7  /* FLL_FRC_NCO_VAL - [12:7] */
#define WM8993_FLL_FRC_NCO_VAL_WIDTH                 6  /* FLL_FRC_NCO_VAL - [12:7] */
#define WM8993_FLL_FRC_NCO                      0x0040  /* FLL_FRC_NCO */
#define WM8993_FLL_FRC_NCO_MASK                 0x0040  /* FLL_FRC_NCO */
#define WM8993_FLL_FRC_NCO_SHIFT                     6  /* FLL_FRC_NCO */
#define WM8993_FLL_FRC_NCO_WIDTH                     1  /* FLL_FRC_NCO */
#define WM8993_FLL_CLK_REF_DIV_MASK             0x0018  /* FLL_CLK_REF_DIV - [4:3] */
#define WM8993_FLL_CLK_REF_DIV_SHIFT                 3  /* FLL_CLK_REF_DIV - [4:3] */
#define WM8993_FLL_CLK_REF_DIV_WIDTH                 2  /* FLL_CLK_REF_DIV - [4:3] */
#define WM8993_FLL_CLK_SRC_MASK                 0x0003  /* FLL_CLK_SRC - [1:0] */
#define WM8993_FLL_CLK_SRC_SHIFT                     0  /* FLL_CLK_SRC - [1:0] */
#define WM8993_FLL_CLK_SRC_WIDTH                     2  /* FLL_CLK_SRC - [1:0] */

/*
 * R65 (0x41) - Clocking 3
 */
#define WM8993_CLK_DCS_DIV_MASK                 0x3C00  /* CLK_DCS_DIV - [13:10] */
#define WM8993_CLK_DCS_DIV_SHIFT                    10  /* CLK_DCS_DIV - [13:10] */
#define WM8993_CLK_DCS_DIV_WIDTH                     4  /* CLK_DCS_DIV - [13:10] */
#define WM8993_SAMPLE_RATE_MASK                 0x0380  /* SAMPLE_RATE - [9:7] */
#define WM8993_SAMPLE_RATE_SHIFT                     7  /* SAMPLE_RATE - [9:7] */
#define WM8993_SAMPLE_RATE_WIDTH                     3  /* SAMPLE_RATE - [9:7] */
#define WM8993_CLK_SYS_RATE_MASK                0x001E  /* CLK_SYS_RATE - [4:1] */
#define WM8993_CLK_SYS_RATE_SHIFT                    1  /* CLK_SYS_RATE - [4:1] */
#define WM8993_CLK_SYS_RATE_WIDTH                    4  /* CLK_SYS_RATE - [4:1] */
#define WM8993_CLK_DSP_ENA                      0x0001  /* CLK_DSP_ENA */
#define WM8993_CLK_DSP_ENA_MASK                 0x0001  /* CLK_DSP_ENA */
#define WM8993_CLK_DSP_ENA_SHIFT                     0  /* CLK_DSP_ENA */
#define WM8993_CLK_DSP_ENA_WIDTH                     1  /* CLK_DSP_ENA */

/*
 * R66 (0x42) - Clocking 4
 */
#define WM8993_DAC_DIV4                         0x0200  /* DAC_DIV4 */
#define WM8993_DAC_DIV4_MASK                    0x0200  /* DAC_DIV4 */
#define WM8993_DAC_DIV4_SHIFT                        9  /* DAC_DIV4 */
#define WM8993_DAC_DIV4_WIDTH                        1  /* DAC_DIV4 */
#define WM8993_CLK_256K_DIV_MASK                0x007E  /* CLK_256K_DIV - [6:1] */
#define WM8993_CLK_256K_DIV_SHIFT                    1  /* CLK_256K_DIV - [6:1] */
#define WM8993_CLK_256K_DIV_WIDTH                    6  /* CLK_256K_DIV - [6:1] */
#define WM8993_SR_MODE                          0x0001  /* SR_MODE */
#define WM8993_SR_MODE_MASK                     0x0001  /* SR_MODE */
#define WM8993_SR_MODE_SHIFT                         0  /* SR_MODE */
#define WM8993_SR_MODE_WIDTH                         1  /* SR_MODE */

/*
 * R67 (0x43) - MW Slave Control
 */
#define WM8993_MASK_WRITE_ENA                   0x0001  /* MASK_WRITE_ENA */
#define WM8993_MASK_WRITE_ENA_MASK              0x0001  /* MASK_WRITE_ENA */
#define WM8993_MASK_WRITE_ENA_SHIFT                  0  /* MASK_WRITE_ENA */
#define WM8993_MASK_WRITE_ENA_WIDTH                  1  /* MASK_WRITE_ENA */

/*
 * R69 (0x45) - Bus Control 1
 */
#define WM8993_CLK_SYS_ENA                      0x0002  /* CLK_SYS_ENA */
#define WM8993_CLK_SYS_ENA_MASK                 0x0002  /* CLK_SYS_ENA */
#define WM8993_CLK_SYS_ENA_SHIFT                     1  /* CLK_SYS_ENA */
#define WM8993_CLK_SYS_ENA_WIDTH                     1  /* CLK_SYS_ENA */

/*
 * R70 (0x46) - Write Sequencer 0
 */
#define WM8993_WSEQ_ENA                         0x0100  /* WSEQ_ENA */
#define WM8993_WSEQ_ENA_MASK                    0x0100  /* WSEQ_ENA */
#define WM8993_WSEQ_ENA_SHIFT                        8  /* WSEQ_ENA */
#define WM8993_WSEQ_ENA_WIDTH                        1  /* WSEQ_ENA */
#define WM8993_WSEQ_WRITE_INDEX_MASK            0x001F  /* WSEQ_WRITE_INDEX - [4:0] */
#define WM8993_WSEQ_WRITE_INDEX_SHIFT                0  /* WSEQ_WRITE_INDEX - [4:0] */
#define WM8993_WSEQ_WRITE_INDEX_WIDTH                5  /* WSEQ_WRITE_INDEX - [4:0] */

/*
 * R71 (0x47) - Write Sequencer 1
 */
#define WM8993_WSEQ_DATA_WIDTH_MASK             0x7000  /* WSEQ_DATA_WIDTH - [14:12] */
#define WM8993_WSEQ_DATA_WIDTH_SHIFT                12  /* WSEQ_DATA_WIDTH - [14:12] */
#define WM8993_WSEQ_DATA_WIDTH_WIDTH                 3  /* WSEQ_DATA_WIDTH - [14:12] */
#define WM8993_WSEQ_DATA_START_MASK             0x0F00  /* WSEQ_DATA_START - [11:8] */
#define WM8993_WSEQ_DATA_START_SHIFT                 8  /* WSEQ_DATA_START - [11:8] */
#define WM8993_WSEQ_DATA_START_WIDTH                 4  /* WSEQ_DATA_START - [11:8] */
#define WM8993_WSEQ_ADDR_MASK                   0x00FF  /* WSEQ_ADDR - [7:0] */
#define WM8993_WSEQ_ADDR_SHIFT                       0  /* WSEQ_ADDR - [7:0] */
#define WM8993_WSEQ_ADDR_WIDTH                       8  /* WSEQ_ADDR - [7:0] */

/*
 * R72 (0x48) - Write Sequencer 2
 */
#define WM8993_WSEQ_EOS                         0x4000  /* WSEQ_EOS */
#define WM8993_WSEQ_EOS_MASK                    0x4000  /* WSEQ_EOS */
#define WM8993_WSEQ_EOS_SHIFT                       14  /* WSEQ_EOS */
#define WM8993_WSEQ_EOS_WIDTH                        1  /* WSEQ_EOS */
#define WM8993_WSEQ_DELAY_MASK                  0x0F00  /* WSEQ_DELAY - [11:8] */
#define WM8993_WSEQ_DELAY_SHIFT                      8  /* WSEQ_DELAY - [11:8] */
#define WM8993_WSEQ_DELAY_WIDTH                      4  /* WSEQ_DELAY - [11:8] */
#define WM8993_WSEQ_DATA_MASK                   0x00FF  /* WSEQ_DATA - [7:0] */
#define WM8993_WSEQ_DATA_SHIFT                       0  /* WSEQ_DATA - [7:0] */
#define WM8993_WSEQ_DATA_WIDTH                       8  /* WSEQ_DATA - [7:0] */

/*
 * R73 (0x49) - Write Sequencer 3
 */
#define WM8993_WSEQ_ABORT                       0x0200  /* WSEQ_ABORT */
#define WM8993_WSEQ_ABORT_MASK                  0x0200  /* WSEQ_ABORT */
#define WM8993_WSEQ_ABORT_SHIFT                      9  /* WSEQ_ABORT */
#define WM8993_WSEQ_ABORT_WIDTH                      1  /* WSEQ_ABORT */
#define WM8993_WSEQ_START                       0x0100  /* WSEQ_START */
#define WM8993_WSEQ_START_MASK                  0x0100  /* WSEQ_START */
#define WM8993_WSEQ_START_SHIFT                      8  /* WSEQ_START */
#define WM8993_WSEQ_START_WIDTH                      1  /* WSEQ_START */
#define WM8993_WSEQ_START_INDEX_MASK            0x003F  /* WSEQ_START_INDEX - [5:0] */
#define WM8993_WSEQ_START_INDEX_SHIFT                0  /* WSEQ_START_INDEX - [5:0] */
#define WM8993_WSEQ_START_INDEX_WIDTH                6  /* WSEQ_START_INDEX - [5:0] */

/*
 * R74 (0x4A) - Write Sequencer 4
 */
#define WM8993_WSEQ_BUSY                        0x0001  /* WSEQ_BUSY */
#define WM8993_WSEQ_BUSY_MASK                   0x0001  /* WSEQ_BUSY */
#define WM8993_WSEQ_BUSY_SHIFT                       0  /* WSEQ_BUSY */
#define WM8993_WSEQ_BUSY_WIDTH                       1  /* WSEQ_BUSY */

/*
 * R75 (0x4B) - Write Sequencer 5
 */
#define WM8993_WSEQ_CURRENT_INDEX_MASK          0x003F  /* WSEQ_CURRENT_INDEX - [5:0] */
#define WM8993_WSEQ_CURRENT_INDEX_SHIFT              0  /* WSEQ_CURRENT_INDEX - [5:0] */
#define WM8993_WSEQ_CURRENT_INDEX_WIDTH              6  /* WSEQ_CURRENT_INDEX - [5:0] */

/*
 * R76 (0x4C) - Charge Pump 1
 */
#define WM8993_CP_ENA                           0x8000  /* CP_ENA */
#define WM8993_CP_ENA_MASK                      0x8000  /* CP_ENA */
#define WM8993_CP_ENA_SHIFT                         15  /* CP_ENA */
#define WM8993_CP_ENA_WIDTH                          1  /* CP_ENA */

/*
 * R81 (0x51) - Class W 0
 */
#define WM8993_CP_DYN_FREQ                      0x0002  /* CP_DYN_FREQ */
#define WM8993_CP_DYN_FREQ_MASK                 0x0002  /* CP_DYN_FREQ */
#define WM8993_CP_DYN_FREQ_SHIFT                     1  /* CP_DYN_FREQ */
#define WM8993_CP_DYN_FREQ_WIDTH                     1  /* CP_DYN_FREQ */
#define WM8993_CP_DYN_V                         0x0001  /* CP_DYN_V */
#define WM8993_CP_DYN_V_MASK                    0x0001  /* CP_DYN_V */
#define WM8993_CP_DYN_V_SHIFT                        0  /* CP_DYN_V */
#define WM8993_CP_DYN_V_WIDTH                        1  /* CP_DYN_V */

/*
 * R84 (0x54) - DC Servo 0
 */
#define WM8993_DCS_TRIG_SINGLE_1                0x2000  /* DCS_TRIG_SINGLE_1 */
#define WM8993_DCS_TRIG_SINGLE_1_MASK           0x2000  /* DCS_TRIG_SINGLE_1 */
#define WM8993_DCS_TRIG_SINGLE_1_SHIFT              13  /* DCS_TRIG_SINGLE_1 */
#define WM8993_DCS_TRIG_SINGLE_1_WIDTH               1  /* DCS_TRIG_SINGLE_1 */
#define WM8993_DCS_TRIG_SINGLE_0                0x1000  /* DCS_TRIG_SINGLE_0 */
#define WM8993_DCS_TRIG_SINGLE_0_MASK           0x1000  /* DCS_TRIG_SINGLE_0 */
#define WM8993_DCS_TRIG_SINGLE_0_SHIFT              12  /* DCS_TRIG_SINGLE_0 */
#define WM8993_DCS_TRIG_SINGLE_0_WIDTH               1  /* DCS_TRIG_SINGLE_0 */
#define WM8993_DCS_TRIG_SERIES_1                0x0200  /* DCS_TRIG_SERIES_1 */
#define WM8993_DCS_TRIG_SERIES_1_MASK           0x0200  /* DCS_TRIG_SERIES_1 */
#define WM8993_DCS_TRIG_SERIES_1_SHIFT               9  /* DCS_TRIG_SERIES_1 */
#define WM8993_DCS_TRIG_SERIES_1_WIDTH               1  /* DCS_TRIG_SERIES_1 */
#define WM8993_DCS_TRIG_SERIES_0                0x0100  /* DCS_TRIG_SERIES_0 */
#define WM8993_DCS_TRIG_SERIES_0_MASK           0x0100  /* DCS_TRIG_SERIES_0 */
#define WM8993_DCS_TRIG_SERIES_0_SHIFT               8  /* DCS_TRIG_SERIES_0 */
#define WM8993_DCS_TRIG_SERIES_0_WIDTH               1  /* DCS_TRIG_SERIES_0 */
#define WM8993_DCS_TRIG_STARTUP_1               0x0020  /* DCS_TRIG_STARTUP_1 */
#define WM8993_DCS_TRIG_STARTUP_1_MASK          0x0020  /* DCS_TRIG_STARTUP_1 */
#define WM8993_DCS_TRIG_STARTUP_1_SHIFT              5  /* DCS_TRIG_STARTUP_1 */
#define WM8993_DCS_TRIG_STARTUP_1_WIDTH              1  /* DCS_TRIG_STARTUP_1 */
#define WM8993_DCS_TRIG_STARTUP_0               0x0010  /* DCS_TRIG_STARTUP_0 */
#define WM8993_DCS_TRIG_STARTUP_0_MASK          0x0010  /* DCS_TRIG_STARTUP_0 */
#define WM8993_DCS_TRIG_STARTUP_0_SHIFT              4  /* DCS_TRIG_STARTUP_0 */
#define WM8993_DCS_TRIG_STARTUP_0_WIDTH              1  /* DCS_TRIG_STARTUP_0 */
#define WM8993_DCS_TRIG_DAC_WR_1                0x0008  /* DCS_TRIG_DAC_WR_1 */
#define WM8993_DCS_TRIG_DAC_WR_1_MASK           0x0008  /* DCS_TRIG_DAC_WR_1 */
#define WM8993_DCS_TRIG_DAC_WR_1_SHIFT               3  /* DCS_TRIG_DAC_WR_1 */
#define WM8993_DCS_TRIG_DAC_WR_1_WIDTH               1  /* DCS_TRIG_DAC_WR_1 */
#define WM8993_DCS_TRIG_DAC_WR_0                0x0004  /* DCS_TRIG_DAC_WR_0 */
#define WM8993_DCS_TRIG_DAC_WR_0_MASK           0x0004  /* DCS_TRIG_DAC_WR_0 */
#define WM8993_DCS_TRIG_DAC_WR_0_SHIFT               2  /* DCS_TRIG_DAC_WR_0 */
#define WM8993_DCS_TRIG_DAC_WR_0_WIDTH               1  /* DCS_TRIG_DAC_WR_0 */
#define WM8993_DCS_ENA_CHAN_1                   0x0002  /* DCS_ENA_CHAN_1 */
#define WM8993_DCS_ENA_CHAN_1_MASK              0x0002  /* DCS_ENA_CHAN_1 */
#define WM8993_DCS_ENA_CHAN_1_SHIFT                  1  /* DCS_ENA_CHAN_1 */
#define WM8993_DCS_ENA_CHAN_1_WIDTH                  1  /* DCS_ENA_CHAN_1 */
#define WM8993_DCS_ENA_CHAN_0                   0x0001  /* DCS_ENA_CHAN_0 */
#define WM8993_DCS_ENA_CHAN_0_MASK              0x0001  /* DCS_ENA_CHAN_0 */
#define WM8993_DCS_ENA_CHAN_0_SHIFT                  0  /* DCS_ENA_CHAN_0 */
#define WM8993_DCS_ENA_CHAN_0_WIDTH                  1  /* DCS_ENA_CHAN_0 */

/*
 * R85 (0x55) - DC Servo 1
 */
#define WM8993_DCS_SERIES_NO_01_MASK            0x0FE0  /* DCS_SERIES_NO_01 - [11:5] */
#define WM8993_DCS_SERIES_NO_01_SHIFT                5  /* DCS_SERIES_NO_01 - [11:5] */
#define WM8993_DCS_SERIES_NO_01_WIDTH                7  /* DCS_SERIES_NO_01 - [11:5] */
#define WM8993_DCS_TIMER_PERIOD_01_MASK         0x000F  /* DCS_TIMER_PERIOD_01 - [3:0] */
#define WM8993_DCS_TIMER_PERIOD_01_SHIFT             0  /* DCS_TIMER_PERIOD_01 - [3:0] */
#define WM8993_DCS_TIMER_PERIOD_01_WIDTH             4  /* DCS_TIMER_PERIOD_01 - [3:0] */

/*
 * R87 (0x57) - DC Servo 3
 */
#define WM8993_DCS_DAC_WR_VAL_1_MASK            0xFF00  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM8993_DCS_DAC_WR_VAL_1_SHIFT                8  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM8993_DCS_DAC_WR_VAL_1_WIDTH                8  /* DCS_DAC_WR_VAL_1 - [15:8] */
#define WM8993_DCS_DAC_WR_VAL_0_MASK            0x00FF  /* DCS_DAC_WR_VAL_0 - [7:0] */
#define WM8993_DCS_DAC_WR_VAL_0_SHIFT                0  /* DCS_DAC_WR_VAL_0 - [7:0] */
#define WM8993_DCS_DAC_WR_VAL_0_WIDTH                8  /* DCS_DAC_WR_VAL_0 - [7:0] */

/*
 * R88 (0x58) - DC Servo Readback 0
 */
#define WM8993_DCS_DATAPATH_BUSY                0x4000  /* DCS_DATAPATH_BUSY */
#define WM8993_DCS_DATAPATH_BUSY_MASK           0x4000  /* DCS_DATAPATH_BUSY */
#define WM8993_DCS_DATAPATH_BUSY_SHIFT              14  /* DCS_DATAPATH_BUSY */
#define WM8993_DCS_DATAPATH_BUSY_WIDTH               1  /* DCS_DATAPATH_BUSY */
#define WM8993_DCS_CHANNEL_MASK                 0x3000  /* DCS_CHANNEL - [13:12] */
#define WM8993_DCS_CHANNEL_SHIFT                    12  /* DCS_CHANNEL - [13:12] */
#define WM8993_DCS_CHANNEL_WIDTH                     2  /* DCS_CHANNEL - [13:12] */
#define WM8993_DCS_CAL_COMPLETE_MASK            0x0300  /* DCS_CAL_COMPLETE - [9:8] */
#define WM8993_DCS_CAL_COMPLETE_SHIFT                8  /* DCS_CAL_COMPLETE - [9:8] */
#define WM8993_DCS_CAL_COMPLETE_WIDTH                2  /* DCS_CAL_COMPLETE - [9:8] */
#define WM8993_DCS_DAC_WR_COMPLETE_MASK         0x0030  /* DCS_DAC_WR_COMPLETE - [5:4] */
#define WM8993_DCS_DAC_WR_COMPLETE_SHIFT             4  /* DCS_DAC_WR_COMPLETE - [5:4] */
#define WM8993_DCS_DAC_WR_COMPLETE_WIDTH             2  /* DCS_DAC_WR_COMPLETE - [5:4] */
#define WM8993_DCS_STARTUP_COMPLETE_MASK        0x0003  /* DCS_STARTUP_COMPLETE - [1:0] */
#define WM8993_DCS_STARTUP_COMPLETE_SHIFT            0  /* DCS_STARTUP_COMPLETE - [1:0] */
#define WM8993_DCS_STARTUP_COMPLETE_WIDTH            2  /* DCS_STARTUP_COMPLETE - [1:0] */

/*
 * R89 (0x59) - DC Servo Readback 1
 */
#define WM8993_DCS_INTEG_CHAN_1_MASK            0x00FF  /* DCS_INTEG_CHAN_1 - [7:0] */
#define WM8993_DCS_INTEG_CHAN_1_SHIFT                0  /* DCS_INTEG_CHAN_1 - [7:0] */
#define WM8993_DCS_INTEG_CHAN_1_WIDTH                8  /* DCS_INTEG_CHAN_1 - [7:0] */

/*
 * R90 (0x5A) - DC Servo Readback 2
 */
#define WM8993_DCS_INTEG_CHAN_0_MASK            0x00FF  /* DCS_INTEG_CHAN_0 - [7:0] */
#define WM8993_DCS_INTEG_CHAN_0_SHIFT                0  /* DCS_INTEG_CHAN_0 - [7:0] */
#define WM8993_DCS_INTEG_CHAN_0_WIDTH                8  /* DCS_INTEG_CHAN_0 - [7:0] */

/*
 * R96 (0x60) - Analogue HP 0
 */
#define WM8993_HPOUT1_AUTO_PU                   0x0100  /* HPOUT1_AUTO_PU */
#define WM8993_HPOUT1_AUTO_PU_MASK              0x0100  /* HPOUT1_AUTO_PU */
#define WM8993_HPOUT1_AUTO_PU_SHIFT                  8  /* HPOUT1_AUTO_PU */
#define WM8993_HPOUT1_AUTO_PU_WIDTH                  1  /* HPOUT1_AUTO_PU */
#define WM8993_HPOUT1L_RMV_SHORT                0x0080  /* HPOUT1L_RMV_SHORT */
#define WM8993_HPOUT1L_RMV_SHORT_MASK           0x0080  /* HPOUT1L_RMV_SHORT */
#define WM8993_HPOUT1L_RMV_SHORT_SHIFT               7  /* HPOUT1L_RMV_SHORT */
#define WM8993_HPOUT1L_RMV_SHORT_WIDTH               1  /* HPOUT1L_RMV_SHORT */
#define WM8993_HPOUT1L_OUTP                     0x0040  /* HPOUT1L_OUTP */
#define WM8993_HPOUT1L_OUTP_MASK                0x0040  /* HPOUT1L_OUTP */
#define WM8993_HPOUT1L_OUTP_SHIFT                    6  /* HPOUT1L_OUTP */
#define WM8993_HPOUT1L_OUTP_WIDTH                    1  /* HPOUT1L_OUTP */
#define WM8993_HPOUT1L_DLY                      0x0020  /* HPOUT1L_DLY */
#define WM8993_HPOUT1L_DLY_MASK                 0x0020  /* HPOUT1L_DLY */
#define WM8993_HPOUT1L_DLY_SHIFT                     5  /* HPOUT1L_DLY */
#define WM8993_HPOUT1L_DLY_WIDTH                     1  /* HPOUT1L_DLY */
#define WM8993_HPOUT1R_RMV_SHORT                0x0008  /* HPOUT1R_RMV_SHORT */
#define WM8993_HPOUT1R_RMV_SHORT_MASK           0x0008  /* HPOUT1R_RMV_SHORT */
#define WM8993_HPOUT1R_RMV_SHORT_SHIFT               3  /* HPOUT1R_RMV_SHORT */
#define WM8993_HPOUT1R_RMV_SHORT_WIDTH               1  /* HPOUT1R_RMV_SHORT */
#define WM8993_HPOUT1R_OUTP                     0x0004  /* HPOUT1R_OUTP */
#define WM8993_HPOUT1R_OUTP_MASK                0x0004  /* HPOUT1R_OUTP */
#define WM8993_HPOUT1R_OUTP_SHIFT                    2  /* HPOUT1R_OUTP */
#define WM8993_HPOUT1R_OUTP_WIDTH                    1  /* HPOUT1R_OUTP */
#define WM8993_HPOUT1R_DLY                      0x0002  /* HPOUT1R_DLY */
#define WM8993_HPOUT1R_DLY_MASK                 0x0002  /* HPOUT1R_DLY */
#define WM8993_HPOUT1R_DLY_SHIFT                     1  /* HPOUT1R_DLY */
#define WM8993_HPOUT1R_DLY_WIDTH                     1  /* HPOUT1R_DLY */

/*
 * R98 (0x62) - EQ1
 */
#define WM8993_EQ_ENA                           0x0001  /* EQ_ENA */
#define WM8993_EQ_ENA_MASK                      0x0001  /* EQ_ENA */
#define WM8993_EQ_ENA_SHIFT                          0  /* EQ_ENA */
#define WM8993_EQ_ENA_WIDTH                          1  /* EQ_ENA */

/*
 * R99 (0x63) - EQ2
 */
#define WM8993_EQ_B1_GAIN_MASK                  0x001F  /* EQ_B1_GAIN - [4:0] */
#define WM8993_EQ_B1_GAIN_SHIFT                      0  /* EQ_B1_GAIN - [4:0] */
#define WM8993_EQ_B1_GAIN_WIDTH                      5  /* EQ_B1_GAIN - [4:0] */

/*
 * R100 (0x64) - EQ3
 */
#define WM8993_EQ_B2_GAIN_MASK                  0x001F  /* EQ_B2_GAIN - [4:0] */
#define WM8993_EQ_B2_GAIN_SHIFT                      0  /* EQ_B2_GAIN - [4:0] */
#define WM8993_EQ_B2_GAIN_WIDTH                      5  /* EQ_B2_GAIN - [4:0] */

/*
 * R101 (0x65) - EQ4
 */
#define WM8993_EQ_B3_GAIN_MASK                  0x001F  /* EQ_B3_GAIN - [4:0] */
#define WM8993_EQ_B3_GAIN_SHIFT                      0  /* EQ_B3_GAIN - [4:0] */
#define WM8993_EQ_B3_GAIN_WIDTH                      5  /* EQ_B3_GAIN - [4:0] */

/*
 * R102 (0x66) - EQ5
 */
#define WM8993_EQ_B4_GAIN_MASK                  0x001F  /* EQ_B4_GAIN - [4:0] */
#define WM8993_EQ_B4_GAIN_SHIFT                      0  /* EQ_B4_GAIN - [4:0] */
#define WM8993_EQ_B4_GAIN_WIDTH                      5  /* EQ_B4_GAIN - [4:0] */

/*
 * R103 (0x67) - EQ6
 */
#define WM8993_EQ_B5_GAIN_MASK                  0x001F  /* EQ_B5_GAIN - [4:0] */
#define WM8993_EQ_B5_GAIN_SHIFT                      0  /* EQ_B5_GAIN - [4:0] */
#define WM8993_EQ_B5_GAIN_WIDTH                      5  /* EQ_B5_GAIN - [4:0] */

/*
 * R104 (0x68) - EQ7
 */
#define WM8993_EQ_B1_A_MASK                     0xFFFF  /* EQ_B1_A - [15:0] */
#define WM8993_EQ_B1_A_SHIFT                         0  /* EQ_B1_A - [15:0] */
#define WM8993_EQ_B1_A_WIDTH                        16  /* EQ_B1_A - [15:0] */

/*
 * R105 (0x69) - EQ8
 */
#define WM8993_EQ_B1_B_MASK                     0xFFFF  /* EQ_B1_B - [15:0] */
#define WM8993_EQ_B1_B_SHIFT                         0  /* EQ_B1_B - [15:0] */
#define WM8993_EQ_B1_B_WIDTH                        16  /* EQ_B1_B - [15:0] */

/*
 * R106 (0x6A) - EQ9
 */
#define WM8993_EQ_B1_PG_MASK                    0xFFFF  /* EQ_B1_PG - [15:0] */
#define WM8993_EQ_B1_PG_SHIFT                        0  /* EQ_B1_PG - [15:0] */
#define WM8993_EQ_B1_PG_WIDTH                       16  /* EQ_B1_PG - [15:0] */

/*
 * R107 (0x6B) - EQ10
 */
#define WM8993_EQ_B2_A_MASK                     0xFFFF  /* EQ_B2_A - [15:0] */
#define WM8993_EQ_B2_A_SHIFT                         0  /* EQ_B2_A - [15:0] */
#define WM8993_EQ_B2_A_WIDTH                        16  /* EQ_B2_A - [15:0] */

/*
 * R108 (0x6C) - EQ11
 */
#define WM8993_EQ_B2_B_MASK                     0xFFFF  /* EQ_B2_B - [15:0] */
#define WM8993_EQ_B2_B_SHIFT                         0  /* EQ_B2_B - [15:0] */
#define WM8993_EQ_B2_B_WIDTH                        16  /* EQ_B2_B - [15:0] */

/*
 * R109 (0x6D) - EQ12
 */
#define WM8993_EQ_B2_C_MASK                     0xFFFF  /* EQ_B2_C - [15:0] */
#define WM8993_EQ_B2_C_SHIFT                         0  /* EQ_B2_C - [15:0] */
#define WM8993_EQ_B2_C_WIDTH                        16  /* EQ_B2_C - [15:0] */

/*
 * R110 (0x6E) - EQ13
 */
#define WM8993_EQ_B2_PG_MASK                    0xFFFF  /* EQ_B2_PG - [15:0] */
#define WM8993_EQ_B2_PG_SHIFT                        0  /* EQ_B2_PG - [15:0] */
#define WM8993_EQ_B2_PG_WIDTH                       16  /* EQ_B2_PG - [15:0] */

/*
 * R111 (0x6F) - EQ14
 */
#define WM8993_EQ_B3_A_MASK                     0xFFFF  /* EQ_B3_A - [15:0] */
#define WM8993_EQ_B3_A_SHIFT                         0  /* EQ_B3_A - [15:0] */
#define WM8993_EQ_B3_A_WIDTH                        16  /* EQ_B3_A - [15:0] */

/*
 * R112 (0x70) - EQ15
 */
#define WM8993_EQ_B3_B_MASK                     0xFFFF  /* EQ_B3_B - [15:0] */
#define WM8993_EQ_B3_B_SHIFT                         0  /* EQ_B3_B - [15:0] */
#define WM8993_EQ_B3_B_WIDTH                        16  /* EQ_B3_B - [15:0] */

/*
 * R113 (0x71) - EQ16
 */
#define WM8993_EQ_B3_C_MASK                     0xFFFF  /* EQ_B3_C - [15:0] */
#define WM8993_EQ_B3_C_SHIFT                         0  /* EQ_B3_C - [15:0] */
#define WM8993_EQ_B3_C_WIDTH                        16  /* EQ_B3_C - [15:0] */

/*
 * R114 (0x72) - EQ17
 */
#define WM8993_EQ_B3_PG_MASK                    0xFFFF  /* EQ_B3_PG - [15:0] */
#define WM8993_EQ_B3_PG_SHIFT                        0  /* EQ_B3_PG - [15:0] */
#define WM8993_EQ_B3_PG_WIDTH                       16  /* EQ_B3_PG - [15:0] */

/*
 * R115 (0x73) - EQ18
 */
#define WM8993_EQ_B4_A_MASK                     0xFFFF  /* EQ_B4_A - [15:0] */
#define WM8993_EQ_B4_A_SHIFT                         0  /* EQ_B4_A - [15:0] */
#define WM8993_EQ_B4_A_WIDTH                        16  /* EQ_B4_A - [15:0] */

/*
 * R116 (0x74) - EQ19
 */
#define WM8993_EQ_B4_B_MASK                     0xFFFF  /* EQ_B4_B - [15:0] */
#define WM8993_EQ_B4_B_SHIFT                         0  /* EQ_B4_B - [15:0] */
#define WM8993_EQ_B4_B_WIDTH                        16  /* EQ_B4_B - [15:0] */

/*
 * R117 (0x75) - EQ20
 */
#define WM8993_EQ_B4_C_MASK                     0xFFFF  /* EQ_B4_C - [15:0] */
#define WM8993_EQ_B4_C_SHIFT                         0  /* EQ_B4_C - [15:0] */
#define WM8993_EQ_B4_C_WIDTH                        16  /* EQ_B4_C - [15:0] */

/*
 * R118 (0x76) - EQ21
 */
#define WM8993_EQ_B4_PG_MASK                    0xFFFF  /* EQ_B4_PG - [15:0] */
#define WM8993_EQ_B4_PG_SHIFT                        0  /* EQ_B4_PG - [15:0] */
#define WM8993_EQ_B4_PG_WIDTH                       16  /* EQ_B4_PG - [15:0] */

/*
 * R119 (0x77) - EQ22
 */
#define WM8993_EQ_B5_A_MASK                     0xFFFF  /* EQ_B5_A - [15:0] */
#define WM8993_EQ_B5_A_SHIFT                         0  /* EQ_B5_A - [15:0] */
#define WM8993_EQ_B5_A_WIDTH                        16  /* EQ_B5_A - [15:0] */

/*
 * R120 (0x78) - EQ23
 */
#define WM8993_EQ_B5_B_MASK                     0xFFFF  /* EQ_B5_B - [15:0] */
#define WM8993_EQ_B5_B_SHIFT                         0  /* EQ_B5_B - [15:0] */
#define WM8993_EQ_B5_B_WIDTH                        16  /* EQ_B5_B - [15:0] */

/*
 * R121 (0x79) - EQ24
 */
#define WM8993_EQ_B5_PG_MASK                    0xFFFF  /* EQ_B5_PG - [15:0] */
#define WM8993_EQ_B5_PG_SHIFT                        0  /* EQ_B5_PG - [15:0] */
#define WM8993_EQ_B5_PG_WIDTH                       16  /* EQ_B5_PG - [15:0] */

/*
 * R122 (0x7A) - Digital Pulls
 */
#define WM8993_MCLK_PU                          0x0080  /* MCLK_PU */
#define WM8993_MCLK_PU_MASK                     0x0080  /* MCLK_PU */
#define WM8993_MCLK_PU_SHIFT                         7  /* MCLK_PU */
#define WM8993_MCLK_PU_WIDTH                         1  /* MCLK_PU */
#define WM8993_MCLK_PD                          0x0040  /* MCLK_PD */
#define WM8993_MCLK_PD_MASK                     0x0040  /* MCLK_PD */
#define WM8993_MCLK_PD_SHIFT                         6  /* MCLK_PD */
#define WM8993_MCLK_PD_WIDTH                         1  /* MCLK_PD */
#define WM8993_DACDAT_PU                        0x0020  /* DACDAT_PU */
#define WM8993_DACDAT_PU_MASK                   0x0020  /* DACDAT_PU */
#define WM8993_DACDAT_PU_SHIFT                       5  /* DACDAT_PU */
#define WM8993_DACDAT_PU_WIDTH                       1  /* DACDAT_PU */
#define WM8993_DACDAT_PD                        0x0010  /* DACDAT_PD */
#define WM8993_DACDAT_PD_MASK                   0x0010  /* DACDAT_PD */
#define WM8993_DACDAT_PD_SHIFT                       4  /* DACDAT_PD */
#define WM8993_DACDAT_PD_WIDTH                       1  /* DACDAT_PD */
#define WM8993_LRCLK_PU                         0x0008  /* LRCLK_PU */
#define WM8993_LRCLK_PU_MASK                    0x0008  /* LRCLK_PU */
#define WM8993_LRCLK_PU_SHIFT                        3  /* LRCLK_PU */
#define WM8993_LRCLK_PU_WIDTH                        1  /* LRCLK_PU */
#define WM8993_LRCLK_PD                         0x0004  /* LRCLK_PD */
#define WM8993_LRCLK_PD_MASK                    0x0004  /* LRCLK_PD */
#define WM8993_LRCLK_PD_SHIFT                        2  /* LRCLK_PD */
#define WM8993_LRCLK_PD_WIDTH                        1  /* LRCLK_PD */
#define WM8993_BCLK_PU                          0x0002  /* BCLK_PU */
#define WM8993_BCLK_PU_MASK                     0x0002  /* BCLK_PU */
#define WM8993_BCLK_PU_SHIFT                         1  /* BCLK_PU */
#define WM8993_BCLK_PU_WIDTH                         1  /* BCLK_PU */
#define WM8993_BCLK_PD                          0x0001  /* BCLK_PD */
#define WM8993_BCLK_PD_MASK                     0x0001  /* BCLK_PD */
#define WM8993_BCLK_PD_SHIFT                         0  /* BCLK_PD */
#define WM8993_BCLK_PD_WIDTH                         1  /* BCLK_PD */

/*
 * R123 (0x7B) - DRC Control 1
 */
#define WM8993_DRC_ENA                          0x8000  /* DRC_ENA */
#define WM8993_DRC_ENA_MASK                     0x8000  /* DRC_ENA */
#define WM8993_DRC_ENA_SHIFT                        15  /* DRC_ENA */
#define WM8993_DRC_ENA_WIDTH                         1  /* DRC_ENA */
#define WM8993_DRC_DAC_PATH                     0x4000  /* DRC_DAC_PATH */
#define WM8993_DRC_DAC_PATH_MASK                0x4000  /* DRC_DAC_PATH */
#define WM8993_DRC_DAC_PATH_SHIFT                   14  /* DRC_DAC_PATH */
#define WM8993_DRC_DAC_PATH_WIDTH                    1  /* DRC_DAC_PATH */
#define WM8993_DRC_SMOOTH_ENA                   0x0800  /* DRC_SMOOTH_ENA */
#define WM8993_DRC_SMOOTH_ENA_MASK              0x0800  /* DRC_SMOOTH_ENA */
#define WM8993_DRC_SMOOTH_ENA_SHIFT                 11  /* DRC_SMOOTH_ENA */
#define WM8993_DRC_SMOOTH_ENA_WIDTH                  1  /* DRC_SMOOTH_ENA */
#define WM8993_DRC_QR_ENA                       0x0400  /* DRC_QR_ENA */
#define WM8993_DRC_QR_ENA_MASK                  0x0400  /* DRC_QR_ENA */
#define WM8993_DRC_QR_ENA_SHIFT                     10  /* DRC_QR_ENA */
#define WM8993_DRC_QR_ENA_WIDTH                      1  /* DRC_QR_ENA */
#define WM8993_DRC_ANTICLIP_ENA                 0x0200  /* DRC_ANTICLIP_ENA */
#define WM8993_DRC_ANTICLIP_ENA_MASK            0x0200  /* DRC_ANTICLIP_ENA */
#define WM8993_DRC_ANTICLIP_ENA_SHIFT                9  /* DRC_ANTICLIP_ENA */
#define WM8993_DRC_ANTICLIP_ENA_WIDTH                1  /* DRC_ANTICLIP_ENA */
#define WM8993_DRC_HYST_ENA                     0x0100  /* DRC_HYST_ENA */
#define WM8993_DRC_HYST_ENA_MASK                0x0100  /* DRC_HYST_ENA */
#define WM8993_DRC_HYST_ENA_SHIFT                    8  /* DRC_HYST_ENA */
#define WM8993_DRC_HYST_ENA_WIDTH                    1  /* DRC_HYST_ENA */
#define WM8993_DRC_THRESH_HYST_MASK             0x0030  /* DRC_THRESH_HYST - [5:4] */
#define WM8993_DRC_THRESH_HYST_SHIFT                 4  /* DRC_THRESH_HYST - [5:4] */
#define WM8993_DRC_THRESH_HYST_WIDTH                 2  /* DRC_THRESH_HYST - [5:4] */
#define WM8993_DRC_MINGAIN_MASK                 0x000C  /* DRC_MINGAIN - [3:2] */
#define WM8993_DRC_MINGAIN_SHIFT                     2  /* DRC_MINGAIN - [3:2] */
#define WM8993_DRC_MINGAIN_WIDTH                     2  /* DRC_MINGAIN - [3:2] */
#define WM8993_DRC_MAXGAIN_MASK                 0x0003  /* DRC_MAXGAIN - [1:0] */
#define WM8993_DRC_MAXGAIN_SHIFT                     0  /* DRC_MAXGAIN - [1:0] */
#define WM8993_DRC_MAXGAIN_WIDTH                     2  /* DRC_MAXGAIN - [1:0] */

/*
 * R124 (0x7C) - DRC Control 2
 */
#define WM8993_DRC_ATTACK_RATE_MASK             0xF000  /* DRC_ATTACK_RATE - [15:12] */
#define WM8993_DRC_ATTACK_RATE_SHIFT                12  /* DRC_ATTACK_RATE - [15:12] */
#define WM8993_DRC_ATTACK_RATE_WIDTH                 4  /* DRC_ATTACK_RATE - [15:12] */
#define WM8993_DRC_DECAY_RATE_MASK              0x0F00  /* DRC_DECAY_RATE - [11:8] */
#define WM8993_DRC_DECAY_RATE_SHIFT                  8  /* DRC_DECAY_RATE - [11:8] */
#define WM8993_DRC_DECAY_RATE_WIDTH                  4  /* DRC_DECAY_RATE - [11:8] */
#define WM8993_DRC_THRESH_COMP_MASK             0x00FC  /* DRC_THRESH_COMP - [7:2] */
#define WM8993_DRC_THRESH_COMP_SHIFT                 2  /* DRC_THRESH_COMP - [7:2] */
#define WM8993_DRC_THRESH_COMP_WIDTH                 6  /* DRC_THRESH_COMP - [7:2] */

/*
 * R125 (0x7D) - DRC Control 3
 */
#define WM8993_DRC_AMP_COMP_MASK                0xF800  /* DRC_AMP_COMP - [15:11] */
#define WM8993_DRC_AMP_COMP_SHIFT                   11  /* DRC_AMP_COMP - [15:11] */
#define WM8993_DRC_AMP_COMP_WIDTH                    5  /* DRC_AMP_COMP - [15:11] */
#define WM8993_DRC_R0_SLOPE_COMP_MASK           0x0700  /* DRC_R0_SLOPE_COMP - [10:8] */
#define WM8993_DRC_R0_SLOPE_COMP_SHIFT               8  /* DRC_R0_SLOPE_COMP - [10:8] */
#define WM8993_DRC_R0_SLOPE_COMP_WIDTH               3  /* DRC_R0_SLOPE_COMP - [10:8] */
#define WM8993_DRC_FF_DELAY                     0x0080  /* DRC_FF_DELAY */
#define WM8993_DRC_FF_DELAY_MASK                0x0080  /* DRC_FF_DELAY */
#define WM8993_DRC_FF_DELAY_SHIFT                    7  /* DRC_FF_DELAY */
#define WM8993_DRC_FF_DELAY_WIDTH                    1  /* DRC_FF_DELAY */
#define WM8993_DRC_THRESH_QR_MASK               0x000C  /* DRC_THRESH_QR - [3:2] */
#define WM8993_DRC_THRESH_QR_SHIFT                   2  /* DRC_THRESH_QR - [3:2] */
#define WM8993_DRC_THRESH_QR_WIDTH                   2  /* DRC_THRESH_QR - [3:2] */
#define WM8993_DRC_RATE_QR_MASK                 0x0003  /* DRC_RATE_QR - [1:0] */
#define WM8993_DRC_RATE_QR_SHIFT                     0  /* DRC_RATE_QR - [1:0] */
#define WM8993_DRC_RATE_QR_WIDTH                     2  /* DRC_RATE_QR - [1:0] */

/*
 * R126 (0x7E) - DRC Control 4
 */
#define WM8993_DRC_R1_SLOPE_COMP_MASK           0xE000  /* DRC_R1_SLOPE_COMP - [15:13] */
#define WM8993_DRC_R1_SLOPE_COMP_SHIFT              13  /* DRC_R1_SLOPE_COMP - [15:13] */
#define WM8993_DRC_R1_SLOPE_COMP_WIDTH               3  /* DRC_R1_SLOPE_COMP - [15:13] */
#define WM8993_DRC_STARTUP_GAIN_MASK            0x1F00  /* DRC_STARTUP_GAIN - [12:8] */
#define WM8993_DRC_STARTUP_GAIN_SHIFT                8  /* DRC_STARTUP_GAIN - [12:8] */
#define WM8993_DRC_STARTUP_GAIN_WIDTH                5  /* DRC_STARTUP_GAIN - [12:8] */

#endif
