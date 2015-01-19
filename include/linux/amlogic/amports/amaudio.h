/*
 * AMLOGIC Audio port driver.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Kevin Wang <kevin.wang@amlogic.com>
 *
 */
#ifndef AMAUDIO_H
#define AMAUDIO_H

#include <linux/interrupt.h>

#define AMAUDIO_IOC_MAGIC  'A'

#define AMAUDIO_IOC_GET_I2S_OUT_SIZE		_IOW(AMAUDIO_IOC_MAGIC, 0x00, int)

#define AMAUDIO_IOC_GET_I2S_OUT_PTR			_IOW(AMAUDIO_IOC_MAGIC, 0x01, int)

#define AMAUDIO_IOC_SET_I2S_OUT_RD_PTR	    _IOW(AMAUDIO_IOC_MAGIC, 0x02, int)

#define AMAUDIO_IOC_GET_I2S_IN_SIZE			_IOW(AMAUDIO_IOC_MAGIC, 0x03, int)

#define AMAUDIO_IOC_GET_I2S_IN_PTR			_IOW(AMAUDIO_IOC_MAGIC, 0x04, int)

#define AMAUDIO_IOC_SET_I2S_IN_RD_PTR		_IOW(AMAUDIO_IOC_MAGIC, 0x05, int)

#define AMAUDIO_IOC_SET_I2S_IN_MODE         _IOW(AMAUDIO_IOC_MAGIC, 0x06, int)

#define AMAUDIO_IOC_SET_I2S_OUT_MODE        _IOW(AMAUDIO_IOC_MAGIC, 0x07, int)

#define AMAUDIO_IOC_GET_I2S_IN_RD_PTR       _IOW(AMAUDIO_IOC_MAGIC, 0x08, int)

#define AMAUDIO_IOC_GET_I2S_OUT_RD_PTR      _IOW(AMAUDIO_IOC_MAGIC, 0x09, int)

#define AMAUDIO_IOC_SET_I2S_IN_WR_PTR       _IOW(AMAUDIO_IOC_MAGIC, 0x0a, int)

#define AMAUDIO_IOC_SET_I2S_OUT_WR_PTR      _IOW(AMAUDIO_IOC_MAGIC, 0x0b, int)

#define AMAUDIO_IOC_GET_I2S_IN_WR_PTR       _IOW(AMAUDIO_IOC_MAGIC, 0x0c, int)

#define AMAUDIO_IOC_GET_I2S_OUT_WR_PTR      _IOW(AMAUDIO_IOC_MAGIC, 0x0d, int)

#define AMAUDIO_IOC_SET_LEFT_MONO		_IOW(AMAUDIO_IOC_MAGIC, 0x0e, int)

#define AMAUDIO_IOC_SET_RIGHT_MONO		_IOW(AMAUDIO_IOC_MAGIC, 0x0f, int)

#define AMAUDIO_IOC_SET_STEREO			_IOW(AMAUDIO_IOC_MAGIC, 0x10, int)

#define AMAUDIO_IOC_SET_CHANNEL_SWAP		_IOW(AMAUDIO_IOC_MAGIC, 0x11, int)

#define AMAUDIO_IOC_DIRECT_AUDIO			_IOW(AMAUDIO_IOC_MAGIC, 0x12, int)

#define AMAUDIO_IOC_DIRECT_LEFT_GAIN  _IOW(AMAUDIO_IOC_MAGIC, 0x13, int)

#define AMAUDIO_IOC_DIRECT_RIGHT_GAIN _IOW(AMAUDIO_IOC_MAGIC, 0x14, int)
#define AMAUDIO_IOC_START_LINE_IN           _IOW(AMAUDIO_IOC_MAGIC, 0x15, int)
#define AMAUDIO_IOC_START_HDMI_IN           _IOW(AMAUDIO_IOC_MAGIC, 0x16, int)
#define AMAUDIO_IOC_STOP_LINE_IN            _IOW(AMAUDIO_IOC_MAGIC, 0x17, int)
#define AMAUDIO_IOC_STOP_HDMI_IN            _IOW(AMAUDIO_IOC_MAGIC, 0x18, int)
#define AMAUDIO_IOC_SET_RESAMPLE_ENA        _IOW(AMAUDIO_IOC_MAGIC, 0x19, unsigned long)
#define AMAUDIO_IOC_GET_RESAMPLE_ENA        _IOR(AMAUDIO_IOC_MAGIC, 0x1a, unsigned long)
#define AMAUDIO_IOC_SET_RESAMPLE_TYPE       _IOW(AMAUDIO_IOC_MAGIC, 0x1b, unsigned long)
#define AMAUDIO_IOC_GET_RESAMPLE_TYPE       _IOR(AMAUDIO_IOC_MAGIC, 0x1c, unsigned long)
#define AMAUDIO_IOC_SET_RESAMPLE_DELTA      _IOW(AMAUDIO_IOC_MAGIC, 0x1d, unsigned long)
#define AMAUDIO_IOC_GET_RESAMPLE_DELTA      _IOW(AMAUDIO_IOC_MAGIC, 0x1e, unsigned long)



#define DIRECT_AUDIO_OFF	0
#define DIRECT_AUDIO_ON	    1

#endif

