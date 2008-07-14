#ifndef _SPARC64_CHAFSR_H
#define _SPARC64_CHAFSR_H

/* Cheetah Asynchronous Fault Status register, ASI=0x4C VA<63:0>=0x0 */

/* Comments indicate which processor variants on which the bit definition
 * is valid.  Codes are:
 * ch	-->	cheetah
 * ch+	-->	cheetah plus
 * jp	-->	jalapeno
 */

/* All bits of this register except M_SYNDROME and E_SYNDROME are
 * read, write 1 to clear.  M_SYNDROME and E_SYNDROME are read-only.
 */

/* Software bit set by linux trap handlers to indicate that the trap was
 * signalled at %tl >= 1.
 */
#define CHAFSR_TL1		(1UL << 63UL) /* n/a */

/* Unmapped error from system bus for prefetch queue or
 * store queue read operation
 */
#define CHPAFSR_DTO		(1UL << 59UL) /* ch+ */

/* Bus error from system bus for prefetch queue or store queue
 * read operation
 */
#define CHPAFSR_DBERR		(1UL << 58UL) /* ch+ */

/* Hardware corrected E-cache Tag ECC error */
#define CHPAFSR_THCE		(1UL << 57UL) /* ch+ */
/* System interface protocol error, hw timeout caused */
#define JPAFSR_JETO		(1UL << 57UL) /* jp */

/* SW handled correctable E-cache Tag ECC error */
#define CHPAFSR_TSCE		(1UL << 56UL) /* ch+ */
/* Parity error on system snoop results */
#define JPAFSR_SCE		(1UL << 56UL) /* jp */

/* Uncorrectable E-cache Tag ECC error */
#define CHPAFSR_TUE		(1UL << 55UL) /* ch+ */
/* System interface protocol error, illegal command detected */
#define JPAFSR_JEIC		(1UL << 55UL) /* jp */

/* Uncorrectable system bus data ECC error due to prefetch
 * or store fill request
 */
#define CHPAFSR_DUE		(1UL << 54UL) /* ch+ */
/* System interface protocol error, illegal ADTYPE detected */
#define JPAFSR_JEIT		(1UL << 54UL) /* jp */

/* Multiple errors of the same type have occurred.  This bit is set when
 * an uncorrectable error or a SW correctable error occurs and the status
 * bit to report that error is already set.  When multiple errors of
 * different types are indicated by setting multiple status bits.
 *
 * This bit is not set if multiple HW corrected errors with the same
 * status bit occur, only uncorrectable and SW correctable ones have
 * this behavior.
 *
 * This bit is not set when multiple ECC errors happen within a single
 * 64-byte system bus transaction.  Only the first ECC error in a 16-byte
 * subunit will be logged.  All errors in subsequent 16-byte subunits
 * from the same 64-byte transaction are ignored.
 */
#define CHAFSR_ME		(1UL << 53UL) /* ch,ch+,jp */

/* Privileged state error has occurred.  This is a capture of PSTATE.PRIV
 * at the time the error is detected.
 */
#define CHAFSR_PRIV		(1UL << 52UL) /* ch,ch+,jp */

/* The following bits 51 (CHAFSR_PERR) to 33 (CHAFSR_CE) are sticky error
 * bits and record the most recently detected errors.  Bits accumulate
 * errors that have been detected since the last write to clear the bit.
 */

/* System interface protocol error.  The processor asserts its' ERROR
 * pin when this event occurs and it also logs a specific cause code
 * into a JTAG scannable flop.
 */
#define CHAFSR_PERR		(1UL << 51UL) /* ch,ch+,jp */

/* Internal processor error.  The processor asserts its' ERROR
 * pin when this event occurs and it also logs a specific cause code
 * into a JTAG scannable flop.
 */
#define CHAFSR_IERR		(1UL << 50UL) /* ch,ch+,jp */

/* System request parity error on incoming address */
#define CHAFSR_ISAP		(1UL << 49UL) /* ch,ch+,jp */

/* HW Corrected system bus MTAG ECC error */
#define CHAFSR_EMC		(1UL << 48UL) /* ch,ch+ */
/* Parity error on L2 cache tag SRAM */
#define JPAFSR_ETP		(1UL << 48UL) /* jp */

/* Uncorrectable system bus MTAG ECC error */
#define CHAFSR_EMU		(1UL << 47UL) /* ch,ch+ */
/* Out of range memory error has occurred */
#define JPAFSR_OM		(1UL << 47UL) /* jp */

/* HW Corrected system bus data ECC error for read of interrupt vector */
#define CHAFSR_IVC		(1UL << 46UL) /* ch,ch+ */
/* Error due to unsupported store */
#define JPAFSR_UMS		(1UL << 46UL) /* jp */

/* Uncorrectable system bus data ECC error for read of interrupt vector */
#define CHAFSR_IVU		(1UL << 45UL) /* ch,ch+,jp */

/* Unmapped error from system bus */
#define CHAFSR_TO		(1UL << 44UL) /* ch,ch+,jp */

/* Bus error response from system bus */
#define CHAFSR_BERR		(1UL << 43UL) /* ch,ch+,jp */

/* SW Correctable E-cache ECC error for instruction fetch or data access
 * other than block load.
 */
#define CHAFSR_UCC		(1UL << 42UL) /* ch,ch+,jp */

/* Uncorrectable E-cache ECC error for instruction fetch or data access
 * other than block load.
 */
#define CHAFSR_UCU		(1UL << 41UL) /* ch,ch+,jp */

/* Copyout HW Corrected ECC error */
#define CHAFSR_CPC		(1UL << 40UL) /* ch,ch+,jp */

/* Copyout Uncorrectable ECC error */
#define CHAFSR_CPU		(1UL << 39UL) /* ch,ch+,jp */

/* HW Corrected ECC error from E-cache for writeback */
#define CHAFSR_WDC		(1UL << 38UL) /* ch,ch+,jp */

/* Uncorrectable ECC error from E-cache for writeback */
#define CHAFSR_WDU		(1UL << 37UL) /* ch,ch+,jp */

/* HW Corrected ECC error from E-cache for store merge or block load */
#define CHAFSR_EDC		(1UL << 36UL) /* ch,ch+,jp */

/* Uncorrectable ECC error from E-cache for store merge or block load */
#define CHAFSR_EDU		(1UL << 35UL) /* ch,ch+,jp */

/* Uncorrectable system bus data ECC error for read of memory or I/O */
#define CHAFSR_UE		(1UL << 34UL) /* ch,ch+,jp */

/* HW Corrected system bus data ECC error for read of memory or I/O */
#define CHAFSR_CE		(1UL << 33UL) /* ch,ch+,jp */

/* Uncorrectable ECC error from remote cache/memory */
#define JPAFSR_RUE		(1UL << 32UL) /* jp */

/* Correctable ECC error from remote cache/memory */
#define JPAFSR_RCE		(1UL << 31UL) /* jp */

/* JBUS parity error on returned read data */
#define JPAFSR_BP		(1UL << 30UL) /* jp */

/* JBUS parity error on data for writeback or block store */
#define JPAFSR_WBP		(1UL << 29UL) /* jp */

/* Foreign read to DRAM incurring correctable ECC error */
#define JPAFSR_FRC		(1UL << 28UL) /* jp */

/* Foreign read to DRAM incurring uncorrectable ECC error */
#define JPAFSR_FRU		(1UL << 27UL) /* jp */

#define CHAFSR_ERRORS		(CHAFSR_PERR | CHAFSR_IERR | CHAFSR_ISAP | CHAFSR_EMC | \
				 CHAFSR_EMU | CHAFSR_IVC | CHAFSR_IVU | CHAFSR_TO | \
				 CHAFSR_BERR | CHAFSR_UCC | CHAFSR_UCU | CHAFSR_CPC | \
				 CHAFSR_CPU | CHAFSR_WDC | CHAFSR_WDU | CHAFSR_EDC | \
				 CHAFSR_EDU | CHAFSR_UE | CHAFSR_CE)
#define CHPAFSR_ERRORS		(CHPAFSR_DTO | CHPAFSR_DBERR | CHPAFSR_THCE | \
				 CHPAFSR_TSCE | CHPAFSR_TUE | CHPAFSR_DUE | \
				 CHAFSR_PERR | CHAFSR_IERR | CHAFSR_ISAP | CHAFSR_EMC | \
				 CHAFSR_EMU | CHAFSR_IVC | CHAFSR_IVU | CHAFSR_TO | \
				 CHAFSR_BERR | CHAFSR_UCC | CHAFSR_UCU | CHAFSR_CPC | \
				 CHAFSR_CPU | CHAFSR_WDC | CHAFSR_WDU | CHAFSR_EDC | \
				 CHAFSR_EDU | CHAFSR_UE | CHAFSR_CE)
#define JPAFSR_ERRORS		(JPAFSR_JETO | JPAFSR_SCE | JPAFSR_JEIC | \
				 JPAFSR_JEIT | CHAFSR_PERR | CHAFSR_IERR | \
				 CHAFSR_ISAP | JPAFSR_ETP | JPAFSR_OM | \
				 JPAFSR_UMS | CHAFSR_IVU | CHAFSR_TO | \
				 CHAFSR_BERR | CHAFSR_UCC | CHAFSR_UCU | \
				 CHAFSR_CPC | CHAFSR_CPU | CHAFSR_WDC | \
				 CHAFSR_WDU | CHAFSR_EDC | CHAFSR_EDU | \
				 CHAFSR_UE | CHAFSR_CE | JPAFSR_RUE | \
				 JPAFSR_RCE | JPAFSR_BP | JPAFSR_WBP | \
				 JPAFSR_FRC | JPAFSR_FRU)

/* Active JBUS request signal when error occurred */
#define JPAFSR_JBREQ		(0x7UL << 24UL) /* jp */
#define JPAFSR_JBREQ_SHIFT	24UL

/* L2 cache way information */
#define JPAFSR_ETW		(0x3UL << 22UL) /* jp */
#define JPAFSR_ETW_SHIFT	22UL

/* System bus MTAG ECC syndrome.  This field captures the status of the
 * first occurrence of the highest-priority error according to the M_SYND
 * overwrite policy.  After the AFSR sticky bit, corresponding to the error
 * for which the M_SYND is reported, is cleared, the contents of the M_SYND
 * field will be unchanged by will be unfrozen for further error capture.
 */
#define CHAFSR_M_SYNDROME	(0xfUL << 16UL) /* ch,ch+,jp */
#define CHAFSR_M_SYNDROME_SHIFT	16UL

/* Agenid Id of the foreign device causing the UE/CE errors */
#define JPAFSR_AID		(0x1fUL << 9UL) /* jp */
#define JPAFSR_AID_SHIFT	9UL

/* System bus or E-cache data ECC syndrome.  This field captures the status
 * of the first occurrence of the highest-priority error according to the
 * E_SYND overwrite policy.  After the AFSR sticky bit, corresponding to the
 * error for which the E_SYND is reported, is cleare, the contents of the E_SYND
 * field will be unchanged but will be unfrozen for further error capture.
 */
#define CHAFSR_E_SYNDROME	(0x1ffUL << 0UL) /* ch,ch+,jp */
#define CHAFSR_E_SYNDROME_SHIFT	0UL

/* The AFSR must be explicitly cleared by software, it is not cleared automatically
 * by a read.  Writes to bits <51:33> with bits set will clear the corresponding
 * bits in the AFSR.  Bits associated with disrupting traps must be cleared before
 * interrupts are re-enabled to prevent multiple traps for the same error.  I.e.
 * PSTATE.IE and AFSR bits control delivery of disrupting traps.
 *
 * Since there is only one AFAR, when multiple events have been logged by the
 * bits in the AFSR, at most one of these events will have its status captured
 * in the AFAR.  The highest priority of those event bits will get AFAR logging.
 * The AFAR will be unlocked and available to capture the address of another event
 * as soon as the one bit in AFSR that corresponds to the event logged in AFAR is
 * cleared.  For example, if AFSR.CE is detected, then AFSR.UE (which overwrites
 * the AFAR), and AFSR.UE is cleared by not AFSR.CE, then the AFAR will be unlocked
 * and ready for another event, even though AFSR.CE is still set.  The same rules
 * also apply to the M_SYNDROME and E_SYNDROME fields of the AFSR.
 */

#endif /* _SPARC64_CHAFSR_H */
