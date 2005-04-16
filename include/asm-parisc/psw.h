#ifndef _PARISC_PSW_H
#define	PSW_I	0x00000001
#define	PSW_D	0x00000002
#define	PSW_P	0x00000004
#define	PSW_Q	0x00000008

#define	PSW_R	0x00000010
#define	PSW_F	0x00000020
#define	PSW_G	0x00000040	/* PA1.x only */
#define PSW_O	0x00000080	/* PA2.0 only */

#define PSW_CB	0x0000ff00

#define	PSW_M	0x00010000
#define	PSW_V	0x00020000
#define	PSW_C	0x00040000
#define	PSW_B	0x00080000

#define	PSW_X	0x00100000
#define	PSW_N	0x00200000
#define	PSW_L	0x00400000
#define	PSW_H	0x00800000

#define	PSW_T	0x01000000
#define	PSW_S	0x02000000
#define	PSW_E	0x04000000
#define PSW_W	0x08000000	/* PA2.0 only */
#define PSW_W_BIT       36      /* PA2.0 only */

#define	PSW_Z	0x40000000	/* PA1.x only */
#define	PSW_Y	0x80000000	/* PA1.x only */

#ifdef __LP64__
#define PSW_HI_CB 0x000000ff    /* PA2.0 only */
#endif

/* PSW bits to be used with ssm/rsm */
#define PSW_SM_I        0x1
#define PSW_SM_D        0x2
#define PSW_SM_P        0x4
#define PSW_SM_Q        0x8
#define PSW_SM_R        0x10
#define PSW_SM_F        0x20
#define PSW_SM_G        0x40
#define PSW_SM_O        0x80
#define PSW_SM_E        0x100
#define PSW_SM_W        0x200

#ifdef __LP64__
#  define USER_PSW      (PSW_C | PSW_Q | PSW_P | PSW_D | PSW_I)
#  define KERNEL_PSW    (PSW_W | PSW_C | PSW_Q | PSW_P | PSW_D)
#  define REAL_MODE_PSW (PSW_W | PSW_Q)
#  define USER_PSW_MASK (PSW_W | PSW_T | PSW_N | PSW_X | PSW_B | PSW_V | PSW_CB)
#  define USER_PSW_HI_MASK (PSW_HI_CB)
#else
#  define USER_PSW      (PSW_C | PSW_Q | PSW_P | PSW_D | PSW_I)
#  define KERNEL_PSW    (PSW_C | PSW_Q | PSW_P | PSW_D)
#  define REAL_MODE_PSW (PSW_Q)
#  define USER_PSW_MASK (PSW_T | PSW_N | PSW_X | PSW_B | PSW_V | PSW_CB)
#endif

#endif
