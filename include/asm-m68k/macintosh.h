#ifndef __ASM_MACINTOSH_H
#define __ASM_MACINTOSH_H

#include <linux/seq_file.h>
#include <linux/interrupt.h>

/*
 *	Apple Macintoshisms
 */

extern void mac_reset(void);
extern void mac_poweroff(void);
extern void mac_init_IRQ(void);
extern int mac_irq_pending(unsigned int);
extern void mac_identify(void);
extern void mac_report_hardware(void);
extern void mac_debugging_penguin(int);
extern void mac_boom(int);

/*
 *	Floppy driver magic hook - probably shouldnt be here
 */

extern void via1_set_head(int);

extern void parse_booter(char *ptr);
extern void print_booter(char *ptr);

/*
 *	Macintosh Table
 */

struct mac_model
{
	short ident;
	char *name;
	char adb_type;
	char via_type;
	char scsi_type;
	char ide_type;
	char scc_type;
	char ether_type;
	char nubus_type;
};

#define MAC_ADB_NONE		0
#define MAC_ADB_II		1
#define MAC_ADB_IISI		2
#define MAC_ADB_CUDA		3
#define MAC_ADB_PB1		4
#define MAC_ADB_PB2		5
#define MAC_ADB_IOP		6

#define MAC_VIA_II		1
#define MAC_VIA_IIci		2
#define MAC_VIA_QUADRA		3

#define MAC_SCSI_NONE		0
#define MAC_SCSI_OLD		1
#define MAC_SCSI_QUADRA		2
#define MAC_SCSI_QUADRA2	3
#define MAC_SCSI_QUADRA3	4

#define MAC_IDE_NONE		0
#define MAC_IDE_QUADRA		1
#define MAC_IDE_PB		2
#define MAC_IDE_BABOON		3

#define MAC_SCC_II		1
#define MAC_SCC_IOP		2
#define MAC_SCC_QUADRA		3
#define MAC_SCC_PSC		4

#define MAC_ETHER_NONE		0
#define MAC_ETHER_SONIC		1
#define MAC_ETHER_MACE		2

#define MAC_NO_NUBUS		0
#define MAC_NUBUS		1

/*
 *	Gestalt numbers
 */

#define MAC_MODEL_II		6
#define MAC_MODEL_IIX		7
#define MAC_MODEL_IICX		8
#define MAC_MODEL_SE30		9
#define MAC_MODEL_IICI		11
#define MAC_MODEL_IIFX		13	/* And well numbered it is too */
#define MAC_MODEL_IISI		18
#define MAC_MODEL_LC		19
#define MAC_MODEL_Q900		20
#define MAC_MODEL_PB170		21
#define MAC_MODEL_Q700		22
#define MAC_MODEL_CLII		23	/* aka: P200 */
#define MAC_MODEL_PB140		25
#define MAC_MODEL_Q950		26	/* aka: WGS95 */
#define MAC_MODEL_LCIII		27	/* aka: P450 */
#define MAC_MODEL_PB210		29
#define MAC_MODEL_C650		30
#define MAC_MODEL_PB230		32
#define MAC_MODEL_PB180		33
#define MAC_MODEL_PB160		34
#define MAC_MODEL_Q800		35	/* aka: WGS80 */
#define MAC_MODEL_Q650		36
#define MAC_MODEL_LCII		37	/* aka: P400/405/410/430 */
#define MAC_MODEL_PB250		38
#define MAC_MODEL_IIVI		44
#define MAC_MODEL_P600		45	/* aka: P600CD */
#define MAC_MODEL_IIVX		48
#define MAC_MODEL_CCL		49	/* aka: P250 */
#define MAC_MODEL_PB165C	50
#define MAC_MODEL_C610		52	/* aka: WGS60 */
#define MAC_MODEL_Q610		53
#define MAC_MODEL_PB145		54	/* aka: PB145B */
#define MAC_MODEL_P520		56	/* aka: LC520 */
#define MAC_MODEL_C660		60
#define MAC_MODEL_P460		62	/* aka: LCIII+, P466/P467 */
#define MAC_MODEL_PB180C	71
#define MAC_MODEL_PB520		72	/* aka: PB520C, PB540, PB540C, PB550C */
#define MAC_MODEL_PB270C	77
#define MAC_MODEL_Q840		78
#define MAC_MODEL_P550		80	/* aka: LC550, P560 */
#define MAC_MODEL_CCLII		83	/* aka: P275 */
#define MAC_MODEL_PB165		84
#define MAC_MODEL_PB190		85	/* aka: PB190CS */
#define MAC_MODEL_TV		88
#define MAC_MODEL_P475		89	/* aka: LC475, P476 */
#define MAC_MODEL_P475F		90	/* aka: P475 w/ FPU (no LC040) */
#define MAC_MODEL_P575		92	/* aka: LC575, P577/P578 */
#define MAC_MODEL_Q605		94
#define MAC_MODEL_Q605_ACC	95	/* Q605 accelerated to 33 MHz */
#define MAC_MODEL_Q630		98	/* aka: LC630, P630/631/635/636/637/638/640 */
#define MAC_MODEL_P588		99	/* aka: LC580, P580 */
#define MAC_MODEL_PB280		102
#define MAC_MODEL_PB280C	103
#define MAC_MODEL_PB150		115

extern struct mac_model *macintosh_config;

#endif
