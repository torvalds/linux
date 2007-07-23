/*
 * linux/include/asm-arm/vfp.h
 *
 * VFP register definitions.
 * First, the standard VFP set.
 */

#define FPSID			cr0
#define FPSCR			cr1
#define FPEXC			cr8

/* FPSID bits */
#define FPSID_IMPLEMENTER_BIT	(24)
#define FPSID_IMPLEMENTER_MASK	(0xff << FPSID_IMPLEMENTER_BIT)
#define FPSID_SOFTWARE		(1<<23)
#define FPSID_FORMAT_BIT	(21)
#define FPSID_FORMAT_MASK	(0x3  << FPSID_FORMAT_BIT)
#define FPSID_NODOUBLE		(1<<20)
#define FPSID_ARCH_BIT		(16)
#define FPSID_ARCH_MASK		(0xF  << FPSID_ARCH_BIT)
#define FPSID_PART_BIT		(8)
#define FPSID_PART_MASK		(0xFF << FPSID_PART_BIT)
#define FPSID_VARIANT_BIT	(4)
#define FPSID_VARIANT_MASK	(0xF  << FPSID_VARIANT_BIT)
#define FPSID_REV_BIT		(0)
#define FPSID_REV_MASK		(0xF  << FPSID_REV_BIT)

/* FPEXC bits */
#define FPEXC_EX		(1 << 31)
#define FPEXC_EN		(1 << 30)

/* FPSCR bits */
#define FPSCR_DEFAULT_NAN	(1<<25)
#define FPSCR_FLUSHTOZERO	(1<<24)
#define FPSCR_ROUND_NEAREST	(0<<22)
#define FPSCR_ROUND_PLUSINF	(1<<22)
#define FPSCR_ROUND_MINUSINF	(2<<22)
#define FPSCR_ROUND_TOZERO	(3<<22)
#define FPSCR_RMODE_BIT		(22)
#define FPSCR_RMODE_MASK	(3 << FPSCR_RMODE_BIT)
#define FPSCR_STRIDE_BIT	(20)
#define FPSCR_STRIDE_MASK	(3 << FPSCR_STRIDE_BIT)
#define FPSCR_LENGTH_BIT	(16)
#define FPSCR_LENGTH_MASK	(7 << FPSCR_LENGTH_BIT)
#define FPSCR_IOE		(1<<8)
#define FPSCR_DZE		(1<<9)
#define FPSCR_OFE		(1<<10)
#define FPSCR_UFE		(1<<11)
#define FPSCR_IXE		(1<<12)
#define FPSCR_IDE		(1<<15)
#define FPSCR_IOC		(1<<0)
#define FPSCR_DZC		(1<<1)
#define FPSCR_OFC		(1<<2)
#define FPSCR_UFC		(1<<3)
#define FPSCR_IXC		(1<<4)
#define FPSCR_IDC		(1<<7)

/*
 * VFP9-S specific.
 */
#define FPINST			cr9
#define FPINST2			cr10

/* FPEXC bits */
#define FPEXC_FPV2		(1<<28)
#define FPEXC_LENGTH_BIT	(8)
#define FPEXC_LENGTH_MASK	(7 << FPEXC_LENGTH_BIT)
#define FPEXC_INV		(1 << 7)
#define FPEXC_UFC		(1 << 3)
#define FPEXC_OFC		(1 << 2)
#define FPEXC_IOC		(1 << 0)

/* Bit patterns for decoding the packaged operation descriptors */
#define VFPOPDESC_LENGTH_BIT	(9)
#define VFPOPDESC_LENGTH_MASK	(0x07 << VFPOPDESC_LENGTH_BIT)
#define VFPOPDESC_UNUSED_BIT	(24)
#define VFPOPDESC_UNUSED_MASK	(0xFF << VFPOPDESC_UNUSED_BIT)
#define VFPOPDESC_OPDESC_MASK	(~(VFPOPDESC_LENGTH_MASK | VFPOPDESC_UNUSED_MASK))
