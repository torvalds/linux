/***************************************************************************
 *            au88x0_sb.h
 *
 *  Wed Oct 29 22:10:42 2003
 *  
 ****************************************************************************/

#ifdef CHIP_AU8820
/* AU8820 starting @ 64KiB offset */
#define SBEMU_BASE 0x10000
#else
/* AU8810? and AU8830 starting @ 164KiB offset */
#define SBEMU_BASE 0x29000
#endif

#define FM_A_STATUS			(SBEMU_BASE + 0x00)	/* read */
#define FM_A_ADDRESS		(SBEMU_BASE + 0x00)	/* write */
#define FM_A_DATA			(SBEMU_BASE + 0x04)
#define FM_B_STATUS			(SBEMU_BASE + 0x08)
#define FM_B_ADDRESS		(SBEMU_BASE + 0x08)
#define FM_B_DATA			(SBEMU_BASE + 0x0C)
#define SB_MIXER_ADDR		(SBEMU_BASE + 0x10)
#define SB_MIXER_DATA		(SBEMU_BASE + 0x14)
#define SB_RESET			(SBEMU_BASE + 0x18)
#define SB_RESET_ALIAS		(SBEMU_BASE + 0x1C)
#define FM_STATUS2			(SBEMU_BASE + 0x20)
#define FM_ADDR2			(SBEMU_BASE + 0x20)
#define FM_DATA2			(SBEMU_BASE + 0x24)
#define SB_DSP_READ			(SBEMU_BASE + 0x28)
#define SB_DSP_WRITE		(SBEMU_BASE + 0x30)
#define SB_DSP_WRITE_STATUS	(SBEMU_BASE + 0x30)	/* bit 7 */
#define SB_DSP_READ_STATUS	(SBEMU_BASE + 0x38)	/* bit 7 */
#define SB_LACR				(SBEMU_BASE + 0x40)	/* ? */
#define SB_LADCR			(SBEMU_BASE + 0x44)	/* ? */
#define SB_LAMR				(SBEMU_BASE + 0x48)	/* ? */
#define SB_LARR				(SBEMU_BASE + 0x4C)	/* ? */
#define SB_VERSION			(SBEMU_BASE + 0x50)
#define SB_CTRLSTAT			(SBEMU_BASE + 0x54)
#define SB_TIMERSTAT		(SBEMU_BASE + 0x58)
#define FM_RAM				(SBEMU_BASE + 0x100)	/* 0x40 ULONG */
