#include <sys/types.h>
#include <sys/socket.h>		/* for PF_LINK */
#include <sys/sysctl.h>
#include <sys/time.h>

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_mib.h>

#include "ifinfo.h"

#define print(msg, var) \
	if (var) printf("\t" msg ": %lu\n", (u_long)var)

static void identify_chipset(u_int32_t chipset);

void
print_1650(const void *xmd, size_t len)
{
	const struct ifmib_iso_8802_3 *md = xmd;

	if (len != sizeof *md)
		warnx("cannot interpret %lu bytes of MIB data", (u_long)len);

	identify_chipset(md->dot3StatsEtherChipSet);
	print("Alignment errors", md->dot3StatsAlignmentErrors);
	print("FCS errors", md->dot3StatsFCSErrors);
	print("Single-collision frames", md->dot3StatsSingleCollisionFrames);
	print("Multiple-collision frames", md->dot3StatsMultipleCollisionFrames);
	print("SQE (Heartbeat) test errors", md->dot3StatsSQETestErrors);
	print("Deferred transmissions", md->dot3StatsDeferredTransmissions);
	print("Late collisions", md->dot3StatsLateCollisions);
	print("Excessive collisions", md->dot3StatsExcessiveCollisions);
	print("Internal transmit errors", md->dot3StatsInternalMacTransmitErrors);
	print("Carrier sense errors", md->dot3StatsCarrierSenseErrors);
	print("Frame-too-long errors", md->dot3StatsFrameTooLongs);
	print("Internal receive errors", md->dot3StatsInternalMacReceiveErrors);
	print("Missed frames", md->dot3StatsMissedFrames);
#define	cprint(num) print("Packets with " #num " collisions", \
			  md->dot3StatsCollFrequencies[num - 1])
	if (md->dot3Compliance >= DOT3COMPLIANCE_COLLS) {
		cprint(1); cprint(2); cprint(3); cprint(4);
		cprint(5); cprint(6); cprint(7); cprint(8);
		cprint(9); cprint(10); cprint(11); cprint(12);
		cprint(13); cprint(14); cprint(15); cprint(16);
	}
	switch(md->dot3Compliance) {
	case DOT3COMPLIANCE_STATS:
		printf("\tCompliance: statistics only\n");
		break;
	case DOT3COMPLIANCE_COLLS:
		printf("\tCompliance: statistics and collisions\n");
		break;
	}
}

static const char *const amd[] = {
	0, "Am7990", "Am79900", "Am79C940"
};

static const char *const intel[] = {
	0, "82586", "82596", "82557"
};

static const char *const national[] = {
	0, "8390", "Sonic"
};

static const char *const fujitsu[] = {
	0, "86950"
};

static const char *const digital[] = {
	0, "DC21040", "DC21140", "DC21041", "DC21140A", "DC21142"
};

static const char *const westerndigital[] = {
	0, "83C690", "83C790"
};

#define vendor(name, sets) { name, sets, (sizeof sets)/(sizeof sets[0]) }
static struct {
	const char *name;
	const char *const *chips;
	size_t len;
} chipset_names[] = {
	{ 0 },
	vendor("AMD", amd),
	vendor("Intel", intel),
	{ 0 },
	vendor("National Semiconductor", national),
	vendor("Fujitsu", fujitsu),
	vendor("Digital", digital),
	vendor("Western Digital", westerndigital)
};

static void
identify_chipset(u_int32_t chipset)
{
	enum dot3Vendors vendor = DOT3CHIPSET_VENDOR(chipset);
	u_int part = DOT3CHIPSET_PART(chipset);

	printf("\tChipset: ");
	if (vendor < 1 
	    || vendor >= (sizeof chipset_names)/(sizeof chipset_names[0])
	    || !chipset_names[vendor].name) {
		printf("unknown\n");
		return;
	}

	printf("%s ", chipset_names[vendor].name);
	if (part < 1 || part >= chipset_names[vendor].len) {
		printf("unknown\n");
		return;
	}

	printf("%s\n", chipset_names[vendor].chips[part]);
}

