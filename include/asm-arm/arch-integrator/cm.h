/*
 * update the core module control register.
 */
void cm_control(u32, u32);

#define CM_CTRL_LED			(1 << 0)
#define CM_CTRL_nMBDET			(1 << 1)
#define CM_CTRL_REMAP			(1 << 2)
#define CM_CTRL_RESET			(1 << 3)

/*
 * Integrator/AP,PP2 specific
 */
#define CM_CTRL_HIGHVECTORS		(1 << 4)
#define CM_CTRL_BIGENDIAN		(1 << 5)
#define CM_CTRL_FASTBUS			(1 << 6)
#define CM_CTRL_SYNC			(1 << 7)

/*
 * ARM926/946/966 Integrator/CP specific
 */
#define CM_CTRL_LCDBIASEN		(1 << 8)
#define CM_CTRL_LCDBIASUP		(1 << 9)
#define CM_CTRL_LCDBIASDN		(1 << 10)
#define CM_CTRL_LCDMUXSEL_MASK		(7 << 11)
#define CM_CTRL_LCDMUXSEL_GENLCD	(1 << 11)
#define CM_CTRL_LCDMUXSEL_VGA_16BPP	(2 << 11)
#define CM_CTRL_LCDMUXSEL_SHARPLCD	(3 << 11)
#define CM_CTRL_LCDMUXSEL_VGA_8421BPP	(4 << 11)
#define CM_CTRL_LCDEN0			(1 << 14)
#define CM_CTRL_LCDEN1			(1 << 15)
#define CM_CTRL_STATIC1			(1 << 16)
#define CM_CTRL_STATIC2			(1 << 17)
#define CM_CTRL_STATIC			(1 << 18)
#define CM_CTRL_n24BITEN		(1 << 19)
#define CM_CTRL_EBIWP			(1 << 20)
