/*
 * HDMI Channel map support helpers
 */

#include <linux/module.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/hda_chmap.h>

/*
 * CEA speaker placement:
 *
 *        FLH       FCH        FRH
 *  FLW    FL  FLC   FC   FRC   FR   FRW
 *
 *                                  LFE
 *                     TC
 *
 *          RL  RLC   RC   RRC   RR
 *
 * The Left/Right Surround channel _notions_ LS/RS in SMPTE 320M corresponds to
 * CEA RL/RR; The SMPTE channel _assignment_ C/LFE is swapped to CEA LFE/FC.
 */
enum cea_speaker_placement {
	FL  = (1 <<  0),	/* Front Left           */
	FC  = (1 <<  1),	/* Front Center         */
	FR  = (1 <<  2),	/* Front Right          */
	FLC = (1 <<  3),	/* Front Left Center    */
	FRC = (1 <<  4),	/* Front Right Center   */
	RL  = (1 <<  5),	/* Rear Left            */
	RC  = (1 <<  6),	/* Rear Center          */
	RR  = (1 <<  7),	/* Rear Right           */
	RLC = (1 <<  8),	/* Rear Left Center     */
	RRC = (1 <<  9),	/* Rear Right Center    */
	LFE = (1 << 10),	/* Low Frequency Effect */
	FLW = (1 << 11),	/* Front Left Wide      */
	FRW = (1 << 12),	/* Front Right Wide     */
	FLH = (1 << 13),	/* Front Left High      */
	FCH = (1 << 14),	/* Front Center High    */
	FRH = (1 << 15),	/* Front Right High     */
	TC  = (1 << 16),	/* Top Center           */
};

static const char * const cea_speaker_allocation_names[] = {
	/*  0 */ "FL/FR",
	/*  1 */ "LFE",
	/*  2 */ "FC",
	/*  3 */ "RL/RR",
	/*  4 */ "RC",
	/*  5 */ "FLC/FRC",
	/*  6 */ "RLC/RRC",
	/*  7 */ "FLW/FRW",
	/*  8 */ "FLH/FRH",
	/*  9 */ "TC",
	/* 10 */ "FCH",
};

/*
 * ELD SA bits in the CEA Speaker Allocation data block
 */
static int eld_speaker_allocation_bits[] = {
	[0] = FL | FR,
	[1] = LFE,
	[2] = FC,
	[3] = RL | RR,
	[4] = RC,
	[5] = FLC | FRC,
	[6] = RLC | RRC,
	/* the following are not defined in ELD yet */
	[7] = FLW | FRW,
	[8] = FLH | FRH,
	[9] = TC,
	[10] = FCH,
};

/*
 * ALSA sequence is:
 *
 *       surround40   surround41   surround50   surround51   surround71
 * ch0   front left   =            =            =            =
 * ch1   front right  =            =            =            =
 * ch2   rear left    =            =            =            =
 * ch3   rear right   =            =            =            =
 * ch4                LFE          center       center       center
 * ch5                                          LFE          LFE
 * ch6                                                       side left
 * ch7                                                       side right
 *
 * surround71 = {FL, FR, RLC, RRC, FC, LFE, RL, RR}
 */
static int hdmi_channel_mapping[0x32][8] = {
	/* stereo */
	[0x00] = { 0x00, 0x11, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7 },
	/* 2.1 */
	[0x01] = { 0x00, 0x11, 0x22, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7 },
	/* Dolby Surround */
	[0x02] = { 0x00, 0x11, 0x23, 0xf2, 0xf4, 0xf5, 0xf6, 0xf7 },
	/* surround40 */
	[0x08] = { 0x00, 0x11, 0x24, 0x35, 0xf3, 0xf2, 0xf6, 0xf7 },
	/* 4ch */
	[0x03] = { 0x00, 0x11, 0x23, 0x32, 0x44, 0xf5, 0xf6, 0xf7 },
	/* surround41 */
	[0x09] = { 0x00, 0x11, 0x24, 0x35, 0x42, 0xf3, 0xf6, 0xf7 },
	/* surround50 */
	[0x0a] = { 0x00, 0x11, 0x24, 0x35, 0x43, 0xf2, 0xf6, 0xf7 },
	/* surround51 */
	[0x0b] = { 0x00, 0x11, 0x24, 0x35, 0x43, 0x52, 0xf6, 0xf7 },
	/* 7.1 */
	[0x13] = { 0x00, 0x11, 0x26, 0x37, 0x43, 0x52, 0x64, 0x75 },
};

/*
 * This is an ordered list!
 *
 * The preceding ones have better chances to be selected by
 * hdmi_channel_allocation().
 */
static struct hdac_cea_channel_speaker_allocation channel_allocations[] = {
/*			  channel:   7     6    5    4    3     2    1    0  */
{ .ca_index = 0x00,  .speakers = {   0,    0,   0,   0,   0,    0,  FR,  FL } },
				 /* 2.1 */
{ .ca_index = 0x01,  .speakers = {   0,    0,   0,   0,   0,  LFE,  FR,  FL } },
				 /* Dolby Surround */
{ .ca_index = 0x02,  .speakers = {   0,    0,   0,   0,  FC,    0,  FR,  FL } },
				 /* surround40 */
{ .ca_index = 0x08,  .speakers = {   0,    0,  RR,  RL,   0,    0,  FR,  FL } },
				 /* surround41 */
{ .ca_index = 0x09,  .speakers = {   0,    0,  RR,  RL,   0,  LFE,  FR,  FL } },
				 /* surround50 */
{ .ca_index = 0x0a,  .speakers = {   0,    0,  RR,  RL,  FC,    0,  FR,  FL } },
				 /* surround51 */
{ .ca_index = 0x0b,  .speakers = {   0,    0,  RR,  RL,  FC,  LFE,  FR,  FL } },
				 /* 6.1 */
{ .ca_index = 0x0f,  .speakers = {   0,   RC,  RR,  RL,  FC,  LFE,  FR,  FL } },
				 /* surround71 */
{ .ca_index = 0x13,  .speakers = { RRC,  RLC,  RR,  RL,  FC,  LFE,  FR,  FL } },

{ .ca_index = 0x03,  .speakers = {   0,    0,   0,   0,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x04,  .speakers = {   0,    0,   0,  RC,   0,    0,  FR,  FL } },
{ .ca_index = 0x05,  .speakers = {   0,    0,   0,  RC,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x06,  .speakers = {   0,    0,   0,  RC,  FC,    0,  FR,  FL } },
{ .ca_index = 0x07,  .speakers = {   0,    0,   0,  RC,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x0c,  .speakers = {   0,   RC,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x0d,  .speakers = {   0,   RC,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x0e,  .speakers = {   0,   RC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x10,  .speakers = { RRC,  RLC,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x11,  .speakers = { RRC,  RLC,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x12,  .speakers = { RRC,  RLC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x14,  .speakers = { FRC,  FLC,   0,   0,   0,    0,  FR,  FL } },
{ .ca_index = 0x15,  .speakers = { FRC,  FLC,   0,   0,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x16,  .speakers = { FRC,  FLC,   0,   0,  FC,    0,  FR,  FL } },
{ .ca_index = 0x17,  .speakers = { FRC,  FLC,   0,   0,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x18,  .speakers = { FRC,  FLC,   0,  RC,   0,    0,  FR,  FL } },
{ .ca_index = 0x19,  .speakers = { FRC,  FLC,   0,  RC,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x1a,  .speakers = { FRC,  FLC,   0,  RC,  FC,    0,  FR,  FL } },
{ .ca_index = 0x1b,  .speakers = { FRC,  FLC,   0,  RC,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x1c,  .speakers = { FRC,  FLC,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x1d,  .speakers = { FRC,  FLC,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x1e,  .speakers = { FRC,  FLC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x1f,  .speakers = { FRC,  FLC,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x20,  .speakers = {   0,  FCH,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x21,  .speakers = {   0,  FCH,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x22,  .speakers = {  TC,    0,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x23,  .speakers = {  TC,    0,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x24,  .speakers = { FRH,  FLH,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x25,  .speakers = { FRH,  FLH,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x26,  .speakers = { FRW,  FLW,  RR,  RL,   0,    0,  FR,  FL } },
{ .ca_index = 0x27,  .speakers = { FRW,  FLW,  RR,  RL,   0,  LFE,  FR,  FL } },
{ .ca_index = 0x28,  .speakers = {  TC,   RC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x29,  .speakers = {  TC,   RC,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x2a,  .speakers = { FCH,   RC,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x2b,  .speakers = { FCH,   RC,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x2c,  .speakers = {  TC,  FCH,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x2d,  .speakers = {  TC,  FCH,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x2e,  .speakers = { FRH,  FLH,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x2f,  .speakers = { FRH,  FLH,  RR,  RL,  FC,  LFE,  FR,  FL } },
{ .ca_index = 0x30,  .speakers = { FRW,  FLW,  RR,  RL,  FC,    0,  FR,  FL } },
{ .ca_index = 0x31,  .speakers = { FRW,  FLW,  RR,  RL,  FC,  LFE,  FR,  FL } },
};

static int hdmi_pin_set_slot_channel(struct hdac_device *codec,
		hda_nid_t pin_nid, int asp_slot, int channel)
{
	return snd_hdac_codec_write(codec, pin_nid, 0,
				AC_VERB_SET_HDMI_CHAN_SLOT,
				(channel << 4) | asp_slot);
}

static int hdmi_pin_get_slot_channel(struct hdac_device *codec,
			hda_nid_t pin_nid, int asp_slot)
{
	return (snd_hdac_codec_read(codec, pin_nid, 0,
				   AC_VERB_GET_HDMI_CHAN_SLOT,
				   asp_slot) & 0xf0) >> 4;
}

static int hdmi_get_channel_count(struct hdac_device *codec, hda_nid_t cvt_nid)
{
	return 1 + snd_hdac_codec_read(codec, cvt_nid, 0,
					AC_VERB_GET_CVT_CHAN_COUNT, 0);
}

static void hdmi_set_channel_count(struct hdac_device *codec,
				   hda_nid_t cvt_nid, int chs)
{
	if (chs != hdmi_get_channel_count(codec, cvt_nid))
		snd_hdac_codec_write(codec, cvt_nid, 0,
				    AC_VERB_SET_CVT_CHAN_COUNT, chs - 1);
}

/*
 * Channel mapping routines
 */

/*
 * Compute derived values in channel_allocations[].
 */
static void init_channel_allocations(void)
{
	int i, j;
	struct hdac_cea_channel_speaker_allocation *p;

	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		p = channel_allocations + i;
		p->channels = 0;
		p->spk_mask = 0;
		for (j = 0; j < ARRAY_SIZE(p->speakers); j++)
			if (p->speakers[j]) {
				p->channels++;
				p->spk_mask |= p->speakers[j];
			}
	}
}

static int get_channel_allocation_order(int ca)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		if (channel_allocations[i].ca_index == ca)
			break;
	}
	return i;
}

void snd_hdac_print_channel_allocation(int spk_alloc, char *buf, int buflen)
{
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(cea_speaker_allocation_names); i++) {
		if (spk_alloc & (1 << i))
			j += scnprintf(buf + j, buflen - j,  " %s",
					cea_speaker_allocation_names[i]);
	}
	buf[j] = '\0';	/* necessary when j == 0 */
}
EXPORT_SYMBOL_GPL(snd_hdac_print_channel_allocation);

/*
 * The transformation takes two steps:
 *
 *	eld->spk_alloc => (eld_speaker_allocation_bits[]) => spk_mask
 *	      spk_mask => (channel_allocations[])         => ai->CA
 *
 * TODO: it could select the wrong CA from multiple candidates.
*/
static int hdmi_channel_allocation_spk_alloc_blk(struct hdac_device *codec,
				   int spk_alloc, int channels)
{
	int i;
	int ca = 0;
	int spk_mask = 0;
	char buf[SND_PRINT_CHANNEL_ALLOCATION_ADVISED_BUFSIZE];

	/*
	 * CA defaults to 0 for basic stereo audio
	 */
	if (channels <= 2)
		return 0;

	/*
	 * expand ELD's speaker allocation mask
	 *
	 * ELD tells the speaker mask in a compact(paired) form,
	 * expand ELD's notions to match the ones used by Audio InfoFrame.
	 */
	for (i = 0; i < ARRAY_SIZE(eld_speaker_allocation_bits); i++) {
		if (spk_alloc & (1 << i))
			spk_mask |= eld_speaker_allocation_bits[i];
	}

	/* search for the first working match in the CA table */
	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		if (channels == channel_allocations[i].channels &&
		    (spk_mask & channel_allocations[i].spk_mask) ==
				channel_allocations[i].spk_mask) {
			ca = channel_allocations[i].ca_index;
			break;
		}
	}

	if (!ca) {
		/*
		 * if there was no match, select the regular ALSA channel
		 * allocation with the matching number of channels
		 */
		for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
			if (channels == channel_allocations[i].channels) {
				ca = channel_allocations[i].ca_index;
				break;
			}
		}
	}

	snd_hdac_print_channel_allocation(spk_alloc, buf, sizeof(buf));
	dev_dbg(&codec->dev, "HDMI: select CA 0x%x for %d-channel allocation: %s\n",
		    ca, channels, buf);

	return ca;
}

static void hdmi_debug_channel_mapping(struct hdac_chmap *chmap,
				       hda_nid_t pin_nid)
{
#ifdef CONFIG_SND_DEBUG_VERBOSE
	int i;
	int channel;

	for (i = 0; i < 8; i++) {
		channel = chmap->ops.pin_get_slot_channel(
				chmap->hdac, pin_nid, i);
		dev_dbg(&chmap->hdac->dev, "HDMI: ASP channel %d => slot %d\n",
						channel, i);
	}
#endif
}

static void hdmi_std_setup_channel_mapping(struct hdac_chmap *chmap,
				       hda_nid_t pin_nid,
				       bool non_pcm,
				       int ca)
{
	struct hdac_cea_channel_speaker_allocation *ch_alloc;
	int i;
	int err;
	int order;
	int non_pcm_mapping[8];

	order = get_channel_allocation_order(ca);
	ch_alloc = &channel_allocations[order];

	if (hdmi_channel_mapping[ca][1] == 0) {
		int hdmi_slot = 0;
		/* fill actual channel mappings in ALSA channel (i) order */
		for (i = 0; i < ch_alloc->channels; i++) {
			while (!WARN_ON(hdmi_slot >= 8) &&
			       !ch_alloc->speakers[7 - hdmi_slot])
				hdmi_slot++; /* skip zero slots */

			hdmi_channel_mapping[ca][i] = (i << 4) | hdmi_slot++;
		}
		/* fill the rest of the slots with ALSA channel 0xf */
		for (hdmi_slot = 0; hdmi_slot < 8; hdmi_slot++)
			if (!ch_alloc->speakers[7 - hdmi_slot])
				hdmi_channel_mapping[ca][i++] = (0xf << 4) | hdmi_slot;
	}

	if (non_pcm) {
		for (i = 0; i < ch_alloc->channels; i++)
			non_pcm_mapping[i] = (i << 4) | i;
		for (; i < 8; i++)
			non_pcm_mapping[i] = (0xf << 4) | i;
	}

	for (i = 0; i < 8; i++) {
		int slotsetup = non_pcm ? non_pcm_mapping[i] : hdmi_channel_mapping[ca][i];
		int hdmi_slot = slotsetup & 0x0f;
		int channel = (slotsetup & 0xf0) >> 4;

		err = chmap->ops.pin_set_slot_channel(chmap->hdac,
				pin_nid, hdmi_slot, channel);
		if (err) {
			dev_dbg(&chmap->hdac->dev, "HDMI: channel mapping failed\n");
			break;
		}
	}
}

struct channel_map_table {
	unsigned char map;		/* ALSA API channel map position */
	int spk_mask;			/* speaker position bit mask */
};

static struct channel_map_table map_tables[] = {
	{ SNDRV_CHMAP_FL,	FL },
	{ SNDRV_CHMAP_FR,	FR },
	{ SNDRV_CHMAP_RL,	RL },
	{ SNDRV_CHMAP_RR,	RR },
	{ SNDRV_CHMAP_LFE,	LFE },
	{ SNDRV_CHMAP_FC,	FC },
	{ SNDRV_CHMAP_RLC,	RLC },
	{ SNDRV_CHMAP_RRC,	RRC },
	{ SNDRV_CHMAP_RC,	RC },
	{ SNDRV_CHMAP_FLC,	FLC },
	{ SNDRV_CHMAP_FRC,	FRC },
	{ SNDRV_CHMAP_TFL,	FLH },
	{ SNDRV_CHMAP_TFR,	FRH },
	{ SNDRV_CHMAP_FLW,	FLW },
	{ SNDRV_CHMAP_FRW,	FRW },
	{ SNDRV_CHMAP_TC,	TC },
	{ SNDRV_CHMAP_TFC,	FCH },
	{} /* terminator */
};

/* from ALSA API channel position to speaker bit mask */
int snd_hdac_chmap_to_spk_mask(unsigned char c)
{
	struct channel_map_table *t = map_tables;

	for (; t->map; t++) {
		if (t->map == c)
			return t->spk_mask;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_chmap_to_spk_mask);

/* from ALSA API channel position to CEA slot */
static int to_cea_slot(int ordered_ca, unsigned char pos)
{
	int mask = snd_hdac_chmap_to_spk_mask(pos);
	int i;

	/* Add sanity check to pass klockwork check.
	 * This should never happen.
	 */
	if (ordered_ca >= ARRAY_SIZE(channel_allocations))
		return -1;

	if (mask) {
		for (i = 0; i < 8; i++) {
			if (channel_allocations[ordered_ca].speakers[7 - i] == mask)
				return i;
		}
	}

	return -1;
}

/* from speaker bit mask to ALSA API channel position */
int snd_hdac_spk_to_chmap(int spk)
{
	struct channel_map_table *t = map_tables;

	for (; t->map; t++) {
		if (t->spk_mask == spk)
			return t->map;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_spk_to_chmap);

/* from CEA slot to ALSA API channel position */
static int from_cea_slot(int ordered_ca, unsigned char slot)
{
	int mask;

	/* Add sanity check to pass klockwork check.
	 * This should never happen.
	 */
	if (slot >= 8)
		return 0;

	mask = channel_allocations[ordered_ca].speakers[7 - slot];

	return snd_hdac_spk_to_chmap(mask);
}

/* get the CA index corresponding to the given ALSA API channel map */
static int hdmi_manual_channel_allocation(int chs, unsigned char *map)
{
	int i, spks = 0, spk_mask = 0;

	for (i = 0; i < chs; i++) {
		int mask = snd_hdac_chmap_to_spk_mask(map[i]);

		if (mask) {
			spk_mask |= mask;
			spks++;
		}
	}

	for (i = 0; i < ARRAY_SIZE(channel_allocations); i++) {
		if ((chs == channel_allocations[i].channels ||
		     spks == channel_allocations[i].channels) &&
		    (spk_mask & channel_allocations[i].spk_mask) ==
				channel_allocations[i].spk_mask)
			return channel_allocations[i].ca_index;
	}
	return -1;
}

/* set up the channel slots for the given ALSA API channel map */
static int hdmi_manual_setup_channel_mapping(struct hdac_chmap *chmap,
					     hda_nid_t pin_nid,
					     int chs, unsigned char *map,
					     int ca)
{
	int ordered_ca = get_channel_allocation_order(ca);
	int alsa_pos, hdmi_slot;
	int assignments[8] = {[0 ... 7] = 0xf};

	for (alsa_pos = 0; alsa_pos < chs; alsa_pos++) {

		hdmi_slot = to_cea_slot(ordered_ca, map[alsa_pos]);

		if (hdmi_slot < 0)
			continue; /* unassigned channel */

		assignments[hdmi_slot] = alsa_pos;
	}

	for (hdmi_slot = 0; hdmi_slot < 8; hdmi_slot++) {
		int err;

		err = chmap->ops.pin_set_slot_channel(chmap->hdac,
				pin_nid, hdmi_slot, assignments[hdmi_slot]);
		if (err)
			return -EINVAL;
	}
	return 0;
}

/* store ALSA API channel map from the current default map */
static void hdmi_setup_fake_chmap(unsigned char *map, int ca)
{
	int i;
	int ordered_ca = get_channel_allocation_order(ca);

	for (i = 0; i < 8; i++) {
		if (ordered_ca < ARRAY_SIZE(channel_allocations) &&
		    i < channel_allocations[ordered_ca].channels)
			map[i] = from_cea_slot(ordered_ca, hdmi_channel_mapping[ca][i] & 0x0f);
		else
			map[i] = 0;
	}
}

void snd_hdac_setup_channel_mapping(struct hdac_chmap *chmap,
				       hda_nid_t pin_nid, bool non_pcm, int ca,
				       int channels, unsigned char *map,
				       bool chmap_set)
{
	if (!non_pcm && chmap_set) {
		hdmi_manual_setup_channel_mapping(chmap, pin_nid,
						  channels, map, ca);
	} else {
		hdmi_std_setup_channel_mapping(chmap, pin_nid, non_pcm, ca);
		hdmi_setup_fake_chmap(map, ca);
	}

	hdmi_debug_channel_mapping(chmap, pin_nid);
}
EXPORT_SYMBOL_GPL(snd_hdac_setup_channel_mapping);

int snd_hdac_get_active_channels(int ca)
{
	int ordered_ca = get_channel_allocation_order(ca);

	/* Add sanity check to pass klockwork check.
	 * This should never happen.
	 */
	if (ordered_ca >= ARRAY_SIZE(channel_allocations))
		ordered_ca = 0;

	return channel_allocations[ordered_ca].channels;
}
EXPORT_SYMBOL_GPL(snd_hdac_get_active_channels);

struct hdac_cea_channel_speaker_allocation *snd_hdac_get_ch_alloc_from_ca(int ca)
{
	return &channel_allocations[get_channel_allocation_order(ca)];
}
EXPORT_SYMBOL_GPL(snd_hdac_get_ch_alloc_from_ca);

int snd_hdac_channel_allocation(struct hdac_device *hdac, int spk_alloc,
		int channels, bool chmap_set, bool non_pcm, unsigned char *map)
{
	int ca;

	if (!non_pcm && chmap_set)
		ca = hdmi_manual_channel_allocation(channels, map);
	else
		ca = hdmi_channel_allocation_spk_alloc_blk(hdac,
					spk_alloc, channels);

	if (ca < 0)
		ca = 0;

	return ca;
}
EXPORT_SYMBOL_GPL(snd_hdac_channel_allocation);

/*
 * ALSA API channel-map control callbacks
 */
static int hdmi_chmap_ctl_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct hdac_chmap *chmap = info->private_data;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = chmap->channels_max;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SNDRV_CHMAP_LAST;
	return 0;
}

static int hdmi_chmap_cea_alloc_validate_get_type(struct hdac_chmap *chmap,
		struct hdac_cea_channel_speaker_allocation *cap, int channels)
{
	/* If the speaker allocation matches the channel count, it is OK.*/
	if (cap->channels != channels)
		return -1;

	/* all channels are remappable freely */
	return SNDRV_CTL_TLVT_CHMAP_VAR;
}

static void hdmi_cea_alloc_to_tlv_chmap(struct hdac_chmap *hchmap,
		struct hdac_cea_channel_speaker_allocation *cap,
		unsigned int *chmap, int channels)
{
	int count = 0;
	int c;

	for (c = 7; c >= 0; c--) {
		int spk = cap->speakers[c];

		if (!spk)
			continue;

		chmap[count++] = snd_hdac_spk_to_chmap(spk);
	}

	WARN_ON(count != channels);
}

static int spk_mask_from_spk_alloc(int spk_alloc)
{
	int i;
	int spk_mask = eld_speaker_allocation_bits[0];

	for (i = 0; i < ARRAY_SIZE(eld_speaker_allocation_bits); i++) {
		if (spk_alloc & (1 << i))
			spk_mask |= eld_speaker_allocation_bits[i];
	}

	return spk_mask;
}

static int hdmi_chmap_ctl_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			      unsigned int size, unsigned int __user *tlv)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct hdac_chmap *chmap = info->private_data;
	int pcm_idx = kcontrol->private_value;
	unsigned int __user *dst;
	int chs, count = 0;
	unsigned long max_chs;
	int type;
	int spk_alloc, spk_mask;

	if (size < 8)
		return -ENOMEM;
	if (put_user(SNDRV_CTL_TLVT_CONTAINER, tlv))
		return -EFAULT;
	size -= 8;
	dst = tlv + 2;

	spk_alloc = chmap->ops.get_spk_alloc(chmap->hdac, pcm_idx);
	spk_mask = spk_mask_from_spk_alloc(spk_alloc);

	max_chs = hweight_long(spk_mask);

	for (chs = 2; chs <= max_chs; chs++) {
		int i;
		struct hdac_cea_channel_speaker_allocation *cap;

		cap = channel_allocations;
		for (i = 0; i < ARRAY_SIZE(channel_allocations); i++, cap++) {
			int chs_bytes = chs * 4;
			unsigned int tlv_chmap[8];

			if (cap->channels != chs)
				continue;

			if (!(cap->spk_mask == (spk_mask & cap->spk_mask)))
				continue;

			type = chmap->ops.chmap_cea_alloc_validate_get_type(
							chmap, cap, chs);
			if (type < 0)
				return -ENODEV;
			if (size < 8)
				return -ENOMEM;

			if (put_user(type, dst) ||
			    put_user(chs_bytes, dst + 1))
				return -EFAULT;

			dst += 2;
			size -= 8;
			count += 8;

			if (size < chs_bytes)
				return -ENOMEM;

			size -= chs_bytes;
			count += chs_bytes;
			chmap->ops.cea_alloc_to_tlv_chmap(chmap, cap,
						tlv_chmap, chs);

			if (copy_to_user(dst, tlv_chmap, chs_bytes))
				return -EFAULT;
			dst += chs;
		}
	}

	if (put_user(count, tlv + 1))
		return -EFAULT;

	return 0;
}

static int hdmi_chmap_ctl_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct hdac_chmap *chmap = info->private_data;
	int pcm_idx = kcontrol->private_value;
	unsigned char pcm_chmap[8];
	int i;

	memset(pcm_chmap, 0, sizeof(pcm_chmap));
	chmap->ops.get_chmap(chmap->hdac, pcm_idx, pcm_chmap);

	for (i = 0; i < ARRAY_SIZE(pcm_chmap); i++)
		ucontrol->value.integer.value[i] = pcm_chmap[i];

	return 0;
}

static int hdmi_chmap_ctl_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct hdac_chmap *hchmap = info->private_data;
	int pcm_idx = kcontrol->private_value;
	unsigned int ctl_idx;
	struct snd_pcm_substream *substream;
	unsigned char chmap[8], per_pin_chmap[8];
	int i, err, ca, prepared = 0;

	/* No monitor is connected in dyn_pcm_assign.
	 * It's invalid to setup the chmap
	 */
	if (!hchmap->ops.is_pcm_attached(hchmap->hdac, pcm_idx))
		return 0;

	ctl_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	substream = snd_pcm_chmap_substream(info, ctl_idx);
	if (!substream || !substream->runtime)
		return 0; /* just for avoiding error from alsactl restore */
	switch (substream->runtime->status->state) {
	case SNDRV_PCM_STATE_OPEN:
	case SNDRV_PCM_STATE_SETUP:
		break;
	case SNDRV_PCM_STATE_PREPARED:
		prepared = 1;
		break;
	default:
		return -EBUSY;
	}
	memset(chmap, 0, sizeof(chmap));
	for (i = 0; i < ARRAY_SIZE(chmap); i++)
		chmap[i] = ucontrol->value.integer.value[i];

	hchmap->ops.get_chmap(hchmap->hdac, pcm_idx, per_pin_chmap);
	if (!memcmp(chmap, per_pin_chmap, sizeof(chmap)))
		return 0;
	ca = hdmi_manual_channel_allocation(ARRAY_SIZE(chmap), chmap);
	if (ca < 0)
		return -EINVAL;
	if (hchmap->ops.chmap_validate) {
		err = hchmap->ops.chmap_validate(hchmap, ca,
				ARRAY_SIZE(chmap), chmap);
		if (err)
			return err;
	}

	hchmap->ops.set_chmap(hchmap->hdac, pcm_idx, chmap, prepared);

	return 0;
}

static const struct hdac_chmap_ops chmap_ops = {
	.chmap_cea_alloc_validate_get_type	= hdmi_chmap_cea_alloc_validate_get_type,
	.cea_alloc_to_tlv_chmap			= hdmi_cea_alloc_to_tlv_chmap,
	.pin_get_slot_channel			= hdmi_pin_get_slot_channel,
	.pin_set_slot_channel			= hdmi_pin_set_slot_channel,
	.set_channel_count			= hdmi_set_channel_count,
};

void snd_hdac_register_chmap_ops(struct hdac_device *hdac,
				struct hdac_chmap *chmap)
{
	chmap->ops = chmap_ops;
	chmap->hdac = hdac;
	init_channel_allocations();
}
EXPORT_SYMBOL_GPL(snd_hdac_register_chmap_ops);

int snd_hdac_add_chmap_ctls(struct snd_pcm *pcm, int pcm_idx,
				struct hdac_chmap *hchmap)
{
	struct snd_pcm_chmap *chmap;
	struct snd_kcontrol *kctl;
	int err, i;

	err = snd_pcm_add_chmap_ctls(pcm,
				     SNDRV_PCM_STREAM_PLAYBACK,
				     NULL, 0, pcm_idx, &chmap);
	if (err < 0)
		return err;
	/* override handlers */
	chmap->private_data = hchmap;
	kctl = chmap->kctl;
	for (i = 0; i < kctl->count; i++)
		kctl->vd[i].access |= SNDRV_CTL_ELEM_ACCESS_WRITE;
	kctl->info = hdmi_chmap_ctl_info;
	kctl->get = hdmi_chmap_ctl_get;
	kctl->put = hdmi_chmap_ctl_put;
	kctl->tlv.c = hdmi_chmap_ctl_tlv;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_add_chmap_ctls);
