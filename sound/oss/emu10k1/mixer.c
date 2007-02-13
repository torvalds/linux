/*
 **********************************************************************
 *     mixer.c - /dev/mixer interface for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999     Alan Cox        cleaned up stuff
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/fs.h>

#include "hwaccess.h"
#include "8010.h"
#include "recmgr.h"


static const u32 bass_table[41][5] = {
	{ 0x3e4f844f, 0x84ed4cc3, 0x3cc69927, 0x7b03553a, 0xc4da8486 },
	{ 0x3e69a17a, 0x84c280fb, 0x3cd77cd4, 0x7b2f2a6f, 0xc4b08d1d },
	{ 0x3e82ff42, 0x849991d5, 0x3ce7466b, 0x7b5917c6, 0xc48863ee },
	{ 0x3e9bab3c, 0x847267f0, 0x3cf5ffe8, 0x7b813560, 0xc461f22c },
	{ 0x3eb3b275, 0x844ced29, 0x3d03b295, 0x7ba79a1c, 0xc43d223b },
	{ 0x3ecb2174, 0x84290c8b, 0x3d106714, 0x7bcc5ba3, 0xc419dfa5 },
	{ 0x3ee2044b, 0x8406b244, 0x3d1c2561, 0x7bef8e77, 0xc3f8170f },
	{ 0x3ef86698, 0x83e5cb96, 0x3d26f4d8, 0x7c114600, 0xc3d7b625 },
	{ 0x3f0e5390, 0x83c646c9, 0x3d30dc39, 0x7c319498, 0xc3b8ab97 },
	{ 0x3f23d60b, 0x83a81321, 0x3d39e1af, 0x7c508b9c, 0xc39ae704 },
	{ 0x3f38f884, 0x838b20d2, 0x3d420ad2, 0x7c6e3b75, 0xc37e58f1 },
	{ 0x3f4dc52c, 0x836f60ef, 0x3d495cab, 0x7c8ab3a6, 0xc362f2be },
	{ 0x3f6245e8, 0x8354c565, 0x3d4fdbb8, 0x7ca602d6, 0xc348a69b },
	{ 0x3f76845f, 0x833b40ec, 0x3d558bf0, 0x7cc036df, 0xc32f677c },
	{ 0x3f8a8a03, 0x8322c6fb, 0x3d5a70c4, 0x7cd95cd7, 0xc317290b },
	{ 0x3f9e6014, 0x830b4bc3, 0x3d5e8d25, 0x7cf1811a, 0xc2ffdfa5 },
	{ 0x3fb20fae, 0x82f4c420, 0x3d61e37f, 0x7d08af56, 0xc2e9804a },
	{ 0x3fc5a1cc, 0x82df2592, 0x3d6475c3, 0x7d1ef294, 0xc2d40096 },
	{ 0x3fd91f55, 0x82ca6632, 0x3d664564, 0x7d345541, 0xc2bf56b9 },
	{ 0x3fec9120, 0x82b67cac, 0x3d675356, 0x7d48e138, 0xc2ab796e },
	{ 0x40000000, 0x82a36037, 0x3d67a012, 0x7d5c9fc9, 0xc2985fee },
	{ 0x401374c7, 0x8291088a, 0x3d672b93, 0x7d6f99c3, 0xc28601f2 },
	{ 0x4026f857, 0x827f6dd7, 0x3d65f559, 0x7d81d77c, 0xc27457a3 },
	{ 0x403a939f, 0x826e88c5, 0x3d63fc63, 0x7d9360d4, 0xc2635996 },
	{ 0x404e4faf, 0x825e5266, 0x3d613f32, 0x7da43d42, 0xc25300c6 },
	{ 0x406235ba, 0x824ec434, 0x3d5dbbc3, 0x7db473d7, 0xc243468e },
	{ 0x40764f1f, 0x823fd80c, 0x3d596f8f, 0x7dc40b44, 0xc23424a2 },
	{ 0x408aa576, 0x82318824, 0x3d545787, 0x7dd309e2, 0xc2259509 },
	{ 0x409f4296, 0x8223cf0b, 0x3d4e7012, 0x7de175b5, 0xc2179218 },
	{ 0x40b430a0, 0x8216a7a1, 0x3d47b505, 0x7def5475, 0xc20a1670 },
	{ 0x40c97a0a, 0x820a0d12, 0x3d4021a1, 0x7dfcab8d, 0xc1fd1cf5 },
	{ 0x40df29a6, 0x81fdfad6, 0x3d37b08d, 0x7e098028, 0xc1f0a0ca },
	{ 0x40f54ab1, 0x81f26ca9, 0x3d2e5bd1, 0x7e15d72b, 0xc1e49d52 },
	{ 0x410be8da, 0x81e75e89, 0x3d241cce, 0x7e21b544, 0xc1d90e24 },
	{ 0x41231051, 0x81dcccb3, 0x3d18ec37, 0x7e2d1ee6, 0xc1cdef10 },
	{ 0x413acdd0, 0x81d2b39e, 0x3d0cc20a, 0x7e38184e, 0xc1c33c13 },
	{ 0x41532ea7, 0x81c90ffb, 0x3cff9585, 0x7e42a58b, 0xc1b8f15a },
	{ 0x416c40cd, 0x81bfdeb2, 0x3cf15d21, 0x7e4cca7c, 0xc1af0b3f },
	{ 0x418612ea, 0x81b71cdc, 0x3ce20e85, 0x7e568ad3, 0xc1a58640 },
	{ 0x41a0b465, 0x81aec7c5, 0x3cd19e7c, 0x7e5fea1e, 0xc19c5f03 },
	{ 0x41bc3573, 0x81a6dcea, 0x3cc000e9, 0x7e68ebc2, 0xc1939250 }
};

static const u32 treble_table[41][5] = {
	{ 0x0125cba9, 0xfed5debd, 0x00599b6c, 0x0d2506da, 0xfa85b354 },
	{ 0x0142f67e, 0xfeb03163, 0x0066cd0f, 0x0d14c69d, 0xfa914473 },
	{ 0x016328bd, 0xfe860158, 0x0075b7f2, 0x0d03eb27, 0xfa9d32d2 },
	{ 0x0186b438, 0xfe56c982, 0x00869234, 0x0cf27048, 0xfaa97fca },
	{ 0x01adf358, 0xfe21f5fe, 0x00999842, 0x0ce051c2, 0xfab62ca5 },
	{ 0x01d949fa, 0xfde6e287, 0x00af0d8d, 0x0ccd8b4a, 0xfac33aa7 },
	{ 0x02092669, 0xfda4d8bf, 0x00c73d4c, 0x0cba1884, 0xfad0ab07 },
	{ 0x023e0268, 0xfd5b0e4a, 0x00e27b54, 0x0ca5f509, 0xfade7ef2 },
	{ 0x0278645c, 0xfd08a2b0, 0x01012509, 0x0c911c63, 0xfaecb788 },
	{ 0x02b8e091, 0xfcac9d1a, 0x0123a262, 0x0c7b8a14, 0xfafb55df },
	{ 0x03001a9a, 0xfc45e9ce, 0x014a6709, 0x0c65398f, 0xfb0a5aff },
	{ 0x034ec6d7, 0xfbd3576b, 0x0175f397, 0x0c4e2643, 0xfb19c7e4 },
	{ 0x03a5ac15, 0xfb5393ee, 0x01a6d6ed, 0x0c364b94, 0xfb299d7c },
	{ 0x0405a562, 0xfac52968, 0x01ddafae, 0x0c1da4e2, 0xfb39dca5 },
	{ 0x046fa3fe, 0xfa267a66, 0x021b2ddd, 0x0c042d8d, 0xfb4a8631 },
	{ 0x04e4b17f, 0xf975be0f, 0x0260149f, 0x0be9e0f2, 0xfb5b9ae0 },
	{ 0x0565f220, 0xf8b0fbe5, 0x02ad3c29, 0x0bceba73, 0xfb6d1b60 },
	{ 0x05f4a745, 0xf7d60722, 0x030393d4, 0x0bb2b578, 0xfb7f084d },
	{ 0x06923236, 0xf6e279bd, 0x03642465, 0x0b95cd75, 0xfb916233 },
	{ 0x07401713, 0xf5d3aef9, 0x03d01283, 0x0b77fded, 0xfba42984 },
	{ 0x08000000, 0xf4a6bd88, 0x0448a161, 0x0b594278, 0xfbb75e9f },
	{ 0x08d3c097, 0xf3587131, 0x04cf35a4, 0x0b3996c9, 0xfbcb01cb },
	{ 0x09bd59a2, 0xf1e543f9, 0x05655880, 0x0b18f6b2, 0xfbdf1333 },
	{ 0x0abefd0f, 0xf04956ca, 0x060cbb12, 0x0af75e2c, 0xfbf392e8 },
	{ 0x0bdb123e, 0xee806984, 0x06c739fe, 0x0ad4c962, 0xfc0880dd },
	{ 0x0d143a94, 0xec85d287, 0x0796e150, 0x0ab134b0, 0xfc1ddce5 },
	{ 0x0e6d5664, 0xea547598, 0x087df0a0, 0x0a8c9cb6, 0xfc33a6ad },
	{ 0x0fe98a2a, 0xe7e6ba35, 0x097edf83, 0x0a66fe5b, 0xfc49ddc2 },
	{ 0x118c4421, 0xe536813a, 0x0a9c6248, 0x0a4056d7, 0xfc608185 },
	{ 0x1359422e, 0xe23d19eb, 0x0bd96efb, 0x0a18a3bf, 0xfc77912c },
	{ 0x1554982b, 0xdef33645, 0x0d3942bd, 0x09efe312, 0xfc8f0bc1 },
	{ 0x1782b68a, 0xdb50deb1, 0x0ebf676d, 0x09c6133f, 0xfca6f019 },
	{ 0x19e8715d, 0xd74d64fd, 0x106fb999, 0x099b3337, 0xfcbf3cd6 },
	{ 0x1c8b07b8, 0xd2df56ab, 0x124e6ec8, 0x096f4274, 0xfcd7f060 },
	{ 0x1f702b6d, 0xcdfc6e92, 0x14601c10, 0x0942410b, 0xfcf108e5 },
	{ 0x229e0933, 0xc89985cd, 0x16a9bcfa, 0x09142fb5, 0xfd0a8451 },
	{ 0x261b5118, 0xc2aa8409, 0x1930bab6, 0x08e50fdc, 0xfd24604d },
	{ 0x29ef3f5d, 0xbc224f28, 0x1bfaf396, 0x08b4e3aa, 0xfd3e9a3b },
	{ 0x2e21a59b, 0xb4f2ba46, 0x1f0ec2d6, 0x0883ae15, 0xfd592f33 },
	{ 0x32baf44b, 0xad0c7429, 0x227308a3, 0x085172eb, 0xfd741bfd },
	{ 0x37c4448b, 0xa45ef51d, 0x262f3267, 0x081e36dc, 0xfd8f5d14 }
};


static void set_bass(struct emu10k1_card *card, int l, int r)
{
	int i;

	l = (l * 40 + 50) / 100;
	r = (r * 40 + 50) / 100;

	for (i = 0; i < 5; i++)
		sblive_writeptr(card, (card->is_audigy ? A_GPR_BASE : GPR_BASE) + card->mgr.ctrl_gpr[SOUND_MIXER_BASS][0] + i, 0, bass_table[l][i]);
}

static void set_treble(struct emu10k1_card *card, int l, int r)
{
	int i;

	l = (l * 40 + 50) / 100;
	r = (r * 40 + 50) / 100;

	for (i = 0; i < 5; i++)
		sblive_writeptr(card, (card->is_audigy ? A_GPR_BASE : GPR_BASE) + card->mgr.ctrl_gpr[SOUND_MIXER_TREBLE][0] + i , 0, treble_table[l][i]);
}

const char volume_params[SOUND_MIXER_NRDEVICES]= {
/* Used by the ac97 driver */
	[SOUND_MIXER_VOLUME]	=	VOL_6BIT,
	[SOUND_MIXER_BASS]	=	VOL_4BIT,
	[SOUND_MIXER_TREBLE]	=	VOL_4BIT,
	[SOUND_MIXER_PCM]	=	VOL_5BIT,
	[SOUND_MIXER_SPEAKER]	=	VOL_4BIT,
	[SOUND_MIXER_LINE]	=	VOL_5BIT,
	[SOUND_MIXER_MIC]	=	VOL_5BIT,
	[SOUND_MIXER_CD]	=	VOL_5BIT,
	[SOUND_MIXER_ALTPCM]	=	VOL_6BIT,
	[SOUND_MIXER_IGAIN]	=	VOL_4BIT,
	[SOUND_MIXER_LINE1]	=	VOL_5BIT,
	[SOUND_MIXER_PHONEIN]	= 	VOL_5BIT,
	[SOUND_MIXER_PHONEOUT]	= 	VOL_6BIT,
	[SOUND_MIXER_VIDEO]	=	VOL_5BIT,
/* Not used by the ac97 driver */
	[SOUND_MIXER_SYNTH]	=	VOL_5BIT,
	[SOUND_MIXER_IMIX]	=	VOL_5BIT,
	[SOUND_MIXER_RECLEV]	=	VOL_5BIT,
	[SOUND_MIXER_OGAIN]	=	VOL_5BIT,
	[SOUND_MIXER_LINE2]	=	VOL_5BIT,
	[SOUND_MIXER_LINE3]	=	VOL_5BIT,
	[SOUND_MIXER_DIGITAL1]	=	VOL_5BIT,
	[SOUND_MIXER_DIGITAL2]	=	VOL_5BIT,
	[SOUND_MIXER_DIGITAL3]	=	VOL_5BIT,
	[SOUND_MIXER_RADIO]	=	VOL_5BIT,
	[SOUND_MIXER_MONITOR]	=	VOL_5BIT
};

/* Mixer file operations */
static int emu10k1_private_mixer(struct emu10k1_card *card, unsigned int cmd, unsigned long arg)
{
	struct mixer_private_ioctl *ctl;
	struct dsp_patch *patch;
	u32 size, page;
	int addr, size_reg, i, ret;
	unsigned int id, ch;
	void __user *argp = (void __user *)arg;

	switch (cmd) {

	case SOUND_MIXER_PRIVATE3:

		ctl = kmalloc(sizeof(struct mixer_private_ioctl), GFP_KERNEL);
		if (ctl == NULL)
			return -ENOMEM;

		if (copy_from_user(ctl, argp, sizeof(struct mixer_private_ioctl))) {
			kfree(ctl);
			return -EFAULT;
		}

		ret = 0;
		switch (ctl->cmd) {
#ifdef DBGEMU
		case CMD_WRITEFN0:
			emu10k1_writefn0_2(card, ctl->val[0], ctl->val[1], ctl->val[2]);
			break;
#endif
		case CMD_WRITEPTR:
#ifdef DBGEMU
			if (ctl->val[1] >= 0x40 || ctl->val[0] >= 0x1000) {
#else
			if (ctl->val[1] >= 0x40 || ctl->val[0] >= 0x1000 || ((ctl->val[0] < 0x100 ) &&
		    //Any register allowed raw access goes here:
				     (ctl->val[0] != A_SPDIF_SAMPLERATE) && (ctl->val[0] != A_DBG)
			)
				) {
#endif
				ret = -EINVAL;
				break;
			}
			sblive_writeptr(card, ctl->val[0], ctl->val[1], ctl->val[2]);
			break;

		case CMD_READFN0:
			ctl->val[2] = emu10k1_readfn0(card, ctl->val[0]);

			if (copy_to_user(argp, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_READPTR:
			if (ctl->val[1] >= 0x40 || (ctl->val[0] & 0x7ff) > 0xff) {
				ret = -EINVAL;
				break;
			}

			if ((ctl->val[0] & 0x7ff) > 0x3f)
				ctl->val[1] = 0x00;

			ctl->val[2] = sblive_readptr(card, ctl->val[0], ctl->val[1]);

			if (copy_to_user(argp, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_SETRECSRC:
			switch (ctl->val[0]) {
			case WAVERECORD_AC97:
				if (card->is_aps) {
					ret = -EINVAL;
					break;
				}

				card->wavein.recsrc = WAVERECORD_AC97;
				break;

			case WAVERECORD_MIC:
				card->wavein.recsrc = WAVERECORD_MIC;
				break;

			case WAVERECORD_FX:
				card->wavein.recsrc = WAVERECORD_FX;
				card->wavein.fxwc = ctl->val[1] & 0xffff;

				if (!card->wavein.fxwc)
					ret = -EINVAL;

				break;

			default:
				ret = -EINVAL;
				break;
			}
			break;

		case CMD_GETRECSRC:
			ctl->val[0] = card->wavein.recsrc;
			ctl->val[1] = card->wavein.fxwc;
			if (copy_to_user(argp, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_GETVOICEPARAM:
			ctl->val[0] = card->waveout.send_routing[0];
			ctl->val[1] = card->waveout.send_dcba[0];

			ctl->val[2] = card->waveout.send_routing[1];
			ctl->val[3] = card->waveout.send_dcba[1];

			ctl->val[4] = card->waveout.send_routing[2];
			ctl->val[5] = card->waveout.send_dcba[2];

			if (copy_to_user(argp, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_SETVOICEPARAM:
			card->waveout.send_routing[0] = ctl->val[0];
			card->waveout.send_dcba[0] = ctl->val[1];

			card->waveout.send_routing[1] = ctl->val[2];
			card->waveout.send_dcba[1] = ctl->val[3];

			card->waveout.send_routing[2] = ctl->val[4];
			card->waveout.send_dcba[2] = ctl->val[5];

			break;
		
		case CMD_SETMCH_FX:
			card->mchannel_fx = ctl->val[0] & 0x000f;
			break;
		
		case CMD_GETPATCH:
			if (ctl->val[0] == 0) {
				if (copy_to_user(argp, &card->mgr.rpatch, sizeof(struct dsp_rpatch)))
                                	ret = -EFAULT;
			} else {
				if ((ctl->val[0] - 1) / PATCHES_PER_PAGE >= card->mgr.current_pages) {
					ret = -EINVAL;
					break;
				}

				if (copy_to_user(argp, PATCH(&card->mgr, ctl->val[0] - 1), sizeof(struct dsp_patch)))
					ret = -EFAULT;
			}

			break;

		case CMD_GETGPR:
			id = ctl->val[0];

			if (id > NUM_GPRS) {
				ret = -EINVAL;
				break;
			}

			if (copy_to_user(argp, &card->mgr.gpr[id], sizeof(struct dsp_gpr)))
				ret = -EFAULT;

			break;

		case CMD_GETCTLGPR:
			addr = emu10k1_find_control_gpr(&card->mgr, (char *) ctl->val, &((char *) ctl->val)[PATCH_NAME_SIZE]);
			ctl->val[0] = sblive_readptr(card, addr, 0);

			if (copy_to_user(argp, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_SETPATCH:
			if (ctl->val[0] == 0)
				memcpy(&card->mgr.rpatch, &ctl->val[1], sizeof(struct dsp_rpatch));
			else {
				page = (ctl->val[0] - 1) / PATCHES_PER_PAGE;
				if (page > MAX_PATCHES_PAGES) {
					ret = -EINVAL;
					break;
				}

				if (page >= card->mgr.current_pages) {
					for (i = card->mgr.current_pages; i < page + 1; i++) {
				                card->mgr.patch[i] = (void *)__get_free_page(GFP_KERNEL);
						if(card->mgr.patch[i] == NULL) {
							card->mgr.current_pages = i;
							ret = -ENOMEM;
							break;
						}
						memset(card->mgr.patch[i], 0, PAGE_SIZE);
					}
					card->mgr.current_pages = page + 1;
				}

				patch = PATCH(&card->mgr, ctl->val[0] - 1);

				memcpy(patch, &ctl->val[1], sizeof(struct dsp_patch));

				if (patch->code_size == 0) {
					for(i = page + 1; i < card->mgr.current_pages; i++)
                                                free_page((unsigned long) card->mgr.patch[i]);

					card->mgr.current_pages = page + 1;
				}
			}
			break;

		case CMD_SETGPR:
			if (ctl->val[0] > NUM_GPRS) {
				ret = -EINVAL;
				break;
			}

			memcpy(&card->mgr.gpr[ctl->val[0]], &ctl->val[1], sizeof(struct dsp_gpr));
			break;

		case CMD_SETCTLGPR:
			addr = emu10k1_find_control_gpr(&card->mgr, (char *) ctl->val, (char *) ctl->val + PATCH_NAME_SIZE);
			emu10k1_set_control_gpr(card, addr, *((s32 *)((char *) ctl->val + 2 * PATCH_NAME_SIZE)), 0);
			break;

		case CMD_SETGPOUT:
			if ( ((ctl->val[0] > 2) && (!card->is_audigy))
			     || (ctl->val[0] > 15) || ctl->val[1] > 1) {
				ret= -EINVAL;
				break;
			}

			if (card->is_audigy)
				emu10k1_writefn0(card, (1 << 24) | ((ctl->val[0]) << 16) | A_IOCFG, ctl->val[1]);
			else
				emu10k1_writefn0(card, (1 << 24) | (((ctl->val[0]) + 10) << 16) | HCFG, ctl->val[1]);
			break;

		case CMD_GETGPR2OSS:
			id = ctl->val[0];
			ch = ctl->val[1];

			if (id >= SOUND_MIXER_NRDEVICES || ch >= 2) {
				ret = -EINVAL;
				break;
			}

			ctl->val[2] = card->mgr.ctrl_gpr[id][ch];

			if (copy_to_user(argp, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;

			break;

		case CMD_SETGPR2OSS:
			id = ctl->val[0];
			/* 0 == left, 1 == right */
			ch = ctl->val[1];
			addr = ctl->val[2];

			if (id >= SOUND_MIXER_NRDEVICES || ch >= 2) {
				ret = -EINVAL;
				break;
			}

			card->mgr.ctrl_gpr[id][ch] = addr;

			if (card->is_aps)
				break;

			if (addr >= 0) {
				unsigned int state = card->ac97->mixer_state[id];

				if (ch == 1) {
					state >>= 8;
					card->ac97->stereo_mixers |= (1 << id);
				}

				card->ac97->supported_mixers |= (1 << id);

				if (id == SOUND_MIXER_TREBLE) {
					set_treble(card, card->ac97->mixer_state[id] & 0xff, (card->ac97->mixer_state[id] >> 8) & 0xff);
				} else if (id == SOUND_MIXER_BASS) {
					set_bass(card, card->ac97->mixer_state[id] & 0xff, (card->ac97->mixer_state[id] >> 8) & 0xff);
				} else
					emu10k1_set_volume_gpr(card, addr, state & 0xff,
							       volume_params[id]);
			} else {
				card->ac97->stereo_mixers &= ~(1 << id);
				card->ac97->stereo_mixers |= card->ac97_stereo_mixers;

				if (ch == 0) {
					card->ac97->supported_mixers &= ~(1 << id);
					card->ac97->supported_mixers |= card->ac97_supported_mixers;
				}
			}
			break;

		case CMD_SETPASSTHROUGH:
			card->pt.selected = ctl->val[0] ? 1 : 0;
			if (card->pt.state != PT_STATE_INACTIVE)
				break;

			card->pt.spcs_to_use = ctl->val[0] & 0x07;
			break;

		case CMD_PRIVATE3_VERSION:
			ctl->val[0] = PRIVATE3_VERSION;	//private3 version
			ctl->val[1] = MAJOR_VER;	//major driver version
			ctl->val[2] = MINOR_VER;	//minor driver version
			ctl->val[3] = card->is_audigy;	//1=card is audigy

			if (card->is_audigy)
				ctl->val[4]=emu10k1_readfn0(card, 0x18);

			if (copy_to_user(argp, ctl, sizeof(struct mixer_private_ioctl)))
				ret = -EFAULT;
			break;

		case CMD_AC97_BOOST:
			if (ctl->val[0])
				emu10k1_ac97_write(card->ac97, 0x18, 0x0);	
			else
				emu10k1_ac97_write(card->ac97, 0x18, 0x0808);
			break;
		default:
			ret = -EINVAL;
			break;
		}

		kfree(ctl);
		return ret;
		break;

	case SOUND_MIXER_PRIVATE4:

		if (copy_from_user(&size, argp, sizeof(size)))
			return -EFAULT;

		DPD(2, "External tram size %#x\n", size);

		if (size > 0x1fffff)
			return -EINVAL;

		size_reg = 0;

		if (size != 0) {
			size = (size - 1) >> 14;

			while (size) {
				size >>= 1;
				size_reg++;
			}

			size = 0x4000 << size_reg;
		}

		DPD(2, "External tram size %#x %#x\n", size, size_reg);

		if (size != card->tankmem.size) {
			if (card->tankmem.size > 0) {
				emu10k1_writefn0(card, HCFG_LOCKTANKCACHE, 1);

				sblive_writeptr_tag(card, 0, TCB, 0, TCBS, 0, TAGLIST_END);

				pci_free_consistent(card->pci_dev, card->tankmem.size, card->tankmem.addr, card->tankmem.dma_handle);

				card->tankmem.size = 0;
			}

			if (size != 0) {
				card->tankmem.addr = pci_alloc_consistent(card->pci_dev, size, &card->tankmem.dma_handle);
				if (card->tankmem.addr == NULL)
					return -ENOMEM;

				card->tankmem.size = size;

				sblive_writeptr_tag(card, 0, TCB, (u32) card->tankmem.dma_handle, TCBS,(u32) size_reg, TAGLIST_END);

				emu10k1_writefn0(card, HCFG_LOCKTANKCACHE, 0);
			}
		}
		return 0;
		break;

	default:
		break;
	}

	return -EINVAL;
}

static int emu10k1_dsp_mixer(struct emu10k1_card *card, unsigned int oss_mixer, unsigned long arg)
{
	unsigned int left, right;
	int val;
	int scale;

	card->ac97->modcnt++;

	if (get_user(val, (int __user *)arg))
		return -EFAULT;

	/* cleanse input a little */
	right = ((val >> 8)  & 0xff);
	left = (val  & 0xff);

	if (right > 100) right = 100;
	if (left > 100) left = 100;

	card->ac97->mixer_state[oss_mixer] = (right << 8) | left;
	if (oss_mixer == SOUND_MIXER_TREBLE) {
		set_treble(card, left, right);
		return 0;
	} if (oss_mixer == SOUND_MIXER_BASS) {
		set_bass(card, left, right);
		return 0;
	}

	if (oss_mixer == SOUND_MIXER_VOLUME)
		scale = 1 << card->ac97->bit_resolution;
	else
		scale = volume_params[oss_mixer];

	emu10k1_set_volume_gpr(card, card->mgr.ctrl_gpr[oss_mixer][0], left, scale);
	emu10k1_set_volume_gpr(card, card->mgr.ctrl_gpr[oss_mixer][1], right, scale);

	if (card->ac97_supported_mixers & (1 << oss_mixer))
		card->ac97->write_mixer(card->ac97, oss_mixer, left, right);

	return 0;
}

static int emu10k1_mixer_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;
	struct emu10k1_card *card = file->private_data;
	unsigned int oss_mixer = _IOC_NR(cmd);
	
	ret = -EINVAL;
	if (!card->is_aps) {
		if (cmd == SOUND_MIXER_INFO) {
			mixer_info info;

			strlcpy(info.id, card->ac97->name, sizeof(info.id));

			if (card->is_audigy)
				strlcpy(info.name, "Audigy - Emu10k1", sizeof(info.name));
			else
				strlcpy(info.name, "Creative SBLive - Emu10k1", sizeof(info.name));
				
			info.modify_counter = card->ac97->modcnt;

			if (copy_to_user((void __user *)arg, &info, sizeof(info)))
				return -EFAULT;

			return 0;
		}

		if ((_SIOC_DIR(cmd) == (_SIOC_WRITE|_SIOC_READ)) && oss_mixer <= SOUND_MIXER_NRDEVICES)
			ret = emu10k1_dsp_mixer(card, oss_mixer, arg);
		else
			ret = card->ac97->mixer_ioctl(card->ac97, cmd, arg);
	}
	
	if (ret < 0)
		ret = emu10k1_private_mixer(card, cmd, arg);

	return ret;
}

static int emu10k1_mixer_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct emu10k1_card *card = NULL;
	struct list_head *entry;

	DPF(4, "emu10k1_mixer_open()\n");

	list_for_each(entry, &emu10k1_devs) {
		card = list_entry(entry, struct emu10k1_card, list);

		if (card->ac97->dev_mixer == minor)
			goto match;
	}

	return -ENODEV;

      match:
	file->private_data = card;
	return 0;
}

static int emu10k1_mixer_release(struct inode *inode, struct file *file)
{
	DPF(4, "emu10k1_mixer_release()\n");
	return 0;
}

const struct file_operations emu10k1_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= emu10k1_mixer_ioctl,
	.open		= emu10k1_mixer_open,
	.release	= emu10k1_mixer_release,
};
