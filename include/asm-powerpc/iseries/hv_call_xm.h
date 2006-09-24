/*
 * This file contains the "hypervisor call" interface which is used to
 * drive the hypervisor from SLIC.
 */
#ifndef _ASM_POWERPC_ISERIES_HV_CALL_XM_H
#define _ASM_POWERPC_ISERIES_HV_CALL_XM_H

#include <asm/iseries/hv_call_sc.h>
#include <asm/iseries/hv_types.h>

#define HvCallXmGetTceTableParms	HvCallXm +  0
#define HvCallXmTestBus			HvCallXm +  1
#define HvCallXmConnectBusUnit		HvCallXm +  2
#define HvCallXmLoadTod			HvCallXm +  8
#define HvCallXmTestBusUnit		HvCallXm +  9
#define HvCallXmSetTce			HvCallXm + 11
#define HvCallXmSetTces			HvCallXm + 13

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

#endif /* _ASM_POWERPC_ISERIES_HV_CALL_XM_H */
