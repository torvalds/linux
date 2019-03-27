/*
 * Offsets into structures used from asm.  Must be kept in sync with
 * appropriate headers.
 *
 * $FreeBSD$
 */

#define	FPRS_FEF	0x4

#define	CCR_MASK	0xff
#define	ASI_MASK	0xff

#define	UF_G0		0x0
#define	UF_G1		0x8
#define	UF_G2		0x10
#define	UF_G3		0x18
#define	UF_G4		0x20
#define	UF_G5		0x28
#define	UF_G6		0x30
#define	UF_G7		0x38
#define	UF_O0		0x40
#define	UF_O1		0x48
#define	UF_O2		0x50
#define	UF_O3		0x58
#define	UF_O4		0x60
#define	UF_O5		0x68
#define	UF_O6		0x70
#define	UF_O7		0x78
#define	UF_PC		0x80
#define	UF_NPC		0x88
#define	UF_SFAR		0x90
#define	UF_SFSR		0x98
#define	UF_TAR		0xa0
#define	UF_TYPE		0xa8
#define	UF_STATE	0xb0
#define	UF_FSR		0xb8
#define	UF_SIZEOF	0xc0

#define	SF_UC		0x0
