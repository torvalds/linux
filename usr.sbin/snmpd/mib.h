/*	$OpenBSD: mib.h,v 1.44 2024/01/27 09:53:59 martijn Exp $	*/

/*
 * Copyright (c) 2007, 2008 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SNMPD_MIB_H
#define SNMPD_MIB_H

#include <stddef.h>

struct ber_oid;
enum mib_oidfmt {
	MIB_OIDNUMERIC,
	MIB_OIDSYMBOLIC
};

void		 mib_parsefile(const char *);
void		 mib_parsedir(const char *);
void		 mib_resolve(void);
void		 mib_clear(void);
char		*mib_oid2string(struct ber_oid *, char *, size_t,
		    enum mib_oidfmt);
const char	*mib_string2oid(const char *, struct ber_oid *);

#define MIBDECL(...)		{ { MIB_##__VA_ARGS__ },		\
    (sizeof((uint32_t []) { MIB_##__VA_ARGS__ }) / sizeof(uint32_t))}, #__VA_ARGS__
#define MIBEND			{ { 0 } }, NULL

/*
 * Adding new MIBs:
 * - add the OID definitions below
 * - add the OIDs to the MIB_TREE table at the end of this file
 * - optional: write the implementation in mib.c
 */

/* From the SNMPv2-SMI MIB */
#define MIB_iso				1
#define MIB_org				MIB_iso, 3
#define MIB_dod				MIB_org, 6
#define MIB_internet			MIB_dod, 1
#define MIB_directory			MIB_internet, 1
#define MIB_mgmt			MIB_internet, 2
#define MIB_mib_2			MIB_mgmt, 1	/* XXX mib-2 */
#define MIB_system			MIB_mib_2, 1
#define OIDIDX_system			7
#define MIB_sysDescr			MIB_system, 1
#define MIB_sysOID			MIB_system, 2
#define MIB_sysUpTime			MIB_system, 3
#define MIB_sysContact			MIB_system, 4
#define MIB_sysName			MIB_system, 5
#define MIB_sysLocation			MIB_system, 6
#define MIB_sysServices			MIB_system, 7
#define MIB_sysORLastChange		MIB_system, 8
#define MIB_sysORTable			MIB_system, 9
#define MIB_sysOREntry			MIB_sysORTable, 1
#define OIDIDX_sysOR			9
#define OIDIDX_sysOREntry		10
#define MIB_sysORIndex			MIB_sysOREntry, 1
#define MIB_sysORID			MIB_sysOREntry, 2
#define MIB_sysORDescr			MIB_sysOREntry, 3
#define MIB_sysORUpTime			MIB_sysOREntry, 4
#define MIB_transmission		MIB_mib_2, 10
#define MIB_snmp			MIB_mib_2, 11
#define OIDIDX_snmp			7
#define MIB_snmpInPkts			MIB_snmp, 1
#define MIB_snmpOutPkts			MIB_snmp, 2
#define MIB_snmpInBadVersions		MIB_snmp, 3
#define MIB_snmpInBadCommunityNames	MIB_snmp, 4
#define MIB_snmpInBadCommunityUses	MIB_snmp, 5
#define MIB_snmpInASNParseErrs		MIB_snmp, 6
#define MIB_snmpInTooBigs		MIB_snmp, 8
#define MIB_snmpInNoSuchNames		MIB_snmp, 9
#define MIB_snmpInBadValues		MIB_snmp, 10
#define MIB_snmpInReadOnlys		MIB_snmp, 11
#define MIB_snmpInGenErrs		MIB_snmp, 12
#define MIB_snmpInTotalReqVars		MIB_snmp, 13
#define MIB_snmpInTotalSetVars		MIB_snmp, 14
#define MIB_snmpInGetRequests		MIB_snmp, 15
#define MIB_snmpInGetNexts		MIB_snmp, 16
#define MIB_snmpInSetRequests		MIB_snmp, 17
#define MIB_snmpInGetResponses		MIB_snmp, 18
#define MIB_snmpInTraps			MIB_snmp, 19
#define MIB_snmpOutTooBigs		MIB_snmp, 20
#define MIB_snmpOutNoSuchNames		MIB_snmp, 21
#define MIB_snmpOutBadValues		MIB_snmp, 22
#define MIB_snmpOutGenErrs		MIB_snmp, 24
#define MIB_snmpOutGetRequests		MIB_snmp, 25
#define MIB_snmpOutGetNexts		MIB_snmp, 26
#define MIB_snmpOutSetRequests		MIB_snmp, 27
#define MIB_snmpOutGetResponses		MIB_snmp, 28
#define MIB_snmpOutTraps		MIB_snmp, 29
#define MIB_snmpEnableAuthenTraps	MIB_snmp, 30
#define MIB_snmpSilentDrops		MIB_snmp, 31
#define MIB_snmpProxyDrops		MIB_snmp, 32
#define MIB_experimental		MIB_internet, 3
#define MIB_private			MIB_internet, 4
#define MIB_enterprises			MIB_private, 1
#define MIB_security			MIB_internet, 5
#define MIB_snmpV2			MIB_internet, 6
#define MIB_snmpDomains			MIB_snmpV2, 1
#define MIB_snmpProxies			MIB_snmpV2, 2
#define MIB_snmpModules			MIB_snmpV2, 3
#define MIB_snmpMIB			MIB_snmpModules, 1
#define MIB_snmpMIBObjects		MIB_snmpMIB, 1
#define MIB_snmpTrap			MIB_snmpMIBObjects, 4
#define MIB_snmpTrapOID			MIB_snmpTrap, 1
#define MIB_snmpTrapEnterprise		MIB_snmpTrap, 3
#define MIB_snmpTraps			MIB_snmpMIBObjects, 5
#define MIB_coldStart			MIB_snmpTraps, 1
#define MIB_warmStart			MIB_snmpTraps, 2
#define MIB_linkDown			MIB_snmpTraps, 3
#define MIB_linkUp			MIB_snmpTraps, 4
#define MIB_authenticationFailure	MIB_snmpTraps, 5
#define MIB_egpNeighborLoss		MIB_snmpTraps, 6

/* SNMP-USER-BASED-SM-MIB */
#define MIB_framework			MIB_snmpModules, 10
#define MIB_frameworkObjects		MIB_framework, 2
#define OIDIDX_snmpEngine		9
#define MIB_snmpEngine			MIB_frameworkObjects, 1
#define MIB_snmpEngineID		MIB_snmpEngine, 1
#define MIB_snmpEngineBoots		MIB_snmpEngine, 2
#define MIB_snmpEngineTime		MIB_snmpEngine, 3
#define MIB_snmpEngineMaxMsgSize	MIB_snmpEngine, 4
#define MIB_usm				MIB_snmpModules, 15
#define MIB_usmObjects			MIB_usm, 1
#define MIB_usmStats			MIB_usmObjects, 1
#define OIDIDX_usmStats			9
#define OIDVAL_usmErrSecLevel		1
#define OIDVAL_usmErrTimeWindow		2
#define OIDVAL_usmErrUserName		3
#define OIDVAL_usmErrEngineId		4
#define OIDVAL_usmErrDigest		5
#define OIDVAL_usmErrDecrypt		6
#define MIB_usmStatsUnsupportedSecLevels MIB_usmStats, OIDVAL_usmErrSecLevel
#define MIB_usmStatsNotInTimeWindow	MIB_usmStats, OIDVAL_usmErrTimeWindow
#define MIB_usmStatsUnknownUserNames	MIB_usmStats, OIDVAL_usmErrUserName
#define MIB_usmStatsUnknownEngineId	MIB_usmStats, OIDVAL_usmErrEngineId
#define MIB_usmStatsWrongDigests	MIB_usmStats, OIDVAL_usmErrDigest
#define MIB_usmStatsDecryptionErrors	MIB_usmStats, OIDVAL_usmErrDecrypt

/* SNMP-TARGET-MIB */
#define MIB_snmpTargetMIB		MIB_snmpModules, 12
#define MIB_snmpTargetObjects		MIB_snmpTargetMIB, 1
#define MIB_snmpTargetSpinLock		MIB_snmpTargetObjects, 1
#define MIB_snmpTargetAddrTable		MIB_snmpTargetObjects, 2
#define MIB_snmpTargetAddrEntry		MIB_snmpTargetAddrTable, 1
#define MIB_snmpTargetAddrName		MIB_snmpTargetAddrEntry, 1
#define MIB_snmpTargetAddrTDomain	MIB_snmpTargetAddrEntry, 2
#define MIB_snmpTargetAddrTAddress	MIB_snmpTargetAddrEntry, 3
#define MIB_snmpTargetAddrTimeout	MIB_snmpTargetAddrEntry, 4
#define MIB_snmpTargetAddrRetryCount	MIB_snmpTargetAddrEntry, 5
#define MIB_snmpTargetAddrTagList	MIB_snmpTargetAddrEntry, 6
#define MIB_snmpTargetAddrParams	MIB_snmpTargetAddrEntry, 7
#define MIB_snmpTargetAddrStorageType	MIB_snmpTargetAddrEntry, 8
#define MIB_snmpTargetAddrRowStatus	MIB_snmpTargetAddrEntry, 9
#define MIB_snmpTargetParamsTable	MIB_snmpTargetObjects, 3
#define MIB_snmpTargetParamsEntry	MIB_snmpTargetParamsTable, 1
#define MIB_snmpTargetParamsName	MIB_snmpTargetParamsEntry, 1
#define MIB_snmpTargetParamsMPModel	MIB_snmpTargetParamsEntry, 2
#define MIB_snmpTargetParamsSecurityModel	MIB_snmpTargetParamsEntry, 3
#define MIB_snmpTargetParamsSecurityName	MIB_snmpTargetParamsEntry, 4
#define MIB_snmpTargetParamsSecurityLevel	MIB_snmpTargetParamsEntry, 5
#define MIB_snmpTargetParamsStorageType	MIB_snmpTargetParamsEntry, 6
#define MIB_snmpTargetParamsRowStatus	MIB_snmpTargetParamsEntry, 7
#define MIB_snmpUnavailableContexts	MIB_snmpTargetObjects, 4
#define MIB_snmpUnknownContexts		MIB_snmpTargetObjects, 5

/* HOST-RESOURCES-MIB */
#define MIB_host			MIB_mib_2, 25
#define MIB_hrSystem			MIB_host, 1
#define OIDIDX_hrsystem			8
#define MIB_hrSystemUptime		MIB_hrSystem, 1
#define MIB_hrSystemDate		MIB_hrSystem, 2
#define MIB_hrSystemInitialLoadDevice	MIB_hrSystem, 3
#define MIB_hrSystemInitialLoadParameters MIB_hrSystem, 4
#define MIB_hrSystemNumUsers		MIB_hrSystem, 5
#define MIB_hrSystemProcesses		MIB_hrSystem, 6
#define MIB_hrSystemMaxProcesses	MIB_hrSystem, 7
#define MIB_hrStorage			MIB_host, 2
#define MIB_hrStorageTypes		MIB_hrStorage, 1
#define MIB_hrStorageOther		MIB_hrStorageTypes, 1
#define MIB_hrStorageRam		MIB_hrStorageTypes, 2
#define MIB_hrStorageVirtualMemory	MIB_hrStorageTypes, 3
#define MIB_hrStorageFixedDisk		MIB_hrStorageTypes, 4
#define MIB_hrStorageRemovableDisk	MIB_hrStorageTypes, 5
#define MIB_hrStorageFloppyDisk		MIB_hrStorageTypes, 6
#define MIB_hrStorageCompactDisc	MIB_hrStorageTypes, 7
#define MIB_hrStorageRamDisk		MIB_hrStorageTypes, 8
#define MIB_hrStorageFlashMemory	MIB_hrStorageTypes, 9
#define MIB_hrStorageNetworkDisk	MIB_hrStorageTypes, 10
#define MIB_hrMemorySize		MIB_hrStorage, 2
#define MIB_hrStorageTable		MIB_hrStorage, 3
#define MIB_hrStorageEntry		MIB_hrStorageTable, 1
#define OIDIDX_hrStorage		10
#define OIDIDX_hrStorageEntry		11
#define MIB_hrStorageIndex		MIB_hrStorageEntry, 1
#define MIB_hrStorageType		MIB_hrStorageEntry, 2
#define MIB_hrStorageDescr		MIB_hrStorageEntry, 3
#define MIB_hrStorageAllocationUnits	MIB_hrStorageEntry, 4
#define MIB_hrStorageSize		MIB_hrStorageEntry, 5
#define MIB_hrStorageUsed		MIB_hrStorageEntry, 6
#define MIB_hrStorageAllocationFailures	MIB_hrStorageEntry, 7
#define MIB_hrDevice			MIB_host, 3
#define MIB_hrDeviceTypes		MIB_hrDevice, 1
#define MIB_hrDeviceOther		MIB_hrDeviceTypes, 1
#define MIB_hrDeviceUnknown		MIB_hrDeviceTypes, 2
#define MIB_hrDeviceProcessor		MIB_hrDeviceTypes, 3
#define MIB_hrDeviceNetwork		MIB_hrDeviceTypes, 4
#define MIB_hrDevicePrinter		MIB_hrDeviceTypes, 5
#define MIB_hrDeviceDiskStorage		MIB_hrDeviceTypes, 6
#define MIB_hrDeviceVideo		MIB_hrDeviceTypes, 10
#define MIB_hrDeviceAudio		MIB_hrDeviceTypes, 11
#define MIB_hrDeviceCoprocessor		MIB_hrDeviceTypes, 12
#define MIB_hrDeviceKeyboard		MIB_hrDeviceTypes, 13
#define MIB_hrDeviceModem		MIB_hrDeviceTypes, 14
#define MIB_hrDeviceParallelPort	MIB_hrDeviceTypes, 15
#define MIB_hrDevicePointing		MIB_hrDeviceTypes, 16
#define MIB_hrDeviceSerialPort		MIB_hrDeviceTypes, 17
#define MIB_hrDeviceTape		MIB_hrDeviceTypes, 18
#define MIB_hrDeviceClock		MIB_hrDeviceTypes, 19
#define MIB_hrDeviceVolatileMemory	MIB_hrDeviceTypes, 20
#define MIB_hrDeviceNonVolatileMemory	MIB_hrDeviceTypes, 21
#define MIB_hrDeviceTable		MIB_hrDevice, 2
#define MIB_hrDeviceEntry		MIB_hrDeviceTable, 1
#define OIDIDX_hrDevice			10
#define OIDIDX_hrDeviceEntry		11
#define MIB_hrDeviceIndex		MIB_hrDeviceEntry, 1
#define MIB_hrDeviceType		MIB_hrDeviceEntry, 2
#define MIB_hrDeviceDescr		MIB_hrDeviceEntry, 3
#define MIB_hrDeviceID			MIB_hrDeviceEntry, 4
#define MIB_hrDeviceStatus		MIB_hrDeviceEntry, 5
#define MIB_hrDeviceErrors		MIB_hrDeviceEntry, 6
#define MIB_hrProcessorTable		MIB_hrDevice, 3
#define MIB_hrProcessorEntry		MIB_hrProcessorTable, 1
#define OIDIDX_hrProcessor		10
#define OIDIDX_hrProcessorEntry		11
#define MIB_hrProcessorFrwID		MIB_hrProcessorEntry, 1
#define MIB_hrProcessorLoad		MIB_hrProcessorEntry, 2
#define MIB_hrSWRun			MIB_host, 4
#define MIB_hrSWOSIndex			MIB_hrSWRun, 1
#define MIB_hrSWRunTable		MIB_hrSWRun, 2
#define MIB_hrSWRunEntry		MIB_hrSWRunTable, 1
#define OIDIDX_hrSWRun			10
#define OIDIDX_hrSWRunEntry		11
#define MIB_hrSWRunIndex		MIB_hrSWRunEntry, 1
#define MIB_hrSWRunName			MIB_hrSWRunEntry, 2
#define MIB_hrSWRunID			MIB_hrSWRunEntry, 3
#define MIB_hrSWRunPath			MIB_hrSWRunEntry, 4
#define MIB_hrSWRunParameters		MIB_hrSWRunEntry, 5
#define MIB_hrSWRunType			MIB_hrSWRunEntry, 6
#define MIB_hrSWRunStatus		MIB_hrSWRunEntry, 7
#define MIB_hrSWRunPerf			MIB_host, 5
#define MIB_hrSWRunPerfTable		MIB_hrSWRunPerf, 1
#define OIDIDX_hrSWRunPerf		10
#define OIDIDX_hrSWRunPerfEntry		11
#define MIB_hrSWRunPerfEntry		MIB_hrSWRunPerfTable, 1
#define MIB_hrSWRunPerfCPU		MIB_hrSWRunPerfEntry, 1
#define MIB_hrSWRunPerfMem		MIB_hrSWRunPerfEntry, 2
#define MIB_hrSWInstalled		MIB_host, 6
#define MIB_hrMIBAdminInfo		MIB_host, 7

/* IF-MIB */
#define MIB_ifMIB			MIB_mib_2, 31
#define MIB_ifMIBObjects		MIB_ifMIB, 1
#define MIB_ifXTable			MIB_ifMIBObjects, 1
#define MIB_ifXEntry			MIB_ifXTable, 1
#define OIDIDX_ifX			10
#define OIDIDX_ifXEntry			11
#define MIB_ifName			MIB_ifXEntry, 1
#define MIB_ifInMulticastPkts		MIB_ifXEntry, 2
#define MIB_ifInBroadcastPkts		MIB_ifXEntry, 3
#define MIB_ifOutMulticastPkts		MIB_ifXEntry, 4
#define MIB_ifOutBroadcastPkts		MIB_ifXEntry, 5
#define MIB_ifHCInOctets		MIB_ifXEntry, 6
#define MIB_ifHCInUcastPkts		MIB_ifXEntry, 7
#define MIB_ifHCInMulticastPkts		MIB_ifXEntry, 8
#define MIB_ifHCInBroadcastPkts		MIB_ifXEntry, 9
#define MIB_ifHCOutOctets		MIB_ifXEntry, 10
#define MIB_ifHCOutUcastPkts		MIB_ifXEntry, 11
#define MIB_ifHCOutMulticastPkts	MIB_ifXEntry, 12
#define MIB_ifHCOutBroadcastPkts	MIB_ifXEntry, 13
#define MIB_ifLinkUpDownTrapEnable	MIB_ifXEntry, 14
#define MIB_ifHighSpeed			MIB_ifXEntry, 15
#define MIB_ifPromiscuousMode		MIB_ifXEntry, 16
#define MIB_ifConnectorPresent		MIB_ifXEntry, 17
#define MIB_ifAlias			MIB_ifXEntry, 18
#define MIB_ifCounterDiscontinuityTime	MIB_ifXEntry, 19
#define MIB_ifStackTable		MIB_ifMIBObjects, 2
#define MIB_ifStackEntry		MIB_ifStackTable, 1
#define OIDIDX_ifStack			10
#define OIDIDX_ifStackEntry		11
#define MIB_ifStackStatus		MIB_ifStackEntry, 3
#define MIB_ifRcvAddressTable		MIB_ifMIBObjects, 4
#define MIB_ifRcvAddressEntry		MIB_ifRcvAddressTable, 1
#define OIDIDX_ifRcvAddress		10
#define OIDIDX_ifRcvAddressEntry	11
#define MIB_ifRcvAddressStatus		MIB_ifRcvAddressEntry, 2
#define MIB_ifRcvAddressType		MIB_ifRcvAddressEntry, 3
#define MIB_ifStackLastChange		MIB_ifMIBObjects, 6
#define MIB_interfaces			MIB_mib_2, 2
#define MIB_ifNumber			MIB_interfaces, 1
#define MIB_ifTable			MIB_interfaces, 2
#define MIB_ifEntry			MIB_ifTable, 1
#define OIDIDX_if			9
#define OIDIDX_ifEntry			10
#define MIB_ifIndex			MIB_ifEntry, 1
#define MIB_ifDescr			MIB_ifEntry, 2
#define MIB_ifType			MIB_ifEntry, 3
#define MIB_ifMtu			MIB_ifEntry, 4
#define MIB_ifSpeed			MIB_ifEntry, 5
#define MIB_ifPhysAddress		MIB_ifEntry, 6
#define MIB_ifAdminStatus		MIB_ifEntry, 7
#define MIB_ifOperStatus		MIB_ifEntry, 8
#define MIB_ifLastChange		MIB_ifEntry, 9
#define MIB_ifInOctets			MIB_ifEntry, 10
#define MIB_ifInUcastPkts		MIB_ifEntry, 11
#define MIB_ifInNUcastPkts		MIB_ifEntry, 12
#define MIB_ifInDiscards		MIB_ifEntry, 13
#define MIB_ifInErrors			MIB_ifEntry, 14
#define MIB_ifInUnknownProtos		MIB_ifEntry, 15
#define MIB_ifOutOctets			MIB_ifEntry, 16
#define MIB_ifOutUcastPkts		MIB_ifEntry, 17
#define MIB_ifOutNUcastPkts		MIB_ifEntry, 18
#define MIB_ifOutDiscards		MIB_ifEntry, 19
#define MIB_ifOutErrors			MIB_ifEntry, 20
#define MIB_ifOutQLen			MIB_ifEntry, 21
#define MIB_ifSpecific			MIB_ifEntry, 22

/* IP-MIB */
#define MIB_ipMIB			MIB_mib_2, 4
#define OIDIDX_ip			7
#define MIB_ipForwarding		MIB_ipMIB, 1
#define MIB_ipDefaultTTL		MIB_ipMIB, 2
#define MIB_ipInReceives		MIB_ipMIB, 3
#define MIB_ipInHdrErrors		MIB_ipMIB, 4
#define MIB_ipInAddrErrors		MIB_ipMIB, 5
#define MIB_ipForwDatagrams		MIB_ipMIB, 6
#define MIB_ipInUnknownProtos		MIB_ipMIB, 7
#define MIB_ipInDiscards		MIB_ipMIB, 8
#define MIB_ipInDelivers		MIB_ipMIB, 9
#define MIB_ipOutRequests		MIB_ipMIB, 10
#define MIB_ipOutDiscards		MIB_ipMIB, 11
#define MIB_ipOutNoRoutes		MIB_ipMIB, 12
#define MIB_ipReasmTimeout		MIB_ipMIB, 13
#define MIB_ipReasmReqds		MIB_ipMIB, 14
#define MIB_ipReasmOKs			MIB_ipMIB, 15
#define MIB_ipReasmFails		MIB_ipMIB, 16
#define MIB_ipFragOKs			MIB_ipMIB, 17
#define MIB_ipFragFails			MIB_ipMIB, 18
#define MIB_ipFragCreates		MIB_ipMIB, 19
#define MIB_ipAddrTable			MIB_ipMIB, 20
#define MIB_ipAddrEntry			MIB_ipAddrTable, 1
#define OIDIDX_ipAddr			9
#define OIDIDX_ipAddrEntry		10
#define MIB_ipAdEntAddr			MIB_ipAddrEntry, 1
#define MIB_ipAdEntIfIndex		MIB_ipAddrEntry, 2
#define MIB_ipAdEntNetMask		MIB_ipAddrEntry, 3
#define MIB_ipAdEntBcastAddr		MIB_ipAddrEntry, 4
#define MIB_ipAdEntReasmMaxSize		MIB_ipAddrEntry, 5
#define MIB_ipNetToMediaTable		MIB_ipMIB, 22
#define MIB_ipNetToMediaEntry		MIB_ipNetToMediaTable, 1
#define OIDIDX_ipNetToMedia		9
#define MIB_ipNetToMediaIfIndex		MIB_ipNetToMediaEntry, 1
#define MIB_ipNetToMediaPhysAddress	MIB_ipNetToMediaEntry, 2
#define MIB_ipNetToMediaNetAddress	MIB_ipNetToMediaEntry, 3
#define MIB_ipNetToMediaType		MIB_ipNetToMediaEntry, 4
#define MIB_ipRoutingDiscards		MIB_ipMIB, 23

/* IP-FORWARD-MIB */
#define MIB_ipfMIB			MIB_ipMIB, 24
#define MIB_ipfInetCidrRouteNumber	MIB_ipfMIB, 6
#define MIB_ipfInetCidrRouteTable	MIB_ipfMIB, 7
#define MIB_ipfInetCidrRouteEntry	MIB_ipfInetCidrRouteTable, 1
#define OIDIDX_ipfInetCidrRoute		10
#define MIB_ipfRouteEntDestType		MIB_ipfInetCidrRouteEntry, 1
#define MIB_ipfRouteEntDest		MIB_ipfInetCidrRouteEntry, 2
#define MIB_ipfRouteEntPfxLen		MIB_ipfInetCidrRouteEntry, 3
#define MIB_ipfRouteEntPolicy		MIB_ipfInetCidrRouteEntry, 4
#define MIB_ipfRouteEntNextHopType	MIB_ipfInetCidrRouteEntry, 5
#define MIB_ipfRouteEntNextHop		MIB_ipfInetCidrRouteEntry, 6
#define MIB_ipfRouteEntIfIndex		MIB_ipfInetCidrRouteEntry, 7
#define MIB_ipfRouteEntType		MIB_ipfInetCidrRouteEntry, 8
#define MIB_ipfRouteEntProto		MIB_ipfInetCidrRouteEntry, 9
#define MIB_ipfRouteEntAge		MIB_ipfInetCidrRouteEntry, 10
#define MIB_ipfRouteEntNextHopAS	MIB_ipfInetCidrRouteEntry, 11
#define MIB_ipfRouteEntRouteMetric1	MIB_ipfInetCidrRouteEntry, 12
#define MIB_ipfRouteEntRouteMetric2	MIB_ipfInetCidrRouteEntry, 13
#define MIB_ipfRouteEntRouteMetric3	MIB_ipfInetCidrRouteEntry, 14
#define MIB_ipfRouteEntRouteMetric4	MIB_ipfInetCidrRouteEntry, 15
#define MIB_ipfRouteEntRouteMetric5	MIB_ipfInetCidrRouteEntry, 16
#define MIB_ipfRouteEntStatus		MIB_ipfInetCidrRouteEntry, 17
#define MIB_ipfInetCidrRouteDiscards	MIB_ipfMIB, 8

/* BRIDGE-MIB */
#define MIB_dot1dBridge			MIB_mib_2, 17
#define MIB_dot1dBase			MIB_dot1dBridge, 1
#define MIB_dot1dBaseBridgeAddress	MIB_dot1dBase, 1
#define MIB_dot1dBaseNumPorts		MIB_dot1dBase, 2
#define MIB_dot1dBaseType		MIB_dot1dBase, 3
#define MIB_dot1dBasePortTable		MIB_dot1dBase, 4
#define OIDIDX_dot1d			10
#define OIDIDX_dot1dEntry		11
#define MIB_dot1dBasePortEntry		MIB_dot1dBasePortTable, 1
#define MIB_dot1dBasePort		MIB_dot1dBasePortEntry, 1
#define MIB_dot1dBasePortIfIndex	MIB_dot1dBasePortEntry, 2
#define MIB_dot1dBasePortCircuit	MIB_dot1dBasePortEntry, 3
#define MIB_dot1dBasePortDelayExceededDiscards	MIB_dot1dBasePortEntry, 4
#define MIB_dot1dBasePortMtuExceededDiscards	MIB_dot1dBasePortEntry, 5
#define MIB_dot1dStp			MIB_dot1dBridge, 2
#define MIB_dot1dSr			MIB_dot1dBridge, 3
#define MIB_dot1dTp			MIB_dot1dBridge, 4
#define MIB_dot1dStatic			MIB_dot1dBridge, 5

/*
 * PRIVATE ENTERPRISE NUMBERS from
 * https://www.iana.org/assignments/enterprise-numbers
 *
 * This is not the complete list of private enterprise numbers, it only
 * includes some well-known companies and especially network companies
 * that are very common in the datacenters around the world, other
 * companies that contributed to snmpd or OpenBSD in some way, or just
 * any other organizations that we wanted to include. It would be an
 * overkill to include ~30.000 entries for all the organizations from
 * the official list.
 */
#define MIB_ibm				MIB_enterprises, 2
#define MIB_cmu				MIB_enterprises, 3
#define MIB_unix			MIB_enterprises, 4
#define MIB_ciscoSystems		MIB_enterprises, 9
#define MIB_hp				MIB_enterprises, 11
#define MIB_mit				MIB_enterprises, 20
#define MIB_nortelNetworks		MIB_enterprises, 35
#define MIB_sun				MIB_enterprises, 42
#define MIB_3com			MIB_enterprises, 43
#define MIB_synOptics			MIB_enterprises, 45
#define MIB_enterasys			MIB_enterprises, 52
#define MIB_sgi				MIB_enterprises, 59
#define MIB_apple			MIB_enterprises, 63
#define MIB_nasa			MIB_enterprises, 71
#define MIB_att				MIB_enterprises, 74
#define MIB_nokia			MIB_enterprises, 94
#define MIB_cern			MIB_enterprises, 96
#define MIB_oracle			MIB_enterprises, 111
#define MIB_motorola			MIB_enterprises, 161
#define MIB_ncr				MIB_enterprises, 191
#define MIB_ericsson			MIB_enterprises, 193
#define MIB_fsc				MIB_enterprises, 231
#define MIB_compaq			MIB_enterprises, 232
#define MIB_bmw				MIB_enterprises, 513
#define MIB_dell			MIB_enterprises, 674
#define MIB_iij				MIB_enterprises, 770
#define MIB_sandia			MIB_enterprises, 1400
#define MIB_mercedesBenz		MIB_enterprises, 1635
#define MIB_alteon			MIB_enterprises, 1872
#define MIB_extremeNetworks		MIB_enterprises, 1916
#define MIB_foundryNetworks		MIB_enterprises, 1991
#define MIB_huawaiTechnology		MIB_enterprises, 2011
#define MIB_ucDavis			MIB_enterprises, 2021
#define MIB_freeBSD			MIB_enterprises, 2238
#define MIB_checkPoint			MIB_enterprises, 2620
#define MIB_juniper			MIB_enterprises, 2636
#define MIB_printerWorkingGroup		MIB_enterprises, 2699
#define MIB_audi			MIB_enterprises, 3195
#define MIB_volkswagen			MIB_enterprises, 3210
#define MIB_genua			MIB_enterprises, 3717
#define MIB_amazon			MIB_enterprises, 4843
#define MIB_force10Networks		MIB_enterprises, 6027
#define MIB_vMware			MIB_enterprises, 6876
#define MIB_alcatelLucent		MIB_enterprises, 7483
#define MIB_snom			MIB_enterprises, 7526
#define MIB_netSNMP			MIB_enterprises, 8072
#define MIB_netflix			MIB_enterprises, 10949
#define MIB_google			MIB_enterprises, 11129
#define MIB_f5Networks			MIB_enterprises, 12276
#define MIB_bsws			MIB_enterprises, 13635
#define MIB_sFlow			MIB_enterprises, 14706
#define MIB_microSystems		MIB_enterprises, 18623
#define MIB_paloAltoNetworks		MIB_enterprises, 25461
#define MIB_h3c				MIB_enterprises, 25506
#define MIB_vantronix			MIB_enterprises, 26766
#define MIB_netBSD			MIB_enterprises, 32388
#define OIDVAL_openBSD_eid		30155
#define MIB_openBSD			MIB_enterprises, OIDVAL_openBSD_eid
#define MIB_nicira			MIB_enterprises, 39961
#define MIB_esdenera			MIB_enterprises, 42459
#define MIB_arcaTrust			MIB_enterprises, 52198

/* UCD-DISKIO-MIB */
#define MIB_ucdExperimental		MIB_ucDavis, 13
#define MIB_ucdDiskIOMIB		MIB_ucdExperimental, 15
#define MIB_diskIOTable			MIB_ucdDiskIOMIB, 1
#define MIB_diskIOEntry			MIB_diskIOTable, 1
#define OIDIDX_diskIO			11
#define OIDIDX_diskIOEntry		12
#define MIB_diskIOIndex			MIB_diskIOEntry, 1
#define MIB_diskIODevice		MIB_diskIOEntry, 2
#define MIB_diskIONRead			MIB_diskIOEntry, 3
#define MIB_diskIONWritten		MIB_diskIOEntry, 4
#define MIB_diskIOReads			MIB_diskIOEntry, 5
#define MIB_diskIOWrites		MIB_diskIOEntry, 6
#define MIB_diskIONReadX		MIB_diskIOEntry, 12
#define MIB_diskIONWrittenX		MIB_diskIOEntry, 13

/* OPENBSD-MIB */
#define MIB_pfMIBObjects		MIB_openBSD, 1
#define MIB_pfInfo			MIB_pfMIBObjects, 1
#define MIB_pfRunning			MIB_pfInfo, 1
#define MIB_pfRuntime			MIB_pfInfo, 2
#define MIB_pfDebug			MIB_pfInfo, 3
#define MIB_pfHostid			MIB_pfInfo, 4
#define MIB_pfCounters			MIB_pfMIBObjects, 2
#define MIB_pfCntMatch			MIB_pfCounters, 1
#define MIB_pfCntBadOffset		MIB_pfCounters, 2
#define MIB_pfCntFragment		MIB_pfCounters, 3
#define MIB_pfCntShort			MIB_pfCounters, 4
#define MIB_pfCntNormalize		MIB_pfCounters, 5
#define MIB_pfCntMemory			MIB_pfCounters, 6
#define MIB_pfCntTimestamp		MIB_pfCounters, 7
#define MIB_pfCntCongestion		MIB_pfCounters, 8
#define MIB_pfCntIpOptions		MIB_pfCounters, 9
#define MIB_pfCntProtoCksum		MIB_pfCounters, 10
#define MIB_pfCntStateMismatch		MIB_pfCounters, 11
#define MIB_pfCntStateInsert		MIB_pfCounters, 12
#define MIB_pfCntStateLimit		MIB_pfCounters, 13
#define MIB_pfCntSrcLimit		MIB_pfCounters, 14
#define MIB_pfCntSynproxy		MIB_pfCounters, 15
#define MIB_pfCntTranslate		MIB_pfCounters, 16
#define MIB_pfCntNoRoute		MIB_pfCounters, 17
#define MIB_pfStateTable		MIB_pfMIBObjects, 3
#define MIB_pfStateCount		MIB_pfStateTable, 1
#define MIB_pfStateSearches		MIB_pfStateTable, 2
#define MIB_pfStateInserts		MIB_pfStateTable, 3
#define MIB_pfStateRemovals		MIB_pfStateTable, 4
#define MIB_pfLogInterface		MIB_pfMIBObjects, 4
#define MIB_pfLogIfName			MIB_pfLogInterface, 1
#define MIB_pfLogIfIpBytesIn		MIB_pfLogInterface, 2
#define MIB_pfLogIfIpBytesOut		MIB_pfLogInterface, 3
#define MIB_pfLogIfIpPktsInPass		MIB_pfLogInterface, 4
#define MIB_pfLogIfIpPktsInDrop		MIB_pfLogInterface, 5
#define MIB_pfLogIfIpPktsOutPass	MIB_pfLogInterface, 6
#define MIB_pfLogIfIpPktsOutDrop	MIB_pfLogInterface, 7
#define MIB_pfLogIfIp6BytesIn		MIB_pfLogInterface, 8
#define MIB_pfLogIfIp6BytesOut		MIB_pfLogInterface, 9
#define MIB_pfLogIfIp6PktsInPass	MIB_pfLogInterface, 10
#define MIB_pfLogIfIp6PktsInDrop	MIB_pfLogInterface, 11
#define MIB_pfLogIfIp6PktsOutPass	MIB_pfLogInterface, 12
#define MIB_pfLogIfIp6PktsOutDrop	MIB_pfLogInterface, 13
#define MIB_pfSrcTracking		MIB_pfMIBObjects, 5
#define MIB_pfSrcTrackCount		MIB_pfSrcTracking, 1
#define MIB_pfSrcTrackSearches		MIB_pfSrcTracking, 2
#define MIB_pfSrcTrackInserts		MIB_pfSrcTracking, 3
#define MIB_pfSrcTrackRemovals		MIB_pfSrcTracking, 4
#define MIB_pfLimits			MIB_pfMIBObjects, 6
#define MIB_pfLimitStates		MIB_pfLimits, 1
#define MIB_pfLimitSourceNodes		MIB_pfLimits, 2
#define MIB_pfLimitFragments		MIB_pfLimits, 3
#define MIB_pfLimitMaxTables		MIB_pfLimits, 4
#define MIB_pfLimitMaxTableEntries	MIB_pfLimits, 5
#define MIB_pfTimeouts			MIB_pfMIBObjects, 7
#define MIB_pfTimeoutTcpFirst		MIB_pfTimeouts, 1
#define MIB_pfTimeoutTcpOpening		MIB_pfTimeouts, 2
#define MIB_pfTimeoutTcpEstablished	MIB_pfTimeouts, 3
#define MIB_pfTimeoutTcpClosing		MIB_pfTimeouts, 4
#define MIB_pfTimeoutTcpFinWait		MIB_pfTimeouts, 5
#define MIB_pfTimeoutTcpClosed		MIB_pfTimeouts, 6
#define MIB_pfTimeoutUdpFirst		MIB_pfTimeouts, 7
#define MIB_pfTimeoutUdpSingle		MIB_pfTimeouts, 8
#define MIB_pfTimeoutUdpMultiple	MIB_pfTimeouts, 9
#define MIB_pfTimeoutIcmpFirst		MIB_pfTimeouts, 10
#define MIB_pfTimeoutIcmpError		MIB_pfTimeouts, 11
#define MIB_pfTimeoutOtherFirst		MIB_pfTimeouts, 12
#define MIB_pfTimeoutOtherSingle	MIB_pfTimeouts, 13
#define MIB_pfTimeoutOtherMultiple	MIB_pfTimeouts, 14
#define MIB_pfTimeoutFragment		MIB_pfTimeouts, 15
#define MIB_pfTimeoutInterval		MIB_pfTimeouts, 16
#define MIB_pfTimeoutAdaptiveStart	MIB_pfTimeouts, 17
#define MIB_pfTimeoutAdaptiveEnd	MIB_pfTimeouts, 18
#define MIB_pfTimeoutSrcTrack		MIB_pfTimeouts, 19
#define OIDIDX_pfstatus			9
#define MIB_pfInterfaces		MIB_pfMIBObjects, 8
#define MIB_pfIfNumber			MIB_pfInterfaces, 1
#define MIB_pfIfTable			MIB_pfInterfaces, 128
#define MIB_pfIfEntry			MIB_pfIfTable, 1
#define OIDIDX_pfInterface		11
#define OIDIDX_pfIfEntry		12
#define MIB_pfIfIndex			MIB_pfIfEntry, 1
#define MIB_pfIfDescr			MIB_pfIfEntry, 2
#define MIB_pfIfType			MIB_pfIfEntry, 3
#define MIB_pfIfRefs			MIB_pfIfEntry, 4
#define MIB_pfIfRules			MIB_pfIfEntry, 5
#define MIB_pfIfIn4PassPkts		MIB_pfIfEntry, 6
#define MIB_pfIfIn4PassBytes		MIB_pfIfEntry, 7
#define MIB_pfIfIn4BlockPkts		MIB_pfIfEntry, 8
#define MIB_pfIfIn4BlockBytes		MIB_pfIfEntry, 9
#define MIB_pfIfOut4PassPkts		MIB_pfIfEntry, 10
#define MIB_pfIfOut4PassBytes		MIB_pfIfEntry, 11
#define MIB_pfIfOut4BlockPkts		MIB_pfIfEntry, 12
#define MIB_pfIfOut4BlockBytes		MIB_pfIfEntry, 13
#define MIB_pfIfIn6PassPkts		MIB_pfIfEntry, 14
#define MIB_pfIfIn6PassBytes		MIB_pfIfEntry, 15
#define MIB_pfIfIn6BlockPkts		MIB_pfIfEntry, 16
#define MIB_pfIfIn6BlockBytes		MIB_pfIfEntry, 17
#define MIB_pfIfOut6PassPkts		MIB_pfIfEntry, 18
#define MIB_pfIfOut6PassBytes		MIB_pfIfEntry, 19
#define MIB_pfIfOut6BlockPkts		MIB_pfIfEntry, 20
#define MIB_pfIfOut6BlockBytes		MIB_pfIfEntry, 21
#define MIB_pfTables			MIB_pfMIBObjects, 9
#define MIB_pfTblNumber			MIB_pfTables, 1
#define MIB_pfTblTable			MIB_pfTables, 128
#define MIB_pfTblEntry			MIB_pfTblTable, 1
#define OIDIDX_pfTable			11
#define OIDIDX_pfTableEntry		12
#define MIB_pfTblIndex			MIB_pfTblEntry, 1
#define MIB_pfTblName			MIB_pfTblEntry, 2
#define MIB_pfTblAddresses		MIB_pfTblEntry, 3
#define MIB_pfTblAnchorRefs		MIB_pfTblEntry, 4
#define MIB_pfTblRuleRefs		MIB_pfTblEntry, 5
#define MIB_pfTblEvalsMatch		MIB_pfTblEntry, 6
#define MIB_pfTblEvalsNoMatch		MIB_pfTblEntry, 7
#define MIB_pfTblInPassPkts		MIB_pfTblEntry, 8
#define MIB_pfTblInPassBytes		MIB_pfTblEntry, 9
#define MIB_pfTblInBlockPkts		MIB_pfTblEntry, 10
#define MIB_pfTblInBlockBytes		MIB_pfTblEntry, 11
#define MIB_pfTblInXPassPkts		MIB_pfTblEntry, 12
#define MIB_pfTblInXPassBytes		MIB_pfTblEntry, 13
#define MIB_pfTblOutPassPkts		MIB_pfTblEntry, 14
#define MIB_pfTblOutPassBytes		MIB_pfTblEntry, 15
#define MIB_pfTblOutBlockPkts		MIB_pfTblEntry, 16
#define MIB_pfTblOutBlockBytes		MIB_pfTblEntry, 17
#define MIB_pfTblOutXPassPkts		MIB_pfTblEntry, 18
#define MIB_pfTblOutXPassBytes		MIB_pfTblEntry, 19
#define MIB_pfTblStatsCleared		MIB_pfTblEntry, 20
#define MIB_pfTblInMatchPkts		MIB_pfTblEntry, 21
#define MIB_pfTblInMatchBytes		MIB_pfTblEntry, 22
#define MIB_pfTblOutMatchPkts		MIB_pfTblEntry, 23
#define MIB_pfTblOutMatchBytes		MIB_pfTblEntry, 24
#define MIB_pfTblAddrTable		MIB_pfTables, 129
#define MIB_pfTblAddrEntry		MIB_pfTblAddrTable, 1
#define OIDIDX_pfTblAddr		11
#define MIB_pfTblAddrTblIndex		MIB_pfTblAddrEntry, 1
#define MIB_pfTblAddrNet		MIB_pfTblAddrEntry, 2
#define MIB_pfTblAddrMask		MIB_pfTblAddrEntry, 3
#define MIB_pfTblAddrCleared		MIB_pfTblAddrEntry, 4
#define MIB_pfTblAddrInBlockPkts	MIB_pfTblAddrEntry, 5
#define MIB_pfTblAddrInBlockBytes	MIB_pfTblAddrEntry, 6
#define MIB_pfTblAddrInPassPkts		MIB_pfTblAddrEntry, 7
#define MIB_pfTblAddrInPassBytes	MIB_pfTblAddrEntry, 8
#define MIB_pfTblAddrOutBlockPkts	MIB_pfTblAddrEntry, 9
#define MIB_pfTblAddrOutBlockBytes	MIB_pfTblAddrEntry, 10
#define MIB_pfTblAddrOutPassPkts	MIB_pfTblAddrEntry, 11
#define MIB_pfTblAddrOutPassBytes	MIB_pfTblAddrEntry, 12
#define MIB_pfTblAddrInMatchPkts	MIB_pfTblAddrEntry, 13
#define MIB_pfTblAddrInMatchBytes	MIB_pfTblAddrEntry, 14
#define MIB_pfTblAddrOutMatchPkts	MIB_pfTblAddrEntry, 15
#define MIB_pfTblAddrOutMatchBytes	MIB_pfTblAddrEntry, 16
#define MIB_pfLabels			MIB_pfMIBObjects, 10
#define MIB_pfLabelNumber		MIB_pfLabels, 1
#define MIB_pfLabelTable		MIB_pfLabels, 128
#define OIDIDX_pfLabel			11
#define OIDIDX_pfLabelEntry		12
#define MIB_pfLabelEntry		MIB_pfLabelTable, 1
#define MIB_pfLabelIndex		MIB_pfLabelEntry, 1
#define MIB_pfLabelName			MIB_pfLabelEntry, 2
#define MIB_pfLabelEvals		MIB_pfLabelEntry, 3
#define MIB_pfLabelPkts			MIB_pfLabelEntry, 4
#define MIB_pfLabelBytes		MIB_pfLabelEntry, 5
#define MIB_pfLabelInPkts		MIB_pfLabelEntry, 6
#define MIB_pfLabelInBytes		MIB_pfLabelEntry, 7
#define MIB_pfLabelOutPkts		MIB_pfLabelEntry, 8
#define MIB_pfLabelOutBytes		MIB_pfLabelEntry, 9
#define MIB_pfLabelTotalStates		MIB_pfLabelEntry, 10
#define MIB_pfsyncStats			MIB_pfMIBObjects, 11
#define MIB_pfsyncIpPktsRecv		MIB_pfsyncStats, 1
#define MIB_pfsyncIp6PktsRecv		MIB_pfsyncStats, 2
#define MIB_pfsyncPktDiscardsForBadInterface	MIB_pfsyncStats, 3
#define MIB_pfsyncPktDiscardsForBadTtl		MIB_pfsyncStats, 4
#define MIB_pfsyncPktShorterThanHeader		MIB_pfsyncStats, 5
#define MIB_pfsyncPktDiscardsForBadVersion	MIB_pfsyncStats, 6
#define MIB_pfsyncPktDiscardsForBadAction	MIB_pfsyncStats, 7
#define MIB_pfsyncPktDiscardsForBadLength	MIB_pfsyncStats, 8
#define MIB_pfsyncPktDiscardsForBadAuth		MIB_pfsyncStats, 9
#define MIB_pfsyncPktDiscardsForStaleState	MIB_pfsyncStats, 10
#define MIB_pfsyncPktDiscardsForBadValues	MIB_pfsyncStats, 11
#define MIB_pfsyncPktDiscardsForBadState	MIB_pfsyncStats, 12
#define MIB_pfsyncIpPktsSent		MIB_pfsyncStats, 13
#define MIB_pfsyncIp6PktsSent		MIB_pfsyncStats, 14
#define MIB_pfsyncNoMemory		MIB_pfsyncStats, 15
#define MIB_pfsyncOutputErrors		MIB_pfsyncStats, 16
#define MIB_sensorsMIBObjects		MIB_openBSD, 2
#define MIB_sensors			MIB_sensorsMIBObjects, 1
#define MIB_sensorNumber		MIB_sensors, 1
#define MIB_sensorTable			MIB_sensors, 2
#define MIB_sensorEntry			MIB_sensorTable, 1
#define OIDIDX_sensor			11
#define OIDIDX_sensorEntry		12
#define MIB_sensorIndex			MIB_sensorEntry, 1
#define MIB_sensorDescr			MIB_sensorEntry, 2
#define MIB_sensorType			MIB_sensorEntry, 3
#define MIB_sensorDevice		MIB_sensorEntry, 4
#define MIB_sensorValue			MIB_sensorEntry, 5
#define MIB_sensorUnits			MIB_sensorEntry, 6
#define MIB_sensorStatus		MIB_sensorEntry, 7
#define MIB_relaydMIBObjects		MIB_openBSD, 3
#define MIB_relaydHostTrap		MIB_relaydMIBObjects, 1
#define MIB_relaydHostTrapHostName	MIB_relaydHostTrap, 1
#define MIB_relaydHostTrapUp		MIB_relaydHostTrap, 2
#define MIB_relaydHostTrapLastUp	MIB_relaydHostTrap, 3
#define MIB_relaydHostTrapUpCount	MIB_relaydHostTrap, 4
#define MIB_relaydHostTrapCheckCount	MIB_relaydHostTrap, 5
#define MIB_relaydHostTrapTableName	MIB_relaydHostTrap, 6
#define MIB_relaydHostTrapTableUp	MIB_relaydHostTrap, 7
#define MIB_relaydHostTrapRetry		MIB_relaydHostTrap, 8
#define MIB_relaydHostTrapRetryCount	MIB_relaydHostTrap, 9
#define MIB_ipsecMIBObjects		MIB_openBSD, 4
#define MIB_memMIBObjects		MIB_openBSD, 5
#define MIB_memMIBVersion		MIB_memMIBObjects, 1
#define OIDVER_OPENBSD_MEM		1
#define MIB_memIfTable			MIB_memMIBObjects, 2
#define MIB_memIfEntry			MIB_memIfTable, 1
#define OIDIDX_memIf			10
#define OIDIDX_memIfEntry		11
#define MIB_memIfName			MIB_memIfEntry, 1
#define MIB_memIfLiveLocks		MIB_memIfEntry, 2
#define MIB_carpMIBObjects		MIB_openBSD, 6
#define MIB_carpSysctl			MIB_carpMIBObjects, 1
#define MIB_carpAllow			MIB_carpSysctl, 1
#define MIB_carpPreempt			MIB_carpSysctl, 2
#define MIB_carpLog			MIB_carpSysctl, 3
#define OIDIDX_carpsysctl		9
#define MIB_carpIf			MIB_carpMIBObjects, 2
#define MIB_carpIfNumber		MIB_carpIf, 1
#define MIB_carpIfTable			MIB_carpIf, 2
#define MIB_carpIfEntry			MIB_carpIfTable, 1
#define OIDIDX_carpIf			11
#define OIDIDX_carpIfEntry		12
#define MIB_carpIfIndex			MIB_carpIfEntry, 1
#define MIB_carpIfDescr			MIB_carpIfEntry, 2
#define MIB_carpIfVhid			MIB_carpIfEntry, 3
#define MIB_carpIfDev			MIB_carpIfEntry, 4
#define MIB_carpIfAdvbase		MIB_carpIfEntry, 5
#define MIB_carpIfAdvskew		MIB_carpIfEntry, 6
#define MIB_carpIfState			MIB_carpIfEntry, 7
#define OIDIDX_carpstats		9
#define MIB_carpStats			MIB_carpMIBObjects, 3
#define MIB_carpIpPktsRecv		MIB_carpStats, 1
#define MIB_carpIp6PktsRecv		MIB_carpStats, 2
#define MIB_carpPktDiscardsBadIface	MIB_carpStats, 3
#define MIB_carpPktDiscardsBadTtl	MIB_carpStats, 4
#define MIB_carpPktShorterThanHdr	MIB_carpStats, 5
#define MIB_carpDiscardsBadCksum	MIB_carpStats, 6
#define MIB_carpDiscardsBadVersion	MIB_carpStats, 7
#define MIB_carpDiscardsTooShort	MIB_carpStats, 8
#define MIB_carpDiscardsBadAuth		MIB_carpStats, 9
#define MIB_carpDiscardsBadVhid		MIB_carpStats, 10
#define MIB_carpDiscardsBadAddrList	MIB_carpStats, 11
#define MIB_carpIpPktsSent		MIB_carpStats, 12
#define MIB_carpIp6PktsSent		MIB_carpStats, 13
#define MIB_carpNoMemory		MIB_carpStats, 14
#define MIB_carpTransitionsToMaster	MIB_carpStats, 15
#define MIB_carpGroupTable		MIB_carpMIBObjects, 4
#define MIB_carpGroupEntry		MIB_carpGroupTable, 1
#define OIDIDX_carpGroupEntry		10
#define OIDIDX_carpGroupIndex		11
#define MIB_carpGroupName		MIB_carpGroupEntry, 2
#define MIB_carpGroupDemote		MIB_carpGroupEntry, 3
#define MIB_localSystem			MIB_openBSD, 23
#define MIB_SYSOID_DEFAULT		MIB_openBSD, 23, 1
#define MIB_localTest			MIB_openBSD, 42

#define MIB_TREE			{		\
	{ MIBDECL(iso) },				\
	{ MIBDECL(org) },				\
	{ MIBDECL(dod) },				\
	{ MIBDECL(internet) },				\
	{ MIBDECL(directory) },				\
	{ MIBDECL(mgmt) },				\
	{ MIBDECL(mib_2) },				\
	{ MIBDECL(system) },				\
	{ MIBDECL(sysDescr) },				\
	{ MIBDECL(sysOID) },				\
	{ MIBDECL(sysUpTime) },				\
	{ MIBDECL(sysContact) },			\
	{ MIBDECL(sysName) },				\
	{ MIBDECL(sysLocation) },			\
	{ MIBDECL(sysServices) },			\
	{ MIBDECL(sysORLastChange) },			\
	{ MIBDECL(sysORTable) },			\
	{ MIBDECL(sysOREntry) },			\
	{ MIBDECL(sysORIndex) },			\
	{ MIBDECL(sysORID) },				\
	{ MIBDECL(sysORDescr) },			\
	{ MIBDECL(sysORUpTime) },			\
	{ MIBDECL(transmission) },			\
	{ MIBDECL(snmp) },				\
	{ MIBDECL(snmpInPkts) },			\
	{ MIBDECL(snmpOutPkts) },			\
	{ MIBDECL(snmpInBadVersions) },			\
	{ MIBDECL(snmpInBadCommunityNames) },		\
	{ MIBDECL(snmpInBadCommunityUses) },		\
	{ MIBDECL(snmpInASNParseErrs) },		\
	{ MIBDECL(snmpInTooBigs) },			\
	{ MIBDECL(snmpInNoSuchNames) },			\
	{ MIBDECL(snmpInBadValues) },			\
	{ MIBDECL(snmpInReadOnlys) },			\
	{ MIBDECL(snmpInGenErrs) },			\
	{ MIBDECL(snmpInTotalReqVars) },		\
	{ MIBDECL(snmpInTotalSetVars) },		\
	{ MIBDECL(snmpInGetRequests) },			\
	{ MIBDECL(snmpInGetNexts) },			\
	{ MIBDECL(snmpInSetRequests) },			\
	{ MIBDECL(snmpInGetResponses) },		\
	{ MIBDECL(snmpInTraps) },			\
	{ MIBDECL(snmpOutTooBigs) },			\
	{ MIBDECL(snmpOutNoSuchNames) },		\
	{ MIBDECL(snmpOutBadValues) },			\
	{ MIBDECL(snmpOutGenErrs) },			\
	{ MIBDECL(snmpOutGetRequests) },		\
	{ MIBDECL(snmpOutGetNexts) },			\
	{ MIBDECL(snmpOutSetRequests) },		\
	{ MIBDECL(snmpOutGetResponses) },		\
	{ MIBDECL(snmpOutTraps) },			\
	{ MIBDECL(snmpEnableAuthenTraps) },		\
	{ MIBDECL(snmpSilentDrops) },			\
	{ MIBDECL(snmpProxyDrops) },			\
	{ MIBDECL(experimental) },			\
	{ MIBDECL(private) },				\
	{ MIBDECL(enterprises) },			\
	{ MIBDECL(security) },				\
	{ MIBDECL(snmpV2) },				\
	{ MIBDECL(snmpDomains) },			\
	{ MIBDECL(snmpProxies) },			\
	{ MIBDECL(snmpModules) },			\
	{ MIBDECL(snmpMIB) },				\
	{ MIBDECL(snmpMIBObjects) },			\
	{ MIBDECL(snmpTrap) },				\
	{ MIBDECL(snmpTrapOID) },			\
	{ MIBDECL(snmpTrapEnterprise) },		\
	{ MIBDECL(snmpTraps) },				\
	{ MIBDECL(coldStart) },				\
	{ MIBDECL(warmStart) },				\
	{ MIBDECL(linkDown) },				\
	{ MIBDECL(linkUp) },				\
	{ MIBDECL(authenticationFailure) },		\
	{ MIBDECL(egpNeighborLoss) },			\
							\
	{ MIBDECL(framework) },				\
	{ MIBDECL(frameworkObjects) },			\
	{ MIBDECL(snmpEngine) },			\
	{ MIBDECL(snmpEngineID) },			\
	{ MIBDECL(snmpEngineBoots) },			\
	{ MIBDECL(snmpEngineTime) },			\
	{ MIBDECL(snmpEngineMaxMsgSize) },		\
	{ MIBDECL(usm) },				\
	{ MIBDECL(usmObjects) },			\
	{ MIBDECL(usmStats) },				\
	{ MIBDECL(usmStatsUnsupportedSecLevels) },	\
	{ MIBDECL(usmStatsNotInTimeWindow) },		\
	{ MIBDECL(usmStatsUnknownUserNames) },		\
	{ MIBDECL(usmStatsUnknownEngineId) },		\
	{ MIBDECL(usmStatsWrongDigests) },		\
	{ MIBDECL(usmStatsDecryptionErrors) },		\
							\
	{ MIBDECL(snmpTargetMIB) },			\
	{ MIBDECL(snmpTargetObjects) },			\
	{ MIBDECL(snmpTargetSpinLock) },		\
	{ MIBDECL(snmpTargetAddrTable) },		\
	{ MIBDECL(snmpTargetAddrEntry) },		\
	{ MIBDECL(snmpTargetAddrName) },		\
	{ MIBDECL(snmpTargetAddrTDomain) },		\
	{ MIBDECL(snmpTargetAddrTAddress) },		\
	{ MIBDECL(snmpTargetAddrTimeout) },		\
	{ MIBDECL(snmpTargetAddrRetryCount) },		\
	{ MIBDECL(snmpTargetAddrTagList) },		\
	{ MIBDECL(snmpTargetAddrParams) },		\
	{ MIBDECL(snmpTargetAddrStorageType) },		\
	{ MIBDECL(snmpTargetAddrRowStatus) },		\
	{ MIBDECL(snmpTargetParamsTable) },		\
	{ MIBDECL(snmpTargetParamsEntry) },		\
	{ MIBDECL(snmpTargetParamsName) },		\
	{ MIBDECL(snmpTargetParamsMPModel) },		\
	{ MIBDECL(snmpTargetParamsSecurityModel) },	\
	{ MIBDECL(snmpTargetParamsSecurityName) },	\
	{ MIBDECL(snmpTargetParamsSecurityLevel) },	\
	{ MIBDECL(snmpTargetParamsStorageType) },	\
	{ MIBDECL(snmpTargetParamsRowStatus) },		\
	{ MIBDECL(snmpUnavailableContexts) },		\
	{ MIBDECL(snmpUnknownContexts) },		\
							\
	{ MIBDECL(host) },				\
	{ MIBDECL(hrSystem) },				\
	{ MIBDECL(hrSystemUptime) },			\
	{ MIBDECL(hrSystemDate) },			\
	{ MIBDECL(hrSystemInitialLoadDevice) },		\
	{ MIBDECL(hrSystemInitialLoadParameters) },	\
	{ MIBDECL(hrSystemNumUsers) },			\
	{ MIBDECL(hrSystemProcesses) },			\
	{ MIBDECL(hrSystemMaxProcesses) },		\
	{ MIBDECL(hrStorage) },				\
	{ MIBDECL(hrStorageTypes) },			\
	{ MIBDECL(hrMemorySize) },			\
	{ MIBDECL(hrStorageTable) },			\
	{ MIBDECL(hrStorageEntry) },			\
	{ MIBDECL(hrStorageIndex) },			\
	{ MIBDECL(hrStorageType) },			\
	{ MIBDECL(hrStorageDescr) },			\
	{ MIBDECL(hrStorageAllocationUnits) },		\
	{ MIBDECL(hrStorageSize) },			\
	{ MIBDECL(hrStorageUsed) },			\
	{ MIBDECL(hrStorageAllocationFailures) },	\
	{ MIBDECL(hrDevice) },				\
	{ MIBDECL(hrDeviceTypes) },			\
	{ MIBDECL(hrDeviceOther) },			\
	{ MIBDECL(hrDeviceUnknown) },			\
	{ MIBDECL(hrDeviceProcessor) },			\
	{ MIBDECL(hrDeviceNetwork) },			\
	{ MIBDECL(hrDevicePrinter) },			\
	{ MIBDECL(hrDeviceDiskStorage) },		\
	{ MIBDECL(hrDeviceVideo) },			\
	{ MIBDECL(hrDeviceAudio) },			\
	{ MIBDECL(hrDeviceCoprocessor) },		\
	{ MIBDECL(hrDeviceKeyboard) },			\
	{ MIBDECL(hrDeviceModem) },			\
	{ MIBDECL(hrDeviceParallelPort) },		\
	{ MIBDECL(hrDevicePointing) },			\
	{ MIBDECL(hrDeviceSerialPort) },		\
	{ MIBDECL(hrDeviceTape) },			\
	{ MIBDECL(hrDeviceClock) },			\
	{ MIBDECL(hrDeviceVolatileMemory) },		\
	{ MIBDECL(hrDeviceNonVolatileMemory) },		\
	{ MIBDECL(hrDeviceTable) },			\
	{ MIBDECL(hrDeviceEntry) },			\
	{ MIBDECL(hrDeviceIndex) },			\
	{ MIBDECL(hrDeviceType) },			\
	{ MIBDECL(hrDeviceDescr) },			\
	{ MIBDECL(hrDeviceID) },			\
	{ MIBDECL(hrDeviceStatus) },			\
	{ MIBDECL(hrDeviceErrors) },			\
	{ MIBDECL(hrProcessorTable) },			\
	{ MIBDECL(hrProcessorEntry) },			\
	{ MIBDECL(hrProcessorFrwID) },			\
	{ MIBDECL(hrProcessorLoad) },			\
	{ MIBDECL(hrSWRun) },				\
	{ MIBDECL(hrSWOSIndex) },			\
	{ MIBDECL(hrSWRunTable) },			\
	{ MIBDECL(hrSWRunEntry) },			\
	{ MIBDECL(hrSWRunIndex) },			\
	{ MIBDECL(hrSWRunName) },			\
	{ MIBDECL(hrSWRunID) },				\
	{ MIBDECL(hrSWRunPath) },			\
	{ MIBDECL(hrSWRunParameters) },			\
	{ MIBDECL(hrSWRunType) },			\
	{ MIBDECL(hrSWRunStatus) },			\
	{ MIBDECL(hrSWRunPerf) },			\
	{ MIBDECL(hrSWRunPerfTable) },			\
	{ MIBDECL(hrSWRunPerfEntry) },			\
	{ MIBDECL(hrSWRunPerfCPU) },			\
	{ MIBDECL(hrSWRunPerfMem) },			\
							\
	{ MIBDECL(ifMIB) },				\
	{ MIBDECL(ifMIBObjects) },			\
	{ MIBDECL(ifXTable) },				\
	{ MIBDECL(ifXEntry) },				\
	{ MIBDECL(ifName) },				\
	{ MIBDECL(ifInMulticastPkts) },			\
	{ MIBDECL(ifInBroadcastPkts) },			\
	{ MIBDECL(ifOutMulticastPkts) },		\
	{ MIBDECL(ifOutBroadcastPkts) },		\
	{ MIBDECL(ifHCInOctets) },			\
	{ MIBDECL(ifHCInUcastPkts) },			\
	{ MIBDECL(ifHCInMulticastPkts) },		\
	{ MIBDECL(ifHCInBroadcastPkts) },		\
	{ MIBDECL(ifHCOutOctets) },			\
	{ MIBDECL(ifHCOutUcastPkts) },			\
	{ MIBDECL(ifHCOutMulticastPkts) },		\
	{ MIBDECL(ifHCOutBroadcastPkts) },		\
	{ MIBDECL(ifLinkUpDownTrapEnable) },		\
	{ MIBDECL(ifHighSpeed) },			\
	{ MIBDECL(ifPromiscuousMode) },			\
	{ MIBDECL(ifConnectorPresent) },		\
	{ MIBDECL(ifAlias) },				\
	{ MIBDECL(ifCounterDiscontinuityTime) },	\
	{ MIBDECL(ifStackTable) },			\
	{ MIBDECL(ifStackEntry) },			\
	{ MIBDECL(ifRcvAddressTable) },			\
	{ MIBDECL(ifRcvAddressEntry) },			\
	{ MIBDECL(ifRcvAddressStatus) },		\
	{ MIBDECL(ifRcvAddressType) },			\
	{ MIBDECL(ifStackLastChange) },			\
	{ MIBDECL(interfaces) },			\
	{ MIBDECL(ifNumber) },				\
	{ MIBDECL(ifTable) },				\
	{ MIBDECL(ifEntry) },				\
	{ MIBDECL(ifIndex) },				\
	{ MIBDECL(ifDescr) },				\
	{ MIBDECL(ifType) },				\
	{ MIBDECL(ifMtu) },				\
	{ MIBDECL(ifSpeed) },				\
	{ MIBDECL(ifPhysAddress) },			\
	{ MIBDECL(ifAdminStatus) },			\
	{ MIBDECL(ifOperStatus) },			\
	{ MIBDECL(ifLastChange) },			\
	{ MIBDECL(ifInOctets) },			\
	{ MIBDECL(ifInUcastPkts) },			\
	{ MIBDECL(ifInNUcastPkts) },			\
	{ MIBDECL(ifInDiscards) },			\
	{ MIBDECL(ifInErrors) },			\
	{ MIBDECL(ifInUnknownProtos) },			\
	{ MIBDECL(ifOutOctets) },			\
	{ MIBDECL(ifOutUcastPkts) },			\
	{ MIBDECL(ifOutNUcastPkts) },			\
	{ MIBDECL(ifOutDiscards) },			\
	{ MIBDECL(ifOutErrors) },			\
	{ MIBDECL(ifOutQLen) },				\
	{ MIBDECL(ifSpecific) },			\
							\
	{ MIBDECL(dot1dBridge) },			\
	{ MIBDECL(dot1dBase) },				\
	{ MIBDECL(dot1dBaseBridgeAddress) },		\
	{ MIBDECL(dot1dBaseNumPorts) },			\
	{ MIBDECL(dot1dBaseType) },			\
	{ MIBDECL(dot1dBasePortTable) },		\
	{ MIBDECL(dot1dBasePortEntry) },		\
	{ MIBDECL(dot1dBasePort) },			\
	{ MIBDECL(dot1dBasePortIfIndex) },		\
	{ MIBDECL(dot1dBasePortCircuit) },		\
	{ MIBDECL(dot1dBasePortDelayExceededDiscards) },\
	{ MIBDECL(dot1dBasePortMtuExceededDiscards) },	\
	{ MIBDECL(dot1dStp) },				\
	{ MIBDECL(dot1dSr) },				\
	{ MIBDECL(dot1dTp) },				\
	{ MIBDECL(dot1dStatic) },			\
							\
	{ MIBDECL(ibm) },				\
	{ MIBDECL(cmu) },				\
	{ MIBDECL(unix) },				\
	{ MIBDECL(ciscoSystems) },			\
	{ MIBDECL(hp) },				\
	{ MIBDECL(mit) },				\
	{ MIBDECL(nortelNetworks) },			\
	{ MIBDECL(sun) },				\
	{ MIBDECL(3com) },				\
	{ MIBDECL(synOptics) },				\
	{ MIBDECL(enterasys) },				\
	{ MIBDECL(sgi) },				\
	{ MIBDECL(apple) },				\
	{ MIBDECL(nasa) },				\
	{ MIBDECL(att) },				\
	{ MIBDECL(nokia) },				\
	{ MIBDECL(cern) },				\
	{ MIBDECL(oracle) },				\
	{ MIBDECL(motorola) },				\
	{ MIBDECL(ncr) },				\
	{ MIBDECL(ericsson) },				\
	{ MIBDECL(fsc) },				\
	{ MIBDECL(compaq) },				\
	{ MIBDECL(bmw) },				\
	{ MIBDECL(dell) },				\
	{ MIBDECL(iij) },				\
	{ MIBDECL(sandia) },				\
	{ MIBDECL(mercedesBenz) },			\
	{ MIBDECL(alteon) },				\
	{ MIBDECL(extremeNetworks) },			\
	{ MIBDECL(foundryNetworks) },			\
	{ MIBDECL(huawaiTechnology) },			\
	{ MIBDECL(ucDavis) },				\
	{ MIBDECL(freeBSD) },				\
	{ MIBDECL(checkPoint) },			\
	{ MIBDECL(juniper) },				\
	{ MIBDECL(printerWorkingGroup) },		\
	{ MIBDECL(audi) },				\
	{ MIBDECL(volkswagen) },			\
	{ MIBDECL(genua) },				\
	{ MIBDECL(amazon) },				\
	{ MIBDECL(force10Networks) },			\
	{ MIBDECL(vMware) },				\
	{ MIBDECL(alcatelLucent) },			\
	{ MIBDECL(snom) },				\
	{ MIBDECL(netSNMP) },				\
	{ MIBDECL(netflix) },				\
	{ MIBDECL(google) },				\
	{ MIBDECL(f5Networks) },			\
	{ MIBDECL(bsws) },				\
	{ MIBDECL(sFlow) },				\
	{ MIBDECL(microSystems) },			\
	{ MIBDECL(paloAltoNetworks) },			\
	{ MIBDECL(h3c) },				\
	{ MIBDECL(vantronix) },				\
	{ MIBDECL(netBSD) },				\
	{ MIBDECL(openBSD) },				\
	{ MIBDECL(nicira) },				\
	{ MIBDECL(esdenera) },				\
	{ MIBDECL(arcaTrust) },				\
							\
	{ MIBDECL(ucdExperimental) },			\
	{ MIBDECL(ucdDiskIOMIB) },			\
	{ MIBDECL(diskIOTable) },			\
	{ MIBDECL(diskIOEntry) },			\
	{ MIBDECL(diskIOIndex) },			\
	{ MIBDECL(diskIODevice) },			\
	{ MIBDECL(diskIONRead) },			\
	{ MIBDECL(diskIONWritten) },			\
	{ MIBDECL(diskIOReads) },			\
	{ MIBDECL(diskIOWrites) },			\
	{ MIBDECL(diskIONReadX) },			\
	{ MIBDECL(diskIONWrittenX) },			\
							\
	{ MIBDECL(pfMIBObjects) },			\
	{ MIBDECL(pfInfo) },				\
	{ MIBDECL(pfRunning) },				\
	{ MIBDECL(pfRuntime) },				\
	{ MIBDECL(pfDebug) },				\
	{ MIBDECL(pfHostid) },				\
	{ MIBDECL(pfCounters) },			\
	{ MIBDECL(pfCntMatch) },			\
	{ MIBDECL(pfCntBadOffset) },			\
	{ MIBDECL(pfCntFragment) },			\
	{ MIBDECL(pfCntShort) },			\
	{ MIBDECL(pfCntNormalize) },			\
	{ MIBDECL(pfCntMemory) },			\
	{ MIBDECL(pfCntTimestamp) },			\
	{ MIBDECL(pfCntCongestion) },			\
	{ MIBDECL(pfCntIpOptions) },			\
	{ MIBDECL(pfCntProtoCksum) },			\
	{ MIBDECL(pfCntStateMismatch) },		\
	{ MIBDECL(pfCntStateInsert) },			\
	{ MIBDECL(pfCntStateLimit) },			\
	{ MIBDECL(pfCntSrcLimit) },			\
	{ MIBDECL(pfCntSynproxy) },			\
	{ MIBDECL(pfCntTranslate) },			\
	{ MIBDECL(pfCntNoRoute) },			\
	{ MIBDECL(pfStateTable) },			\
	{ MIBDECL(pfStateCount) },			\
	{ MIBDECL(pfStateSearches) },			\
	{ MIBDECL(pfStateInserts) },			\
	{ MIBDECL(pfStateRemovals) },			\
	{ MIBDECL(pfLogInterface) },			\
	{ MIBDECL(pfLogIfName) },			\
	{ MIBDECL(pfLogIfIpBytesIn) },			\
	{ MIBDECL(pfLogIfIpBytesOut) },			\
	{ MIBDECL(pfLogIfIpPktsInPass) },		\
	{ MIBDECL(pfLogIfIpPktsInDrop) },		\
	{ MIBDECL(pfLogIfIpPktsOutPass) },		\
	{ MIBDECL(pfLogIfIpPktsOutDrop) },		\
	{ MIBDECL(pfLogIfIp6BytesIn) },			\
	{ MIBDECL(pfLogIfIp6BytesOut) },		\
	{ MIBDECL(pfLogIfIp6PktsInPass) },		\
	{ MIBDECL(pfLogIfIp6PktsInDrop) },		\
	{ MIBDECL(pfLogIfIp6PktsOutPass) },		\
	{ MIBDECL(pfLogIfIp6PktsOutDrop) },		\
	{ MIBDECL(pfSrcTracking) },			\
	{ MIBDECL(pfSrcTrackCount) },			\
	{ MIBDECL(pfSrcTrackSearches) },		\
	{ MIBDECL(pfSrcTrackInserts) },			\
	{ MIBDECL(pfSrcTrackRemovals) },		\
	{ MIBDECL(pfLimits) },				\
	{ MIBDECL(pfLimitStates) },			\
	{ MIBDECL(pfLimitSourceNodes) },		\
	{ MIBDECL(pfLimitFragments) },			\
	{ MIBDECL(pfLimitMaxTables) },			\
	{ MIBDECL(pfLimitMaxTableEntries) },		\
	{ MIBDECL(pfTimeouts) },			\
	{ MIBDECL(pfTimeoutTcpFirst) },			\
	{ MIBDECL(pfTimeoutTcpOpening) },		\
	{ MIBDECL(pfTimeoutTcpEstablished) },		\
	{ MIBDECL(pfTimeoutTcpClosing) },		\
	{ MIBDECL(pfTimeoutTcpFinWait) },		\
	{ MIBDECL(pfTimeoutTcpClosed) },		\
	{ MIBDECL(pfTimeoutUdpFirst) },			\
	{ MIBDECL(pfTimeoutUdpSingle) },		\
	{ MIBDECL(pfTimeoutUdpMultiple) },		\
	{ MIBDECL(pfTimeoutIcmpFirst) },		\
	{ MIBDECL(pfTimeoutIcmpError) },		\
	{ MIBDECL(pfTimeoutOtherFirst) },		\
	{ MIBDECL(pfTimeoutOtherSingle) },		\
	{ MIBDECL(pfTimeoutOtherMultiple) },		\
	{ MIBDECL(pfTimeoutFragment) },			\
	{ MIBDECL(pfTimeoutInterval) },			\
	{ MIBDECL(pfTimeoutAdaptiveStart) },		\
	{ MIBDECL(pfTimeoutAdaptiveEnd) },		\
	{ MIBDECL(pfTimeoutSrcTrack) },			\
	{ MIBDECL(pfInterfaces) },			\
	{ MIBDECL(pfIfNumber) },			\
	{ MIBDECL(pfIfTable) },				\
	{ MIBDECL(pfIfEntry) },				\
	{ MIBDECL(pfIfIndex) },				\
	{ MIBDECL(pfIfDescr) },				\
	{ MIBDECL(pfIfType) },				\
	{ MIBDECL(pfIfRefs) },				\
	{ MIBDECL(pfIfRules) },				\
	{ MIBDECL(pfIfIn4PassPkts) },			\
	{ MIBDECL(pfIfIn4PassBytes) },			\
	{ MIBDECL(pfIfIn4BlockPkts) },			\
	{ MIBDECL(pfIfIn4BlockBytes) },			\
	{ MIBDECL(pfIfOut4PassPkts) },			\
	{ MIBDECL(pfIfOut4PassBytes) },			\
	{ MIBDECL(pfIfOut4BlockPkts) },			\
	{ MIBDECL(pfIfOut4BlockBytes) },		\
	{ MIBDECL(pfIfIn6PassPkts) },			\
	{ MIBDECL(pfIfIn6PassBytes) },			\
	{ MIBDECL(pfIfIn6BlockPkts) },			\
	{ MIBDECL(pfIfIn6BlockBytes) },			\
	{ MIBDECL(pfIfOut6PassPkts) },			\
	{ MIBDECL(pfIfOut6PassBytes) },			\
	{ MIBDECL(pfIfOut6BlockPkts) },			\
	{ MIBDECL(pfIfOut6BlockBytes) },		\
	{ MIBDECL(pfTables) },				\
	{ MIBDECL(pfTblNumber) },			\
	{ MIBDECL(pfTblTable) },			\
	{ MIBDECL(pfTblEntry) },			\
	{ MIBDECL(pfTblIndex) },			\
	{ MIBDECL(pfTblName) },				\
	{ MIBDECL(pfTblAddresses) },			\
	{ MIBDECL(pfTblAnchorRefs) },			\
	{ MIBDECL(pfTblRuleRefs) },			\
	{ MIBDECL(pfTblEvalsMatch) },			\
	{ MIBDECL(pfTblEvalsNoMatch) },			\
	{ MIBDECL(pfTblInPassPkts) },			\
	{ MIBDECL(pfTblInPassBytes) },			\
	{ MIBDECL(pfTblInBlockPkts) },			\
	{ MIBDECL(pfTblInBlockBytes) },			\
	{ MIBDECL(pfTblInXPassPkts) },			\
	{ MIBDECL(pfTblInXPassBytes) },			\
	{ MIBDECL(pfTblOutPassPkts) },			\
	{ MIBDECL(pfTblOutPassBytes) },			\
	{ MIBDECL(pfTblOutBlockPkts) },			\
	{ MIBDECL(pfTblOutBlockBytes) },		\
	{ MIBDECL(pfTblOutXPassPkts) },			\
	{ MIBDECL(pfTblOutXPassBytes) },		\
	{ MIBDECL(pfTblStatsCleared) },			\
	{ MIBDECL(pfTblInMatchPkts) },			\
	{ MIBDECL(pfTblInMatchBytes) },			\
	{ MIBDECL(pfTblOutMatchPkts) },			\
	{ MIBDECL(pfTblOutMatchBytes) },		\
	{ MIBDECL(pfTblAddrTable) },			\
	{ MIBDECL(pfTblAddrEntry) },			\
	{ MIBDECL(pfTblAddrTblIndex) },			\
	{ MIBDECL(pfTblAddrNet) },			\
	{ MIBDECL(pfTblAddrMask) },			\
	{ MIBDECL(pfTblAddrCleared) },			\
	{ MIBDECL(pfTblAddrInBlockPkts) },		\
	{ MIBDECL(pfTblAddrInBlockBytes) },		\
	{ MIBDECL(pfTblAddrInPassPkts) },		\
	{ MIBDECL(pfTblAddrInPassBytes) },		\
	{ MIBDECL(pfTblAddrOutBlockPkts) },		\
	{ MIBDECL(pfTblAddrOutBlockBytes) },		\
	{ MIBDECL(pfTblAddrOutPassPkts) },		\
	{ MIBDECL(pfTblAddrOutPassBytes) },		\
	{ MIBDECL(pfTblAddrInMatchPkts) },		\
	{ MIBDECL(pfTblAddrInMatchBytes) },		\
	{ MIBDECL(pfTblAddrOutMatchPkts) },		\
	{ MIBDECL(pfTblAddrOutMatchBytes) },		\
	{ MIBDECL(pfLabels) },				\
	{ MIBDECL(pfLabelNumber) },			\
	{ MIBDECL(pfLabelTable) },			\
	{ MIBDECL(pfLabelEntry) },			\
	{ MIBDECL(pfLabelIndex) },			\
	{ MIBDECL(pfLabelName) },			\
	{ MIBDECL(pfLabelEvals) },			\
	{ MIBDECL(pfLabelPkts) },			\
	{ MIBDECL(pfLabelBytes) },			\
	{ MIBDECL(pfLabelInPkts) },			\
	{ MIBDECL(pfLabelInBytes) },			\
	{ MIBDECL(pfLabelOutPkts) },			\
	{ MIBDECL(pfLabelOutBytes) },			\
	{ MIBDECL(pfLabelTotalStates) },		\
	{ MIBDECL(pfsyncStats) },			\
	{ MIBDECL(pfsyncIpPktsRecv) },			\
	{ MIBDECL(pfsyncIp6PktsRecv) },			\
	{ MIBDECL(pfsyncPktDiscardsForBadInterface) },	\
	{ MIBDECL(pfsyncPktDiscardsForBadTtl) },	\
	{ MIBDECL(pfsyncPktShorterThanHeader) },	\
	{ MIBDECL(pfsyncPktDiscardsForBadVersion) },	\
	{ MIBDECL(pfsyncPktDiscardsForBadAction) },	\
	{ MIBDECL(pfsyncPktDiscardsForBadLength) },	\
	{ MIBDECL(pfsyncPktDiscardsForBadAuth) },	\
	{ MIBDECL(pfsyncPktDiscardsForStaleState) },	\
	{ MIBDECL(pfsyncPktDiscardsForBadValues) },	\
	{ MIBDECL(pfsyncPktDiscardsForBadState) },	\
	{ MIBDECL(pfsyncIpPktsSent) },			\
	{ MIBDECL(pfsyncIp6PktsSent) },			\
	{ MIBDECL(pfsyncNoMemory) },			\
	{ MIBDECL(pfsyncOutputErrors) },		\
	{ MIBDECL(sensorsMIBObjects) },			\
	{ MIBDECL(relaydMIBObjects) },			\
	{ MIBDECL(relaydHostTrap) },			\
	{ MIBDECL(relaydHostTrapHostName) },		\
	{ MIBDECL(relaydHostTrapUp) },			\
	{ MIBDECL(relaydHostTrapLastUp) },		\
	{ MIBDECL(relaydHostTrapUpCount) },		\
	{ MIBDECL(relaydHostTrapCheckCount) },		\
	{ MIBDECL(relaydHostTrapTableName) },		\
	{ MIBDECL(relaydHostTrapTableUp) },		\
	{ MIBDECL(relaydHostTrapRetry) },		\
	{ MIBDECL(relaydHostTrapRetryCount) },		\
	{ MIBDECL(sensors) },				\
	{ MIBDECL(sensorNumber) },			\
	{ MIBDECL(sensorTable) },			\
	{ MIBDECL(sensorEntry) },			\
	{ MIBDECL(sensorIndex) },			\
	{ MIBDECL(sensorDescr) },			\
	{ MIBDECL(sensorType) },			\
	{ MIBDECL(sensorDevice) },			\
	{ MIBDECL(sensorValue) },			\
	{ MIBDECL(sensorUnits) },			\
	{ MIBDECL(sensorStatus) },			\
	{ MIBDECL(memMIBObjects) },			\
	{ MIBDECL(memMIBVersion) },			\
	{ MIBDECL(memIfTable) },			\
	{ MIBDECL(memIfEntry) },			\
	{ MIBDECL(memIfName) },				\
	{ MIBDECL(memIfLiveLocks) },			\
	{ MIBDECL(carpMIBObjects) },			\
	{ MIBDECL(carpSysctl) },			\
	{ MIBDECL(carpAllow) },				\
	{ MIBDECL(carpPreempt) },			\
	{ MIBDECL(carpLog) },				\
	{ MIBDECL(carpIf) },				\
	{ MIBDECL(carpIfNumber) },			\
	{ MIBDECL(carpIfTable) },			\
	{ MIBDECL(carpIfEntry) },			\
	{ MIBDECL(carpIfIndex) },			\
	{ MIBDECL(carpIfDescr) },			\
	{ MIBDECL(carpIfVhid) },			\
	{ MIBDECL(carpIfDev) },				\
	{ MIBDECL(carpIfAdvbase) },			\
	{ MIBDECL(carpIfAdvskew) },			\
	{ MIBDECL(carpIfState) },			\
	{ MIBDECL(carpStats) },				\
	{ MIBDECL(carpIpPktsRecv) },			\
	{ MIBDECL(carpIp6PktsRecv) },			\
	{ MIBDECL(carpPktDiscardsBadIface) },		\
	{ MIBDECL(carpPktDiscardsBadTtl) },		\
	{ MIBDECL(carpPktShorterThanHdr) },		\
	{ MIBDECL(carpDiscardsBadCksum) },		\
	{ MIBDECL(carpDiscardsBadVersion) },		\
	{ MIBDECL(carpDiscardsTooShort) },		\
	{ MIBDECL(carpDiscardsBadAuth) },		\
	{ MIBDECL(carpDiscardsBadVhid) },		\
	{ MIBDECL(carpDiscardsBadAddrList) },		\
	{ MIBDECL(carpIpPktsSent) },			\
	{ MIBDECL(carpIp6PktsSent) },			\
	{ MIBDECL(carpNoMemory) },			\
	{ MIBDECL(carpTransitionsToMaster) },		\
	{ MIBDECL(carpGroupTable) },			\
	{ MIBDECL(carpGroupEntry) },			\
	{ MIBDECL(carpGroupName) },			\
	{ MIBDECL(carpGroupDemote) },			\
	{ MIBDECL(localSystem) },			\
	{ MIBDECL(localTest) },				\
							\
	{ MIBDECL(ipMIB) },				\
	{ MIBDECL(ipForwarding) },			\
	{ MIBDECL(ipDefaultTTL) },			\
	{ MIBDECL(ipInReceives) },			\
	{ MIBDECL(ipInHdrErrors) },			\
	{ MIBDECL(ipInAddrErrors) },			\
	{ MIBDECL(ipForwDatagrams) },			\
	{ MIBDECL(ipInUnknownProtos) },			\
	{ MIBDECL(ipInDiscards) },			\
	{ MIBDECL(ipInDelivers) },			\
	{ MIBDECL(ipOutRequests) },			\
	{ MIBDECL(ipOutDiscards) },			\
	{ MIBDECL(ipOutNoRoutes) },			\
	{ MIBDECL(ipReasmTimeout) },			\
	{ MIBDECL(ipReasmReqds) },			\
	{ MIBDECL(ipReasmOKs) },			\
	{ MIBDECL(ipReasmFails) },			\
	{ MIBDECL(ipFragOKs) },				\
	{ MIBDECL(ipFragFails) },			\
	{ MIBDECL(ipFragCreates) },			\
	{ MIBDECL(ipRoutingDiscards) },			\
	{ MIBDECL(ipAddrTable) },			\
	{ MIBDECL(ipAddrEntry) },			\
	{ MIBDECL(ipAdEntAddr) },			\
	{ MIBDECL(ipAdEntIfIndex) },			\
	{ MIBDECL(ipAdEntNetMask) },			\
	{ MIBDECL(ipAdEntBcastAddr) },			\
	{ MIBDECL(ipAdEntReasmMaxSize) },		\
	{ MIBDECL(ipNetToMediaTable) },			\
	{ MIBDECL(ipNetToMediaEntry) },			\
	{ MIBDECL(ipNetToMediaIfIndex) },		\
	{ MIBDECL(ipNetToMediaPhysAddress) },		\
	{ MIBDECL(ipNetToMediaNetAddress) },		\
	{ MIBDECL(ipNetToMediaType) },			\
							\
	{ MIBDECL(ipfMIB) },				\
	{ MIBDECL(ipfInetCidrRouteNumber) },		\
	{ MIBDECL(ipfInetCidrRouteTable) },		\
	{ MIBDECL(ipfInetCidrRouteEntry) },		\
	{ MIBDECL(ipfRouteEntIfIndex) },		\
	{ MIBDECL(ipfRouteEntType) },			\
	{ MIBDECL(ipfRouteEntProto) },			\
	{ MIBDECL(ipfRouteEntAge) },			\
	{ MIBDECL(ipfRouteEntNextHopAS) },		\
	{ MIBDECL(ipfRouteEntRouteMetric1) },		\
	{ MIBDECL(ipfRouteEntRouteMetric2) },		\
	{ MIBDECL(ipfRouteEntRouteMetric3) },		\
	{ MIBDECL(ipfRouteEntRouteMetric4) },		\
	{ MIBDECL(ipfRouteEntRouteMetric5) },		\
	{ MIBDECL(ipfRouteEntStatus) },			\
	{ MIBEND }					\
}

#endif /* SNMPD_MIB_H */
