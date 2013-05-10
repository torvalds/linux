/*
 * sound/oss/sb_card.h
 *
 * This file is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this
 * software for more info.
 *
 * 02-05-2002 Original Release, Paul Laufer <paul@laufernet.com>
 */

struct sb_card_config {
	struct address_info conf;
	struct address_info mpucnf;
	const  char         *card_id;
	const  char         *dev_id;
	int                 mpu;
};

#ifdef CONFIG_PNP

/*
 * SoundBlaster PnP tables and structures.
 */

/* Card PnP ID Table */
static struct pnp_card_device_id sb_pnp_card_table[] = {
	/* Sound Blaster 16 */
	{.id = "CTL0024", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster 16 */
	{.id = "CTL0025", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster 16 */
	{.id = "CTL0026", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster 16 */
	{.id = "CTL0027", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster 16 */
	{.id = "CTL0028", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster 16 */
	{.id = "CTL0029", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster 16 */
	{.id = "CTL002a", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster 16 */
	{.id = "CTL002b", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster 16 */
	{.id = "CTL002c", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster 16 */
	{.id = "CTL00ed", .driver_data = 0, .devs = { {.id="CTL0041"}, } },
	/* Sound Blaster 16 */
	{.id = "CTL0086", .driver_data = 0, .devs = { {.id="CTL0041"}, } },
	/* Sound Blaster Vibra16S */
	{.id = "CTL0051", .driver_data = 0, .devs = { {.id="CTL0001"}, } },
	/* Sound Blaster Vibra16C */
	{.id = "CTL0070", .driver_data = 0, .devs = { {.id="CTL0001"}, } },
	/* Sound Blaster Vibra16CL */
	{.id = "CTL0080", .driver_data = 0, .devs = { {.id="CTL0041"}, } },
	/* Sound Blaster Vibra16CL */
	{.id = "CTL00F0", .driver_data = 0, .devs = { {.id="CTL0043"}, } },
	/* Sound Blaster AWE 32 */
	{.id = "CTL0039", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster AWE 32 */
	{.id = "CTL0042", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster AWE 32 */
	{.id = "CTL0043", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster AWE 32 */
	{.id = "CTL0044", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster AWE 32 */
	{.id = "CTL0045", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster AWE 32 */
	{.id = "CTL0046", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster AWE 32 */
	{.id = "CTL0047", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster AWE 32 */
	{.id = "CTL0048", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster AWE 32 */
	{.id = "CTL0054", .driver_data = 0, .devs = { {.id="CTL0031"}, } },
	/* Sound Blaster AWE 32 */
	{.id = "CTL009C", .driver_data = 0, .devs = { {.id="CTL0041"}, } },
	/* Createive SB32 PnP */
	{.id = "CTL009F", .driver_data = 0, .devs = { {.id="CTL0041"}, } },
	/* Sound Blaster AWE 64 */
	{.id = "CTL009D", .driver_data = 0, .devs = { {.id="CTL0042"}, } },
	/* Sound Blaster AWE 64 Gold */
	{.id = "CTL009E", .driver_data = 0, .devs = { {.id="CTL0044"}, } },
	/* Sound Blaster AWE 64 Gold */
	{.id = "CTL00B2", .driver_data = 0, .devs = { {.id="CTL0044"}, } },
	/* Sound Blaster AWE 64 */
	{.id = "CTL00C1", .driver_data = 0, .devs = { {.id="CTL0042"}, } },
	/* Sound Blaster AWE 64 */
	{.id = "CTL00C3", .driver_data = 0, .devs = { {.id="CTL0045"}, } },
	/* Sound Blaster AWE 64 */
	{.id = "CTL00C5", .driver_data = 0, .devs = { {.id="CTL0045"}, } },
	/* Sound Blaster AWE 64 */
	{.id = "CTL00C7", .driver_data = 0, .devs = { {.id="CTL0045"}, } },
	/* Sound Blaster AWE 64 */
	{.id = "CTL00E4", .driver_data = 0, .devs = { {.id="CTL0045"}, } },
	/* Sound Blaster AWE 64 */
	{.id = "CTL00E9", .driver_data = 0, .devs = { {.id="CTL0045"}, } },
	/* ESS 1868 */
	{.id = "ESS0968", .driver_data = 0, .devs = { {.id="ESS0968"}, } },
	/* ESS 1868 */
	{.id = "ESS1868", .driver_data = 0, .devs = { {.id="ESS1868"}, } },
	/* ESS 1868 */
	{.id = "ESS1868", .driver_data = 0, .devs = { {.id="ESS8611"}, } },
	/* ESS 1869 PnP AudioDrive */
	{.id = "ESS0003", .driver_data = 0, .devs = { {.id="ESS1869"}, } },
	/* ESS 1869 */
	{.id = "ESS1869", .driver_data = 0, .devs = { {.id="ESS1869"}, } },
	/* ESS 1878 */
	{.id = "ESS1878", .driver_data = 0, .devs = { {.id="ESS1878"}, } },
	/* ESS 1879 */
	{.id = "ESS1879", .driver_data = 0, .devs = { {.id="ESS1879"}, } },
	/* CMI 8330 SoundPRO */
	{.id = "CMI0001", .driver_data = 0, .devs = { {.id="@X@0001"},
						     {.id="@H@0001"},
						     {.id="@@@0001"}, } },
	/* Diamond DT0197H */
	{.id = "RWR1688", .driver_data = 0, .devs = { {.id="@@@0001"},
						     {.id="@X@0001"},
						     {.id="@H@0001"}, } },
	/* ALS007 */
	{.id = "ALS0007", .driver_data = 0, .devs = { {.id="@@@0001"},
						     {.id="@X@0001"},
						     {.id="@H@0001"}, } },
	/* ALS100 */
	{.id = "ALS0001", .driver_data = 0, .devs = { {.id="@@@0001"},
						     {.id="@X@0001"},
						     {.id="@H@0001"}, } },
	/* ALS110 */
	{.id = "ALS0110", .driver_data = 0, .devs = { {.id="@@@1001"},
						     {.id="@X@1001"},
						     {.id="@H@0001"}, } },
	/* ALS120 */
	{.id = "ALS0120", .driver_data = 0, .devs = { {.id="@@@2001"},
						     {.id="@X@2001"},
						     {.id="@H@0001"}, } },
	/* ALS200 */
	{.id = "ALS0200", .driver_data = 0, .devs = { {.id="@@@0020"},
						     {.id="@X@0030"},
						     {.id="@H@0001"}, } },
	/* ALS200 */
	{.id = "RTL3000", .driver_data = 0, .devs = { {.id="@@@2001"},
						     {.id="@X@2001"},
						     {.id="@H@0001"}, } },
	/* Sound Blaster 16 (Virtual PC 2004) */
	{.id = "tBA03b0", .driver_data = 0, .devs = { {.id="PNPb003"}, } },
	/* -end- */
	{.id = "", }
};

#endif
