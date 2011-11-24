/* Copyright (C) 2007 Nanoradio AB */
/* $Id: cvt.c 9954 2008-09-15 09:41:38Z joda $ */

#include "nrx_lib.h"
#include "nrx_priv.h"

/* This is adapted from WiFiEngine we_chan.c */

#define F(N) ((N) * 1000)

#define F11b(C) { (C), F(2412 + ((C) - 1) * 5) }
#define F11a(C) { (C), F(5000 + (C) * 5) }
#define F11j(C) { (C), F(4000 + (C) * 5) }

static struct ftable { 
   uint16_t channel; 
   uint32_t khz;
} ftable[] = {
   /* 802.11b/g */
   F11b(1),
   F11b(2),
   F11b(3),
   F11b(4),
   F11b(5),
   F11b(6),
   F11b(7),
   F11b(8),
   F11b(9),
   F11b(10),
   F11b(11),
   F11b(12),
   F11b(13),
   { 14, F(2484) },

   /* 802.11a/h */
   F11a(36),
   F11a(40),
   F11a(44),
   F11a(48),
   F11a(52),
   F11a(56),
   F11a(60),
   F11a(64),

   /* 802.11h */
   F11a(100),
   F11a(104),
   F11a(108),
   F11a(112),
   F11a(116),
   F11a(120),
   F11a(124),
   F11a(128),
   F11a(132),
   F11a(136),
   F11a(140),

   /* 802.11a */
   F11a(149),
   F11a(153),
   F11a(157),
   F11a(161),

   /* 802.11j */
   F11j(184),
   F11j(188),
   F11j(192),
   F11j(196)
};


/*! 
 * @internal
 * @brief Convert a channel number to a frequency
 *
 * @param [in]  channel    the channel number
 * @param [out] frequency  returned frequency in kHz
 *
 * @retval 0 on success
 * @retval EINVAL if channel could not be converted
 */
int
nrx_convert_channel_to_frequency(nrx_context ctx, 
                                 nrx_channel_t channel, 
                                 uint32_t *frequency)
{
   int i;

   for(i = 0; i < NRX_ARRAY_SIZE(ftable); i++) {
      if(channel == ftable[i].channel) {
         *frequency = ftable[i].khz;
         return 0;
      }
   }
   
   return EINVAL;
}

/*! 
 * @internal
 * @brief Convert a frequency to a channel number
 *
 * @param [in] frequency  returned frequency in kHz
 * @param [out]  channel    the channel number
 *
 * @retval 0 on success
 * @retval EINVAL if channel could not be converted
 */
int
nrx_convert_frequency_to_channel(nrx_context ctx,
                                 uint32_t frequency, 
                                 nrx_channel_t *channel)
{
   int i;
   
   for(i = 0; i < NRX_ARRAY_SIZE(ftable); i++) {
      if(frequency == ftable[i].khz) {
         *channel = ftable[i].channel;
         return 0;
      }
   }
   return EINVAL;
}
