/*
 * File:         sound/soc/codecs/ssm2602.h
 * Author:       Cliff Cai <Cliff.Cai@analog.com>
 *
 * Created:      Tue June 06 2008
 *
 * Modified:
 *               Copyright 2008 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _SSM2602_H
#define _SSM2602_H

/* SSM2602 Codec Register definitions */

#define SSM2602_LINVOL   0x00
#define SSM2602_RINVOL   0x01
#define SSM2602_LOUT1V   0x02
#define SSM2602_ROUT1V   0x03
#define SSM2602_APANA    0x04
#define SSM2602_APDIGI   0x05
#define SSM2602_PWR      0x06
#define SSM2602_IFACE    0x07
#define SSM2602_SRATE    0x08
#define SSM2602_ACTIVE   0x09
#define SSM2602_RESET	 0x0f

/*SSM2602 Codec Register Field definitions
 *(Mask value to extract the corresponding Register field)
 */

/*Left ADC Volume Control (SSM2602_REG_LEFT_ADC_VOL)*/
#define     LINVOL_LIN_VOL                0x01F   /* Left Channel PGA Volume control                      */
#define     LINVOL_LIN_ENABLE_MUTE        0x080   /* Left Channel Input Mute                              */
#define     LINVOL_LRIN_BOTH              0x100   /* Left Channel Line Input Volume update                */

/*Right ADC Volume Control (SSM2602_REG_RIGHT_ADC_VOL)*/
#define     RINVOL_RIN_VOL                0x01F   /* Right Channel PGA Volume control                     */
#define     RINVOL_RIN_ENABLE_MUTE        0x080   /* Right Channel Input Mute                             */
#define     RINVOL_RLIN_BOTH              0x100   /* Right Channel Line Input Volume update               */

/*Left DAC Volume Control (SSM2602_REG_LEFT_DAC_VOL)*/
#define     LOUT1V_LHP_VOL                0x07F   /* Left Channel Headphone volume control                */
#define     LOUT1V_ENABLE_LZC             0x080   /* Left Channel Zero cross detect enable                */
#define     LOUT1V_LRHP_BOTH              0x100   /* Left Channel Headphone volume update                 */

/*Right DAC Volume Control (SSM2602_REG_RIGHT_DAC_VOL)*/
#define     ROUT1V_RHP_VOL                0x07F   /* Right Channel Headphone volume control               */
#define     ROUT1V_ENABLE_RZC             0x080   /* Right Channel Zero cross detect enable               */
#define     ROUT1V_RLHP_BOTH              0x100   /* Right Channel Headphone volume update                */

/*Analogue Audio Path Control (SSM2602_REG_ANALOGUE_PATH)*/
#define     APANA_ENABLE_MIC_BOOST       0x001   /* Primary Microphone Amplifier gain booster control    */
#define     APANA_ENABLE_MIC_MUTE        0x002   /* Microphone Mute Control                              */
#define     APANA_ADC_IN_SELECT          0x004   /* Microphone/Line IN select to ADC (1=MIC, 0=Line In)  */
#define     APANA_ENABLE_BYPASS          0x008   /* Line input bypass to line output                     */
#define     APANA_SELECT_DAC             0x010   /* Select DAC (1=Select DAC, 0=Don't Select DAC)        */
#define     APANA_ENABLE_SIDETONE        0x020   /* Enable/Disable Side Tone                             */
#define     APANA_SIDETONE_ATTN          0x0C0   /* Side Tone Attenuation                                */
#define     APANA_ENABLE_MIC_BOOST2      0x100   /* Secondary Microphone Amplifier gain booster control  */

/*Digital Audio Path Control (SSM2602_REG_DIGITAL_PATH)*/
#define     APDIGI_ENABLE_ADC_HPF         0x001   /* Enable/Disable ADC Highpass Filter                   */
#define     APDIGI_DE_EMPHASIS            0x006   /* De-Emphasis Control                                  */
#define     APDIGI_ENABLE_DAC_MUTE        0x008   /* DAC Mute Control                                     */
#define     APDIGI_STORE_OFFSET           0x010   /* Store/Clear DC offset when HPF is disabled           */

/*Power Down Control (SSM2602_REG_POWER)
 *(1=Enable PowerDown, 0=Disable PowerDown)
 */
#define     PWR_LINE_IN_PDN            0x001   /* Line Input Power Down                                */
#define     PWR_MIC_PDN                0x002   /* Microphone Input & Bias Power Down                   */
#define     PWR_ADC_PDN                0x004   /* ADC Power Down                                       */
#define     PWR_DAC_PDN                0x008   /* DAC Power Down                                       */
#define     PWR_OUT_PDN                0x010   /* Outputs Power Down                                   */
#define     PWR_OSC_PDN                0x020   /* Oscillator Power Down                                */
#define     PWR_CLK_OUT_PDN            0x040   /* CLKOUT Power Down                                    */
#define     PWR_POWER_OFF              0x080   /* POWEROFF Mode                                        */

/*Digital Audio Interface Format (SSM2602_REG_DIGITAL_IFACE)*/
#define     IFACE_IFACE_FORMAT           0x003   /* Digital Audio input format control                   */
#define     IFACE_AUDIO_DATA_LEN         0x00C   /* Audio Data word length control                       */
#define     IFACE_DAC_LR_POLARITY        0x010   /* Polarity Control for clocks in RJ,LJ and I2S modes   */
#define     IFACE_DAC_LR_SWAP            0x020   /* Swap DAC data control                                */
#define     IFACE_ENABLE_MASTER          0x040   /* Enable/Disable Master Mode                           */
#define     IFACE_BCLK_INVERT            0x080   /* Bit Clock Inversion control                          */

/*Sampling Control (SSM2602_REG_SAMPLING_CTRL)*/
#define     SRATE_ENABLE_USB_MODE        0x001   /* Enable/Disable USB Mode                              */
#define     SRATE_BOS_RATE               0x002   /* Base Over-Sampling rate                              */
#define     SRATE_SAMPLE_RATE            0x03C   /* Clock setting condition (Sampling rate control)      */
#define     SRATE_CORECLK_DIV2           0x040   /* Core Clock divider select                            */
#define     SRATE_CLKOUT_DIV2            0x080   /* Clock Out divider select                             */

/*Active Control (SSM2602_REG_ACTIVE_CTRL)*/
#define     ACTIVE_ACTIVATE_CODEC         0x001   /* Activate Codec Digital Audio Interface               */

/*********************************************************************/

#define SSM2602_CACHEREGNUM 	10

#define SSM2602_SYSCLK	0
#define SSM2602_DAI		0

struct ssm2602_setup_data {
	int i2c_bus;
	unsigned short i2c_address;
};

extern struct snd_soc_dai ssm2602_dai;
extern struct snd_soc_codec_device soc_codec_dev_ssm2602;

#endif
