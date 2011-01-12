/*************************************************/
/*                                               */
/*       Font file modified from 		 */
/*  http://blog.chinaunix.net/u/13265/showart.php?id=1008020       */
/*  microcaicai@gmail modifiy it to use in-kernel*/
/*  font solution			         */
/*                                               */
/*************************************************/

#include <linux/font.h>

static const unsigned char fontdata_16x16[] = {
	#include "font_cjk.h"
};

const struct font_desc font_16x16_cjk = {
	.idx	= FONT_16x16_CJK_IDX,
	.name	= "VGA_CJK",
	.width	= 8, // have to do this to make curser appear 8dots length
	.height	= 16,
	.data	= fontdata_16x16,
	.pref	= 10, // make it big enough to be selected
	.charcount = 65535,
};
