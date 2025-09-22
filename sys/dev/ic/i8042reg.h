/*	$OpenBSD: i8042reg.h,v 1.9 2015/05/05 16:27:20 shadchin Exp $	*/
/*	$NetBSD: i8042reg.h,v 1.7 1998/01/18 14:41:37 drochner Exp $	*/

#define	KBSTATP		4	/* kbd controller status port (I) */
#define	 KBS_DIB	0x01	/* kbd data in buffer */
#define	 KBS_IBF	0x02	/* kbd input buffer low */
#define	 KBS_WARM	0x04	/* kbd input buffer low */
#define	 KBS_OCMD	0x08	/* kbd output buffer has command */
#define	 KBS_NOSEC	0x10	/* kbd security lock not engaged */
#define	 KBS_AUXDATA	0x20	/* kbd data in buffer from aux port */
#define	 KBS_RERR	0x40	/* kbd receive error */
#define	 KBS_PERR	0x80	/* kbd parity error */

#define	KBCMDP		4	/* kbd controller port (O) */
#define	 KBC_RAMREAD	0x20	/* read from RAM */
#define	 KBC_RAMWRITE	0x60	/* write to RAM */
#define	 KBC_AUXDISABLE	0xa7	/* disable auxiliary port */
#define	 KBC_AUXENABLE	0xa8	/* enable auxiliary port */
#define	 KBC_AUXTEST	0xa9	/* test auxiliary port */
#define	 KBC_CMDWOUT	0xd1	/* write output port */
#define	 KBC_KBDECHO	0xd2	/* echo to keyboard port */
#define	 KBC_AUXECHO	0xd3	/* echo to auxiliary port */
#define	 KBC_AUXWRITE	0xd4	/* write to auxiliary port */
#define	 KBC_SELFTEST	0xaa	/* start self-test */
#define	 KBC_KBDTEST	0xab	/* test keyboard port */
#define	 KBC_KBDDISABLE	0xad	/* disable keyboard port */
#define	 KBC_KBDENABLE	0xae	/* enable keyboard port */
#define	 KBC_READID	0xf2	/* read device id */
#define	 KBC_PULSE0	0xfe	/* pulse output bit 0 */
#define	 KBC_PULSE1	0xfd	/* pulse output bit 1 */
#define	 KBC_PULSE2	0xfb	/* pulse output bit 2 */
#define	 KBC_PULSE3	0xf7	/* pulse output bit 3 */

#define	KBDATAP		0	/* kbd data port (I) */
#define	KBOUTP		0	/* kbd data port (O) */

#define	K_RDCMDBYTE	0x20
#define	K_LDCMDBYTE	0x60

#define	KC8_TRANS	0x40	/* convert to old scan codes */
#define	KC8_MDISABLE	0x20	/* disable mouse */
#define	KC8_KDISABLE	0x10	/* disable keyboard */
#define	KC8_IGNSEC	0x08	/* ignore security lock */
#define	KC8_CPU		0x04	/* exit from protected mode reset */
#define	KC8_MENABLE	0x02	/* enable mouse interrupt */
#define	KC8_KENABLE	0x01	/* enable keyboard interrupt */
#define	CMDBYTE		(KC8_TRANS|KC8_CPU|KC8_MENABLE|KC8_KENABLE)

#define	KCID_KBD1	0xAB
#define	KCID_KBD2	0x83
#define	KCID_MOUSE	0x00
