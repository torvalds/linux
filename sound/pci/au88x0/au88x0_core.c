/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
    Vortex core low level functions.
	
 Author: Manuel Jander (mjander@users.sourceforge.cl)
 These functions are mainly the result of translations made
 from the original disassembly of the au88x0 binary drivers,
 written by Aureal before they went down.
 Many thanks to the Jeff Muizelaar, Kester Maddock, and whoever
 contributed to the OpenVortex project.
 The author of this file, put the few available pieces together
 and translated the rest of the riddle (Mix, Src and connection stuff).
 Some things are still to be discovered, and their meanings are unclear.

 Some of these functions aren't intended to be really used, rather
 to help to understand how does the AU88X0 chips work. Keep them in, because
 they could be used somewhere in the future.

 This code hasn't been tested or proof read thoroughly. If you wanna help,
 take a look at the AU88X0 assembly and check if this matches.
 Functions tested ok so far are (they show the desired effect
 at least):
   vortex_routes(); (1 bug fixed).
   vortex_adb_addroute();
   vortex_adb_addroutes();
   vortex_connect_codecplay();
   vortex_src_flushbuffers();
   vortex_adbdma_setmode();  note: still some unknown arguments!
   vortex_adbdma_startfifo();
   vortex_adbdma_stopfifo();
   vortex_fifo_setadbctrl(); note: still some unknown arguments!
   vortex_mix_setinputvolumebyte();
   vortex_mix_enableinput();
   vortex_mixer_addWTD(); (fixed)
   vortex_connection_adbdma_src_src();
   vortex_connection_adbdma_src();
   vortex_src_change_convratio();
   vortex_src_addWTD(); (fixed)

 History:

 01-03-2003 First revision.
 01-21-2003 Some bug fixes.
 17-02-2003 many bugfixes after a big versioning mess.
 18-02-2003 JAAAAAHHHUUUUUU!!!! The mixer works !! I'm just so happy !
			 (2 hours later...) I cant believe it! Im really lucky today.
			 Now the SRC is working too! Yeah! XMMS works !
 20-02-2003 First steps into the ALSA world.
 28-02-2003 As my birthday present, i discovered how the DMA buffer pages really
            work :-). It was all wrong.
 12-03-2003 ALSA driver starts working (2 channels).
 16-03-2003 More srcblock_setupchannel discoveries.
 12-04-2003 AU8830 playback support. Recording in the works.
 17-04-2003 vortex_route() and vortex_routes() bug fixes. AU8830 recording
 			works now, but chipn' dale effect is still there.
 16-05-2003 SrcSetupChannel cleanup. Moved the Src setup stuff entirely
            into au88x0_pcm.c .
 06-06-2003 Buffer shifter bugfix. Mixer volume fix.
 07-12-2003 A3D routing finally fixed. Believed to be OK.
 25-03-2004 Many thanks to Claudia, for such valuable bug reports.
 
*/

#include "au88x0.h"
#include "au88x0_a3d.h"
#include <linux/delay.h>

/*  MIXER (CAsp4Mix.s and CAsp4Mixer.s) */

// FIXME: get rid of this.
static int mchannels[NR_MIXIN];
static int rampchs[NR_MIXIN];

static void vortex_mixer_en_sr(vortex_t * vortex, int channel)
{
	hwwrite(vortex->mmio, VORTEX_MIXER_SR,
		hwread(vortex->mmio, VORTEX_MIXER_SR) | (0x1 << channel));
}
static void vortex_mixer_dis_sr(vortex_t * vortex, int channel)
{
	hwwrite(vortex->mmio, VORTEX_MIXER_SR,
		hwread(vortex->mmio, VORTEX_MIXER_SR) & ~(0x1 << channel));
}

#if 0
static void
vortex_mix_muteinputgain(vortex_t * vortex, unsigned char mix,
			 unsigned char channel)
{
	hwwrite(vortex->mmio, VORTEX_MIX_INVOL_A + ((mix << 5) + channel),
		0x80);
	hwwrite(vortex->mmio, VORTEX_MIX_INVOL_B + ((mix << 5) + channel),
		0x80);
}

static int vortex_mix_getvolume(vortex_t * vortex, unsigned char mix)
{
	int a;
	a = hwread(vortex->mmio, VORTEX_MIX_VOL_A + (mix << 2)) & 0xff;
	//FP2LinearFrac(a);
	return (a);
}

static int
vortex_mix_getinputvolume(vortex_t * vortex, unsigned char mix,
			  int channel, int *vol)
{
	int a;
	if (!(mchannels[mix] & (1 << channel)))
		return 0;
	a = hwread(vortex->mmio,
		   VORTEX_MIX_INVOL_A + (((mix << 5) + channel) << 2));
	/*
	   if (rampchs[mix] == 0)
	   a = FP2LinearFrac(a);
	   else
	   a = FP2LinearFracWT(a);
	 */
	*vol = a;
	return (0);
}

static unsigned int vortex_mix_boost6db(unsigned char vol)
{
	return (vol + 8);	/* WOW! what a complex function! */
}

static void vortex_mix_rampvolume(vortex_t * vortex, int mix)
{
	int ch;
	char a;
	// This function is intended for ramping down only (see vortex_disableinput()).
	for (ch = 0; ch < 0x20; ch++) {
		if (((1 << ch) & rampchs[mix]) == 0)
			continue;
		a = hwread(vortex->mmio,
			   VORTEX_MIX_INVOL_B + (((mix << 5) + ch) << 2));
		if (a > -126) {
			a -= 2;
			hwwrite(vortex->mmio,
				VORTEX_MIX_INVOL_A +
				(((mix << 5) + ch) << 2), a);
			hwwrite(vortex->mmio,
				VORTEX_MIX_INVOL_B +
				(((mix << 5) + ch) << 2), a);
		} else
			vortex_mix_killinput(vortex, mix, ch);
	}
}

static int
vortex_mix_getenablebit(vortex_t * vortex, unsigned char mix, int mixin)
{
	int addr, temp;
	if (mixin >= 0)
		addr = mixin;
	else
		addr = mixin + 3;
	addr = ((mix << 3) + (addr >> 2)) << 2;
	temp = hwread(vortex->mmio, VORTEX_MIX_ENIN + addr);
	return ((temp >> (mixin & 3)) & 1);
}
#endif
static void
vortex_mix_setvolumebyte(vortex_t * vortex, unsigned char mix,
			 unsigned char vol)
{
	int temp;
	hwwrite(vortex->mmio, VORTEX_MIX_VOL_A + (mix << 2), vol);
	if (1) {		/*if (this_10) */
		temp = hwread(vortex->mmio, VORTEX_MIX_VOL_B + (mix << 2));
		if ((temp != 0x80) || (vol == 0x80))
			return;
	}
	hwwrite(vortex->mmio, VORTEX_MIX_VOL_B + (mix << 2), vol);
}

static void
vortex_mix_setinputvolumebyte(vortex_t * vortex, unsigned char mix,
			      int mixin, unsigned char vol)
{
	int temp;

	hwwrite(vortex->mmio,
		VORTEX_MIX_INVOL_A + (((mix << 5) + mixin) << 2), vol);
	if (1) {		/* this_10, initialized to 1. */
		temp =
		    hwread(vortex->mmio,
			   VORTEX_MIX_INVOL_B + (((mix << 5) + mixin) << 2));
		if ((temp != 0x80) || (vol == 0x80))
			return;
	}
	hwwrite(vortex->mmio,
		VORTEX_MIX_INVOL_B + (((mix << 5) + mixin) << 2), vol);
}

static void
vortex_mix_setenablebit(vortex_t * vortex, unsigned char mix, int mixin, int en)
{
	int temp, addr;

	if (mixin < 0)
		addr = (mixin + 3);
	else
		addr = mixin;
	addr = ((mix << 3) + (addr >> 2)) << 2;
	temp = hwread(vortex->mmio, VORTEX_MIX_ENIN + addr);
	if (en)
		temp |= (1 << (mixin & 3));
	else
		temp &= ~(1 << (mixin & 3));
	/* Mute input. Astatic void crackling? */
	hwwrite(vortex->mmio,
		VORTEX_MIX_INVOL_B + (((mix << 5) + mixin) << 2), 0x80);
	/* Looks like clear buffer. */
	hwwrite(vortex->mmio, VORTEX_MIX_SMP + (mixin << 2), 0x0);
	hwwrite(vortex->mmio, VORTEX_MIX_SMP + 4 + (mixin << 2), 0x0);
	/* Write enable bit. */
	hwwrite(vortex->mmio, VORTEX_MIX_ENIN + addr, temp);
}

static void
vortex_mix_killinput(vortex_t * vortex, unsigned char mix, int mixin)
{
	rampchs[mix] &= ~(1 << mixin);
	vortex_mix_setinputvolumebyte(vortex, mix, mixin, 0x80);
	mchannels[mix] &= ~(1 << mixin);
	vortex_mix_setenablebit(vortex, mix, mixin, 0);
}

static void
vortex_mix_enableinput(vortex_t * vortex, unsigned char mix, int mixin)
{
	vortex_mix_killinput(vortex, mix, mixin);
	if ((mchannels[mix] & (1 << mixin)) == 0) {
		vortex_mix_setinputvolumebyte(vortex, mix, mixin, 0x80);	/*0x80 : mute */
		mchannels[mix] |= (1 << mixin);
	}
	vortex_mix_setenablebit(vortex, mix, mixin, 1);
}

static void
vortex_mix_disableinput(vortex_t * vortex, unsigned char mix, int channel,
			int ramp)
{
	if (ramp) {
		rampchs[mix] |= (1 << channel);
		// Register callback.
		//vortex_mix_startrampvolume(vortex);
		vortex_mix_killinput(vortex, mix, channel);
	} else
		vortex_mix_killinput(vortex, mix, channel);
}

static int
vortex_mixer_addWTD(vortex_t * vortex, unsigned char mix, unsigned char ch)
{
	int temp, lifeboat = 0, prev;

	temp = hwread(vortex->mmio, VORTEX_MIXER_SR);
	if ((temp & (1 << ch)) == 0) {
		hwwrite(vortex->mmio, VORTEX_MIXER_CHNBASE + (ch << 2), mix);
		vortex_mixer_en_sr(vortex, ch);
		return 1;
	}
	prev = VORTEX_MIXER_CHNBASE + (ch << 2);
	temp = hwread(vortex->mmio, prev);
	while (temp & 0x10) {
		prev = VORTEX_MIXER_RTBASE + ((temp & 0xf) << 2);
		temp = hwread(vortex->mmio, prev);
		//printk(KERN_INFO "vortex: mixAddWTD: while addr=%x, val=%x\n", prev, temp);
		if ((++lifeboat) > 0xf) {
			printk(KERN_ERR
			       "vortex_mixer_addWTD: lifeboat overflow\n");
			return 0;
		}
	}
	hwwrite(vortex->mmio, VORTEX_MIXER_RTBASE + ((temp & 0xf) << 2), mix);
	hwwrite(vortex->mmio, prev, (temp & 0xf) | 0x10);
	return 1;
}

static int
vortex_mixer_delWTD(vortex_t * vortex, unsigned char mix, unsigned char ch)
{
	int esp14 = -1, esp18, eax, ebx, edx, ebp, esi = 0;
	//int esp1f=edi(while)=src, esp10=ch;

	eax = hwread(vortex->mmio, VORTEX_MIXER_SR);
	if (((1 << ch) & eax) == 0) {
		printk(KERN_ERR "mix ALARM %x\n", eax);
		return 0;
	}
	ebp = VORTEX_MIXER_CHNBASE + (ch << 2);
	esp18 = hwread(vortex->mmio, ebp);
	if (esp18 & 0x10) {
		ebx = (esp18 & 0xf);
		if (mix == ebx) {
			ebx = VORTEX_MIXER_RTBASE + (mix << 2);
			edx = hwread(vortex->mmio, ebx);
			//7b60
			hwwrite(vortex->mmio, ebp, edx);
			hwwrite(vortex->mmio, ebx, 0);
		} else {
			//7ad3
			edx =
			    hwread(vortex->mmio,
				   VORTEX_MIXER_RTBASE + (ebx << 2));
			//printk(KERN_INFO "vortex: mixdelWTD: 1 addr=%x, val=%x, src=%x\n", ebx, edx, src);
			while ((edx & 0xf) != mix) {
				if ((esi) > 0xf) {
					printk(KERN_ERR
					       "vortex: mixdelWTD: error lifeboat overflow\n");
					return 0;
				}
				esp14 = ebx;
				ebx = edx & 0xf;
				ebp = ebx << 2;
				edx =
				    hwread(vortex->mmio,
					   VORTEX_MIXER_RTBASE + ebp);
				//printk(KERN_INFO "vortex: mixdelWTD: while addr=%x, val=%x\n", ebp, edx);
				esi++;
			}
			//7b30
			ebp = ebx << 2;
			if (edx & 0x10) {	/* Delete entry in between others */
				ebx = VORTEX_MIXER_RTBASE + ((edx & 0xf) << 2);
				edx = hwread(vortex->mmio, ebx);
				//7b60
				hwwrite(vortex->mmio,
					VORTEX_MIXER_RTBASE + ebp, edx);
				hwwrite(vortex->mmio, ebx, 0);
				//printk(KERN_INFO "vortex mixdelWTD between addr= 0x%x, val= 0x%x\n", ebp, edx);
			} else {	/* Delete last entry */
				//7b83
				if (esp14 == -1)
					hwwrite(vortex->mmio,
						VORTEX_MIXER_CHNBASE +
						(ch << 2), esp18 & 0xef);
				else {
					ebx = (0xffffffe0 & edx) | (0xf & ebx);
					hwwrite(vortex->mmio,
						VORTEX_MIXER_RTBASE +
						(esp14 << 2), ebx);
					//printk(KERN_INFO "vortex mixdelWTD last addr= 0x%x, val= 0x%x\n", esp14, ebx);
				}
				hwwrite(vortex->mmio,
					VORTEX_MIXER_RTBASE + ebp, 0);
				return 1;
			}
		}
	} else {
		//printk(KERN_INFO "removed last mix\n");
		//7be0
		vortex_mixer_dis_sr(vortex, ch);
		hwwrite(vortex->mmio, ebp, 0);
	}
	return 1;
}

static void vortex_mixer_init(vortex_t * vortex)
{
	u32 addr;
	int x;

	// FIXME: get rid of this crap.
	memset(mchannels, 0, NR_MIXOUT * sizeof(int));
	memset(rampchs, 0, NR_MIXOUT * sizeof(int));

	addr = VORTEX_MIX_SMP + 0x17c;
	for (x = 0x5f; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, 0);
		addr -= 4;
	}
	addr = VORTEX_MIX_ENIN + 0x1fc;
	for (x = 0x7f; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, 0);
		addr -= 4;
	}
	addr = VORTEX_MIX_SMP + 0x17c;
	for (x = 0x5f; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, 0);
		addr -= 4;
	}
	addr = VORTEX_MIX_INVOL_A + 0x7fc;
	for (x = 0x1ff; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, 0x80);
		addr -= 4;
	}
	addr = VORTEX_MIX_VOL_A + 0x3c;
	for (x = 0xf; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, 0x80);
		addr -= 4;
	}
	addr = VORTEX_MIX_INVOL_B + 0x7fc;
	for (x = 0x1ff; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, 0x80);
		addr -= 4;
	}
	addr = VORTEX_MIX_VOL_B + 0x3c;
	for (x = 0xf; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, 0x80);
		addr -= 4;
	}
	addr = VORTEX_MIXER_RTBASE + (MIXER_RTBASE_SIZE - 1) * 4;
	for (x = (MIXER_RTBASE_SIZE - 1); x >= 0; x--) {
		hwwrite(vortex->mmio, addr, 0x0);
		addr -= 4;
	}
	hwwrite(vortex->mmio, VORTEX_MIXER_SR, 0);

	/* Set clipping ceiling (this may be all wrong). */
	/*
	for (x = 0; x < 0x80; x++) {
		hwwrite(vortex->mmio, VORTEX_MIXER_CLIP + (x << 2), 0x3ffff);
	}
	*/
	/*
	   call CAsp4Mix__Initialize_CAsp4HwIO____CAsp4Mixer____
	   Register ISR callback for volume smooth fade out.
	   Maybe this avoids clicks when press "stop" ?
	 */
}

/*  SRC (CAsp4Src.s and CAsp4SrcBlock) */

static void vortex_src_en_sr(vortex_t * vortex, int channel)
{
	hwwrite(vortex->mmio, VORTEX_SRCBLOCK_SR,
		hwread(vortex->mmio, VORTEX_SRCBLOCK_SR) | (0x1 << channel));
}

static void vortex_src_dis_sr(vortex_t * vortex, int channel)
{
	hwwrite(vortex->mmio, VORTEX_SRCBLOCK_SR,
		hwread(vortex->mmio, VORTEX_SRCBLOCK_SR) & ~(0x1 << channel));
}

static void vortex_src_flushbuffers(vortex_t * vortex, unsigned char src)
{
	int i;

	for (i = 0x1f; i >= 0; i--)
		hwwrite(vortex->mmio,
			VORTEX_SRC_DATA0 + (src << 7) + (i << 2), 0);
	hwwrite(vortex->mmio, VORTEX_SRC_DATA + (src << 3), 0);
	hwwrite(vortex->mmio, VORTEX_SRC_DATA + (src << 3) + 4, 0);
}

static void vortex_src_cleardrift(vortex_t * vortex, unsigned char src)
{
	hwwrite(vortex->mmio, VORTEX_SRC_DRIFT0 + (src << 2), 0);
	hwwrite(vortex->mmio, VORTEX_SRC_DRIFT1 + (src << 2), 0);
	hwwrite(vortex->mmio, VORTEX_SRC_DRIFT2 + (src << 2), 1);
}

static void
vortex_src_set_throttlesource(vortex_t * vortex, unsigned char src, int en)
{
	int temp;

	temp = hwread(vortex->mmio, VORTEX_SRC_SOURCE);
	if (en)
		temp |= 1 << src;
	else
		temp &= ~(1 << src);
	hwwrite(vortex->mmio, VORTEX_SRC_SOURCE, temp);
}

static int
vortex_src_persist_convratio(vortex_t * vortex, unsigned char src, int ratio)
{
	int temp, lifeboat = 0;

	do {
		hwwrite(vortex->mmio, VORTEX_SRC_CONVRATIO + (src << 2), ratio);
		temp = hwread(vortex->mmio, VORTEX_SRC_CONVRATIO + (src << 2));
		if ((++lifeboat) > 0x9) {
			printk(KERN_ERR "Vortex: Src cvr fail\n");
			break;
		}
	}
	while (temp != ratio);
	return temp;
}

#if 0
static void vortex_src_slowlock(vortex_t * vortex, unsigned char src)
{
	int temp;

	hwwrite(vortex->mmio, VORTEX_SRC_DRIFT2 + (src << 2), 1);
	hwwrite(vortex->mmio, VORTEX_SRC_DRIFT0 + (src << 2), 0);
	temp = hwread(vortex->mmio, VORTEX_SRC_U0 + (src << 2));
	if (temp & 0x200)
		hwwrite(vortex->mmio, VORTEX_SRC_U0 + (src << 2),
			temp & ~0x200L);
}

static void
vortex_src_change_convratio(vortex_t * vortex, unsigned char src, int ratio)
{
	int temp, a;

	if ((ratio & 0x10000) && (ratio != 0x10000)) {
		if (ratio & 0x3fff)
			a = (0x11 - ((ratio >> 0xe) & 0x3)) - 1;
		else
			a = (0x11 - ((ratio >> 0xe) & 0x3)) - 2;
	} else
		a = 0xc;
	temp = hwread(vortex->mmio, VORTEX_SRC_U0 + (src << 2));
	if (((temp >> 4) & 0xf) != a)
		hwwrite(vortex->mmio, VORTEX_SRC_U0 + (src << 2),
			(temp & 0xf) | ((a & 0xf) << 4));

	vortex_src_persist_convratio(vortex, src, ratio);
}

static int
vortex_src_checkratio(vortex_t * vortex, unsigned char src,
		      unsigned int desired_ratio)
{
	int hw_ratio, lifeboat = 0;

	hw_ratio = hwread(vortex->mmio, VORTEX_SRC_CONVRATIO + (src << 2));

	while (hw_ratio != desired_ratio) {
		hwwrite(vortex->mmio, VORTEX_SRC_CONVRATIO + (src << 2), desired_ratio);

		if ((lifeboat++) > 15) {
			printk(KERN_ERR "Vortex: could not set src-%d from %d to %d\n",
			       src, hw_ratio, desired_ratio);
			break;
		}
	}

	return hw_ratio;
}

#endif
/*
 Objective: Set samplerate for given SRC module.
 Arguments:
	card:	pointer to vortex_t strcut.
	src:	Integer index of the SRC module.
	cr:		Current sample rate conversion factor.
	b:		unknown 16 bit value.
	sweep:	Enable Samplerate fade from cr toward tr flag.
	dirplay: 1: playback, 0: recording.
	sl:		Slow Lock flag.
	tr:		Target samplerate conversion.
	thsource: Throttle source flag (no idea what that means).
*/
static void vortex_src_setupchannel(vortex_t * card, unsigned char src,
			unsigned int cr, unsigned int b, int sweep, int d,
			int dirplay, int sl, unsigned int tr, int thsource)
{
	// noplayback: d=2,4,7,0xa,0xb when using first 2 src's.
	// c: enables pitch sweep.
	// looks like g is c related. Maybe g is a sweep parameter ?
	// g = cvr
	// dirplay: 0 = recording, 1 = playback
	// d = src hw index.

	int esi, ebp = 0, esp10;

	vortex_src_flushbuffers(card, src);

	if (sweep) {
		if ((tr & 0x10000) && (tr != 0x10000)) {
			tr = 0;
			esi = 0x7;
		} else {
			if ((((short)tr) < 0) && (tr != 0x8000)) {
				tr = 0;
				esi = 0x8;
			} else {
				tr = 1;
				esi = 0xc;
			}
		}
	} else {
		if ((cr & 0x10000) && (cr != 0x10000)) {
			tr = 0;	/*ebx = 0 */
			esi = 0x11 - ((cr >> 0xe) & 7);
			if (cr & 0x3fff)
				esi -= 1;
			else
				esi -= 2;
		} else {
			tr = 1;
			esi = 0xc;
		}
	}
	vortex_src_cleardrift(card, src);
	vortex_src_set_throttlesource(card, src, thsource);

	if ((dirplay == 0) && (sweep == 0)) {
		if (tr)
			esp10 = 0xf;
		else
			esp10 = 0xc;
		ebp = 0;
	} else {
		if (tr)
			ebp = 0xf;
		else
			ebp = 0xc;
		esp10 = 0;
	}
	hwwrite(card->mmio, VORTEX_SRC_U0 + (src << 2),
		(sl << 0x9) | (sweep << 0x8) | ((esi & 0xf) << 4) | d);
	/* 0xc0   esi=0xc c=f=0 d=0 */
	vortex_src_persist_convratio(card, src, cr);
	hwwrite(card->mmio, VORTEX_SRC_U1 + (src << 2), b & 0xffff);
	/* 0   b=0 */
	hwwrite(card->mmio, VORTEX_SRC_U2 + (src << 2),
		(tr << 0x11) | (dirplay << 0x10) | (ebp << 0x8) | esp10);
	/* 0x30f00 e=g=1 esp10=0 ebp=f */
	//printk(KERN_INFO "vortex: SRC %d, d=0x%x, esi=0x%x, esp10=0x%x, ebp=0x%x\n", src, d, esi, esp10, ebp);
}

static void vortex_srcblock_init(vortex_t * vortex)
{
	u32 addr;
	int x;
	hwwrite(vortex->mmio, VORTEX_SRC_SOURCESIZE, 0x1ff);
	/*
	   for (x=0; x<0x10; x++) {
	   vortex_src_init(&vortex_src[x], x);
	   }
	 */
	//addr = 0xcc3c;
	//addr = 0x26c3c;
	addr = VORTEX_SRC_RTBASE + 0x3c;
	for (x = 0xf; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, 0);
		addr -= 4;
	}
	//addr = 0xcc94;
	//addr = 0x26c94;
	addr = VORTEX_SRC_CHNBASE + 0x54;
	for (x = 0x15; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, 0);
		addr -= 4;
	}
}

static int
vortex_src_addWTD(vortex_t * vortex, unsigned char src, unsigned char ch)
{
	int temp, lifeboat = 0, prev;
	// esp13 = src

	temp = hwread(vortex->mmio, VORTEX_SRCBLOCK_SR);
	if ((temp & (1 << ch)) == 0) {
		hwwrite(vortex->mmio, VORTEX_SRC_CHNBASE + (ch << 2), src);
		vortex_src_en_sr(vortex, ch);
		return 1;
	}
	prev = VORTEX_SRC_CHNBASE + (ch << 2);	/*ebp */
	temp = hwread(vortex->mmio, prev);
	//while (temp & NR_SRC) {
	while (temp & 0x10) {
		prev = VORTEX_SRC_RTBASE + ((temp & 0xf) << 2);	/*esp12 */
		//prev = VORTEX_SRC_RTBASE + ((temp & (NR_SRC-1)) << 2); /*esp12*/
		temp = hwread(vortex->mmio, prev);
		//printk(KERN_INFO "vortex: srcAddWTD: while addr=%x, val=%x\n", prev, temp);
		if ((++lifeboat) > 0xf) {
			printk(KERN_ERR
			       "vortex_src_addWTD: lifeboat overflow\n");
			return 0;
		}
	}
	hwwrite(vortex->mmio, VORTEX_SRC_RTBASE + ((temp & 0xf) << 2), src);
	//hwwrite(vortex->mmio, prev, (temp & (NR_SRC-1)) | NR_SRC);
	hwwrite(vortex->mmio, prev, (temp & 0xf) | 0x10);
	return 1;
}

static int
vortex_src_delWTD(vortex_t * vortex, unsigned char src, unsigned char ch)
{
	int esp14 = -1, esp18, eax, ebx, edx, ebp, esi = 0;
	//int esp1f=edi(while)=src, esp10=ch;

	eax = hwread(vortex->mmio, VORTEX_SRCBLOCK_SR);
	if (((1 << ch) & eax) == 0) {
		printk(KERN_ERR "src alarm\n");
		return 0;
	}
	ebp = VORTEX_SRC_CHNBASE + (ch << 2);
	esp18 = hwread(vortex->mmio, ebp);
	if (esp18 & 0x10) {
		ebx = (esp18 & 0xf);
		if (src == ebx) {
			ebx = VORTEX_SRC_RTBASE + (src << 2);
			edx = hwread(vortex->mmio, ebx);
			//7b60
			hwwrite(vortex->mmio, ebp, edx);
			hwwrite(vortex->mmio, ebx, 0);
		} else {
			//7ad3
			edx =
			    hwread(vortex->mmio,
				   VORTEX_SRC_RTBASE + (ebx << 2));
			//printk(KERN_INFO "vortex: srcdelWTD: 1 addr=%x, val=%x, src=%x\n", ebx, edx, src);
			while ((edx & 0xf) != src) {
				if ((esi) > 0xf) {
					printk
					    ("vortex: srcdelWTD: error, lifeboat overflow\n");
					return 0;
				}
				esp14 = ebx;
				ebx = edx & 0xf;
				ebp = ebx << 2;
				edx =
				    hwread(vortex->mmio,
					   VORTEX_SRC_RTBASE + ebp);
				//printk(KERN_INFO "vortex: srcdelWTD: while addr=%x, val=%x\n", ebp, edx);
				esi++;
			}
			//7b30
			ebp = ebx << 2;
			if (edx & 0x10) {	/* Delete entry in between others */
				ebx = VORTEX_SRC_RTBASE + ((edx & 0xf) << 2);
				edx = hwread(vortex->mmio, ebx);
				//7b60
				hwwrite(vortex->mmio,
					VORTEX_SRC_RTBASE + ebp, edx);
				hwwrite(vortex->mmio, ebx, 0);
				//printk(KERN_INFO "vortex srcdelWTD between addr= 0x%x, val= 0x%x\n", ebp, edx);
			} else {	/* Delete last entry */
				//7b83
				if (esp14 == -1)
					hwwrite(vortex->mmio,
						VORTEX_SRC_CHNBASE +
						(ch << 2), esp18 & 0xef);
				else {
					ebx = (0xffffffe0 & edx) | (0xf & ebx);
					hwwrite(vortex->mmio,
						VORTEX_SRC_RTBASE +
						(esp14 << 2), ebx);
					//printk(KERN_INFO"vortex srcdelWTD last addr= 0x%x, val= 0x%x\n", esp14, ebx);
				}
				hwwrite(vortex->mmio,
					VORTEX_SRC_RTBASE + ebp, 0);
				return 1;
			}
		}
	} else {
		//7be0
		vortex_src_dis_sr(vortex, ch);
		hwwrite(vortex->mmio, ebp, 0);
	}
	return 1;
}

 /*FIFO*/ 

static void
vortex_fifo_clearadbdata(vortex_t * vortex, int fifo, int x)
{
	for (x--; x >= 0; x--)
		hwwrite(vortex->mmio,
			VORTEX_FIFO_ADBDATA +
			(((fifo << FIFO_SIZE_BITS) + x) << 2), 0);
}

#if 0
static void vortex_fifo_adbinitialize(vortex_t * vortex, int fifo, int j)
{
	vortex_fifo_clearadbdata(vortex, fifo, FIFO_SIZE);
#ifdef CHIP_AU8820
	hwwrite(vortex->mmio, VORTEX_FIFO_ADBCTRL + (fifo << 2),
		(FIFO_U1 | ((j & FIFO_MASK) << 0xb)));
#else
	hwwrite(vortex->mmio, VORTEX_FIFO_ADBCTRL + (fifo << 2),
		(FIFO_U1 | ((j & FIFO_MASK) << 0xc)));
#endif
}
#endif
static void vortex_fifo_setadbvalid(vortex_t * vortex, int fifo, int en)
{
	hwwrite(vortex->mmio, VORTEX_FIFO_ADBCTRL + (fifo << 2),
		(hwread(vortex->mmio, VORTEX_FIFO_ADBCTRL + (fifo << 2)) &
		 0xffffffef) | ((1 & en) << 4) | FIFO_U1);
}

static void
vortex_fifo_setadbctrl(vortex_t * vortex, int fifo, int stereo, int priority,
		       int empty, int valid, int f)
{
	int temp, lifeboat = 0;
	//int this_8[NR_ADB] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; /* position */
	int this_4 = 0x2;
	/* f seems priority related.
	 * CAsp4AdbDma::SetPriority is the only place that calls SetAdbCtrl with f set to 1
	 * every where else it is set to 0. It seems, however, that CAsp4AdbDma::SetPriority
	 * is never called, thus the f related bits remain a mystery for now.
	 */
	do {
		temp = hwread(vortex->mmio, VORTEX_FIFO_ADBCTRL + (fifo << 2));
		if (lifeboat++ > 0xbb8) {
			printk(KERN_ERR
			       "Vortex: vortex_fifo_setadbctrl fail\n");
			break;
		}
	}
	while (temp & FIFO_RDONLY);

	// AU8830 semes to take some special care about fifo content (data).
	// But i'm just to lazy to translate that :)
	if (valid) {
		if ((temp & FIFO_VALID) == 0) {
			//this_8[fifo] = 0;
			vortex_fifo_clearadbdata(vortex, fifo, FIFO_SIZE);	// this_4
#ifdef CHIP_AU8820
			temp = (this_4 & 0x1f) << 0xb;
#else
			temp = (this_4 & 0x3f) << 0xc;
#endif
			temp = (temp & 0xfffffffd) | ((stereo & 1) << 1);
			temp = (temp & 0xfffffff3) | ((priority & 3) << 2);
			temp = (temp & 0xffffffef) | ((valid & 1) << 4);
			temp |= FIFO_U1;
			temp = (temp & 0xffffffdf) | ((empty & 1) << 5);
#ifdef CHIP_AU8820
			temp = (temp & 0xfffbffff) | ((f & 1) << 0x12);
#endif
#ifdef CHIP_AU8830
			temp = (temp & 0xf7ffffff) | ((f & 1) << 0x1b);
			temp = (temp & 0xefffffff) | ((f & 1) << 0x1c);
#endif
#ifdef CHIP_AU8810
			temp = (temp & 0xfeffffff) | ((f & 1) << 0x18);
			temp = (temp & 0xfdffffff) | ((f & 1) << 0x19);
#endif
		}
	} else {
		if (temp & FIFO_VALID) {
#ifdef CHIP_AU8820
			temp = ((f & 1) << 0x12) | (temp & 0xfffbffef);
#endif
#ifdef CHIP_AU8830
			temp =
			    ((f & 1) << 0x1b) | (temp & 0xe7ffffef) | FIFO_BITS;
#endif
#ifdef CHIP_AU8810
			temp =
			    ((f & 1) << 0x18) | (temp & 0xfcffffef) | FIFO_BITS;
#endif
		} else
			/*if (this_8[fifo]) */
			vortex_fifo_clearadbdata(vortex, fifo, FIFO_SIZE);
	}
	hwwrite(vortex->mmio, VORTEX_FIFO_ADBCTRL + (fifo << 2), temp);
	hwread(vortex->mmio, VORTEX_FIFO_ADBCTRL + (fifo << 2));
}

#ifndef CHIP_AU8810
static void vortex_fifo_clearwtdata(vortex_t * vortex, int fifo, int x)
{
	if (x < 1)
		return;
	for (x--; x >= 0; x--)
		hwwrite(vortex->mmio,
			VORTEX_FIFO_WTDATA +
			(((fifo << FIFO_SIZE_BITS) + x) << 2), 0);
}

static void vortex_fifo_wtinitialize(vortex_t * vortex, int fifo, int j)
{
	vortex_fifo_clearwtdata(vortex, fifo, FIFO_SIZE);
#ifdef CHIP_AU8820
	hwwrite(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2),
		(FIFO_U1 | ((j & FIFO_MASK) << 0xb)));
#else
	hwwrite(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2),
		(FIFO_U1 | ((j & FIFO_MASK) << 0xc)));
#endif
}

static void vortex_fifo_setwtvalid(vortex_t * vortex, int fifo, int en)
{
	hwwrite(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2),
		(hwread(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2)) &
		 0xffffffef) | ((en & 1) << 4) | FIFO_U1);
}

static void
vortex_fifo_setwtctrl(vortex_t * vortex, int fifo, int ctrl, int priority,
		      int empty, int valid, int f)
{
	int temp = 0, lifeboat = 0;
	int this_4 = 2;

	do {
		temp = hwread(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2));
		if (lifeboat++ > 0xbb8) {
			printk(KERN_ERR "Vortex: vortex_fifo_setwtctrl fail\n");
			break;
		}
	}
	while (temp & FIFO_RDONLY);

	if (valid) {
		if ((temp & FIFO_VALID) == 0) {
			vortex_fifo_clearwtdata(vortex, fifo, FIFO_SIZE);	// this_4
#ifdef CHIP_AU8820
			temp = (this_4 & 0x1f) << 0xb;
#else
			temp = (this_4 & 0x3f) << 0xc;
#endif
			temp = (temp & 0xfffffffd) | ((ctrl & 1) << 1);
			temp = (temp & 0xfffffff3) | ((priority & 3) << 2);
			temp = (temp & 0xffffffef) | ((valid & 1) << 4);
			temp |= FIFO_U1;
			temp = (temp & 0xffffffdf) | ((empty & 1) << 5);
#ifdef CHIP_AU8820
			temp = (temp & 0xfffbffff) | ((f & 1) << 0x12);
#endif
#ifdef CHIP_AU8830
			temp = (temp & 0xf7ffffff) | ((f & 1) << 0x1b);
			temp = (temp & 0xefffffff) | ((f & 1) << 0x1c);
#endif
#ifdef CHIP_AU8810
			temp = (temp & 0xfeffffff) | ((f & 1) << 0x18);
			temp = (temp & 0xfdffffff) | ((f & 1) << 0x19);
#endif
		}
	} else {
		if (temp & FIFO_VALID) {
#ifdef CHIP_AU8820
			temp = ((f & 1) << 0x12) | (temp & 0xfffbffef);
#endif
#ifdef CHIP_AU8830
			temp =
			    ((f & 1) << 0x1b) | (temp & 0xe7ffffef) | FIFO_BITS;
#endif
#ifdef CHIP_AU8810
			temp =
			    ((f & 1) << 0x18) | (temp & 0xfcffffef) | FIFO_BITS;
#endif
		} else
			/*if (this_8[fifo]) */
			vortex_fifo_clearwtdata(vortex, fifo, FIFO_SIZE);
	}
	hwwrite(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2), temp);
	hwread(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2));

/*	
    do {
		temp = hwread(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2));
		if (lifeboat++ > 0xbb8) {
			printk(KERN_ERR "Vortex: vortex_fifo_setwtctrl fail (hanging)\n");
			break;
		}
    } while ((temp & FIFO_RDONLY)&&(temp & FIFO_VALID)&&(temp != 0xFFFFFFFF));
	
	
	if (valid) {
		if (temp & FIFO_VALID) {
			temp = 0x40000;
			//temp |= 0x08000000;
			//temp |= 0x10000000;
			//temp |= 0x04000000;
			//temp |= 0x00400000;
			temp |= 0x1c400000;
			temp &= 0xFFFFFFF3;
			temp &= 0xFFFFFFEF;
			temp |= (valid & 1) << 4;
			hwwrite(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2), temp);
			return;
		} else {
			vortex_fifo_clearwtdata(vortex, fifo, FIFO_SIZE);
			return;
		}
	} else {
		temp &= 0xffffffef;
		temp |= 0x08000000;
		temp |= 0x10000000;
		temp |= 0x04000000;
		temp |= 0x00400000;
		hwwrite(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2), temp);
		temp = hwread(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2));
		//((temp >> 6) & 0x3f) 
		
		priority = 0;
		if (((temp & 0x0fc0) ^ ((temp >> 6) & 0x0fc0)) & 0FFFFFFC0)
			vortex_fifo_clearwtdata(vortex, fifo, FIFO_SIZE);
		valid = 0xfb;
		temp = (temp & 0xfffffffd) | ((ctrl & 1) << 1);
		temp = (temp & 0xfffdffff) | ((f & 1) << 0x11);
		temp = (temp & 0xfffffff3) | ((priority & 3) << 2);
		temp = (temp & 0xffffffef) | ((valid & 1) << 4);
		temp = (temp & 0xffffffdf) | ((empty & 1) << 5);
		hwwrite(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2), temp);
	}
	
	*/

	/*
	   temp = (temp & 0xfffffffd) | ((ctrl & 1) << 1);
	   temp = (temp & 0xfffdffff) | ((f & 1) << 0x11);
	   temp = (temp & 0xfffffff3) | ((priority & 3) << 2);
	   temp = (temp & 0xffffffef) | ((valid & 1) << 4);
	   temp = (temp & 0xffffffdf) | ((empty & 1) << 5);
	   #ifdef FIFO_BITS
	   temp = temp | FIFO_BITS | 40000;
	   #endif
	   // 0x1c440010, 0x1c400000
	   hwwrite(vortex->mmio, VORTEX_FIFO_WTCTRL + (fifo << 2), temp);
	 */
}

#endif
static void vortex_fifo_init(vortex_t * vortex)
{
	int x;
	u32 addr;

	/* ADB DMA channels fifos. */
	addr = VORTEX_FIFO_ADBCTRL + ((NR_ADB - 1) * 4);
	for (x = NR_ADB - 1; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, (FIFO_U0 | FIFO_U1));
		if (hwread(vortex->mmio, addr) != (FIFO_U0 | FIFO_U1))
			printk(KERN_ERR "bad adb fifo reset!");
		vortex_fifo_clearadbdata(vortex, x, FIFO_SIZE);
		addr -= 4;
	}

#ifndef CHIP_AU8810
	/* WT DMA channels fifos. */
	addr = VORTEX_FIFO_WTCTRL + ((NR_WT - 1) * 4);
	for (x = NR_WT - 1; x >= 0; x--) {
		hwwrite(vortex->mmio, addr, FIFO_U0);
		if (hwread(vortex->mmio, addr) != FIFO_U0)
			printk(KERN_ERR
			       "bad wt fifo reset (0x%08x, 0x%08x)!\n",
			       addr, hwread(vortex->mmio, addr));
		vortex_fifo_clearwtdata(vortex, x, FIFO_SIZE);
		addr -= 4;
	}
#endif
	/* trigger... */
#ifdef CHIP_AU8820
	hwwrite(vortex->mmio, 0xf8c0, 0xd03);	//0x0843 0xd6b
#else
#ifdef CHIP_AU8830
	hwwrite(vortex->mmio, 0x17000, 0x61);	/* wt a */
	hwwrite(vortex->mmio, 0x17004, 0x61);	/* wt b */
#endif
	hwwrite(vortex->mmio, 0x17008, 0x61);	/* adb */
#endif
}

/* ADBDMA */

static void vortex_adbdma_init(vortex_t * vortex)
{
}

static void vortex_adbdma_setfirstbuffer(vortex_t * vortex, int adbdma)
{
	stream_t *dma = &vortex->dma_adb[adbdma];

	hwwrite(vortex->mmio, VORTEX_ADBDMA_CTRL + (adbdma << 2),
		dma->dma_ctrl);
}

static void vortex_adbdma_setstartbuffer(vortex_t * vortex, int adbdma, int sb)
{
	stream_t *dma = &vortex->dma_adb[adbdma];
	//hwwrite(vortex->mmio, VORTEX_ADBDMA_START + (adbdma << 2), sb << (((NR_ADB-1)-((adbdma&0xf)*2))));
	hwwrite(vortex->mmio, VORTEX_ADBDMA_START + (adbdma << 2),
		sb << ((0xf - (adbdma & 0xf)) * 2));
	dma->period_real = dma->period_virt = sb;
}

static void
vortex_adbdma_setbuffers(vortex_t * vortex, int adbdma,
			 int psize, int count)
{
	stream_t *dma = &vortex->dma_adb[adbdma];

	dma->period_bytes = psize;
	dma->nr_periods = count;

	dma->cfg0 = 0;
	dma->cfg1 = 0;
	switch (count) {
		/* Four or more pages */
	default:
	case 4:
		dma->cfg1 |= 0x88000000 | 0x44000000 | 0x30000000 | (psize - 1);
		hwwrite(vortex->mmio,
			VORTEX_ADBDMA_BUFBASE + (adbdma << 4) + 0xc,
			snd_pcm_sgbuf_get_addr(dma->substream, psize * 3));
		/* 3 pages */
	case 3:
		dma->cfg0 |= 0x12000000;
		dma->cfg1 |= 0x80000000 | 0x40000000 | ((psize - 1) << 0xc);
		hwwrite(vortex->mmio,
			VORTEX_ADBDMA_BUFBASE + (adbdma << 4) + 0x8,
			snd_pcm_sgbuf_get_addr(dma->substream, psize * 2));
		/* 2 pages */
	case 2:
		dma->cfg0 |= 0x88000000 | 0x44000000 | 0x10000000 | (psize - 1);
		hwwrite(vortex->mmio,
			VORTEX_ADBDMA_BUFBASE + (adbdma << 4) + 0x4,
			snd_pcm_sgbuf_get_addr(dma->substream, psize));
		/* 1 page */
	case 1:
		dma->cfg0 |= 0x80000000 | 0x40000000 | ((psize - 1) << 0xc);
		hwwrite(vortex->mmio,
			VORTEX_ADBDMA_BUFBASE + (adbdma << 4),
			snd_pcm_sgbuf_get_addr(dma->substream, 0));
		break;
	}
	/*
	printk(KERN_DEBUG "vortex: cfg0 = 0x%x\nvortex: cfg1=0x%x\n",
	       dma->cfg0, dma->cfg1);
	*/
	hwwrite(vortex->mmio, VORTEX_ADBDMA_BUFCFG0 + (adbdma << 3), dma->cfg0);
	hwwrite(vortex->mmio, VORTEX_ADBDMA_BUFCFG1 + (adbdma << 3), dma->cfg1);

	vortex_adbdma_setfirstbuffer(vortex, adbdma);
	vortex_adbdma_setstartbuffer(vortex, adbdma, 0);
}

static void
vortex_adbdma_setmode(vortex_t * vortex, int adbdma, int ie, int dir,
		      int fmt, int stereo, u32 offset)
{
	stream_t *dma = &vortex->dma_adb[adbdma];

	dma->dma_unknown = stereo;
	dma->dma_ctrl =
	    ((offset & OFFSET_MASK) | (dma->dma_ctrl & ~OFFSET_MASK));
	/* Enable PCMOUT interrupts. */
	dma->dma_ctrl =
	    (dma->dma_ctrl & ~IE_MASK) | ((ie << IE_SHIFT) & IE_MASK);

	dma->dma_ctrl =
	    (dma->dma_ctrl & ~DIR_MASK) | ((dir << DIR_SHIFT) & DIR_MASK);
	dma->dma_ctrl =
	    (dma->dma_ctrl & ~FMT_MASK) | ((fmt << FMT_SHIFT) & FMT_MASK);

	hwwrite(vortex->mmio, VORTEX_ADBDMA_CTRL + (adbdma << 2),
		dma->dma_ctrl);
	hwread(vortex->mmio, VORTEX_ADBDMA_CTRL + (adbdma << 2));
}

static int vortex_adbdma_bufshift(vortex_t * vortex, int adbdma)
{
	stream_t *dma = &vortex->dma_adb[adbdma];
	int page, p, pp, delta, i;

	page =
	    (hwread(vortex->mmio, VORTEX_ADBDMA_STAT + (adbdma << 2)) &
	     ADB_SUBBUF_MASK) >> ADB_SUBBUF_SHIFT;
	if (dma->nr_periods >= 4)
		delta = (page - dma->period_real) & 3;
	else {
		delta = (page - dma->period_real);
		if (delta < 0)
			delta += dma->nr_periods;
	}
	if (delta == 0)
		return 0;

	/* refresh hw page table */
	if (dma->nr_periods > 4) {
		for (i = 0; i < delta; i++) {
			/* p: audio buffer page index */
			p = dma->period_virt + i + 4;
			if (p >= dma->nr_periods)
				p -= dma->nr_periods;
			/* pp: hardware DMA page index. */
			pp = dma->period_real + i;
			if (pp >= 4)
				pp -= 4;
			//hwwrite(vortex->mmio, VORTEX_ADBDMA_BUFBASE+(((adbdma << 2)+pp) << 2), dma->table[p].addr);
			hwwrite(vortex->mmio,
				VORTEX_ADBDMA_BUFBASE + (((adbdma << 2) + pp) << 2),
				snd_pcm_sgbuf_get_addr(dma->substream,
				dma->period_bytes * p));
			/* Force write thru cache. */
			hwread(vortex->mmio, VORTEX_ADBDMA_BUFBASE +
			       (((adbdma << 2) + pp) << 2));
		}
	}
	dma->period_virt += delta;
	dma->period_real = page;
	if (dma->period_virt >= dma->nr_periods)
		dma->period_virt -= dma->nr_periods;
	if (delta != 1)
		printk(KERN_INFO "vortex: %d virt=%d, real=%d, delta=%d\n",
		       adbdma, dma->period_virt, dma->period_real, delta);

	return delta;
}


static void vortex_adbdma_resetup(vortex_t *vortex, int adbdma) {
	stream_t *dma = &vortex->dma_adb[adbdma];
	int p, pp, i;

	/* refresh hw page table */
	for (i=0 ; i < 4 && i < dma->nr_periods; i++) {
		/* p: audio buffer page index */
		p = dma->period_virt + i;
		if (p >= dma->nr_periods)
			p -= dma->nr_periods;
		/* pp: hardware DMA page index. */
		pp = dma->period_real + i;
		if (dma->nr_periods < 4) {
			if (pp >= dma->nr_periods)
				pp -= dma->nr_periods;
		}
		else {
			if (pp >= 4)
				pp -= 4;
		}
		hwwrite(vortex->mmio,
			VORTEX_ADBDMA_BUFBASE + (((adbdma << 2) + pp) << 2),
			snd_pcm_sgbuf_get_addr(dma->substream,
					       dma->period_bytes * p));
		/* Force write thru cache. */
		hwread(vortex->mmio, VORTEX_ADBDMA_BUFBASE + (((adbdma << 2)+pp) << 2));
	}
}

static inline int vortex_adbdma_getlinearpos(vortex_t * vortex, int adbdma)
{
	stream_t *dma = &vortex->dma_adb[adbdma];
	int temp, page, delta;

	temp = hwread(vortex->mmio, VORTEX_ADBDMA_STAT + (adbdma << 2));
	page = (temp & ADB_SUBBUF_MASK) >> ADB_SUBBUF_SHIFT;
	if (dma->nr_periods >= 4)
		delta = (page - dma->period_real) & 3;
	else {
		delta = (page - dma->period_real);
		if (delta < 0)
			delta += dma->nr_periods;
	}
	return (dma->period_virt + delta) * dma->period_bytes
		+ (temp & (dma->period_bytes - 1));
}

static void vortex_adbdma_startfifo(vortex_t * vortex, int adbdma)
{
	int this_8 = 0 /*empty */ , this_4 = 0 /*priority */ ;
	stream_t *dma = &vortex->dma_adb[adbdma];

	switch (dma->fifo_status) {
	case FIFO_START:
		vortex_fifo_setadbvalid(vortex, adbdma,
					dma->fifo_enabled ? 1 : 0);
		break;
	case FIFO_STOP:
		this_8 = 1;
		hwwrite(vortex->mmio, VORTEX_ADBDMA_CTRL + (adbdma << 2),
			dma->dma_ctrl);
		vortex_fifo_setadbctrl(vortex, adbdma, dma->dma_unknown,
				       this_4, this_8,
				       dma->fifo_enabled ? 1 : 0, 0);
		break;
	case FIFO_PAUSE:
		vortex_fifo_setadbctrl(vortex, adbdma, dma->dma_unknown,
				       this_4, this_8,
				       dma->fifo_enabled ? 1 : 0, 0);
		break;
	}
	dma->fifo_status = FIFO_START;
}

static void vortex_adbdma_resumefifo(vortex_t * vortex, int adbdma)
{
	stream_t *dma = &vortex->dma_adb[adbdma];

	int this_8 = 1, this_4 = 0;
	switch (dma->fifo_status) {
	case FIFO_STOP:
		hwwrite(vortex->mmio, VORTEX_ADBDMA_CTRL + (adbdma << 2),
			dma->dma_ctrl);
		vortex_fifo_setadbctrl(vortex, adbdma, dma->dma_unknown,
				       this_4, this_8,
				       dma->fifo_enabled ? 1 : 0, 0);
		break;
	case FIFO_PAUSE:
		vortex_fifo_setadbctrl(vortex, adbdma, dma->dma_unknown,
				       this_4, this_8,
				       dma->fifo_enabled ? 1 : 0, 0);
		break;
	}
	dma->fifo_status = FIFO_START;
}

static void vortex_adbdma_pausefifo(vortex_t * vortex, int adbdma)
{
	stream_t *dma = &vortex->dma_adb[adbdma];

	int this_8 = 0, this_4 = 0;
	switch (dma->fifo_status) {
	case FIFO_START:
		vortex_fifo_setadbctrl(vortex, adbdma, dma->dma_unknown,
				       this_4, this_8, 0, 0);
		break;
	case FIFO_STOP:
		hwwrite(vortex->mmio, VORTEX_ADBDMA_CTRL + (adbdma << 2),
			dma->dma_ctrl);
		vortex_fifo_setadbctrl(vortex, adbdma, dma->dma_unknown,
				       this_4, this_8, 0, 0);
		break;
	}
	dma->fifo_status = FIFO_PAUSE;
}

static void vortex_adbdma_stopfifo(vortex_t * vortex, int adbdma)
{
	stream_t *dma = &vortex->dma_adb[adbdma];

	int this_4 = 0, this_8 = 0;
	if (dma->fifo_status == FIFO_START)
		vortex_fifo_setadbctrl(vortex, adbdma, dma->dma_unknown,
				       this_4, this_8, 0, 0);
	else if (dma->fifo_status == FIFO_STOP)
		return;
	dma->fifo_status = FIFO_STOP;
	dma->fifo_enabled = 0;
}

/* WTDMA */

#ifndef CHIP_AU8810
static void vortex_wtdma_setfirstbuffer(vortex_t * vortex, int wtdma)
{
	//int this_7c=dma_ctrl;
	stream_t *dma = &vortex->dma_wt[wtdma];

	hwwrite(vortex->mmio, VORTEX_WTDMA_CTRL + (wtdma << 2), dma->dma_ctrl);
}

static void vortex_wtdma_setstartbuffer(vortex_t * vortex, int wtdma, int sb)
{
	stream_t *dma = &vortex->dma_wt[wtdma];
	//hwwrite(vortex->mmio, VORTEX_WTDMA_START + (wtdma << 2), sb << ((0x1f-(wtdma&0xf)*2)));
	hwwrite(vortex->mmio, VORTEX_WTDMA_START + (wtdma << 2),
		sb << ((0xf - (wtdma & 0xf)) * 2));
	dma->period_real = dma->period_virt = sb;
}

static void
vortex_wtdma_setbuffers(vortex_t * vortex, int wtdma,
			int psize, int count)
{
	stream_t *dma = &vortex->dma_wt[wtdma];

	dma->period_bytes = psize;
	dma->nr_periods = count;

	dma->cfg0 = 0;
	dma->cfg1 = 0;
	switch (count) {
		/* Four or more pages */
	default:
	case 4:
		dma->cfg1 |= 0x88000000 | 0x44000000 | 0x30000000 | (psize-1);
		hwwrite(vortex->mmio, VORTEX_WTDMA_BUFBASE + (wtdma << 4) + 0xc,
			snd_pcm_sgbuf_get_addr(dma->substream, psize * 3));
		/* 3 pages */
	case 3:
		dma->cfg0 |= 0x12000000;
		dma->cfg1 |= 0x80000000 | 0x40000000 | ((psize-1) << 0xc);
		hwwrite(vortex->mmio, VORTEX_WTDMA_BUFBASE + (wtdma << 4)  + 0x8,
			snd_pcm_sgbuf_get_addr(dma->substream, psize * 2));
		/* 2 pages */
	case 2:
		dma->cfg0 |= 0x88000000 | 0x44000000 | 0x10000000 | (psize-1);
		hwwrite(vortex->mmio, VORTEX_WTDMA_BUFBASE + (wtdma << 4) + 0x4,
			snd_pcm_sgbuf_get_addr(dma->substream, psize));
		/* 1 page */
	case 1:
		dma->cfg0 |= 0x80000000 | 0x40000000 | ((psize-1) << 0xc);
		hwwrite(vortex->mmio, VORTEX_WTDMA_BUFBASE + (wtdma << 4),
			snd_pcm_sgbuf_get_addr(dma->substream, 0));
		break;
	}
	hwwrite(vortex->mmio, VORTEX_WTDMA_BUFCFG0 + (wtdma << 3), dma->cfg0);
	hwwrite(vortex->mmio, VORTEX_WTDMA_BUFCFG1 + (wtdma << 3), dma->cfg1);

	vortex_wtdma_setfirstbuffer(vortex, wtdma);
	vortex_wtdma_setstartbuffer(vortex, wtdma, 0);
}

static void
vortex_wtdma_setmode(vortex_t * vortex, int wtdma, int ie, int fmt, int d,
		     /*int e, */ u32 offset)
{
	stream_t *dma = &vortex->dma_wt[wtdma];

	//dma->this_08 = e;
	dma->dma_unknown = d;
	dma->dma_ctrl = 0;
	dma->dma_ctrl =
	    ((offset & OFFSET_MASK) | (dma->dma_ctrl & ~OFFSET_MASK));
	/* PCMOUT interrupt */
	dma->dma_ctrl =
	    (dma->dma_ctrl & ~IE_MASK) | ((ie << IE_SHIFT) & IE_MASK);
	/* Always playback. */
	dma->dma_ctrl |= (1 << DIR_SHIFT);
	/* Audio Format */
	dma->dma_ctrl =
	    (dma->dma_ctrl & FMT_MASK) | ((fmt << FMT_SHIFT) & FMT_MASK);
	/* Write into hardware */
	hwwrite(vortex->mmio, VORTEX_WTDMA_CTRL + (wtdma << 2), dma->dma_ctrl);
}

static int vortex_wtdma_bufshift(vortex_t * vortex, int wtdma)
{
	stream_t *dma = &vortex->dma_wt[wtdma];
	int page, p, pp, delta, i;

	page =
	    (hwread(vortex->mmio, VORTEX_WTDMA_STAT + (wtdma << 2)) &
	     WT_SUBBUF_MASK)
	    >> WT_SUBBUF_SHIFT;
	if (dma->nr_periods >= 4)
		delta = (page - dma->period_real) & 3;
	else {
		delta = (page - dma->period_real);
		if (delta < 0)
			delta += dma->nr_periods;
	}
	if (delta == 0)
		return 0;

	/* refresh hw page table */
	if (dma->nr_periods > 4) {
		for (i = 0; i < delta; i++) {
			/* p: audio buffer page index */
			p = dma->period_virt + i + 4;
			if (p >= dma->nr_periods)
				p -= dma->nr_periods;
			/* pp: hardware DMA page index. */
			pp = dma->period_real + i;
			if (pp >= 4)
				pp -= 4;
			hwwrite(vortex->mmio,
				VORTEX_WTDMA_BUFBASE +
				(((wtdma << 2) + pp) << 2),
				snd_pcm_sgbuf_get_addr(dma->substream,
						       dma->period_bytes * p));
			/* Force write thru cache. */
			hwread(vortex->mmio, VORTEX_WTDMA_BUFBASE +
			       (((wtdma << 2) + pp) << 2));
		}
	}
	dma->period_virt += delta;
	if (dma->period_virt >= dma->nr_periods)
		dma->period_virt -= dma->nr_periods;
	dma->period_real = page;

	if (delta != 1)
		printk(KERN_WARNING "vortex: wt virt = %d, delta = %d\n",
		       dma->period_virt, delta);

	return delta;
}

#if 0
static void
vortex_wtdma_getposition(vortex_t * vortex, int wtdma, int *subbuf, int *pos)
{
	int temp;
	temp = hwread(vortex->mmio, VORTEX_WTDMA_STAT + (wtdma << 2));
	*subbuf = (temp >> WT_SUBBUF_SHIFT) & WT_SUBBUF_MASK;
	*pos = temp & POS_MASK;
}

static int vortex_wtdma_getcursubuffer(vortex_t * vortex, int wtdma)
{
	return ((hwread(vortex->mmio, VORTEX_WTDMA_STAT + (wtdma << 2)) >>
		 POS_SHIFT) & POS_MASK);
}
#endif
static inline int vortex_wtdma_getlinearpos(vortex_t * vortex, int wtdma)
{
	stream_t *dma = &vortex->dma_wt[wtdma];
	int temp;

	temp = hwread(vortex->mmio, VORTEX_WTDMA_STAT + (wtdma << 2));
	temp = (dma->period_virt * dma->period_bytes) + (temp & (dma->period_bytes - 1));
	return temp;
}

static void vortex_wtdma_startfifo(vortex_t * vortex, int wtdma)
{
	stream_t *dma = &vortex->dma_wt[wtdma];
	int this_8 = 0, this_4 = 0;

	switch (dma->fifo_status) {
	case FIFO_START:
		vortex_fifo_setwtvalid(vortex, wtdma,
				       dma->fifo_enabled ? 1 : 0);
		break;
	case FIFO_STOP:
		this_8 = 1;
		hwwrite(vortex->mmio, VORTEX_WTDMA_CTRL + (wtdma << 2),
			dma->dma_ctrl);
		vortex_fifo_setwtctrl(vortex, wtdma, dma->dma_unknown,
				      this_4, this_8,
				      dma->fifo_enabled ? 1 : 0, 0);
		break;
	case FIFO_PAUSE:
		vortex_fifo_setwtctrl(vortex, wtdma, dma->dma_unknown,
				      this_4, this_8,
				      dma->fifo_enabled ? 1 : 0, 0);
		break;
	}
	dma->fifo_status = FIFO_START;
}

static void vortex_wtdma_resumefifo(vortex_t * vortex, int wtdma)
{
	stream_t *dma = &vortex->dma_wt[wtdma];

	int this_8 = 0, this_4 = 0;
	switch (dma->fifo_status) {
	case FIFO_STOP:
		hwwrite(vortex->mmio, VORTEX_WTDMA_CTRL + (wtdma << 2),
			dma->dma_ctrl);
		vortex_fifo_setwtctrl(vortex, wtdma, dma->dma_unknown,
				      this_4, this_8,
				      dma->fifo_enabled ? 1 : 0, 0);
		break;
	case FIFO_PAUSE:
		vortex_fifo_setwtctrl(vortex, wtdma, dma->dma_unknown,
				      this_4, this_8,
				      dma->fifo_enabled ? 1 : 0, 0);
		break;
	}
	dma->fifo_status = FIFO_START;
}

static void vortex_wtdma_pausefifo(vortex_t * vortex, int wtdma)
{
	stream_t *dma = &vortex->dma_wt[wtdma];

	int this_8 = 0, this_4 = 0;
	switch (dma->fifo_status) {
	case FIFO_START:
		vortex_fifo_setwtctrl(vortex, wtdma, dma->dma_unknown,
				      this_4, this_8, 0, 0);
		break;
	case FIFO_STOP:
		hwwrite(vortex->mmio, VORTEX_WTDMA_CTRL + (wtdma << 2),
			dma->dma_ctrl);
		vortex_fifo_setwtctrl(vortex, wtdma, dma->dma_unknown,
				      this_4, this_8, 0, 0);
		break;
	}
	dma->fifo_status = FIFO_PAUSE;
}

static void vortex_wtdma_stopfifo(vortex_t * vortex, int wtdma)
{
	stream_t *dma = &vortex->dma_wt[wtdma];

	int this_4 = 0, this_8 = 0;
	if (dma->fifo_status == FIFO_START)
		vortex_fifo_setwtctrl(vortex, wtdma, dma->dma_unknown,
				      this_4, this_8, 0, 0);
	else if (dma->fifo_status == FIFO_STOP)
		return;
	dma->fifo_status = FIFO_STOP;
	dma->fifo_enabled = 0;
}

#endif
/* ADB Routes */

typedef int ADBRamLink;
static void vortex_adb_init(vortex_t * vortex)
{
	int i;
	/* it looks like we are writing more than we need to...
	 * if we write what we are supposed to it breaks things... */
	hwwrite(vortex->mmio, VORTEX_ADB_SR, 0);
	for (i = 0; i < VORTEX_ADB_RTBASE_COUNT; i++)
		hwwrite(vortex->mmio, VORTEX_ADB_RTBASE + (i << 2),
			hwread(vortex->mmio,
			       VORTEX_ADB_RTBASE + (i << 2)) | ROUTE_MASK);
	for (i = 0; i < VORTEX_ADB_CHNBASE_COUNT; i++) {
		hwwrite(vortex->mmio, VORTEX_ADB_CHNBASE + (i << 2),
			hwread(vortex->mmio,
			       VORTEX_ADB_CHNBASE + (i << 2)) | ROUTE_MASK);
	}
}

static void vortex_adb_en_sr(vortex_t * vortex, int channel)
{
	hwwrite(vortex->mmio, VORTEX_ADB_SR,
		hwread(vortex->mmio, VORTEX_ADB_SR) | (0x1 << channel));
}

static void vortex_adb_dis_sr(vortex_t * vortex, int channel)
{
	hwwrite(vortex->mmio, VORTEX_ADB_SR,
		hwread(vortex->mmio, VORTEX_ADB_SR) & ~(0x1 << channel));
}

static void
vortex_adb_addroutes(vortex_t * vortex, unsigned char channel,
		     ADBRamLink * route, int rnum)
{
	int temp, prev, lifeboat = 0;

	if ((rnum <= 0) || (route == NULL))
		return;
	/* Write last routes. */
	rnum--;
	hwwrite(vortex->mmio,
		VORTEX_ADB_RTBASE + ((route[rnum] & ADB_MASK) << 2),
		ROUTE_MASK);
	while (rnum > 0) {
		hwwrite(vortex->mmio,
			VORTEX_ADB_RTBASE +
			((route[rnum - 1] & ADB_MASK) << 2), route[rnum]);
		rnum--;
	}
	/* Write first route. */
	temp =
	    hwread(vortex->mmio,
		   VORTEX_ADB_CHNBASE + (channel << 2)) & ADB_MASK;
	if (temp == ADB_MASK) {
		/* First entry on this channel. */
		hwwrite(vortex->mmio, VORTEX_ADB_CHNBASE + (channel << 2),
			route[0]);
		vortex_adb_en_sr(vortex, channel);
		return;
	}
	/* Not first entry on this channel. Need to link. */
	do {
		prev = temp;
		temp =
		    hwread(vortex->mmio,
			   VORTEX_ADB_RTBASE + (temp << 2)) & ADB_MASK;
		if ((lifeboat++) > ADB_MASK) {
			printk(KERN_ERR
			       "vortex_adb_addroutes: unending route! 0x%x\n",
			       *route);
			return;
		}
	}
	while (temp != ADB_MASK);
	hwwrite(vortex->mmio, VORTEX_ADB_RTBASE + (prev << 2), route[0]);
}

static void
vortex_adb_delroutes(vortex_t * vortex, unsigned char channel,
		     ADBRamLink route0, ADBRamLink route1)
{
	int temp, lifeboat = 0, prev;

	/* Find route. */
	temp =
	    hwread(vortex->mmio,
		   VORTEX_ADB_CHNBASE + (channel << 2)) & ADB_MASK;
	if (temp == (route0 & ADB_MASK)) {
		temp =
		    hwread(vortex->mmio,
			   VORTEX_ADB_RTBASE + ((route1 & ADB_MASK) << 2));
		if ((temp & ADB_MASK) == ADB_MASK)
			vortex_adb_dis_sr(vortex, channel);
		hwwrite(vortex->mmio, VORTEX_ADB_CHNBASE + (channel << 2),
			temp);
		return;
	}
	do {
		prev = temp;
		temp =
		    hwread(vortex->mmio,
			   VORTEX_ADB_RTBASE + (prev << 2)) & ADB_MASK;
		if (((lifeboat++) > ADB_MASK) || (temp == ADB_MASK)) {
			printk(KERN_ERR
			       "vortex_adb_delroutes: route not found! 0x%x\n",
			       route0);
			return;
		}
	}
	while (temp != (route0 & ADB_MASK));
	temp = hwread(vortex->mmio, VORTEX_ADB_RTBASE + (temp << 2));
	if ((temp & ADB_MASK) == route1)
		temp = hwread(vortex->mmio, VORTEX_ADB_RTBASE + (temp << 2));
	/* Make bridge over deleted route. */
	hwwrite(vortex->mmio, VORTEX_ADB_RTBASE + (prev << 2), temp);
}

static void
vortex_route(vortex_t * vortex, int en, unsigned char channel,
	     unsigned char source, unsigned char dest)
{
	ADBRamLink route;

	route = ((source & ADB_MASK) << ADB_SHIFT) | (dest & ADB_MASK);
	if (en) {
		vortex_adb_addroutes(vortex, channel, &route, 1);
		if ((source < (OFFSET_SRCOUT + NR_SRC))
		    && (source >= OFFSET_SRCOUT))
			vortex_src_addWTD(vortex, (source - OFFSET_SRCOUT),
					  channel);
		else if ((source < (OFFSET_MIXOUT + NR_MIXOUT))
			 && (source >= OFFSET_MIXOUT))
			vortex_mixer_addWTD(vortex,
					    (source - OFFSET_MIXOUT), channel);
	} else {
		vortex_adb_delroutes(vortex, channel, route, route);
		if ((source < (OFFSET_SRCOUT + NR_SRC))
		    && (source >= OFFSET_SRCOUT))
			vortex_src_delWTD(vortex, (source - OFFSET_SRCOUT),
					  channel);
		else if ((source < (OFFSET_MIXOUT + NR_MIXOUT))
			 && (source >= OFFSET_MIXOUT))
			vortex_mixer_delWTD(vortex,
					    (source - OFFSET_MIXOUT), channel);
	}
}

#if 0
static void
vortex_routes(vortex_t * vortex, int en, unsigned char channel,
	      unsigned char source, unsigned char dest0, unsigned char dest1)
{
	ADBRamLink route[2];

	route[0] = ((source & ADB_MASK) << ADB_SHIFT) | (dest0 & ADB_MASK);
	route[1] = ((source & ADB_MASK) << ADB_SHIFT) | (dest1 & ADB_MASK);

	if (en) {
		vortex_adb_addroutes(vortex, channel, route, 2);
		if ((source < (OFFSET_SRCOUT + NR_SRC))
		    && (source >= (OFFSET_SRCOUT)))
			vortex_src_addWTD(vortex, (source - OFFSET_SRCOUT),
					  channel);
		else if ((source < (OFFSET_MIXOUT + NR_MIXOUT))
			 && (source >= (OFFSET_MIXOUT)))
			vortex_mixer_addWTD(vortex,
					    (source - OFFSET_MIXOUT), channel);
	} else {
		vortex_adb_delroutes(vortex, channel, route[0], route[1]);
		if ((source < (OFFSET_SRCOUT + NR_SRC))
		    && (source >= (OFFSET_SRCOUT)))
			vortex_src_delWTD(vortex, (source - OFFSET_SRCOUT),
					  channel);
		else if ((source < (OFFSET_MIXOUT + NR_MIXOUT))
			 && (source >= (OFFSET_MIXOUT)))
			vortex_mixer_delWTD(vortex,
					    (source - OFFSET_MIXOUT), channel);
	}
}

#endif
/* Route two sources to same target. Sources must be of same class !!! */
static void
vortex_routeLRT(vortex_t * vortex, int en, unsigned char ch,
		unsigned char source0, unsigned char source1,
		unsigned char dest)
{
	ADBRamLink route[2];

	route[0] = ((source0 & ADB_MASK) << ADB_SHIFT) | (dest & ADB_MASK);
	route[1] = ((source1 & ADB_MASK) << ADB_SHIFT) | (dest & ADB_MASK);

	if (dest < 0x10)
		route[1] = (route[1] & ~ADB_MASK) | (dest + 0x20);	/* fifo A */

	if (en) {
		vortex_adb_addroutes(vortex, ch, route, 2);
		if ((source0 < (OFFSET_SRCOUT + NR_SRC))
		    && (source0 >= OFFSET_SRCOUT)) {
			vortex_src_addWTD(vortex,
					  (source0 - OFFSET_SRCOUT), ch);
			vortex_src_addWTD(vortex,
					  (source1 - OFFSET_SRCOUT), ch);
		} else if ((source0 < (OFFSET_MIXOUT + NR_MIXOUT))
			   && (source0 >= OFFSET_MIXOUT)) {
			vortex_mixer_addWTD(vortex,
					    (source0 - OFFSET_MIXOUT), ch);
			vortex_mixer_addWTD(vortex,
					    (source1 - OFFSET_MIXOUT), ch);
		}
	} else {
		vortex_adb_delroutes(vortex, ch, route[0], route[1]);
		if ((source0 < (OFFSET_SRCOUT + NR_SRC))
		    && (source0 >= OFFSET_SRCOUT)) {
			vortex_src_delWTD(vortex,
					  (source0 - OFFSET_SRCOUT), ch);
			vortex_src_delWTD(vortex,
					  (source1 - OFFSET_SRCOUT), ch);
		} else if ((source0 < (OFFSET_MIXOUT + NR_MIXOUT))
			   && (source0 >= OFFSET_MIXOUT)) {
			vortex_mixer_delWTD(vortex,
					    (source0 - OFFSET_MIXOUT), ch);
			vortex_mixer_delWTD(vortex,
					    (source1 - OFFSET_MIXOUT), ch);
		}
	}
}

/* Connection stuff */

// Connect adbdma to src('s).
static void
vortex_connection_adbdma_src(vortex_t * vortex, int en, unsigned char ch,
			     unsigned char adbdma, unsigned char src)
{
	vortex_route(vortex, en, ch, ADB_DMA(adbdma), ADB_SRCIN(src));
}

// Connect SRC to mixin.
static void
vortex_connection_src_mixin(vortex_t * vortex, int en,
			    unsigned char channel, unsigned char src,
			    unsigned char mixin)
{
	vortex_route(vortex, en, channel, ADB_SRCOUT(src), ADB_MIXIN(mixin));
}

// Connect mixin with mix output.
static void
vortex_connection_mixin_mix(vortex_t * vortex, int en, unsigned char mixin,
			    unsigned char mix, int a)
{
	if (en) {
		vortex_mix_enableinput(vortex, mix, mixin);
		vortex_mix_setinputvolumebyte(vortex, mix, mixin, MIX_DEFIGAIN);	// added to original code.
	} else
		vortex_mix_disableinput(vortex, mix, mixin, a);
}

// Connect absolut address to mixin.
static void
vortex_connection_adb_mixin(vortex_t * vortex, int en,
			    unsigned char channel, unsigned char source,
			    unsigned char mixin)
{
	vortex_route(vortex, en, channel, source, ADB_MIXIN(mixin));
}

static void
vortex_connection_src_adbdma(vortex_t * vortex, int en, unsigned char ch,
			     unsigned char src, unsigned char adbdma)
{
	vortex_route(vortex, en, ch, ADB_SRCOUT(src), ADB_DMA(adbdma));
}

static void
vortex_connection_src_src_adbdma(vortex_t * vortex, int en,
				 unsigned char ch, unsigned char src0,
				 unsigned char src1, unsigned char adbdma)
{

	vortex_routeLRT(vortex, en, ch, ADB_SRCOUT(src0), ADB_SRCOUT(src1),
			ADB_DMA(adbdma));
}

// mix to absolut address.
static void
vortex_connection_mix_adb(vortex_t * vortex, int en, unsigned char ch,
			  unsigned char mix, unsigned char dest)
{
	vortex_route(vortex, en, ch, ADB_MIXOUT(mix), dest);
	vortex_mix_setvolumebyte(vortex, mix, MIX_DEFOGAIN);	// added to original code.
}

// mixer to src.
static void
vortex_connection_mix_src(vortex_t * vortex, int en, unsigned char ch,
			  unsigned char mix, unsigned char src)
{
	vortex_route(vortex, en, ch, ADB_MIXOUT(mix), ADB_SRCIN(src));
	vortex_mix_setvolumebyte(vortex, mix, MIX_DEFOGAIN);	// added to original code.
}

#if 0
static void
vortex_connection_adbdma_src_src(vortex_t * vortex, int en,
				 unsigned char channel,
				 unsigned char adbdma, unsigned char src0,
				 unsigned char src1)
{
	vortex_routes(vortex, en, channel, ADB_DMA(adbdma),
		      ADB_SRCIN(src0), ADB_SRCIN(src1));
}

// Connect two mix to AdbDma.
static void
vortex_connection_mix_mix_adbdma(vortex_t * vortex, int en,
				 unsigned char ch, unsigned char mix0,
				 unsigned char mix1, unsigned char adbdma)
{

	ADBRamLink routes[2];
	routes[0] =
	    (((mix0 +
	       OFFSET_MIXOUT) & ADB_MASK) << ADB_SHIFT) | (adbdma & ADB_MASK);
	routes[1] =
	    (((mix1 + OFFSET_MIXOUT) & ADB_MASK) << ADB_SHIFT) | ((adbdma +
								   0x20) &
								  ADB_MASK);
	if (en) {
		vortex_adb_addroutes(vortex, ch, routes, 0x2);
		vortex_mixer_addWTD(vortex, mix0, ch);
		vortex_mixer_addWTD(vortex, mix1, ch);
	} else {
		vortex_adb_delroutes(vortex, ch, routes[0], routes[1]);
		vortex_mixer_delWTD(vortex, mix0, ch);
		vortex_mixer_delWTD(vortex, mix1, ch);
	}
}
#endif

/* CODEC connect. */

static void
vortex_connect_codecplay(vortex_t * vortex, int en, unsigned char mixers[])
{
#ifdef CHIP_AU8820
	vortex_connection_mix_adb(vortex, en, 0x11, mixers[0], ADB_CODECOUT(0));
	vortex_connection_mix_adb(vortex, en, 0x11, mixers[1], ADB_CODECOUT(1));
#else
#if 1
	// Connect front channels through EQ.
	vortex_connection_mix_adb(vortex, en, 0x11, mixers[0], ADB_EQIN(0));
	vortex_connection_mix_adb(vortex, en, 0x11, mixers[1], ADB_EQIN(1));
	/* Lower volume, since EQ has some gain. */
	vortex_mix_setvolumebyte(vortex, mixers[0], 0);
	vortex_mix_setvolumebyte(vortex, mixers[1], 0);
	vortex_route(vortex, en, 0x11, ADB_EQOUT(0), ADB_CODECOUT(0));
	vortex_route(vortex, en, 0x11, ADB_EQOUT(1), ADB_CODECOUT(1));

	/* Check if reg 0x28 has SDAC bit set. */
	if (VORTEX_IS_QUAD(vortex)) {
		/* Rear channel. Note: ADB_CODECOUT(0+2) and (1+2) is for AC97 modem */
		vortex_connection_mix_adb(vortex, en, 0x11, mixers[2],
					  ADB_CODECOUT(0 + 4));
		vortex_connection_mix_adb(vortex, en, 0x11, mixers[3],
					  ADB_CODECOUT(1 + 4));
		/* printk(KERN_DEBUG "SDAC detected "); */
	}
#else
	// Use plain direct output to codec.
	vortex_connection_mix_adb(vortex, en, 0x11, mixers[0], ADB_CODECOUT(0));
	vortex_connection_mix_adb(vortex, en, 0x11, mixers[1], ADB_CODECOUT(1));
#endif
#endif
}

static void
vortex_connect_codecrec(vortex_t * vortex, int en, unsigned char mixin0,
			unsigned char mixin1)
{
	/*
	   Enable: 0x1, 0x1
	   Channel: 0x11, 0x11
	   ADB Source address: 0x48, 0x49
	   Destination Asp4Topology_0x9c,0x98
	 */
	vortex_connection_adb_mixin(vortex, en, 0x11, ADB_CODECIN(0), mixin0);
	vortex_connection_adb_mixin(vortex, en, 0x11, ADB_CODECIN(1), mixin1);
}

// Higher level ADB audio path (de)allocator.

/* Resource manager */
static int resnum[VORTEX_RESOURCE_LAST] =
    { NR_ADB, NR_SRC, NR_MIXIN, NR_MIXOUT, NR_A3D };
/*
 Checkout/Checkin resource of given type. 
 resmap: resource map to be used. If NULL means that we want to allocate
 a DMA resource (root of all other resources of a dma channel).
 out: Mean checkout if != 0. Else mean Checkin resource.
 restype: Indicates type of resource to be checked in or out.
*/
static char
vortex_adb_checkinout(vortex_t * vortex, int resmap[], int out, int restype)
{
	int i, qty = resnum[restype], resinuse = 0;

	if (out) {
		/* Gather used resources by all streams. */
		for (i = 0; i < NR_ADB; i++) {
			resinuse |= vortex->dma_adb[i].resources[restype];
		}
		resinuse |= vortex->fixed_res[restype];
		/* Find and take free resource. */
		for (i = 0; i < qty; i++) {
			if ((resinuse & (1 << i)) == 0) {
				if (resmap != NULL)
					resmap[restype] |= (1 << i);
				else
					vortex->dma_adb[i].resources[restype] |= (1 << i);
				/*
				printk(KERN_DEBUG
				       "vortex: ResManager: type %d out %d\n",
				       restype, i);
				*/
				return i;
			}
		}
	} else {
		if (resmap == NULL)
			return -EINVAL;
		/* Checkin first resource of type restype. */
		for (i = 0; i < qty; i++) {
			if (resmap[restype] & (1 << i)) {
				resmap[restype] &= ~(1 << i);
				/*
				printk(KERN_DEBUG
				       "vortex: ResManager: type %d in %d\n",
				       restype, i);
				*/
				return i;
			}
		}
	}
	printk(KERN_ERR "vortex: FATAL: ResManager: resource type %d exhausted.\n", restype);
	return -ENOMEM;
}

/* Default Connections  */

static void vortex_connect_default(vortex_t * vortex, int en)
{
	// Connect AC97 codec.
	vortex->mixplayb[0] = vortex_adb_checkinout(vortex, vortex->fixed_res, en,
				  VORTEX_RESOURCE_MIXOUT);
	vortex->mixplayb[1] = vortex_adb_checkinout(vortex, vortex->fixed_res, en,
				  VORTEX_RESOURCE_MIXOUT);
	if (VORTEX_IS_QUAD(vortex)) {
		vortex->mixplayb[2] = vortex_adb_checkinout(vortex, vortex->fixed_res, en,
					  VORTEX_RESOURCE_MIXOUT);
		vortex->mixplayb[3] = vortex_adb_checkinout(vortex, vortex->fixed_res, en,
					  VORTEX_RESOURCE_MIXOUT);
	}
	vortex_connect_codecplay(vortex, en, vortex->mixplayb);

	vortex->mixcapt[0] = vortex_adb_checkinout(vortex, vortex->fixed_res, en,
				  VORTEX_RESOURCE_MIXIN);
	vortex->mixcapt[1] = vortex_adb_checkinout(vortex, vortex->fixed_res, en,
				  VORTEX_RESOURCE_MIXIN);
	vortex_connect_codecrec(vortex, en, MIX_CAPT(0), MIX_CAPT(1));

	// Connect SPDIF
#ifndef CHIP_AU8820
	vortex->mixspdif[0] = vortex_adb_checkinout(vortex, vortex->fixed_res, en,
				  VORTEX_RESOURCE_MIXOUT);
	vortex->mixspdif[1] = vortex_adb_checkinout(vortex, vortex->fixed_res, en,
				  VORTEX_RESOURCE_MIXOUT);
	vortex_connection_mix_adb(vortex, en, 0x14, vortex->mixspdif[0],
				  ADB_SPDIFOUT(0));
	vortex_connection_mix_adb(vortex, en, 0x14, vortex->mixspdif[1],
				  ADB_SPDIFOUT(1));
#endif
	// Connect WT
#ifndef CHIP_AU8810
	vortex_wt_connect(vortex, en);
#endif
	// A3D (crosstalk canceler and A3D slices). AU8810 disabled for now.
#ifndef CHIP_AU8820
	vortex_Vort3D_connect(vortex, en);
#endif
	// Connect I2S

	// Connect DSP interface for SQ3500 turbo (not here i think...)

	// Connect AC98 modem codec
	
}

/*
  Allocate nr_ch pcm audio routes if dma < 0. If dma >= 0, existing routes
  are deallocated.
  dma: DMA engine routes to be deallocated when dma >= 0.
  nr_ch: Number of channels to be de/allocated.
  dir: direction of stream. Uses same values as substream->stream.
  type: Type of audio output/source (codec, spdif, i2s, dsp, etc)
  Return: Return allocated DMA or same DMA passed as "dma" when dma >= 0.
*/
static int
vortex_adb_allocroute(vortex_t *vortex, int dma, int nr_ch, int dir,
			int type, int subdev)
{
	stream_t *stream;
	int i, en;
	struct pcm_vol *p;
	
	if (dma >= 0) {
		en = 0;
		vortex_adb_checkinout(vortex,
				      vortex->dma_adb[dma].resources, en,
				      VORTEX_RESOURCE_DMA);
	} else {
		en = 1;
		if ((dma =
		     vortex_adb_checkinout(vortex, NULL, en,
					   VORTEX_RESOURCE_DMA)) < 0)
			return -EBUSY;
	}

	stream = &vortex->dma_adb[dma];
	stream->dma = dma;
	stream->dir = dir;
	stream->type = type;

	/* PLAYBACK ROUTES. */
	if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
		int src[4], mix[4], ch_top;
#ifndef CHIP_AU8820
		int a3d = 0;
#endif
		/* Get SRC and MIXER hardware resources. */
		if (stream->type != VORTEX_PCM_SPDIF) {
			for (i = 0; i < nr_ch; i++) {
				if ((src[i] = vortex_adb_checkinout(vortex,
							   stream->resources, en,
							   VORTEX_RESOURCE_SRC)) < 0) {
					memset(stream->resources, 0,
					       sizeof(unsigned char) *
					       VORTEX_RESOURCE_LAST);
					return -EBUSY;
				}
				if (stream->type != VORTEX_PCM_A3D) {
					if ((mix[i] = vortex_adb_checkinout(vortex,
								   stream->resources,
								   en,
								   VORTEX_RESOURCE_MIXIN)) < 0) {
						memset(stream->resources,
						       0,
						       sizeof(unsigned char) * VORTEX_RESOURCE_LAST);
						return -EBUSY;
					}
				}
			}
		}
#ifndef CHIP_AU8820
		if (stream->type == VORTEX_PCM_A3D) {
			if ((a3d =
			     vortex_adb_checkinout(vortex,
						   stream->resources, en,
						   VORTEX_RESOURCE_A3D)) < 0) {
				memset(stream->resources, 0,
				       sizeof(unsigned char) *
				       VORTEX_RESOURCE_LAST);
				printk(KERN_ERR "vortex: out of A3D sources. Sorry\n");
				return -EBUSY;
			}
			/* (De)Initialize A3D hardware source. */
			vortex_Vort3D_InitializeSource(&(vortex->a3d[a3d]), en);
		}
		/* Make SPDIF out exclusive to "spdif" device when in use. */
		if ((stream->type == VORTEX_PCM_SPDIF) && (en)) {
			vortex_route(vortex, 0, 0x14,
				     ADB_MIXOUT(vortex->mixspdif[0]),
				     ADB_SPDIFOUT(0));
			vortex_route(vortex, 0, 0x14,
				     ADB_MIXOUT(vortex->mixspdif[1]),
				     ADB_SPDIFOUT(1));
		}
#endif
		/* Make playback routes. */
		for (i = 0; i < nr_ch; i++) {
			if (stream->type == VORTEX_PCM_ADB) {
				vortex_connection_adbdma_src(vortex, en,
							     src[nr_ch - 1],
							     dma,
							     src[i]);
				vortex_connection_src_mixin(vortex, en,
							    0x11, src[i],
							    mix[i]);
				vortex_connection_mixin_mix(vortex, en,
							    mix[i],
							    MIX_PLAYB(i), 0);
#ifndef CHIP_AU8820
				vortex_connection_mixin_mix(vortex, en,
							    mix[i],
							    MIX_SPDIF(i % 2), 0);
				vortex_mix_setinputvolumebyte(vortex,
							      MIX_SPDIF(i % 2),
							      mix[i],
							      MIX_DEFIGAIN);
#endif
			}
#ifndef CHIP_AU8820
			if (stream->type == VORTEX_PCM_A3D) {
				vortex_connection_adbdma_src(vortex, en,
							     src[nr_ch - 1], 
								 dma,
							     src[i]);
				vortex_route(vortex, en, 0x11, ADB_SRCOUT(src[i]), ADB_A3DIN(a3d));
				/* XTalk test. */
				//vortex_route(vortex, en, 0x11, dma, ADB_XTALKIN(i?9:4));
				//vortex_route(vortex, en, 0x11, ADB_SRCOUT(src[i]), ADB_XTALKIN(i?4:9));
			}
			if (stream->type == VORTEX_PCM_SPDIF)
				vortex_route(vortex, en, 0x14,
					     ADB_DMA(stream->dma),
					     ADB_SPDIFOUT(i));
#endif
		}
		if (stream->type != VORTEX_PCM_SPDIF && stream->type != VORTEX_PCM_A3D) {
			ch_top = (VORTEX_IS_QUAD(vortex) ? 4 : 2);
			for (i = nr_ch; i < ch_top; i++) {
				vortex_connection_mixin_mix(vortex, en,
							    mix[i % nr_ch],
							    MIX_PLAYB(i), 0);
#ifndef CHIP_AU8820
				vortex_connection_mixin_mix(vortex, en,
							    mix[i % nr_ch],
							    MIX_SPDIF(i % 2),
								0);
				vortex_mix_setinputvolumebyte(vortex,
							      MIX_SPDIF(i % 2),
							      mix[i % nr_ch],
							      MIX_DEFIGAIN);
#endif
			}
			if (stream->type == VORTEX_PCM_ADB && en) {
				p = &vortex->pcm_vol[subdev];
				p->dma = dma;
				for (i = 0; i < nr_ch; i++)
					p->mixin[i] = mix[i];
				for (i = 0; i < ch_top; i++)
					p->vol[i] = 0;
			}
		}
#ifndef CHIP_AU8820
		else {
			if (nr_ch == 1 && stream->type == VORTEX_PCM_SPDIF)
				vortex_route(vortex, en, 0x14,
					     ADB_DMA(stream->dma),
					     ADB_SPDIFOUT(1));
		}
		/* Reconnect SPDIF out when "spdif" device is down. */
		if ((stream->type == VORTEX_PCM_SPDIF) && (!en)) {
			vortex_route(vortex, 1, 0x14,
				     ADB_MIXOUT(vortex->mixspdif[0]),
				     ADB_SPDIFOUT(0));
			vortex_route(vortex, 1, 0x14,
				     ADB_MIXOUT(vortex->mixspdif[1]),
				     ADB_SPDIFOUT(1));
		}
#endif
	/* CAPTURE ROUTES. */
	} else {
		int src[2], mix[2];

		/* Get SRC and MIXER hardware resources. */
		for (i = 0; i < nr_ch; i++) {
			if ((mix[i] =
			     vortex_adb_checkinout(vortex,
						   stream->resources, en,
						   VORTEX_RESOURCE_MIXOUT))
			    < 0) {
				memset(stream->resources, 0,
				       sizeof(unsigned char) *
				       VORTEX_RESOURCE_LAST);
				return -EBUSY;
			}
			if ((src[i] =
			     vortex_adb_checkinout(vortex,
						   stream->resources, en,
						   VORTEX_RESOURCE_SRC)) < 0) {
				memset(stream->resources, 0,
				       sizeof(unsigned char) *
				       VORTEX_RESOURCE_LAST);
				return -EBUSY;
			}
		}

		/* Make capture routes. */
		vortex_connection_mixin_mix(vortex, en, MIX_CAPT(0), mix[0], 0);
		vortex_connection_mix_src(vortex, en, 0x11, mix[0], src[0]);
		if (nr_ch == 1) {
			vortex_connection_mixin_mix(vortex, en,
						    MIX_CAPT(1), mix[0], 0);
			vortex_connection_src_adbdma(vortex, en,
						     src[0],
						     src[0], dma);
		} else {
			vortex_connection_mixin_mix(vortex, en,
						    MIX_CAPT(1), mix[1], 0);
			vortex_connection_mix_src(vortex, en, 0x11, mix[1],
						  src[1]);
			vortex_connection_src_src_adbdma(vortex, en,
							 src[1], src[0],
							 src[1], dma);
		}
	}
	vortex->dma_adb[dma].nr_ch = nr_ch;

#if 0
	/* AC97 Codec channel setup. FIXME: this has no effect on some cards !! */
	if (nr_ch < 4) {
		/* Copy stereo to rear channel (surround) */
		snd_ac97_write_cache(vortex->codec,
				     AC97_SIGMATEL_DAC2INVERT,
				     snd_ac97_read(vortex->codec,
						   AC97_SIGMATEL_DAC2INVERT)
				     | 4);
	} else {
		/* Allow separate front and rear channels. */
		snd_ac97_write_cache(vortex->codec,
				     AC97_SIGMATEL_DAC2INVERT,
				     snd_ac97_read(vortex->codec,
						   AC97_SIGMATEL_DAC2INVERT)
				     & ~((u32)
					 4));
	}
#endif
	return dma;
}

/*
 Set the SampleRate of the SRC's attached to the given DMA engine.
 */
static void
vortex_adb_setsrc(vortex_t * vortex, int adbdma, unsigned int rate, int dir)
{
	stream_t *stream = &(vortex->dma_adb[adbdma]);
	int i, cvrt;

	/* dir=1:play ; dir=0:rec */
	if (dir)
		cvrt = SRC_RATIO(rate, 48000);
	else
		cvrt = SRC_RATIO(48000, rate);

	/* Setup SRC's */
	for (i = 0; i < NR_SRC; i++) {
		if (stream->resources[VORTEX_RESOURCE_SRC] & (1 << i))
			vortex_src_setupchannel(vortex, i, cvrt, 0, 0, i, dir, 1, cvrt, dir);
	}
}

// Timer and ISR functions.

static void vortex_settimer(vortex_t * vortex, int period)
{
	//set the timer period to <period> 48000ths of a second.
	hwwrite(vortex->mmio, VORTEX_IRQ_STAT, period);
}

#if 0
static void vortex_enable_timer_int(vortex_t * card)
{
	hwwrite(card->mmio, VORTEX_IRQ_CTRL,
		hwread(card->mmio, VORTEX_IRQ_CTRL) | IRQ_TIMER | 0x60);
}

static void vortex_disable_timer_int(vortex_t * card)
{
	hwwrite(card->mmio, VORTEX_IRQ_CTRL,
		hwread(card->mmio, VORTEX_IRQ_CTRL) & ~IRQ_TIMER);
}

#endif
static void vortex_enable_int(vortex_t * card)
{
	// CAsp4ISR__EnableVortexInt_void_
	hwwrite(card->mmio, VORTEX_CTRL,
		hwread(card->mmio, VORTEX_CTRL) | CTRL_IRQ_ENABLE);
	hwwrite(card->mmio, VORTEX_IRQ_CTRL,
		(hwread(card->mmio, VORTEX_IRQ_CTRL) & 0xffffefc0) | 0x24);
}

static void vortex_disable_int(vortex_t * card)
{
	hwwrite(card->mmio, VORTEX_CTRL,
		hwread(card->mmio, VORTEX_CTRL) & ~CTRL_IRQ_ENABLE);
}

static irqreturn_t vortex_interrupt(int irq, void *dev_id)
{
	vortex_t *vortex = dev_id;
	int i, handled;
	u32 source;

	//check if the interrupt is ours.
	if (!(hwread(vortex->mmio, VORTEX_STAT) & 0x1))
		return IRQ_NONE;

	// This is the Interrupt Enable flag we set before (consistency check).
	if (!(hwread(vortex->mmio, VORTEX_CTRL) & CTRL_IRQ_ENABLE))
		return IRQ_NONE;

	source = hwread(vortex->mmio, VORTEX_IRQ_SOURCE);
	// Reset IRQ flags.
	hwwrite(vortex->mmio, VORTEX_IRQ_SOURCE, source);
	hwread(vortex->mmio, VORTEX_IRQ_SOURCE);
	// Is at least one IRQ flag set?
	if (source == 0) {
		printk(KERN_ERR "vortex: missing irq source\n");
		return IRQ_NONE;
	}

	handled = 0;
	// Attend every interrupt source.
	if (unlikely(source & IRQ_ERR_MASK)) {
		if (source & IRQ_FATAL) {
			printk(KERN_ERR "vortex: IRQ fatal error\n");
		}
		if (source & IRQ_PARITY) {
			printk(KERN_ERR "vortex: IRQ parity error\n");
		}
		if (source & IRQ_REG) {
			printk(KERN_ERR "vortex: IRQ reg error\n");
		}
		if (source & IRQ_FIFO) {
			printk(KERN_ERR "vortex: IRQ fifo error\n");
		}
		if (source & IRQ_DMA) {
			printk(KERN_ERR "vortex: IRQ dma error\n");
		}
		handled = 1;
	}
	if (source & IRQ_PCMOUT) {
		/* ALSA period acknowledge. */
		spin_lock(&vortex->lock);
		for (i = 0; i < NR_ADB; i++) {
			if (vortex->dma_adb[i].fifo_status == FIFO_START) {
				if (!vortex_adbdma_bufshift(vortex, i))
					continue;
				spin_unlock(&vortex->lock);
				snd_pcm_period_elapsed(vortex->dma_adb[i].
						       substream);
				spin_lock(&vortex->lock);
			}
		}
#ifndef CHIP_AU8810
		for (i = 0; i < NR_WT; i++) {
			if (vortex->dma_wt[i].fifo_status == FIFO_START) {
				/* FIXME: we ignore the return value from
				 * vortex_wtdma_bufshift() below as the delta
				 * calculation seems not working for wavetable
				 * by some reason
				 */
				vortex_wtdma_bufshift(vortex, i);
				spin_unlock(&vortex->lock);
				snd_pcm_period_elapsed(vortex->dma_wt[i].
						       substream);
				spin_lock(&vortex->lock);
			}
		}
#endif
		spin_unlock(&vortex->lock);
		handled = 1;
	}
	//Acknowledge the Timer interrupt
	if (source & IRQ_TIMER) {
		hwread(vortex->mmio, VORTEX_IRQ_STAT);
		handled = 1;
	}
	if ((source & IRQ_MIDI) && vortex->rmidi) {
		snd_mpu401_uart_interrupt(vortex->irq,
					  vortex->rmidi->private_data);
		handled = 1;
	}

	if (!handled) {
		printk(KERN_ERR "vortex: unknown irq source %x\n", source);
	}
	return IRQ_RETVAL(handled);
}

/* Codec */

#define POLL_COUNT 1000
static void vortex_codec_init(vortex_t * vortex)
{
	int i;

	for (i = 0; i < 32; i++) {
		/* the windows driver writes -i, so we write -i */
		hwwrite(vortex->mmio, (VORTEX_CODEC_CHN + (i << 2)), -i);
		msleep(2);
	}
	if (0) {
		hwwrite(vortex->mmio, VORTEX_CODEC_CTRL, 0x8068);
		msleep(1);
		hwwrite(vortex->mmio, VORTEX_CODEC_CTRL, 0x00e8);
		msleep(1);
	} else {
		hwwrite(vortex->mmio, VORTEX_CODEC_CTRL, 0x00a8);
		msleep(2);
		hwwrite(vortex->mmio, VORTEX_CODEC_CTRL, 0x80a8);
		msleep(2);
		hwwrite(vortex->mmio, VORTEX_CODEC_CTRL, 0x80e8);
		msleep(2);
		hwwrite(vortex->mmio, VORTEX_CODEC_CTRL, 0x80a8);
		msleep(2);
		hwwrite(vortex->mmio, VORTEX_CODEC_CTRL, 0x00a8);
		msleep(2);
		hwwrite(vortex->mmio, VORTEX_CODEC_CTRL, 0x00e8);
	}
	for (i = 0; i < 32; i++) {
		hwwrite(vortex->mmio, (VORTEX_CODEC_CHN + (i << 2)), -i);
		msleep(5);
	}
	hwwrite(vortex->mmio, VORTEX_CODEC_CTRL, 0xe8);
	msleep(1);
	/* Enable codec channels 0 and 1. */
	hwwrite(vortex->mmio, VORTEX_CODEC_EN,
		hwread(vortex->mmio, VORTEX_CODEC_EN) | EN_CODEC);
}

static void
vortex_codec_write(struct snd_ac97 * codec, unsigned short addr, unsigned short data)
{

	vortex_t *card = (vortex_t *) codec->private_data;
	unsigned int lifeboat = 0;

	/* wait for transactions to clear */
	while (!(hwread(card->mmio, VORTEX_CODEC_CTRL) & 0x100)) {
		udelay(100);
		if (lifeboat++ > POLL_COUNT) {
			printk(KERN_ERR "vortex: ac97 codec stuck busy\n");
			return;
		}
	}
	/* write register */
	hwwrite(card->mmio, VORTEX_CODEC_IO,
		((addr << VORTEX_CODEC_ADDSHIFT) & VORTEX_CODEC_ADDMASK) |
		((data << VORTEX_CODEC_DATSHIFT) & VORTEX_CODEC_DATMASK) |
		VORTEX_CODEC_WRITE |
		(codec->num << VORTEX_CODEC_ID_SHIFT) );

	/* Flush Caches. */
	hwread(card->mmio, VORTEX_CODEC_IO);
}

static unsigned short vortex_codec_read(struct snd_ac97 * codec, unsigned short addr)
{

	vortex_t *card = (vortex_t *) codec->private_data;
	u32 read_addr, data;
	unsigned lifeboat = 0;

	/* wait for transactions to clear */
	while (!(hwread(card->mmio, VORTEX_CODEC_CTRL) & 0x100)) {
		udelay(100);
		if (lifeboat++ > POLL_COUNT) {
			printk(KERN_ERR "vortex: ac97 codec stuck busy\n");
			return 0xffff;
		}
	}
	/* set up read address */
	read_addr = ((addr << VORTEX_CODEC_ADDSHIFT) & VORTEX_CODEC_ADDMASK) |
		(codec->num << VORTEX_CODEC_ID_SHIFT) ;
	hwwrite(card->mmio, VORTEX_CODEC_IO, read_addr);

	/* wait for address */
	do {
		udelay(100);
		data = hwread(card->mmio, VORTEX_CODEC_IO);
		if (lifeboat++ > POLL_COUNT) {
			printk(KERN_ERR "vortex: ac97 address never arrived\n");
			return 0xffff;
		}
	} while ((data & VORTEX_CODEC_ADDMASK) !=
		 (addr << VORTEX_CODEC_ADDSHIFT));

	/* return data. */
	return (u16) (data & VORTEX_CODEC_DATMASK);
}

/* SPDIF support  */

static void vortex_spdif_init(vortex_t * vortex, int spdif_sr, int spdif_mode)
{
	int i, this_38 = 0, this_04 = 0, this_08 = 0, this_0c = 0;

	/* CAsp4Spdif::InitializeSpdifHardware(void) */
	hwwrite(vortex->mmio, VORTEX_SPDIF_FLAGS,
		hwread(vortex->mmio, VORTEX_SPDIF_FLAGS) & 0xfff3fffd);
	//for (i=0x291D4; i<0x29200; i+=4)
	for (i = 0; i < 11; i++)
		hwwrite(vortex->mmio, VORTEX_SPDIF_CFG1 + (i << 2), 0);
	//hwwrite(vortex->mmio, 0x29190, hwread(vortex->mmio, 0x29190) | 0xc0000);
	hwwrite(vortex->mmio, VORTEX_CODEC_EN,
		hwread(vortex->mmio, VORTEX_CODEC_EN) | EN_SPDIF);

	/* CAsp4Spdif::ProgramSRCInHardware(enum  SPDIF_SR,enum  SPDIFMODE) */
	if (this_04 && this_08) {
		int edi;

		i = (((0x5DC00000 / spdif_sr) + 1) >> 1);
		if (i > 0x800) {
			if (i < 0x1ffff)
				edi = (i >> 1);
			else
				edi = 0x1ffff;
		} else {
			i = edi = 0x800;
		}
		/* this_04 and this_08 are the CASp4Src's (samplerate converters) */
		vortex_src_setupchannel(vortex, this_04, edi, 0, 1,
					this_0c, 1, 0, edi, 1);
		vortex_src_setupchannel(vortex, this_08, edi, 0, 1,
					this_0c, 1, 0, edi, 1);
	}

	i = spdif_sr;
	spdif_sr |= 0x8c;
	switch (i) {
	case 32000:
		this_38 &= 0xFFFFFFFE;
		this_38 &= 0xFFFFFFFD;
		this_38 &= 0xF3FFFFFF;
		this_38 |= 0x03000000;	/* set 32khz samplerate */
		this_38 &= 0xFFFFFF3F;
		spdif_sr &= 0xFFFFFFFD;
		spdif_sr |= 1;
		break;
	case 44100:
		this_38 &= 0xFFFFFFFE;
		this_38 &= 0xFFFFFFFD;
		this_38 &= 0xF0FFFFFF;
		this_38 |= 0x03000000;
		this_38 &= 0xFFFFFF3F;
		spdif_sr &= 0xFFFFFFFC;
		break;
	case 48000:
		if (spdif_mode == 1) {
			this_38 &= 0xFFFFFFFE;
			this_38 &= 0xFFFFFFFD;
			this_38 &= 0xF2FFFFFF;
			this_38 |= 0x02000000;	/* set 48khz samplerate */
			this_38 &= 0xFFFFFF3F;
		} else {
			/* J. Gordon Wolfe: I think this stuff is for AC3 */
			this_38 |= 0x00000003;
			this_38 &= 0xFFFFFFBF;
			this_38 |= 0x80;
		}
		spdif_sr |= 2;
		spdif_sr &= 0xFFFFFFFE;
		break;

	}
	/* looks like the next 2 lines transfer a 16-bit value into 2 8-bit 
	   registers. seems to be for the standard IEC/SPDIF initialization 
	   stuff */
	hwwrite(vortex->mmio, VORTEX_SPDIF_CFG0, this_38 & 0xffff);
	hwwrite(vortex->mmio, VORTEX_SPDIF_CFG1, this_38 >> 0x10);
	hwwrite(vortex->mmio, VORTEX_SPDIF_SMPRATE, spdif_sr);
}

/* Initialization */

static int vortex_core_init(vortex_t *vortex)
{

	printk(KERN_INFO "Vortex: init.... ");
	/* Hardware Init. */
	hwwrite(vortex->mmio, VORTEX_CTRL, 0xffffffff);
	msleep(5);
	hwwrite(vortex->mmio, VORTEX_CTRL,
		hwread(vortex->mmio, VORTEX_CTRL) & 0xffdfffff);
	msleep(5);
	/* Reset IRQ flags */
	hwwrite(vortex->mmio, VORTEX_IRQ_SOURCE, 0xffffffff);
	hwread(vortex->mmio, VORTEX_IRQ_STAT);

	vortex_codec_init(vortex);

#ifdef CHIP_AU8830
	hwwrite(vortex->mmio, VORTEX_CTRL,
		hwread(vortex->mmio, VORTEX_CTRL) | 0x1000000);
#endif

	/* Init audio engine. */
	vortex_adbdma_init(vortex);
	hwwrite(vortex->mmio, VORTEX_ENGINE_CTRL, 0x0);	//, 0xc83c7e58, 0xc5f93e58
	vortex_adb_init(vortex);
	/* Init processing blocks. */
	vortex_fifo_init(vortex);
	vortex_mixer_init(vortex);
	vortex_srcblock_init(vortex);
#ifndef CHIP_AU8820
	vortex_eq_init(vortex);
	vortex_spdif_init(vortex, 48000, 1);
	vortex_Vort3D_enable(vortex);
#endif
#ifndef CHIP_AU8810
	vortex_wt_init(vortex);
#endif
	// Moved to au88x0.c
	//vortex_connect_default(vortex, 1);

	vortex_settimer(vortex, 0x90);
	// Enable Interrupts.
	// vortex_enable_int() must be first !!
	//  hwwrite(vortex->mmio, VORTEX_IRQ_CTRL, 0);
	// vortex_enable_int(vortex);
	//vortex_enable_timer_int(vortex);
	//vortex_disable_timer_int(vortex);

	printk(KERN_INFO "done.\n");
	spin_lock_init(&vortex->lock);

	return 0;
}

static int vortex_core_shutdown(vortex_t * vortex)
{

	printk(KERN_INFO "Vortex: shutdown...");
#ifndef CHIP_AU8820
	vortex_eq_free(vortex);
	vortex_Vort3D_disable(vortex);
#endif
	//vortex_disable_timer_int(vortex);
	vortex_disable_int(vortex);
	vortex_connect_default(vortex, 0);
	/* Reset all DMA fifos. */
	vortex_fifo_init(vortex);
	/* Erase all audio routes. */
	vortex_adb_init(vortex);

	/* Disable MPU401 */
	//hwwrite(vortex->mmio, VORTEX_IRQ_CTRL, hwread(vortex->mmio, VORTEX_IRQ_CTRL) & ~IRQ_MIDI);
	//hwwrite(vortex->mmio, VORTEX_CTRL, hwread(vortex->mmio, VORTEX_CTRL) & ~CTRL_MIDI_EN);

	hwwrite(vortex->mmio, VORTEX_IRQ_CTRL, 0);
	hwwrite(vortex->mmio, VORTEX_CTRL, 0);
	msleep(5);
	hwwrite(vortex->mmio, VORTEX_IRQ_SOURCE, 0xffff);

	printk(KERN_INFO "done.\n");
	return 0;
}

/* Alsa support. */

static int vortex_alsafmt_aspfmt(int alsafmt)
{
	int fmt;

	switch (alsafmt) {
	case SNDRV_PCM_FORMAT_U8:
		fmt = 0x1;
		break;
	case SNDRV_PCM_FORMAT_MU_LAW:
		fmt = 0x2;
		break;
	case SNDRV_PCM_FORMAT_A_LAW:
		fmt = 0x3;
		break;
	case SNDRV_PCM_FORMAT_SPECIAL:
		fmt = 0x4;	/* guess. */
		break;
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
		fmt = 0x5;	/* guess. */
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		fmt = 0x8;
		break;
	case SNDRV_PCM_FORMAT_S16_BE:
		fmt = 0x9;	/* check this... */
		break;
	default:
		fmt = 0x8;
		printk(KERN_ERR "vortex: format unsupported %d\n", alsafmt);
		break;
	}
	return fmt;
}

/* Some not yet useful translations. */
#if 0
typedef enum {
	ASPFMTLINEAR16 = 0,	/* 0x8 */
	ASPFMTLINEAR8,		/* 0x1 */
	ASPFMTULAW,		/* 0x2 */
	ASPFMTALAW,		/* 0x3 */
	ASPFMTSPORT,		/* ? */
	ASPFMTSPDIF,		/* ? */
} ASPENCODING;

static int
vortex_translateformat(vortex_t * vortex, char bits, char nch, int encod)
{
	int a, this_194;

	if ((bits != 8) && (bits != 16))
		return -1;

	switch (encod) {
	case 0:
		if (bits == 0x10)
			a = 8;	// 16 bit
		break;
	case 1:
		if (bits == 8)
			a = 1;	// 8 bit
		break;
	case 2:
		a = 2;		// U_LAW
		break;
	case 3:
		a = 3;		// A_LAW
		break;
	}
	switch (nch) {
	case 1:
		this_194 = 0;
		break;
	case 2:
		this_194 = 1;
		break;
	case 4:
		this_194 = 1;
		break;
	case 6:
		this_194 = 1;
		break;
	}
	return (a);
}

static void vortex_cdmacore_setformat(vortex_t * vortex, int bits, int nch)
{
	short int d, this_148;

	d = ((bits >> 3) * nch);
	this_148 = 0xbb80 / d;
}
#endif
