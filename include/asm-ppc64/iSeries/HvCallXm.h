/*
 * This file contains the "hypervisor call" interface which is used to
 * drive the hypervisor from SLIC.
 */
#ifndef _HVCALLXM_H
#define _HVCALLXM_H

#include <asm/iSeries/HvCallSc.h>
#include <asm/iSeries/HvTypes.h>

#define HvCallXmGetTceTableParms	HvCallXm +  0
#define HvCallXmTestBus			HvCallXm +  1
#define HvCallXmConnectBusUnit		HvCallXm +  2
#define HvCallXmLoadTod			HvCallXm +  8
#define HvCallXmTestBusUnit		HvCallXm +  9
#define HvCallXmSetTce			HvCallXm + 11
#define HvCallXmSetTces			HvCallXm + 13

/*
 * Structure passed to HvCallXm_getTceTableParms
 */
struct iommu_table_cb {
	unsigned long	itc_busno;	/* Bus number for this tce table */
	unsigned long	itc_start;	/* Will be NULL for secondary */
	unsigned long	itc_totalsize;	/* Size (in pages) of whole table */
	unsigned long	itc_offset;	/* Index into real tce table of the
					   start of our section */
	unsigned long	itc_size;	/* Size (in pages) of our section */
	unsigned long	itc_index;	/* Index of this tce table */
	unsigned short	itc_maxtables;	/* Max num of tables for partition */
	unsigned char	itc_virtbus;	/* Flag to indicate virtual bus */
	unsigned char	itc_slotno;	/* IOA Tce Slot Index */
	unsigned char	itc_rsvd[4];
};

static inline void HvCallXm_getTceTableParms(u64 cb)
{
	HvCall1(HvCallXmGetTceTableParms, cb);
}

static inline u64 HvCallXm_setTce(u64 tceTableToken, u64 tceOffset, u64 tce)
{
	return HvCall3(HvCallXmSetTce, tceTableToken, tceOffset, tce);
}

static inline u64 HvCallXm_setTces(u64 tceTableToken, u64 tceOffset,
		u64 numTces, u64 tce1, u64 tce2, u64 tce3, u64 tce4)
{
	return HvCall7(HvCallXmSetTces, tceTableToken, tceOffset, numTces,
			     tce1, tce2, tce3, tce4);
}

static inline u64 HvCallXm_testBus(u16 busNumber)
{
	return HvCall1(HvCallXmTestBus, busNumber);
}

static inline u64 HvCallXm_testBusUnit(u16 busNumber, u8 subBusNumber,
		u8 deviceId)
{
	return HvCall2(HvCallXmTestBusUnit, busNumber,
			(subBusNumber << 8) | deviceId);
}

static inline u64 HvCallXm_connectBusUnit(u16 busNumber, u8 subBusNumber,
		u8 deviceId, u64 interruptToken)
{
	return HvCall5(HvCallXmConnectBusUnit, busNumber,
			(subBusNumber << 8) | deviceId, interruptToken, 0,
			0 /* HvLpConfig::mapDsaToQueueIndex(HvLpDSA(busNumber, xBoard, xCard)) */);
}

static inline u64 HvCallXm_loadTod(void)
{
	return HvCall0(HvCallXmLoadTod);
}

#endif /* _HVCALLXM_H */
