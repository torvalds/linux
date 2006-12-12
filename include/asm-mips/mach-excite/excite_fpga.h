#ifndef EXCITE_FPGA_H_INCLUDED
#define EXCITE_FPGA_H_INCLUDED


/**
 * Adress alignment of the individual FPGA bytes.
 * The address arrangement of the individual bytes of the FPGA is two
 * byte aligned at the embedded MK2 platform.
 */
#ifdef EXCITE_CCI_FPGA_MK2
typedef unsigned char excite_cci_fpga_align_t __attribute__ ((aligned(2)));
#else
typedef unsigned char excite_cci_fpga_align_t;
#endif


/**
 * Size of Dual Ported RAM.
 */
#define EXCITE_DPR_SIZE 263


/**
 * Size of Reserved Status Fields in Dual Ported RAM.
 */
#define EXCITE_DPR_STATUS_SIZE 7



/**
 * FPGA.
 * Hardware register layout of the FPGA interface. The FPGA must accessed
 * byte wise solely.
 * @see EXCITE_CCI_DPR_MK2
 */
typedef struct excite_fpga {

	/**
	 * Dual Ported RAM.
	 */
	excite_cci_fpga_align_t dpr[EXCITE_DPR_SIZE];

	/**
	 * Status.
	 */
	excite_cci_fpga_align_t status[EXCITE_DPR_STATUS_SIZE];

#ifdef EXCITE_CCI_FPGA_MK2
	/**
	 * RM9000 Interrupt.
	 * Write access initiates interrupt at the RM9000 (MIPS) processor of the eXcite.
	 */
	excite_cci_fpga_align_t rm9k_int;
#else
	/**
	 * MK2 Interrupt.
	 * Write access initiates interrupt at the ARM processor of the MK2.
	 */
	excite_cci_fpga_align_t mk2_int;

	excite_cci_fpga_align_t gap[0x1000-0x10f];

	/**
	 * IRQ Source/Acknowledge.
	 */
	excite_cci_fpga_align_t rm9k_irq_src;

	/**
	 * IRQ Mask.
	 * Set bits enable the related interrupt.
	 */
	excite_cci_fpga_align_t rm9k_irq_mask;
#endif


} excite_fpga;



#endif	/* ndef EXCITE_FPGA_H_INCLUDED */
