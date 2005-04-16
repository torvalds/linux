#ifndef _TAS_IOCTL_H_
#define _TAS_IOCTL_H_

#include <linux/i2c.h>
#include <linux/soundcard.h>


#define TAS_READ_EQ              _SIOR('t',0,struct tas_biquad_ctrl_t)
#define TAS_WRITE_EQ             _SIOW('t',0,struct tas_biquad_ctrl_t)

#define TAS_READ_EQ_LIST         _SIOR('t',1,struct tas_biquad_ctrl_t)
#define TAS_WRITE_EQ_LIST        _SIOW('t',1,struct tas_biquad_ctrl_t)

#define TAS_READ_EQ_FILTER_COUNT  _SIOR('t',2,int)
#define TAS_READ_EQ_CHANNEL_COUNT _SIOR('t',3,int)

#define TAS_READ_DRCE            _SIOR('t',4,struct tas_drce_ctrl_t)
#define TAS_WRITE_DRCE           _SIOW('t',4,struct tas_drce_ctrl_t)

#define TAS_READ_DRCE_CAPS       _SIOR('t',5,int)
#define TAS_READ_DRCE_MIN        _SIOR('t',6,int)
#define TAS_READ_DRCE_MAX        _SIOR('t',7,int)

#endif
