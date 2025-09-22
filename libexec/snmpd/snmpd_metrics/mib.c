/*	$OpenBSD: mib.c,v 1.9 2024/05/22 08:44:02 martijn Exp $	*/

/*
 * Copyright (c) 2022 Martijn van Duren <martijn@openbsd.org>
 * Copyright (c) 2012 Joel Knight <joel@openbsd.org>
 * Copyright (c) 2007, 2008, 2012 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/queue.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/tree.h>
#include <sys/utsname.h>
#include <sys/sysctl.h>
#include <sys/sensors.h>
#include <sys/sched.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/disk.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_carp.h>
#include <netinet/ip_var.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/pfvar.h>
#include <netinet/ip_ipsp.h>
#include <net/if_pfsync.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <kvm.h>

#include "log.h"
#include "snmpd.h"
#include "mib.h"

struct event		 connev;
const char		*agentxsocket = NULL;
int			 agentxfd = -1;

int			 pageshift;
#define pagetok(size) ((size) << pageshift)

void		 pageshift_init(void);
void		 snmp_connect(struct agentx *, void *, int);
void		 snmp_tryconnect(int, short, void *);
void		 snmp_read(int, short, void *);

struct agentx_context *sac;
struct snmpd *snmpd_env;

/* HOST-RESOURCES-MIB */
struct agentx_object *hrSystemProcesses, *hrSystemMaxProcesses;
struct agentx_index *hrStorageIdx;
struct agentx_object *hrStorageIndex, *hrStorageType, *hrStorageDescr;
struct agentx_object *hrStorageAllocationUnits, *hrStorageSize, *hrStorageUsed;
struct agentx_object *hrStorageAllocationFailures;
struct agentx_index *hrDeviceIdx;
struct agentx_object *hrDeviceIndex, *hrDeviceType, *hrDeviceDescr, *hrDeviceID;
struct agentx_object *hrDeviceStatus, *hrDeviceErrors, *hrProcessorFrwID;
struct agentx_object *hrProcessorLoad;
struct agentx_index *hrSWRunIdx;
struct agentx_object *hrSWRunIndex, *hrSWRunName, *hrSWRunID, *hrSWRunPath;
struct agentx_object *hrSWRunParameters, *hrSWRunType, *hrSWRunStatus;
struct agentx_object *hrSWRunPerfCPU, *hrSWRunPerfMem;

void	 mib_hrsystemuptime(struct agentx_varbind *);
void	 mib_hrsystemdate(struct agentx_varbind *);
void	 mib_hrsystemprocs(struct agentx_varbind *);
void	 mib_hrmemory(struct agentx_varbind *);
void	 mib_hrstorage(struct agentx_varbind *);
void	 mib_hrdevice(struct agentx_varbind *);
void	 mib_hrprocessor(struct agentx_varbind *);
void	 mib_hrswrun(struct agentx_varbind *);

int	 kinfo_proc_comp(const void *, const void *);
int	 kinfo_proc(u_int32_t, struct kinfo_proc **);
void	 kinfo_timer_cb(int, short, void *);
void	 kinfo_proc_free(void);
int	 kinfo_args(struct kinfo_proc *, char ***);
int	 kinfo_path(struct kinfo_proc *, char **);
int	 kinfo_parameters(struct kinfo_proc *, char **);

/* IF-MIB */
struct agentx_index *ifIdx;
struct agentx_object *ifName, *ifInMulticastPkts, *ifInBroadcastPkts;
struct agentx_object *ifOutMulticastPkts, *ifOutBroadcastPkts;
struct agentx_object *ifOutBroadcastPkts, *ifHCInOctets, *ifHCInUcastPkts;
struct agentx_object *ifHCInMulticastPkts, *ifHCInBroadcastPkts, *ifHCOutOctets;
struct agentx_object *ifHCOutUcastPkts, *ifHCOutMulticastPkts;
struct agentx_object *ifHCOutBroadcastPkts, *ifLinkUpDownTrapEnable;
struct agentx_object *ifHighSpeed, *ifPromiscuousMode, *ifConnectorPresent;
struct agentx_object *ifAlias, *ifCounterDiscontinuityTime;
struct agentx_index *ifRcvAddressAddress;
struct agentx_object *ifRcvAddressStatus, *ifRcvAddressType;
struct agentx_object *ifStackLastChange, *ifNumber, *ifIndex, *ifDescr, *ifType;
struct agentx_object *ifMtu, *ifSpeed, *ifPhysAddress, *ifAdminStatus;
struct agentx_object *ifOperStatus, *ifLastChange, *ifInOctets, *ifInUcastPkts;
struct agentx_object *ifInNUcastPkts, *ifInDiscards, *ifInErrors;
struct agentx_object *ifInUnknownProtos, *ifOutOctets, *ifOutUcastPkts;
struct agentx_object *ifOutNUcastPkts, *ifOutDiscards, *ifOutErrors, *ifOutQLen;
struct agentx_object *ifSpecific;

/* OPENBSD-PF-MIB */
struct agentx_object *pfRunning, *pfRuntime, *pfDebug, *pfHostid;
struct agentx_object *pfCntMatch, *pfCntBadOffset, *pfCntFragment, *pfCntShort;
struct agentx_object *pfCntNormalize, *pfCntMemory, *pfCntTimestamp;
struct agentx_object *pfCntCongestion, *pfCntIpOption, *pfCntProtoCksum;
struct agentx_object *pfCntStateMismatch, *pfCntStateInsert, *pfCntStateLimit;
struct agentx_object *pfCntSrcLimit, *pfCntSynproxy, *pfCntTranslate;
struct agentx_object *pfCntNoRoute;
struct agentx_object *pfStateCount, *pfStateSearches, *pfStateInserts;
struct agentx_object *pfStateRemovals;
struct agentx_object *pfLogIfName, *pfLogIfIpBytesIn, *pfLogIfIpBytesOut;
struct agentx_object *pfLogIfIpPktsInPass, *pfLogIfIpPktsInDrop;
struct agentx_object *pfLogIfIpPktsOutPass, *pfLogIfIpPktsOutDrop;
struct agentx_object *pfLogIfIp6BytesIn, *pfLogIfIp6BytesOut;
struct agentx_object *pfLogIfIp6PktsInPass, *pfLogIfIp6PktsInDrop;
struct agentx_object *pfLogIfIp6PktsOutPass, *pfLogIfIp6PktsOutDrop;
struct agentx_object *pfSrcTrackCount, *pfSrcTrackSearches, *pfSrcTrackInserts;
struct agentx_object *pfSrcTrackRemovals;
struct agentx_object *pfLimitStates, *pfLimitSourceNodes, *pfLimitFragments;
struct agentx_object *pfLimitMaxTables, *pfLimitMaxTableEntries;
struct agentx_object *pfTimeoutTcpFirst, *pfTimeoutTcpOpening;
struct agentx_object *pfTimeoutTcpEstablished, *pfTimeoutTcpClosing;
struct agentx_object *pfTimeoutTcpFinWait, *pfTimeoutTcpClosed;
struct agentx_object *pfTimeoutUdpFirst, *pfTimeoutUdpSingle;
struct agentx_object *pfTimeoutUdpMultiple, *pfTimeoutIcmpFirst;
struct agentx_object *pfTimeoutIcmpError, *pfTimeoutOtherFirst;
struct agentx_object *pfTimeoutOtherSingle, *pfTimeoutOtherMultiple;
struct agentx_object *pfTimeoutFragment, *pfTimeoutInterval, *pfTimeoutInterval;
struct agentx_object *pfTimeoutAdaptiveStart, *pfTimeoutAdaptiveEnd;
struct agentx_object *pfTimeoutSrcTrack;
struct agentx_index *pfIfIdx;
struct agentx_object *pfIfNumber, *pfIfIndex, *pfIfDescr, *pfIfType, *pfIfRefs;
struct agentx_object *pfIfRules, *pfIfIn4PassPkts, *pfIfIn4PassBytes;
struct agentx_object *pfIfIn4BlockPkts, *pfIfIn4BlockBytes, *pfIfOut4PassPkts;
struct agentx_object *pfIfOut4PassBytes, *pfIfOut4BlockPkts;
struct agentx_object *pfIfOut4BlockBytes, *pfIfIn6PassPkts, *pfIfIn6PassBytes;
struct agentx_object *pfIfIn6BlockPkts, *pfIfIn6BlockBytes, *pfIfOut6PassPkts;
struct agentx_object *pfIfOut6PassBytes, *pfIfOut6BlockPkts;
struct agentx_object *pfIfOut6BlockBytes;
struct agentx_index *pfTblIdx;
struct agentx_object *pfTblNumber, *pfTblIndex, *pfTblName, *pfTblAddresses;
struct agentx_object *pfTblAnchorRefs, *pfTblRuleRefs, *pfTblEvalsMatch;
struct agentx_object *pfTblEvalsNoMatch, *pfTblInPassPkts, *pfTblInPassBytes;
struct agentx_object *pfTblInBlockPkts, *pfTblInBlockBytes, *pfTblInXPassPkts;
struct agentx_object *pfTblInXPassBytes, *pfTblOutPassPkts, *pfTblOutPassBytes;
struct agentx_object *pfTblOutBlockPkts, *pfTblOutBlockBytes;
struct agentx_object *pfTblOutXPassPkts, *pfTblOutXPassBytes;
struct agentx_object *pfTblStatsCleared, *pfTblInMatchPkts, *pfTblInMatchBytes;
struct agentx_object *pfTblOutMatchPkts, *pfTblOutMatchBytes;
struct agentx_index *pfTblAddrTblIdx, *pfTblAddrNetIdx, *pfTblAddrMaskIdx;
struct agentx_object *pfTblAddrTblIndex, *pfTblAddrNet, *pfTblAddrMask;
struct agentx_object *pfTblAddrCleared, *pfTblAddrInBlockPkts;
struct agentx_object *pfTblAddrInBlockBytes, *pfTblAddrInPassPkts;
struct agentx_object *pfTblAddrInPassBytes, *pfTblAddrOutBlockPkts;
struct agentx_object *pfTblAddrOutBlockBytes, *pfTblAddrOutPassPkts;
struct agentx_object *pfTblAddrOutPassBytes, *pfTblAddrInMatchPkts;
struct agentx_object *pfTblAddrInMatchBytes, *pfTblAddrOutMatchPkts;
struct agentx_object *pfTblAddrOutMatchBytes;
struct agentx_index *pfLabelIdx;
struct agentx_object *pfLabelNumber, *pfLabelIndex, *pfLabelName, *pfLabelEvals;
struct agentx_object *pfLabelPkts, *pfLabelBytes, *pfLabelInPkts;
struct agentx_object *pfLabelInBytes, *pfLabelOutPkts, *pfLabelOutBytes;
struct agentx_object *pfLabelTotalStates;
struct agentx_object *pfsyncIpPktsRecv, *pfsyncIp6PktsRecv;
struct agentx_object *pfsyncPktDiscardsForBadInterface;
struct agentx_object *pfsyncPktDiscardsForBadTtl, *pfsyncPktShorterThanHeader;
struct agentx_object *pfsyncPktDiscardsForBadVersion;
struct agentx_object *pfsyncPktDiscardsForBadAction;
struct agentx_object *pfsyncPktDiscardsForBadLength;
struct agentx_object *pfsyncPktDiscardsForBadAuth;
struct agentx_object *pfsyncPktDiscardsForStaleState;
struct agentx_object *pfsyncPktDiscardsForBadValues;
struct agentx_object *pfsyncPktDiscardsForBadState;
struct agentx_object *pfsyncIpPktsSent, *pfsyncIp6PktsSent, *pfsyncNoMemory;
struct agentx_object *pfsyncOutputErrors;

/* OPENBSD-SENSORS-MIB */
struct agentx_index *sensorIdx;
struct agentx_object *sensorNumber, *sensorIndex, *sensorDescr, *sensorType;
struct agentx_object *sensorDevice, *sensorValue, *sensorUnits, *sensorStatus;

/* OPENBSD-CARP-MIB */
struct agentx_object *carpAllow, *carpPreempt, *carpLog;
struct agentx_index *carpIfIdx;
struct agentx_object *carpIfNumber, *carpIfIndex, *carpIfDescr, *carpIfVhid;
struct agentx_object *carpIfDev, *carpIfAdvbase, *carpIfAdvskew, *carpIfState;
struct agentx_index *carpGroupIdx;
struct agentx_object *carpGroupIndex, *carpGroupName, *carpGroupDemote;
struct agentx_object *carpIpPktsRecv, *carpIp6PktsRecv;
struct agentx_object *carpPktDiscardsForBadInterface;
struct agentx_object *carpPktDiscardsForWrongTtl, *carpPktShorterThanHeader;
struct agentx_object *carpPktDiscardsForBadChecksum;
struct agentx_object *carpPktDiscardsForBadVersion, *carpPktDiscardsForTooShort;
struct agentx_object *carpPktDiscardsForBadAuth, *carpPktDiscardsForBadVhid;
struct agentx_object *carpPktDiscardsForBadAddressList, *carpIpPktsSent;
struct agentx_object *carpIp6PktsSent, *carpNoMemory, *carpTransitionsToMaster;

/* OPENBSD-MEM-MIB */
struct agentx_object *memMIBVersion, *memIfName, *memIfLiveLocks;

/* IP-MIB */
struct agentx_object *ipForwarding, *ipDefaultTTL, *ipInReceives;
struct agentx_object *ipInHdrErrors, *ipInAddrErrors, *ipForwDatagrams;
struct agentx_object *ipInUnknownProtos, *ipInDelivers, *ipOutRequests;
struct agentx_object *ipOutDiscards, *ipOutNoRoutes, *ipReasmTimeout;
struct agentx_object *ipReasmReqds, *ipReasmOKs, *ipReasmFails, *ipFragOKs;
struct agentx_object *ipFragFails, *ipFragCreates, *ipAdEntAddr;
struct agentx_index *ipAdEntAddrIdx;
struct agentx_object *ipAdEntAddr, *ipAdEntIfIndex, *ipAdEntNetMask;
struct agentx_object *ipAdEntBcastAddr, *ipAdEntReasmMaxSize;
struct agentx_index *ipNetToMediaIfIdx, *ipNetToMediaNetAddressIdx;
struct agentx_object *ipNetToMediaIfIndex, *ipNetToMediaPhysAddress;
struct agentx_object *ipNetToMediaNetAddress, *ipNetToMediaType;

/* IP-FORWARD-MIB */
struct agentx_object *inetCidrRouteNumber;
struct agentx_index *inetCidrRouteDestTypeIdx, *inetCidrRouteDestIdx;
struct agentx_index *inetCidrRoutePfxLenIdx, *inetCidrRoutePolicyIdx;
struct agentx_index *inetCidrRouteNextHopTypeIdx, *inetCidrRouteNextHopIdx;
struct agentx_object *inetCidrRouteIfIndex, *inetCidrRouteType;
struct agentx_object *inetCidrRouteProto, *inetCidrRouteAge;
struct agentx_object *inetCidrRouteNextHopAS, *inetCidrRouteMetric1;
struct agentx_object *inetCidrRouteMetric2, *inetCidrRouteMetric3;
struct agentx_object *inetCidrRouteMetric4, *inetCidrRouteMetric5;
struct agentx_object *inetCidrRouteStatus;

/* UCD-DISKIO-MIB */
struct agentx_index *diskIOIdx;
struct agentx_object *diskIOIndex, *diskIODevice, *diskIONRead, *diskIONWritten;
struct agentx_object *diskIOReads, *diskIOWrites, *diskIONReadX;
struct agentx_object *diskIONWrittenX;

/* BRIDGE-MIB */
struct agentx_object *dot1dBaseNumPorts, *dot1dBaseType;
struct agentx_index *dot1dBasePortIdx;
struct agentx_object *dot1dBasePort, *dot1dBasePortIfIndex;
struct agentx_object *dot1dBasePortCircuit, *dot1dBasePortDelayExceededDiscards;
struct agentx_object *dot1dBasePortMtuExceededDiscards;

/* HOST-RESOURCES-MIB */
void
mib_hrsystemuptime(struct agentx_varbind *vb)
{
	struct timespec  uptime;
	long long	 ticks;

	if (clock_gettime(CLOCK_BOOTTIME, &uptime) == -1) {
		log_warn("clock_gettime");
		agentx_varbind_error(vb);
		return;
	}
	ticks = uptime.tv_sec * 100 + uptime.tv_nsec / 10000000;
	agentx_varbind_timeticks(vb, ticks);
}

void
mib_hrsystemdate(struct agentx_varbind *vb)
{
	struct tm	*ptm;
	u_char		 s[11];
	time_t		 now;
	int		 tzoffset;
	unsigned short	 year;

	(void)time(&now);
	ptm = localtime(&now);

	if (ptm == NULL) {
		log_warnx("localtime");
		agentx_varbind_error(vb);
		return;
	}
	year = htons(ptm->tm_year + 1900);
	memcpy(s, &year, 2);
	s[2] = ptm->tm_mon + 1;
	s[3] = ptm->tm_mday;
	s[4] = ptm->tm_hour;
	s[5] = ptm->tm_min;
	s[6] = ptm->tm_sec;
	s[7] = 0;

	tzoffset = ptm->tm_gmtoff;
	if (tzoffset < 0)
		s[8] = '-';
	else
		s[8] = '+';

	s[9] = abs(tzoffset) / 3600;
	s[10] = (abs(tzoffset) - (s[9] * 3600)) / 60;
	agentx_varbind_nstring(vb, s, sizeof(s));
}

void
mib_hrsystemprocs(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	char			 errbuf[_POSIX2_LINE_MAX];
	int			 val;
	int			 mib[] = { CTL_KERN, KERN_MAXPROC };
	kvm_t			*kd;
	size_t			 len;

	obj = agentx_varbind_get_object(vb);
	if (obj == hrSystemProcesses) {
		if ((kd = kvm_openfiles(NULL, NULL, NULL,
		    KVM_NO_FILES, errbuf)) == NULL) {
			log_warn("kvm_openfiles");
			agentx_varbind_error(vb);
			return;
		}

		if (kvm_getprocs(kd, KERN_PROC_ALL, 0,
		    sizeof(struct kinfo_proc), &val) == NULL) {
			log_warn("kvm_getprocs");
			kvm_close(kd);
			agentx_varbind_error(vb);
			return;
		}

		agentx_varbind_gauge32(vb, val);

		kvm_close(kd);
	} else if (obj == hrSystemMaxProcesses) {
		len = sizeof(val);
		if (sysctl(mib, 2, &val, &len, NULL, 0) == -1) {
			log_warn("sysctl");
			agentx_varbind_error(vb);
			return;
		}

		agentx_varbind_integer(vb, val);
	} else
		fatal("%s: Unexpected object", __func__);
}

void
mib_hrmemory(struct agentx_varbind *vb)
{
	int			 mib[] = { CTL_HW, HW_PHYSMEM64 };
	u_int64_t		 physmem;
	size_t			 len = sizeof(physmem);

	if (sysctl(mib, nitems(mib), &physmem, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	agentx_varbind_integer(vb, physmem / 1024);
}

void
mib_hrstorage(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	int32_t				 idx;
	struct statfs			*mntbuf, *mnt;
	int				 mntsize, maxsize;
	uint64_t			 units, size, used, fail = 0;
	const char			*descr = NULL;
	int				 mib[] = { CTL_HW, 0 };
	u_int64_t			 physmem, realmem;
	struct uvmexp			 uvm;
	size_t				 len;
	uint32_t			 sop[] = { HRSTORAGEOTHER };

	/* Physical memory, real memory, swap */
	mib[1] = HW_PHYSMEM64;
	len = sizeof(physmem);
	if (sysctl(mib, nitems(mib), &physmem, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}
	mib[1] = HW_USERMEM64;
	len = sizeof(realmem);
	if (sysctl(mib, nitems(mib), &realmem, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}
	mib[0] = CTL_VM;
	mib[1] = VM_UVMEXP;
	len = sizeof(uvm);
	if (sysctl(mib, nitems(mib), &uvm, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}
	maxsize = 10;

	/* Disks */
	if ((mntsize = getmntinfo(&mntbuf, MNT_NOWAIT)) == 0) {
		log_warn("getmntinfo");
		agentx_varbind_error(vb);
		return;
	}
	maxsize = 30 + mntsize;

	/*
	 * Get and verify the current row index.
	 *
	 * We use a special mapping here that is inspired by other SNMP
	 * agents: index 1 + 2 for RAM, index 10 for swap, index 31 and
	 * higher for disk storage.
	 */
	obj = agentx_varbind_get_object(vb);
	req = agentx_varbind_request(vb);
	idx = agentx_varbind_get_index_integer(vb, hrStorageIdx);
	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}
	if (idx > maxsize) {
		agentx_varbind_notfound(vb);
		return;
	}
	if (req == AGENTX_REQUEST_TYPE_GET) {
		if (idx < 1 || (idx > 2 && idx < 10) ||
		    (idx > 10 && idx < 31)) {
			agentx_varbind_notfound(vb);
			return;
		}
	} else {
		if (idx < 1)
			idx = 1;
		else if (idx > 2 && idx < 10)
			idx = 10;
		else if (idx > 10 && idx < 31)
			idx = 31;
	}

	switch (idx) {
	case 1:
		descr = "Physical memory";
		units = uvm.pagesize;
		size = physmem / uvm.pagesize;
		used = size - uvm.free;
		memcpy(sop, (uint32_t[]){ HRSTORAGERAM }, sizeof(sop));
		break;
	case 2:
		descr = "Real memory";
		units = uvm.pagesize;
		size = realmem / uvm.pagesize;
		used = size - uvm.free;
		memcpy(sop, (uint32_t[]){ HRSTORAGERAM }, sizeof(sop));
		break;
	case 10:
		descr = "Swap space";
		units = uvm.pagesize;
		size = uvm.swpages;
		used = uvm.swpginuse;
		memcpy(sop, (uint32_t[]){ HRSTORAGEVIRTUALMEMORY },
		    sizeof(sop));
		break;
	default:
		mnt = &mntbuf[idx - 31];
		descr = mnt->f_mntonname;
		units = mnt->f_bsize;
		size = mnt->f_blocks;
		used = mnt->f_blocks - mnt->f_bfree;
		memcpy(sop, (uint32_t[]){ HRSTORAGEFIXEDDISK }, sizeof(sop));
		break;
	}

	while (size > INT32_MAX) {
		units *= 2;
		size /= 2;
		used /= 2;
	}

	agentx_varbind_set_index_integer(vb, hrStorageIdx, idx);

	if (obj == hrStorageIndex)
		agentx_varbind_integer(vb, idx);
	else if (obj == hrStorageType)
		agentx_varbind_oid(vb, sop, nitems(sop));
	else if (obj == hrStorageDescr)
		agentx_varbind_string(vb, descr);
	else if (obj == hrStorageAllocationUnits)
		agentx_varbind_integer(vb, units);
	else if (obj == hrStorageSize)
		agentx_varbind_integer(vb, size);
	else if (obj == hrStorageUsed)
		agentx_varbind_integer(vb, used);
	else if (obj == hrStorageAllocationFailures)
		agentx_varbind_counter32(vb, fail);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_hrdevice(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	int32_t				 idx;
	uint32_t			 fail = 0;
	int				 status;
	int				 mib[] = { CTL_HW, HW_MODEL };
	size_t				 len;
	char				 descr[BUFSIZ];
	uint32_t			 sop[] = { HRDEVICEPROCESSOR };

	/* Get and verify the current row index */
	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, hrDeviceIdx);
	req = agentx_varbind_request(vb);
	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
		if (idx < 1)
			idx = 1;
	} else if (req == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE) {
		if (idx < 1)
			idx = 1;
	}
	if (idx < 1 || idx > snmpd_env->sc_ncpu) {
		agentx_varbind_notfound(vb);
		return;
	}

	agentx_varbind_set_index_integer(vb, hrDeviceIdx, idx);

	len = sizeof(descr);
	if (sysctl(mib, nitems(mib), &descr, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}
	/* unknown(1), running(2), warning(3), testing(4), down(5) */
	status = 2;

	if (obj == hrDeviceIndex)
		agentx_varbind_integer(vb, idx);
	else if (obj == hrDeviceType)
		agentx_varbind_oid(vb, sop, nitems(sop));
	else if (obj == hrDeviceDescr)
		agentx_varbind_string(vb, descr);
	else if (obj == hrDeviceID)
		agentx_varbind_oid(vb, AGENTX_OID(0, 0));
	else if (obj == hrDeviceStatus)
		agentx_varbind_integer(vb, status);
	else if (obj == hrDeviceErrors)
		agentx_varbind_counter32(vb, fail);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_hrprocessor(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	int32_t				 idx;
	int64_t				*cptime2, val;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, hrDeviceIdx);
	req = agentx_varbind_request(vb);
	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
		if (idx < 1)
			idx = 1;
	}
	else if (req == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE) {
		if (idx < 1)
			idx = 1;
	}
	if (idx < 1 || idx > snmpd_env->sc_ncpu) {
		agentx_varbind_notfound(vb);
		return;
	}

	agentx_varbind_set_index_integer(vb, hrDeviceIdx, idx);

	if (obj == hrProcessorFrwID)
		agentx_varbind_oid(vb, AGENTX_OID(0, 0));
	else if (obj == hrProcessorLoad) {
		/*
		 * The percentage of time that the system was not
		 * idle during the last minute.
		 */
		if (snmpd_env->sc_cpustates == NULL) {
			log_warnx("cpustates not initialized");
			agentx_varbind_error(vb);
			return;
		}
		cptime2 = snmpd_env->sc_cpustates + (CPUSTATES * (idx - 1));
		val = 100 -
		    (cptime2[CP_IDLE] > 1000 ? 1000 : (cptime2[CP_IDLE] / 10));
		agentx_varbind_integer(vb, val);
	} else
		fatal("%s: Unexpected object", __func__);
}

void
mib_hrswrun(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	int32_t				 idx;
	int32_t				 time;
	struct kinfo_proc		*kinfo;
	char				*s;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, hrSWRunIdx);
	req = agentx_varbind_request(vb);

	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}
	/* Get and verify the current row index */
	if (kinfo_proc(idx, &kinfo) == -1) {
		log_warn("kinfo_proc");
		agentx_varbind_error(vb);
		return;
	}

	if (kinfo == NULL) {
		agentx_varbind_notfound(vb);
		return;
	}
	if (req == AGENTX_REQUEST_TYPE_GET) {
		if (kinfo->p_pid != idx) {
			agentx_varbind_notfound(vb);
			return;
		}
	}
	agentx_varbind_set_index_integer(vb, hrSWRunIdx, kinfo->p_pid);

	if (obj == hrSWRunIndex)
		agentx_varbind_integer(vb, kinfo->p_pid);
	else if (obj == hrSWRunName)
		agentx_varbind_string(vb, kinfo->p_comm);
 	else if (obj == hrSWRunPath) {
		if (kinfo_path(kinfo, &s) == -1) {
			log_warn("kinfo_path");
			agentx_varbind_error(vb);
			return;
		}
		
		agentx_varbind_string(vb, s);
	} else if (obj == hrSWRunID)
		agentx_varbind_oid(vb, AGENTX_OID(0, 0));
	else if (obj == hrSWRunParameters) {
		if (kinfo_parameters(kinfo, &s) == -1) {
			log_warn("kinfo_parameters");
			agentx_varbind_error(vb);
			return;
		}

		agentx_varbind_string(vb, s);
	} else if (obj == hrSWRunType) {
		if (kinfo->p_flag & P_SYSTEM) {
			/* operatingSystem(2) */
			agentx_varbind_integer(vb, 2);
		} else {
			/* application(4) */
			agentx_varbind_integer(vb, 4);
		}
	} else if (obj == hrSWRunStatus) {
		switch (kinfo->p_stat) {
		case SONPROC:
			/* running(1) */
			agentx_varbind_integer(vb, 1);
			break;
		case SIDL:
		case SRUN:
		case SSLEEP:
			/* runnable(2) */
			agentx_varbind_integer(vb, 2);
			break;
		case SSTOP:
			/* notRunnable(3) */
			agentx_varbind_integer(vb, 3);
			break;
		case SDEAD:
		default:
			/* invalid(4) */
			agentx_varbind_integer(vb, 4);
			break;
		}
	} else if (obj == hrSWRunPerfCPU) {
		time = kinfo->p_rtime_sec * 100;
		time += (kinfo->p_rtime_usec + 5000) / 10000;
		agentx_varbind_integer(vb, time);
	} else if (obj == hrSWRunPerfMem) {
		agentx_varbind_integer(vb, pagetok(kinfo->p_vm_tsize +
		    kinfo->p_vm_dsize + kinfo->p_vm_ssize));
	} else
		fatal("%s: Unexpected object", __func__);
}

int
kinfo_proc_comp(const void *a, const void *b)
{
	struct kinfo_proc * const *k1 = a;
	struct kinfo_proc * const *k2 = b;

	return (((*k1)->p_pid > (*k2)->p_pid) ? 1 : -1);
}

static struct event	  kinfo_timer;
static struct kinfo_proc *kp = NULL;
static struct kinfo_proc **klist = NULL;
static size_t		  nkp = 0, nklist = 0;

int
kinfo_proc(u_int32_t idx, struct kinfo_proc **kinfo)
{
	int		 mib[] = { CTL_KERN, KERN_PROC,
			    KERN_PROC_ALL, 0, sizeof(*kp), 0 };
	size_t		 size, count, i;
	struct timeval	 timer;

	if (kp != NULL && klist != NULL)
		goto cached;

	kinfo_proc_free();
	for (;;) {
		size = nkp * sizeof(*kp);
		mib[5] = nkp;
		if (sysctl(mib, nitems(mib), kp, &size, NULL, 0) == -1) {
			if (errno == ENOMEM) {
				kinfo_proc_free();
				continue;
			}

			return (-1);
		}

		count = size / sizeof(*kp);
		if (count <= nkp)
			break;

		kp = malloc(size);
		if (kp == NULL) {
			kinfo_proc_free();
			return (-1);
		}
		nkp = count;
	}

	klist = calloc(count, sizeof(*klist));
	if (klist == NULL) {
		kinfo_proc_free();
		return (-1);
	}
	nklist = count;

	for (i = 0; i < nklist; i++)
		klist[i] = &kp[i];
	qsort(klist, nklist, sizeof(*klist), kinfo_proc_comp);

	evtimer_set(&kinfo_timer, kinfo_timer_cb, NULL);
	timer.tv_sec = 5;
	timer.tv_usec = 0;
	evtimer_add(&kinfo_timer, &timer);

cached:
	*kinfo = NULL;
	for (i = 0; i < nklist; i++) {
		if (klist[i]->p_pid >= (int32_t)idx) {
			*kinfo = klist[i];
			break;
		}
	}

	return (0);
}

void
kinfo_timer_cb(int fd, short event, void *arg)
{
	kinfo_proc_free();
}

void
kinfo_proc_free(void)
{
	free(kp);
	kp = NULL;
	nkp = 0;
	free(klist);
	klist = NULL;
	nklist = 0;
}

int
kinfo_args(struct kinfo_proc *kinfo, char ***s)
{
	static char		*buf = NULL;
	static size_t		 buflen = 128;

	int			 mib[] = { CTL_KERN, KERN_PROC_ARGS,
				    kinfo->p_pid, KERN_PROC_ARGV };
	char			*nbuf;

	*s = NULL;
	if (buf == NULL) {
		buf = malloc(buflen);
		if (buf == NULL)
			return (-1);
	}

	while (sysctl(mib, nitems(mib), buf, &buflen, NULL, 0) == -1) {
		if (errno != ENOMEM) {
			/* some errors are expected, dont get too upset */
			return (0);
		}

		nbuf = realloc(buf, buflen + 128);
		if (nbuf == NULL)
			return (-1);

		buf = nbuf;
		buflen += 128;
	}

	*s = (char **)buf;
	return (0);
}

int
kinfo_path(struct kinfo_proc *kinfo, char **s)
{
	static char		 str[129];
	char			**argv;

	if (kinfo_args(kinfo, &argv) == -1)
		return (-1);

	str[0] = '\0';
	*s = str;
	if (argv != NULL && argv[0] != NULL)
		strlcpy(str, argv[0], sizeof(str));
	return (0);
}

int
kinfo_parameters(struct kinfo_proc *kinfo, char **s)
{
	static char		 str[129];
	char			**argv;

	if (kinfo_args(kinfo, &argv) == -1)
		return (-1);

	str[0] = '\0';
	*s = str;
	if (argv == NULL || argv[0] == NULL)
		return (0);
	argv++;

	while (*argv != NULL) {
		strlcat(str, *argv, sizeof(str));
		argv++;
		if (*argv != NULL)
			strlcat(str, " ", sizeof(str));
	}

	return (0);
}

/*
 * Defined in IF-MIB.txt (RFCs 1229, 1573, 2233, 2863)
 */

void	 mib_ifnumber(struct agentx_varbind *);
struct kif
	*mib_ifget(u_int);
void	 mib_iftable(struct agentx_varbind *);
void	 mib_ifxtable(struct agentx_varbind *);
void	 mib_ifstacklast(struct agentx_varbind *);
void	 mib_ifrcvtable(struct agentx_varbind *);

static uint8_t ether_zeroaddr[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

void
mib_ifnumber(struct agentx_varbind *vb)
{
	agentx_varbind_integer(vb, kr_ifnumber());
}

struct kif *
mib_ifget(u_int idx)
{
	struct kif	*kif;

	if ((kif = kr_getif(idx)) == NULL) {
		/*
		 * It may happen that an interface with a specific index
		 * does not exist or has been removed. Jump to the next
		 * available interface index.
		 */
		for (kif = kr_getif(0); kif != NULL;
		    kif = kr_getnextif(kif->if_index))
			if (kif->if_index > idx)
				break;
		if (kif == NULL)
			return (NULL);
	}
	idx = kif->if_index;

	/* Update interface information */
	kr_updateif(idx);
	if ((kif = kr_getif(idx)) == NULL) {
		log_debug("mib_ifxtable: interface %d disappeared?", idx);
		return (NULL);
	}

	return (kif);
}

void
mib_iftable(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	int32_t				 idx = 0;
	struct kif			*kif;
	long long			 i;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, ifIdx);
	req = agentx_varbind_request(vb);
	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}
	if ((kif = mib_ifget(idx)) == NULL) {
		agentx_varbind_notfound(vb);
		return;
	}
	if (req == AGENTX_REQUEST_TYPE_GET) {
		if (idx != kif->if_index) {
			agentx_varbind_notfound(vb);
			return;
		}
	}
	agentx_varbind_set_index_integer(vb, ifIdx, kif->if_index);

	if (obj == ifIndex)
		agentx_varbind_integer(vb, kif->if_index);
	else if (obj == ifDescr) {
		/*
		 * The ifDescr should contain a vendor, product, etc.
		 * but we just use the interface name (like ifName).
		 * The interface name includes the driver name on OpenBSD.
		 */
		agentx_varbind_string(vb, kif->if_name);
	} else if (obj == ifType) {
		if (kif->if_type >= 0xf0) {
			/*
			 * It does not make sense to announce the private
			 * interface types for CARP, ENC, PFSYNC, etc.
			 */
			agentx_varbind_integer(vb, IFT_OTHER);
		} else
			agentx_varbind_integer(vb, kif->if_type);
	} else if (obj == ifMtu)
		agentx_varbind_integer(vb, kif->if_mtu);
	else if (obj == ifSpeed) {
		if (kif->if_baudrate > UINT32_MAX) {
			/* speed should be obtained from ifHighSpeed instead */
			agentx_varbind_gauge32(vb, UINT32_MAX);
		} else
			agentx_varbind_gauge32(vb, kif->if_baudrate);
	} else if (obj == ifPhysAddress) {
		if (bcmp(kif->if_lladdr, ether_zeroaddr,
		    sizeof(kif->if_lladdr)) == 0) {
			agentx_varbind_string(vb, "");
		} else {
			agentx_varbind_nstring(vb, kif->if_lladdr,
			    sizeof(kif->if_lladdr));
		}
	} else if (obj == ifAdminStatus) {
		/* ifAdminStatus up(1), down(2), testing(3) */
		i = (kif->if_flags & IFF_UP) ? 1 : 2;
		agentx_varbind_integer(vb, i);
	} else if (obj == ifOperStatus) {
		if ((kif->if_flags & IFF_UP) == 0)
			i = 2;	/* down(2) */
		else if (kif->if_link_state == LINK_STATE_UNKNOWN)
			i = 4;	/* unknown(4) */
		else if (LINK_STATE_IS_UP(kif->if_link_state))
			i = 1;	/* up(1) */
		else
			i = 7;	/* lowerLayerDown(7) or dormant(5)? */
		agentx_varbind_integer(vb, i);
	} else if (obj == ifLastChange)
		agentx_varbind_timeticks(vb, kif->if_ticks);
	else if (obj == ifInOctets)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_ibytes);
	else if (obj == ifInUcastPkts)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_ipackets);
	else if (obj == ifInNUcastPkts)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_imcasts);
	else if (obj == ifInDiscards)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_iqdrops);
	else if (obj == ifInErrors)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_ierrors);
	else if (obj == ifInUnknownProtos)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_noproto);
	else if (obj == ifOutOctets)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_obytes);
	else if (obj == ifOutUcastPkts)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_opackets);
	else if (obj == ifOutNUcastPkts)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_omcasts);
	else if (obj == ifOutDiscards)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_oqdrops);
	else if (obj == ifOutErrors)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_oerrors);
	else if (obj == ifOutQLen)
		agentx_varbind_gauge32(vb, 0);
	else if (obj == ifSpecific)
		agentx_varbind_oid(vb, AGENTX_OID(0, 0));
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_ifxtable(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	int32_t				 idx = 0;
	struct kif			*kif;
	int				 i = 0;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, ifIdx);
	req = agentx_varbind_request(vb);
	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}
	if ((kif = mib_ifget(idx)) == NULL) {
		agentx_varbind_notfound(vb);
		return;
	}
	if (req == AGENTX_REQUEST_TYPE_GET) {
		if (idx != kif->if_index) {
			agentx_varbind_notfound(vb);
			return;
		}
	}
	agentx_varbind_set_index_integer(vb, ifIdx, kif->if_index);

	if (obj == ifName)
		agentx_varbind_string(vb, kif->if_name);
	else if (obj == ifInMulticastPkts)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_imcasts);
	else if (obj == ifInBroadcastPkts)
		agentx_varbind_counter32(vb, 0);
	else if (obj == ifOutMulticastPkts)
		agentx_varbind_counter32(vb, (uint32_t)kif->if_omcasts);
	else if (obj == ifOutBroadcastPkts)
		agentx_varbind_counter32(vb, 0);
	else if (obj == ifHCInOctets)
		agentx_varbind_counter64(vb, (uint64_t)kif->if_ibytes);
	else if (obj == ifHCInUcastPkts)
		agentx_varbind_counter64(vb, (uint64_t)kif->if_ipackets);
	else if (obj == ifHCInMulticastPkts)
		agentx_varbind_counter64(vb, (uint64_t)kif->if_imcasts);
	else if (obj == ifHCInBroadcastPkts)
		agentx_varbind_counter64(vb, 0);
	else if (obj == ifHCOutOctets)
		agentx_varbind_counter64(vb, (uint64_t)kif->if_obytes);
	else if (obj == ifHCOutUcastPkts)
		agentx_varbind_counter64(vb, (uint64_t)kif->if_opackets);
	else if (obj == ifHCOutMulticastPkts)
		agentx_varbind_counter64(vb, (uint64_t)kif->if_omcasts);
	else if (obj == ifHCOutBroadcastPkts)
		agentx_varbind_counter64(vb, 0);
	else if (obj == ifLinkUpDownTrapEnable)
		agentx_varbind_integer(vb, 0);	/* enabled(1), disabled(2) */
	else if (obj == ifHighSpeed) {
		i = kif->if_baudrate >= 1000000 ?
		    kif->if_baudrate / 1000000 : 0;
		agentx_varbind_gauge32(vb, i);
	} else if (obj == ifPromiscuousMode) {
		/* ifPromiscuousMode: true(1), false(2) */
		i = kif->if_flags & IFF_PROMISC ? 1 : 2;
		agentx_varbind_integer(vb, i);
	} else if (obj == ifConnectorPresent) {
		/* ifConnectorPresent: false(2), true(1) */
		i = kif->if_type == IFT_ETHER ? 1 : 2;
		agentx_varbind_integer(vb, i);
	} else if (obj == ifAlias)
		agentx_varbind_string(vb, kif->if_descr);
	else if (obj == ifCounterDiscontinuityTime)
		agentx_varbind_timeticks(vb, 0);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_ifstacklast(struct agentx_varbind *vb)
{
	agentx_varbind_timeticks(vb, kr_iflastchange());
}

void
mib_ifrcvtable(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	int32_t				 idx = 0;
	const uint8_t			*lladdr;
	struct kif			*kif;
	int				 i = 0, llcmp, impl;
	size_t				 slen;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, ifIdx);
	lladdr = (const uint8_t *)agentx_varbind_get_index_string(vb,
	    ifRcvAddressAddress, &slen, &impl);
	if (lladdr == NULL)
		lladdr = ether_zeroaddr;
	req = agentx_varbind_request(vb);

	if ((kif = mib_ifget(idx)) == NULL) {
		agentx_varbind_notfound(vb);
		return;
	}
	/*
	 * The lladdr of the interface will be encoded in the returned OID
	 * ifRcvAddressX.ifindex.6.x.x.x.x.x.x = val
	 * Thanks to the virtual cloner interfaces, it is an easy 1:1
	 * mapping in OpenBSD; only one lladdr (MAC) address per interface.
	 */
	if (slen == 6)
		llcmp = bcmp(lladdr, kif->if_lladdr, sizeof(kif->if_lladdr));
	if (req == AGENTX_REQUEST_TYPE_GET) {
		if (idx != kif->if_index || slen != 6 || llcmp != 0) {
			agentx_varbind_notfound(vb);
			return;
		}
	} else if (idx == kif->if_index) {
		if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
			if (slen > 6 || llcmp >= 0)
				kif = kr_getnextif(kif->if_index);
		} else {
			if (slen > 6 || llcmp > 0)
				kif = kr_getnextif(kif->if_index);
		}
		if (kif == NULL) {
			agentx_varbind_notfound(vb);
			return;
		}
	}
	agentx_varbind_set_index_integer(vb, ifIdx, kif->if_index);
	agentx_varbind_set_index_nstring(vb, ifRcvAddressAddress,
	    kif->if_lladdr, sizeof(kif->if_lladdr));

	if (obj == ifRcvAddressStatus) {
		/* ifRcvAddressStatus: RowStatus active(1), notInService(2) */
		i = kif->if_flags & IFF_UP ? 1 : 2;
		agentx_varbind_integer(vb, i);
	} else if (obj == ifRcvAddressType) {
		/* ifRcvAddressType: other(1), volatile(2), nonVolatile(3) */
		agentx_varbind_integer(vb, 1);
	} else
		fatal("%s: Unexpected object", __func__);
}

/*
 * Defined in 
 * - OPENBSD-PF-MIB.txt
 * - OPENBSD-SENSORS-MIB.txt
 * - OPENBSD-CARP-MIB.txt
 * (http://www.packetmischief.ca/openbsd-snmp-mibs/)
 */ 
#define OIDVER_OPENBSD_MEM		1

struct carpif {
	struct carpreq	 carpr;
	struct kif	 kif;
};

void	 mib_close_pftrans(struct agentx_varbind *, u_int32_t);

void	 mib_pfinfo(struct agentx_varbind *);
void	 mib_pfcounters(struct agentx_varbind *);
void	 mib_pfscounters(struct agentx_varbind *);
void	 mib_pflogif(struct agentx_varbind *);
void	 mib_pfsrctrack(struct agentx_varbind *);
void	 mib_pflimits(struct agentx_varbind *);
void	 mib_pftimeouts(struct agentx_varbind *);
void	 mib_pfifnum(struct agentx_varbind *);
void	 mib_pfiftable(struct agentx_varbind *);
void	 mib_pftablenum(struct agentx_varbind *);
void	 mib_pftables(struct agentx_varbind *);
void	 mib_pftableaddrs(struct agentx_varbind *);
void	 mib_pflabelnum(struct agentx_varbind *);
void	 mib_pflabels(struct agentx_varbind *);
void	 mib_pfsyncstats(struct agentx_varbind *);

void	 mib_sensornum(struct agentx_varbind *);
void	 mib_sensors(struct agentx_varbind *);
const char *mib_sensorunit(struct sensor *);
char	*mib_sensorvalue(struct sensor *);

void	 mib_carpsysctl(struct agentx_varbind *);
void	 mib_carpstats(struct agentx_varbind *);
void	 mib_carpiftable(struct agentx_varbind *);
void	 mib_carpgrouptable(struct agentx_varbind *);
void	 mib_carpifnum(struct agentx_varbind *);
struct carpif
	*mib_carpifget(u_int);
void	 mib_memversion(struct agentx_varbind *);
void	 mib_memiftable(struct agentx_varbind *);

void
mib_pfinfo(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	struct pf_status	 s;
	time_t			 runtime = 0;
	struct timespec		 uptime;

	if (pf_get_stats(&s)) {
		agentx_varbind_error(vb);
		return;
	}

	obj = agentx_varbind_get_object(vb);
	if (obj == pfRunning)
		agentx_varbind_integer(vb, s.running);
	else if (obj == pfRuntime) {
		if (!clock_gettime(CLOCK_BOOTTIME, &uptime))
			runtime = uptime.tv_sec - s.since;
		runtime *= 100;
		agentx_varbind_timeticks(vb, runtime);
	} else if (obj == pfDebug)
		agentx_varbind_integer(vb, s.debug);
	else if (obj == pfHostid)
		agentx_varbind_printf(vb, "0x%08x", ntohl(s.hostid));
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_pfcounters(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	struct pf_status	 s;

	if (pf_get_stats(&s)) {
		agentx_varbind_error(vb);
		return;
	}

	obj = agentx_varbind_get_object(vb);
	if (obj == pfCntMatch)
		agentx_varbind_counter64(vb, s.counters[PFRES_MATCH]);
	else if (obj == pfCntBadOffset)
		agentx_varbind_counter64(vb, s.counters[PFRES_BADOFF]);
	else if (obj == pfCntFragment)
		agentx_varbind_counter64(vb, s.counters[PFRES_FRAG]);
	else if (obj == pfCntShort)
		agentx_varbind_counter64(vb, s.counters[PFRES_SHORT]);
	else if (obj == pfCntNormalize)
		agentx_varbind_counter64(vb, s.counters[PFRES_NORM]);
	else if (obj == pfCntMemory)
		agentx_varbind_counter64(vb, s.counters[PFRES_MEMORY]);
	else if (obj == pfCntTimestamp)
		agentx_varbind_counter64(vb, s.counters[PFRES_TS]);
	else if (obj == pfCntCongestion)
		agentx_varbind_counter64(vb, s.counters[PFRES_CONGEST]);
	else if (obj == pfCntIpOption)
		agentx_varbind_counter64(vb, s.counters[PFRES_IPOPTIONS]);
	else if (obj == pfCntProtoCksum)
		agentx_varbind_counter64(vb, s.counters[PFRES_PROTCKSUM]);
	else if (obj == pfCntStateMismatch)
		agentx_varbind_counter64(vb, s.counters[PFRES_BADSTATE]);
	else if (obj == pfCntStateInsert)
		agentx_varbind_counter64(vb, s.counters[PFRES_STATEINS]);
	else if (obj == pfCntStateLimit)
		agentx_varbind_counter64(vb, s.counters[PFRES_MAXSTATES]);
	else if (obj == pfCntSrcLimit)
		agentx_varbind_counter64(vb, s.counters[PFRES_SRCLIMIT]);
	else if (obj == pfCntSynproxy)
		agentx_varbind_counter64(vb, s.counters[PFRES_SYNPROXY]);
	else if (obj == pfCntTranslate)
		agentx_varbind_counter64(vb, s.counters[PFRES_TRANSLATE]);
	else if (obj == pfCntNoRoute)
		agentx_varbind_counter64(vb, s.counters[PFRES_NOROUTE]);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_pfscounters(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	struct pf_status	 s;

	if (pf_get_stats(&s)) {
		agentx_varbind_error(vb);
		return;
	}

	obj = agentx_varbind_get_object(vb);
	if (obj == pfStateCount)
		agentx_varbind_unsigned32(vb, s.states);
	else if (obj == pfStateSearches)
		agentx_varbind_counter64(vb, s.fcounters[FCNT_STATE_SEARCH]);
	else if (obj == pfStateInserts)
		agentx_varbind_counter64(vb, s.fcounters[FCNT_STATE_INSERT]);
	else if (obj == pfStateRemovals)
		agentx_varbind_counter64(vb, s.fcounters[FCNT_STATE_REMOVALS]);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_pflogif(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	struct pf_status	 s;

	if (pf_get_stats(&s)) {
		agentx_varbind_error(vb);
		return;
	}

	obj = agentx_varbind_get_object(vb);
	if (obj == pfLogIfName)
		agentx_varbind_string(vb, s.ifname);
	else if (obj == pfLogIfIpBytesIn)
		agentx_varbind_counter64(vb, s.bcounters[IPV4][IN]);
	else if (obj == pfLogIfIpBytesOut)
		agentx_varbind_counter64(vb, s.bcounters[IPV4][OUT]);
	else if (obj == pfLogIfIpPktsInPass)
		agentx_varbind_counter64(vb, s.pcounters[IPV4][IN][PF_PASS]);
	else if (obj == pfLogIfIpPktsInDrop)
		agentx_varbind_counter64(vb, s.pcounters[IPV4][IN][PF_DROP]);
	else if (obj == pfLogIfIpPktsOutPass)
		agentx_varbind_counter64(vb, s.pcounters[IPV4][OUT][PF_PASS]);
	else if (obj == pfLogIfIpPktsOutDrop)
		agentx_varbind_counter64(vb, s.pcounters[IPV4][OUT][PF_DROP]);
	else if (obj == pfLogIfIp6BytesIn)
		agentx_varbind_counter64(vb, s.bcounters[IPV6][IN]);
	else if (obj == pfLogIfIp6BytesOut)
		agentx_varbind_counter64(vb, s.bcounters[IPV6][OUT]);
	else if (obj == pfLogIfIp6PktsInPass)
		agentx_varbind_counter64(vb, s.pcounters[IPV6][IN][PF_PASS]);
	else if (obj == pfLogIfIp6PktsInDrop)
		agentx_varbind_counter64(vb, s.pcounters[IPV6][IN][PF_DROP]);
	else if (obj == pfLogIfIp6PktsOutPass)
		agentx_varbind_counter64(vb, s.pcounters[IPV6][OUT][PF_PASS]);
	else if (obj == pfLogIfIp6PktsOutDrop)
		agentx_varbind_counter64(vb, s.pcounters[IPV6][OUT][PF_DROP]);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_pfsrctrack(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	struct pf_status	 s;

	if (pf_get_stats(&s)) {
		agentx_varbind_error(vb);
		return;
	}

	obj = agentx_varbind_get_object(vb);
	if (obj == pfSrcTrackCount)
		agentx_varbind_unsigned32(vb, s.src_nodes);
	else if (obj == pfSrcTrackSearches)
		agentx_varbind_counter64(vb, s.scounters[SCNT_SRC_NODE_SEARCH]);
	else if (obj == pfSrcTrackInserts)
		agentx_varbind_counter64(vb, s.scounters[SCNT_SRC_NODE_INSERT]);
	else if (obj == pfSrcTrackRemovals)
		agentx_varbind_counter64(vb,
		    s.scounters[SCNT_SRC_NODE_REMOVALS]);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_pflimits(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	struct pfioc_limit	 pl;
	extern int		 devpf;

	obj = agentx_varbind_get_object(vb);
	memset(&pl, 0, sizeof(pl));
	if (obj == pfLimitStates)
		pl.index = PF_LIMIT_STATES;
	else if (obj == pfLimitSourceNodes)
		pl.index = PF_LIMIT_SRC_NODES;
	else if (obj == pfLimitFragments)
		pl.index = PF_LIMIT_FRAGS;
	else if (obj == pfLimitMaxTables)
		pl.index = PF_LIMIT_TABLES;
	else if (obj == pfLimitMaxTableEntries)
		pl.index = PF_LIMIT_TABLE_ENTRIES;
	else
		fatal("%s: Unexpected object", __func__);

	if (ioctl(devpf, DIOCGETLIMIT, &pl) == -1) {
		log_warn("DIOCGETLIMIT");
		agentx_varbind_error(vb);
		return;
	}

	agentx_varbind_unsigned32(vb, pl.limit);
}

void
mib_pftimeouts(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	struct pfioc_tm		 pt;
	extern int		 devpf;

	obj = agentx_varbind_get_object(vb);
	memset(&pt, 0, sizeof(pt));
	if (obj == pfTimeoutTcpFirst)
		pt.timeout = PFTM_TCP_FIRST_PACKET;
	else if (obj == pfTimeoutTcpOpening)
		pt.timeout = PFTM_TCP_OPENING;
	else if (obj == pfTimeoutTcpEstablished)
		pt.timeout = PFTM_TCP_ESTABLISHED;
	else if (obj == pfTimeoutTcpClosing)
		pt.timeout = PFTM_TCP_CLOSING;
	else if (obj == pfTimeoutTcpFinWait)
		pt.timeout = PFTM_TCP_FIN_WAIT;
	else if (obj == pfTimeoutTcpClosed)
		pt.timeout = PFTM_TCP_CLOSED;
	else if (obj == pfTimeoutUdpFirst)
		pt.timeout = PFTM_UDP_FIRST_PACKET;
	else if (obj == pfTimeoutUdpSingle)
		pt.timeout = PFTM_UDP_SINGLE;
	else if (obj == pfTimeoutUdpMultiple)
		pt.timeout = PFTM_UDP_MULTIPLE;
	else if (obj == pfTimeoutIcmpFirst)
		pt.timeout = PFTM_ICMP_FIRST_PACKET;
	else if (obj == pfTimeoutIcmpError)
		pt.timeout = PFTM_ICMP_ERROR_REPLY;
	else if (obj == pfTimeoutOtherFirst)
		pt.timeout = PFTM_OTHER_FIRST_PACKET;
	else if (obj == pfTimeoutOtherSingle)
		pt.timeout = PFTM_OTHER_SINGLE;
	else if (obj == pfTimeoutOtherMultiple)
		pt.timeout = PFTM_OTHER_MULTIPLE;
	else if (obj == pfTimeoutFragment)
		pt.timeout = PFTM_FRAG;
	else if (obj == pfTimeoutInterval)
		pt.timeout = PFTM_INTERVAL;
	else if (obj == pfTimeoutAdaptiveStart)
		pt.timeout = PFTM_ADAPTIVE_START;
	else if (obj == pfTimeoutAdaptiveEnd)
		pt.timeout = PFTM_ADAPTIVE_END;
	else if (obj == pfTimeoutSrcTrack)
		pt.timeout = PFTM_SRC_NODE;
	else
		fatal("%s: Unexpected object", __func__);
		
	if (ioctl(devpf, DIOCGETTIMEOUT, &pt) == -1) {
		log_warn("DIOCGETTIMEOUT");
		agentx_varbind_error(vb);
		return;
	}

	agentx_varbind_integer(vb, pt.seconds);
}

void
mib_pfifnum(struct agentx_varbind *vb)
{
	int	 c;

	if ((c = pfi_count()) == -1)
		agentx_varbind_error(vb);
	else
		agentx_varbind_integer(vb, c);
}

void
mib_pfiftable(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	struct pfi_kif			 pif;
	int				 idx, iftype;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, pfIfIdx);
	req = agentx_varbind_request(vb);

	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx < 1)
			idx = 1;
		else if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		} else
			idx++;
	} else if (req == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE) {
		if (idx < 1)
			idx = 1;
	}
	if (pfi_get_if(&pif, idx)) {
		agentx_varbind_notfound(vb);
		return;
	}
	agentx_varbind_set_index_integer(vb, pfIfIdx, idx);

	if (obj == pfIfIndex)
		agentx_varbind_integer(vb, idx);
	else if (obj == pfIfDescr)
		agentx_varbind_string(vb, pif.pfik_name);
	else if (obj == pfIfType) {
		iftype = (pif.pfik_ifp == NULL ? PFI_IFTYPE_GROUP
		    : PFI_IFTYPE_INSTANCE);
		agentx_varbind_integer(vb, iftype);
	} else if (obj == pfIfRefs)
		agentx_varbind_unsigned32(vb, pif.pfik_states);
	else if (obj == pfIfRules)
		agentx_varbind_unsigned32(vb, pif.pfik_rules);
	else if (obj == pfIfIn4PassPkts)
		agentx_varbind_counter64(vb, pif.pfik_packets[IPV4][IN][PASS]);
	else if (obj == pfIfIn4PassBytes)
		agentx_varbind_counter64(vb, pif.pfik_bytes[IPV4][IN][PASS]);
	else if (obj == pfIfIn4BlockPkts)
		agentx_varbind_counter64(vb, pif.pfik_packets[IPV4][IN][BLOCK]);
	else if (obj == pfIfIn4BlockBytes)
		agentx_varbind_counter64(vb, pif.pfik_bytes[IPV4][IN][BLOCK]);
	else if (obj == pfIfOut4PassPkts)
		agentx_varbind_counter64(vb, pif.pfik_packets[IPV4][OUT][PASS]);
	else if (obj == pfIfOut4PassBytes)
		agentx_varbind_counter64(vb, pif.pfik_bytes[IPV4][OUT][PASS]);
	else if (obj == pfIfOut4BlockPkts)
		agentx_varbind_counter64(vb, pif.pfik_packets[IPV4][OUT][BLOCK]);
	else if (obj == pfIfOut4BlockBytes)
		agentx_varbind_counter64(vb, pif.pfik_bytes[IPV4][OUT][BLOCK]);
	else if (obj == pfIfIn6PassPkts)
		agentx_varbind_counter64(vb, pif.pfik_packets[IPV6][IN][PASS]);
	else if (obj == pfIfIn6PassBytes)
		agentx_varbind_counter64(vb, pif.pfik_bytes[IPV6][IN][PASS]);
	else if (obj == pfIfIn6BlockPkts)
		agentx_varbind_counter64(vb, pif.pfik_packets[IPV6][IN][BLOCK]);
	else if (obj == pfIfIn6BlockBytes)
		agentx_varbind_counter64(vb, pif.pfik_bytes[IPV6][IN][BLOCK]);
	else if (obj == pfIfOut6PassPkts)
		agentx_varbind_counter64(vb, pif.pfik_packets[IPV6][OUT][PASS]);
	else if (obj == pfIfOut6PassBytes)
		agentx_varbind_counter64(vb, pif.pfik_bytes[IPV6][OUT][PASS]);
	else if (obj == pfIfOut6BlockPkts)
		agentx_varbind_counter64(vb, pif.pfik_packets[IPV6][OUT][BLOCK]);
	else if (obj == pfIfOut6BlockBytes)
		agentx_varbind_counter64(vb, pif.pfik_bytes[IPV6][OUT][BLOCK]);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_pftablenum(struct agentx_varbind *vb)
{
	int	 c;

	if ((c = pft_count()) == -1)
		agentx_varbind_error(vb);
	else
		agentx_varbind_integer(vb, c);
}

void
mib_pftables(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	struct pfr_tstats		 ts;
	time_t				 tzero;
	int				 idx;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, pfTblIdx);
	req = agentx_varbind_request(vb);

	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx < 1)
			idx = 1;
		else if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		} else
			idx++;
	} else if (req == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE) {
		if (idx < 1)
			idx = 1;
	}
	if (pft_get_table(&ts, idx)) {
		agentx_varbind_notfound(vb);
		return;
	}
	agentx_varbind_set_index_integer(vb, pfTblIdx, idx);

	if (obj == pfTblIndex)
		agentx_varbind_integer(vb, idx);
	else if (obj == pfTblName)
		agentx_varbind_string(vb, ts.pfrts_name);
	else if (obj == pfTblAddresses)
		agentx_varbind_integer(vb, ts.pfrts_cnt);
	else if (obj == pfTblAnchorRefs)
		agentx_varbind_integer(vb, ts.pfrts_refcnt[PFR_REFCNT_ANCHOR]);
	else if (obj == pfTblRuleRefs)
		agentx_varbind_integer(vb, ts.pfrts_refcnt[PFR_REFCNT_RULE]);
	else if (obj == pfTblEvalsMatch)
		agentx_varbind_counter64(vb, ts.pfrts_match);
	else if (obj == pfTblEvalsNoMatch)
		agentx_varbind_counter64(vb, ts.pfrts_nomatch);
	else if (obj == pfTblInPassPkts)
		agentx_varbind_counter64(vb, ts.pfrts_packets[IN][PFR_OP_PASS]);
	else if (obj == pfTblInPassBytes)
		agentx_varbind_counter64(vb, ts.pfrts_bytes[IN][PFR_OP_PASS]);
	else if (obj == pfTblInBlockPkts)
		agentx_varbind_counter64(vb, ts.pfrts_packets[IN][PFR_OP_BLOCK]);
	else if (obj == pfTblInBlockBytes)
		agentx_varbind_counter64(vb, ts.pfrts_bytes[IN][PFR_OP_BLOCK]);
	else if (obj == pfTblInXPassPkts)
		agentx_varbind_counter64(vb, ts.pfrts_packets[IN][PFR_OP_XPASS]);
	else if (obj == pfTblInXPassBytes)
		agentx_varbind_counter64(vb, ts.pfrts_bytes[IN][PFR_OP_XPASS]);
	else if (obj == pfTblOutPassPkts)
		agentx_varbind_counter64(vb, ts.pfrts_packets[OUT][PFR_OP_PASS]);
	else if (obj == pfTblOutPassBytes)
		agentx_varbind_counter64(vb, ts.pfrts_bytes[OUT][PFR_OP_PASS]);
	else if (obj == pfTblOutBlockPkts)
		agentx_varbind_counter64(vb, ts.pfrts_packets[OUT][PFR_OP_BLOCK]);
	else if (obj == pfTblOutBlockBytes)
		agentx_varbind_counter64(vb, ts.pfrts_bytes[OUT][PFR_OP_BLOCK]);
	else if (obj == pfTblOutXPassPkts)
		agentx_varbind_counter64(vb, ts.pfrts_packets[OUT][PFR_OP_XPASS]);
	else if (obj == pfTblOutXPassBytes)
		agentx_varbind_counter64(vb, ts.pfrts_bytes[OUT][PFR_OP_XPASS]);
	else if (obj == pfTblStatsCleared) {
		tzero = (time(NULL) - ts.pfrts_tzero) * 100;
		agentx_varbind_timeticks(vb, tzero);
	} else if (obj == pfTblInMatchPkts)
		agentx_varbind_counter64(vb, ts.pfrts_packets[IN][PFR_OP_MATCH]);
	else if (obj == pfTblInMatchBytes)
		agentx_varbind_counter64(vb, ts.pfrts_bytes[IN][PFR_OP_MATCH]);
	else if (obj == pfTblOutMatchPkts)
		agentx_varbind_counter64(vb, ts.pfrts_packets[OUT][PFR_OP_MATCH]);
	else if (obj == pfTblOutMatchBytes)
		agentx_varbind_counter64(vb, ts.pfrts_bytes[OUT][PFR_OP_MATCH]);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_pftableaddrs(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	struct pfr_astats		 as;
	int				 tblidx;

	obj = agentx_varbind_get_object(vb);
	tblidx = agentx_varbind_get_index_integer(vb, pfTblAddrTblIdx);
	req = agentx_varbind_request(vb);

	/*
	 * XXX No consistent way to differentiate between not found and error
	 * Treat everything as not found.
	 */
	if (req == AGENTX_REQUEST_TYPE_GET ||
	    req == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE) {
		as.pfras_a.pfra_ip4addr = *agentx_varbind_get_index_ipaddress(
		    vb, pfTblAddrNetIdx);
		as.pfras_a.pfra_net = agentx_varbind_get_index_integer(vb,
		    pfTblAddrMaskIdx);

		if (pfta_get_addr(&as, tblidx)) {
			if (req == AGENTX_REQUEST_TYPE_GET) {
				agentx_varbind_notfound(vb);
				return;
			}
			req = AGENTX_REQUEST_TYPE_GETNEXT;
		}
	}
	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (tblidx < 1)
			tblidx = 1;
		as.pfras_a.pfra_ip4addr = *agentx_varbind_get_index_ipaddress(
		    vb, pfTblAddrNetIdx);
		as.pfras_a.pfra_net = agentx_varbind_get_index_integer(vb,
		    pfTblAddrMaskIdx);

		if (pfta_get_nextaddr(&as, &tblidx)){ 
			agentx_varbind_notfound(vb);
			return;
		}
	}
	agentx_varbind_set_index_integer(vb, pfTblAddrTblIdx, tblidx);
	agentx_varbind_set_index_ipaddress(vb, pfTblAddrNetIdx,
	    &as.pfras_a.pfra_ip4addr);
	agentx_varbind_set_index_integer(vb, pfTblAddrMaskIdx,
	    as.pfras_a.pfra_net);

	if (obj == pfTblAddrTblIndex)
		agentx_varbind_integer(vb, tblidx);
	else if (obj == pfTblAddrNet)
		agentx_varbind_ipaddress(vb, &as.pfras_a.pfra_ip4addr);
	else if (obj == pfTblAddrMask)
		agentx_varbind_integer(vb, as.pfras_a.pfra_net);
	else if (obj == pfTblAddrCleared)
		agentx_varbind_timeticks(vb, (time(NULL) - as.pfras_tzero) * 100);
	else if (obj == pfTblAddrInBlockPkts)
		agentx_varbind_counter64(vb, as.pfras_packets[IN][PFR_OP_BLOCK]);
	else if (obj == pfTblAddrInBlockBytes)
		agentx_varbind_counter64(vb, as.pfras_bytes[IN][PFR_OP_BLOCK]);
	else if (obj == pfTblAddrInPassPkts)
		agentx_varbind_counter64(vb, as.pfras_packets[IN][PFR_OP_PASS]);
	else if (obj == pfTblAddrInPassBytes)
		agentx_varbind_counter64(vb, as.pfras_bytes[IN][PFR_OP_PASS]);
	else if (obj == pfTblAddrOutBlockPkts)
		agentx_varbind_counter64(vb, as.pfras_packets[OUT][PFR_OP_BLOCK]);
	else if (obj == pfTblAddrOutBlockBytes)
		agentx_varbind_counter64(vb, as.pfras_bytes[OUT][PFR_OP_BLOCK]);
	else if (obj == pfTblAddrOutPassPkts)
		agentx_varbind_counter64(vb, as.pfras_packets[OUT][PFR_OP_PASS]);
	else if (obj == pfTblAddrOutPassBytes)
		agentx_varbind_counter64(vb, as.pfras_bytes[OUT][PFR_OP_PASS]);
	else if (obj == pfTblAddrInMatchPkts)
		agentx_varbind_counter64(vb, as.pfras_packets[IN][PFR_OP_MATCH]);
	else if (obj == pfTblAddrInMatchBytes)
		agentx_varbind_counter64(vb, as.pfras_bytes[IN][PFR_OP_MATCH]);
	else if (obj == pfTblAddrOutMatchPkts)
		agentx_varbind_counter64(vb, as.pfras_packets[OUT][PFR_OP_MATCH]);
	else if (obj == pfTblAddrOutMatchBytes)
		agentx_varbind_counter64(vb, as.pfras_bytes[OUT][PFR_OP_MATCH]);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_close_pftrans(struct agentx_varbind *vb, u_int32_t ticket)
{
	extern int		devpf;

	if (ioctl(devpf, DIOCXEND, &ticket) == -1) {
		log_warn("DIOCXEND");
		agentx_varbind_error(vb);
	}
}

void
mib_pflabelnum(struct agentx_varbind *vb)
{
	struct pfioc_rule	 pr;
	u_int32_t		 nr, mnr, lnr;
	extern int		 devpf;

	memset(&pr, 0, sizeof(pr));
	if (ioctl(devpf, DIOCGETRULES, &pr) == -1) {
		log_warn("DIOCGETRULES");
		agentx_varbind_error(vb);
		return;
	}

	mnr = pr.nr;
	lnr = 0;
	for (nr = 0; nr < mnr; ++nr) {
		pr.nr = nr;
		if (ioctl(devpf, DIOCGETRULE, &pr) == -1) {
			log_warn("DIOCGETRULE");
			agentx_varbind_error(vb);
			mib_close_pftrans(vb, pr.ticket);
			return;
		}

		if (pr.rule.label[0])
			lnr++;
	}

	agentx_varbind_integer(vb, lnr);

	mib_close_pftrans(vb, pr.ticket);
}

void
mib_pflabels(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	struct pfioc_rule		 pr;
	struct pf_rule			*r = NULL;
	u_int32_t			 nr, mnr, lnr;
	u_int32_t			 idx;
	extern int			 devpf;

	memset(&pr, 0, sizeof(pr));
	if (ioctl(devpf, DIOCGETRULES, &pr) == -1) {
		log_warn("DIOCGETRULES");
		agentx_varbind_error(vb);
		return;
	}

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, pfLabelIdx);
	req = agentx_varbind_request(vb);

	if (idx < 1) {
		if (req == AGENTX_REQUEST_TYPE_GET) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx = 1;
	} else if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}

	mnr = pr.nr;
	lnr = 0;
	for (nr = 0; nr < mnr; ++nr) {
		pr.nr = nr;
		if (ioctl(devpf, DIOCGETRULE, &pr) == -1) {
			log_warn("DIOCGETRULE");
			agentx_varbind_error(vb);
			mib_close_pftrans(vb, pr.ticket);
			return;
		}

		if (pr.rule.label[0] && ++lnr == idx) {
			r = &pr.rule;
			break;
		}
	}

	mib_close_pftrans(vb, pr.ticket);

	if (r == NULL) {
		agentx_varbind_notfound(vb);
		return;
	}
	agentx_varbind_set_index_integer(vb, pfLabelIdx, idx);

	if (obj == pfLabelIndex)
		agentx_varbind_integer(vb, lnr);
	else if (obj == pfLabelName)
		agentx_varbind_string(vb, r->label);
	else if (obj == pfLabelEvals)
		agentx_varbind_counter64(vb, r->evaluations);
	else if (obj == pfLabelPkts)
		agentx_varbind_counter64(vb, r->packets[IN] + r->packets[OUT]);
	else if (obj == pfLabelBytes)
		agentx_varbind_counter64(vb, r->bytes[IN] + r->bytes[OUT]);
	else if (obj == pfLabelInPkts)
		agentx_varbind_counter64(vb, r->packets[IN]);
	else if (obj == pfLabelInBytes)
		agentx_varbind_counter64(vb, r->bytes[IN]);
	else if (obj == pfLabelOutPkts)
		agentx_varbind_counter64(vb, r->packets[OUT]);
	else if (obj == pfLabelOutBytes)
		agentx_varbind_counter64(vb, r->bytes[OUT]);
	else if (obj == pfLabelTotalStates)
		agentx_varbind_counter32(vb, r->states_tot);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_pfsyncstats(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	int			 mib[] = { CTL_NET, PF_INET, IPPROTO_PFSYNC,
				    PFSYNCCTL_STATS };
	size_t			 len = sizeof(struct pfsyncstats);
	struct pfsyncstats	 s;

	if (sysctl(mib, 4, &s, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	obj = agentx_varbind_get_object(vb);
	if (obj == pfsyncIpPktsRecv)
		agentx_varbind_counter64(vb, s.pfsyncs_ipackets);
	else if (obj == pfsyncIp6PktsRecv)
		agentx_varbind_counter64(vb, s.pfsyncs_ipackets6);
	else if (obj == pfsyncPktDiscardsForBadInterface)
		agentx_varbind_counter64(vb, s.pfsyncs_badif);
	else if (obj == pfsyncPktDiscardsForBadTtl)
		agentx_varbind_counter64(vb, s.pfsyncs_badttl);
	else if (obj == pfsyncPktShorterThanHeader)
		agentx_varbind_counter64(vb, s.pfsyncs_hdrops);
	else if (obj == pfsyncPktDiscardsForBadVersion)
		agentx_varbind_counter64(vb, s.pfsyncs_badver);
	else if (obj == pfsyncPktDiscardsForBadAction)
		agentx_varbind_counter64(vb, s.pfsyncs_badact);
	else if (obj == pfsyncPktDiscardsForBadLength)
		agentx_varbind_counter64(vb, s.pfsyncs_badlen);
	else if (obj == pfsyncPktDiscardsForBadAuth)
		agentx_varbind_counter64(vb, s.pfsyncs_badauth);
	else if (obj == pfsyncPktDiscardsForStaleState)
		agentx_varbind_counter64(vb, s.pfsyncs_stale);
	else if (obj == pfsyncPktDiscardsForBadValues)
		agentx_varbind_counter64(vb, s.pfsyncs_badval);
	else if (obj == pfsyncPktDiscardsForBadState)
		agentx_varbind_counter64(vb, s.pfsyncs_badstate);
	else if (obj == pfsyncIpPktsSent)
		agentx_varbind_counter64(vb, s.pfsyncs_opackets);
	else if (obj == pfsyncIp6PktsSent)
		agentx_varbind_counter64(vb, s.pfsyncs_opackets6);
	else if (obj == pfsyncNoMemory)
		agentx_varbind_counter64(vb, s.pfsyncs_onomem);
	else if (obj == pfsyncOutputErrors)
		agentx_varbind_counter64(vb, s.pfsyncs_oerrors);
	else
		fatal("%s: Unexpected object", __func__);
}

/* OPENBSD-SENSORS-MIB */
void
mib_sensornum(struct agentx_varbind *vb)
{
	struct sensordev	 sensordev;
	size_t			 len = sizeof(sensordev);
	int			 mib[] = { CTL_HW, HW_SENSORS, 0 };
	int			 i, c;

	for (i = c = 0; ; i++) {
		mib[2] = i;
		if (sysctl(mib, nitems(mib),
		    &sensordev, &len, NULL, 0) == -1) {
			if (errno == ENXIO)
				continue;
			if (errno == ENOENT)
				break;
			log_warn("sysctl");
			agentx_varbind_error(vb);
			return;
		}
		c += sensordev.sensors_count;
	}

	agentx_varbind_integer(vb, c);
}

void
mib_sensors(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	struct sensordev		 sensordev;
	size_t				 len = sizeof(sensordev);
	struct sensor			 sensor;
	size_t				 slen = sizeof(sensor);
	char				 desc[32];
	int				 mib[] =
	    { CTL_HW, HW_SENSORS, 0, 0, 0 };
	int				 i, j, k;
	u_int32_t			 idx = 0, n;
	char				*s;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, sensorIdx);
	req = agentx_varbind_request(vb);

	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}
	if (idx < 1 &&
	    (req == AGENTX_REQUEST_TYPE_GETNEXT ||
	    req == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE))
		idx = 1;

	for (i = 0, n = 1; ; i++) {
		mib[2] = i;
		if (sysctl(mib, 3, &sensordev, &len, NULL, 0) == -1) {
			if (errno == ENXIO)
				continue;
			if (errno == ENOENT)
				break;
			log_warn("sysctl");
			agentx_varbind_error(vb);
			return;
		}
		for (j = 0; j < SENSOR_MAX_TYPES; j++) {
			mib[3] = j;
			for (k = 0; k < sensordev.maxnumt[j]; k++) {
				mib[4] = k;
				if (sysctl(mib, 5,
				    &sensor, &slen, NULL, 0) == -1) {
					if (errno == ENXIO)
						continue;
					if (errno == ENOENT)
						break;
					log_warn("sysctl");
					agentx_varbind_error(vb);
					return;
				}
				if (sensor.flags & SENSOR_FINVALID)
					continue;
				if (n == idx)
					goto found;
				n++;
			}
		}
	}
	agentx_varbind_notfound(vb);
	return;

 found:
	agentx_varbind_set_index_integer(vb, sensorIdx, idx);
	if (obj == sensorIndex)
		agentx_varbind_integer(vb, (int32_t)n);
	else if (obj == sensorDescr) {
		if (sensor.desc[0] == '\0') {
			snprintf(desc, sizeof(desc), "%s%d",
			    sensor_type_s[sensor.type],
			    sensor.numt);
			agentx_varbind_string(vb, desc);
		} else
			agentx_varbind_string(vb, sensor.desc);
	} else if (obj == sensorType)
		agentx_varbind_integer(vb, sensor.type);
	else if (obj == sensorDevice)
		agentx_varbind_string(vb, sensordev.xname);
	else if (obj == sensorValue) {
		if ((s = mib_sensorvalue(&sensor)) == NULL) {
			log_warn("asprintf");
			agentx_varbind_error(vb);
			return;
		}
		agentx_varbind_string(vb, s);
		free(s);
	} else if (obj == sensorUnits)
		agentx_varbind_string(vb, mib_sensorunit(&sensor));
	else if (obj == sensorStatus)
		agentx_varbind_integer(vb, sensor.status);
	else
		fatal("%s: Unexpected object", __func__);
}

#define SENSOR_DRIVE_STATES	(SENSOR_DRIVE_PFAIL + 1)
static const char * const sensor_drive_s[SENSOR_DRIVE_STATES] = {
	NULL, "empty", "ready", "powerup", "online", "idle", "active",
	"rebuild", "powerdown", "fail", "pfail"
};

static const char * const sensor_unit_s[SENSOR_MAX_TYPES + 1] = {
	"degC",	"RPM", "V DC", "V AC", "Ohm", "W", "A", "Wh", "Ah",
	"", "", "%", "lx", "", "sec", "%RH", "Hz", "degree", 
	"m", "Pa", "m/s^2", "m/s", ""
};

const char *
mib_sensorunit(struct sensor *s)
{
	u_int	 idx;
	idx = s->type > SENSOR_MAX_TYPES ?
	    SENSOR_MAX_TYPES : s->type;
	return (sensor_unit_s[idx]);
}

char *
mib_sensorvalue(struct sensor *s)
{
	char	*v;
	int	 ret = -1;

	switch (s->type) {
	case SENSOR_TEMP:
		ret = asprintf(&v, "%.2f",
		    (s->value - 273150000) / 1000000.0);
		break;
	case SENSOR_VOLTS_DC:
	case SENSOR_VOLTS_AC:
	case SENSOR_WATTS:
	case SENSOR_AMPS:
	case SENSOR_WATTHOUR:
	case SENSOR_AMPHOUR:
	case SENSOR_LUX:
	case SENSOR_FREQ:
	case SENSOR_ACCEL:
	case SENSOR_VELOCITY:
	case SENSOR_DISTANCE:
		ret = asprintf(&v, "%.2f", s->value / 1000000.0);
		break;
	case SENSOR_INDICATOR:
		ret = asprintf(&v, "%s", s->value ? "on" : "off");
		break;
	case SENSOR_PERCENT:
	case SENSOR_HUMIDITY:
		ret = asprintf(&v, "%.2f", s->value / 1000.0);
		break;
	case SENSOR_PRESSURE:
		ret = asprintf(&v, "%.2f", s->value / 1000.0);
		break;
	case SENSOR_TIMEDELTA:
		ret = asprintf(&v, "%.6f", s->value / 1000000000.0);
		break;
	case SENSOR_DRIVE:
		if (s->value > 0 && s->value < SENSOR_DRIVE_STATES) {
			ret = asprintf(&v, "%s", sensor_drive_s[s->value]);
			break;
		}
		/* FALLTHROUGH */
	case SENSOR_FANRPM:
	case SENSOR_OHMS:
	case SENSOR_INTEGER:
	default:
		ret = asprintf(&v, "%lld", s->value);
		break;
	}

	if (ret == -1)
		return (NULL);
	return (v);
}

void
mib_carpsysctl(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	size_t			 len;
	int			 mib[] = { CTL_NET, PF_INET, IPPROTO_CARP, 0 };
	int			 v;

	obj = agentx_varbind_get_object(vb);
	if (obj == carpAllow)
		mib[3] = CARPCTL_ALLOW;
	else if (obj == carpPreempt)
		mib[3] = CARPCTL_PREEMPT;
	else if (obj == carpLog)
		mib[3] = CARPCTL_LOG;
	else
		fatal("%s: Unexpected object", __func__);
	len = sizeof(v);

	if (sysctl(mib, 4, &v, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	agentx_varbind_integer(vb, v);
}

void
mib_carpstats(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	int			 mib[] = { CTL_NET, PF_INET, IPPROTO_CARP,
				    CARPCTL_STATS };
	size_t			 len;
	struct			 carpstats stats;

	len = sizeof(stats);

	if (sysctl(mib, 4, &stats, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	obj = agentx_varbind_get_object(vb);
	if (obj == carpIpPktsRecv)
		agentx_varbind_counter64(vb, stats.carps_ipackets);
	else if (obj == carpIp6PktsRecv)
		agentx_varbind_counter64(vb, stats.carps_ipackets6);
	else if (obj == carpPktDiscardsForBadInterface)
		agentx_varbind_counter64(vb, stats.carps_badif);
	else if (obj == carpPktDiscardsForWrongTtl)
		agentx_varbind_counter64(vb, stats.carps_badttl);
	else if (obj == carpPktShorterThanHeader)
		agentx_varbind_counter64(vb, stats.carps_hdrops);
	else if (obj == carpPktDiscardsForBadChecksum)
		agentx_varbind_counter64(vb, stats.carps_badsum);
	else if (obj == carpPktDiscardsForBadVersion)
		agentx_varbind_counter64(vb, stats.carps_badver);
	else if (obj == carpPktDiscardsForTooShort)
		agentx_varbind_counter64(vb, stats.carps_badlen);
	else if (obj == carpPktDiscardsForBadAuth)
		agentx_varbind_counter64(vb, stats.carps_badauth);
	else if (obj == carpPktDiscardsForBadVhid)
		agentx_varbind_counter64(vb, stats.carps_badvhid);
	else if (obj == carpPktDiscardsForBadAddressList)
		agentx_varbind_counter64(vb, stats.carps_badaddrs);
	else if (obj == carpIpPktsSent)
		agentx_varbind_counter64(vb, stats.carps_opackets);
	else if (obj == carpIp6PktsSent)
		agentx_varbind_counter64(vb, stats.carps_opackets6);
	else if (obj == carpNoMemory)
		agentx_varbind_counter64(vb, stats.carps_onomem);
	else if (obj == carpTransitionsToMaster)
		agentx_varbind_counter64(vb, stats.carps_preempt);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_carpifnum(struct agentx_varbind *vb)
{
	struct kif	*kif;
	int		 c = 0;

	for (kif = kr_getif(0); kif != NULL;
	    kif = kr_getnextif(kif->if_index))
		if (kif->if_type == IFT_CARP)
			c++;

	agentx_varbind_integer(vb, c);
}

struct carpif *
mib_carpifget(u_int idx)
{
	struct kif	*kif;
	struct carpif	*cif;
	int		 s;
	struct ifreq	 ifr;
	struct carpreq	 carpr;

	if ((kif = kr_getif(idx)) == NULL || kif->if_type != IFT_CARP) {
		/*
		 * It may happen that an interface with a specific index
		 * does not exist, has been removed, or is not a carp(4)
		 * interface. Jump to the next available carp(4) interface
		 * index.
		 */
		for (kif = kr_getif(0); kif != NULL;
		    kif = kr_getnextif(kif->if_index)) {
			if (kif->if_type != IFT_CARP)
				continue;
			if (kif->if_index > idx)
				break;

		}
		if (kif == NULL)
			return (NULL);
	}
	idx = kif->if_index;

	/* Update interface information */
	kr_updateif(idx);
	if ((kif = kr_getif(idx)) == NULL) {
		log_debug("mib_carpifget: interface %d disappeared?", idx);
		return (NULL);
	}

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		return (NULL);

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, kif->if_name, sizeof(ifr.ifr_name));
	memset((char *)&carpr, 0, sizeof(carpr));
	ifr.ifr_data = (caddr_t)&carpr;

	if (ioctl(s, SIOCGVH, (caddr_t)&ifr) == -1) {
		close(s);
		return (NULL);
	}

	cif = calloc(1, sizeof(struct carpif));
	if (cif != NULL) {
		memcpy(&cif->carpr, &carpr, sizeof(struct carpreq));
		memcpy(&cif->kif, kif, sizeof(struct kif));
	}

	close(s);

	return (cif);
}

void
mib_carpiftable(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	u_int32_t			 idx;
	struct carpif			*cif;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, carpIfIdx);
	req = agentx_varbind_request(vb);

	if (idx < 1) {
		if (req == AGENTX_REQUEST_TYPE_GET) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx = 1;
	} else if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}

	/*
	 * XXX No consistent way to differentiate between not found and error
	 * Treat everything as not found.
	 */
	if ((cif = mib_carpifget(idx)) == NULL) {
		agentx_varbind_notfound(vb);
		return;
	}

	if (req == AGENTX_REQUEST_TYPE_GET && cif->kif.if_index != idx) {
		agentx_varbind_notfound(vb);
		return;
	}
	agentx_varbind_set_index_integer(vb, carpIfIdx, cif->kif.if_index);

	if (obj == carpIfIndex)
		agentx_varbind_integer(vb, cif->kif.if_index);
	else if (obj == carpIfDescr)
		agentx_varbind_string(vb, cif->kif.if_name);
	else if (obj == carpIfVhid)
		agentx_varbind_integer(vb, cif->carpr.carpr_vhids[0]);
	else if (obj == carpIfDev)
		agentx_varbind_string(vb, cif->carpr.carpr_carpdev);
	else if (obj == carpIfAdvbase)
		agentx_varbind_integer(vb, cif->carpr.carpr_advbase);
	else if (obj == carpIfAdvskew)
		agentx_varbind_integer(vb, cif->carpr.carpr_advskews[0]);
	else if (obj == carpIfState)
		agentx_varbind_integer(vb, cif->carpr.carpr_states[0]);
	else
		fatal("%s: Unexpected object", __func__);
	free(cif);
}

static struct ifg_req *
mib_carpgroupget(u_int idx)
{
	struct ifgroupreq	 ifgr;
	struct ifg_req		*ifg = NULL;
	u_int			 len;
	int			 s = -1;

	bzero(&ifgr, sizeof(ifgr));

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		log_warn("socket");
		return (NULL);
	}

	if (ioctl(s, SIOCGIFGLIST, (caddr_t)&ifgr) == -1) {
		log_warn("SIOCGIFGLIST");
		goto err;
	}
	len = ifgr.ifgr_len;

	if (len / sizeof(*ifgr.ifgr_groups) <= idx-1)
		goto err;

	if ((ifgr.ifgr_groups = calloc(1, len)) == NULL) {
		log_warn("alloc");
		goto err;
	}
	if (ioctl(s, SIOCGIFGLIST, (caddr_t)&ifgr) == -1) {
		log_warn("SIOCGIFGLIST");
		goto err;
	}
	close(s);

	if ((ifg = calloc(1, sizeof *ifg)) == NULL) {
		log_warn("alloc");
		goto err;
	}

	memcpy(ifg, &ifgr.ifgr_groups[idx-1], sizeof *ifg);
	free(ifgr.ifgr_groups);
	return ifg;
 err:
	free(ifgr.ifgr_groups);
	close(s);
	return (NULL);
}

void
mib_carpgrouptable(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	struct ifgroupreq		 ifgr;
	struct ifg_req			*ifg;
	uint32_t			 idx;
	int				 s;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, carpGroupIdx);
	req = agentx_varbind_request(vb);

	if (idx < 1) {
		if (req == AGENTX_REQUEST_TYPE_GET) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx = 1;
	} else if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}

	/*
	 * XXX No consistent way to differentiate between not found and error
	 * Treat everything as not found.
	 */
	if ((ifg = mib_carpgroupget(idx)) == NULL) {
		agentx_varbind_notfound(vb);
		return;
	}
	agentx_varbind_set_index_integer(vb, carpGroupIdx, idx);

	if (obj == carpGroupName)
		agentx_varbind_string(vb, ifg->ifgrq_group);
	else if (obj == carpGroupDemote) {
		if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
			log_warn("socket");
			free(ifg);
			agentx_varbind_error(vb);
			return;
		}

		bzero(&ifgr, sizeof(ifgr));
		strlcpy(ifgr.ifgr_name, ifg->ifgrq_group, sizeof(ifgr.ifgr_name));
		if (ioctl(s, SIOCGIFGATTR, (caddr_t)&ifgr) == -1) {
			log_warn("SIOCGIFGATTR");
			close(s);
			free(ifg);
			agentx_varbind_error(vb);
			return;
		}

		close(s);
		agentx_varbind_integer(vb, ifgr.ifgr_attrib.ifg_carp_demoted);
	} else
		fatal("%s: Unexpected object", __func__);

	free(ifg);
}

void
mib_memversion(struct agentx_varbind *vb)
{
	agentx_varbind_integer(vb, 1);
}

void
mib_memiftable(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	u_int32_t			 idx = 0;
	struct kif			*kif;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, ifIdx);
	req = agentx_varbind_request(vb);
	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}
	if ((kif = mib_ifget(idx)) == NULL) {
		agentx_varbind_notfound(vb);
		return;
	}
	if (req == AGENTX_REQUEST_TYPE_GET) {
		if (idx != kif->if_index) {
			agentx_varbind_notfound(vb);
			return;
		}
	}
	agentx_varbind_set_index_integer(vb, ifIdx, kif->if_index);

	if (obj == memIfName)
		agentx_varbind_string(vb, kif->if_name);
	else if (obj == memIfLiveLocks)
		agentx_varbind_counter64(vb, 0);
	else
		fatal("%s: Unexpected object", __func__);
}

/*
 * Defined in IP-MIB.txt
 */
int mib_getipstat(struct ipstat *);
void mib_ipstat(struct agentx_varbind *);
void mib_ipforwarding(struct agentx_varbind *);
void mib_ipdefaultttl(struct agentx_varbind *);
void mib_ipinhdrerrs(struct agentx_varbind *);
void mib_ipinaddrerrs(struct agentx_varbind *);
void mib_ipforwdgrams(struct agentx_varbind *);
void mib_ipreasmtimeout(struct agentx_varbind *);
void mib_ipreasmfails(struct agentx_varbind *);
void mib_ipfragfails(struct agentx_varbind *);
void mib_ipaddr(struct agentx_varbind *);
void mib_physaddr(struct agentx_varbind *);

void
mib_ipforwarding(struct agentx_varbind *vb)
{
	int	mib[] = { CTL_NET, PF_INET, IPPROTO_IP, IPCTL_FORWARDING };
	int	v;
	size_t	len = sizeof(v);

	if (sysctl(mib, nitems(mib), &v, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	/* ipForwarding: forwarding(1), notForwarding(2) */
	agentx_varbind_integer(vb, (v == 0) ? 2 : 1);
}

void
mib_ipdefaultttl(struct agentx_varbind *vb)
{
	int	mib[] = { CTL_NET, PF_INET, IPPROTO_IP, IPCTL_DEFTTL };
	int	v;
	size_t	len = sizeof(v);

	if (sysctl(mib, nitems(mib), &v, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	agentx_varbind_integer(vb, v);
}

int
mib_getipstat(struct ipstat *ipstat)
{
	int	 mib[] = { CTL_NET, PF_INET, IPPROTO_IP, IPCTL_STATS };
	size_t	 len = sizeof(*ipstat);

	return (sysctl(mib, nitems(mib), ipstat, &len, NULL, 0));
}

void
mib_ipstat(struct agentx_varbind *vb)
{
	struct agentx_object	*obj;
	struct ipstat		 ipstat;

	if (mib_getipstat(&ipstat) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	obj = agentx_varbind_get_object(vb);
	if (obj == ipInReceives)
		agentx_varbind_counter32(vb, ipstat.ips_total);
	else if (obj == ipInUnknownProtos)
		agentx_varbind_counter32(vb, ipstat.ips_noproto);
	else if (obj == ipInDelivers)
		agentx_varbind_counter32(vb, ipstat.ips_delivered);
	else if (obj == ipOutRequests)
		agentx_varbind_counter32(vb, ipstat.ips_localout);
	else if (obj == ipOutDiscards)
		agentx_varbind_counter32(vb, ipstat.ips_odropped);
	else if (obj == ipOutNoRoutes)
		agentx_varbind_counter32(vb, ipstat.ips_noroute);
	else if (obj == ipReasmReqds)
		agentx_varbind_counter32(vb, ipstat.ips_fragments);
	else if (obj == ipReasmOKs)
		agentx_varbind_counter32(vb, ipstat.ips_reassembled);
	else if (obj == ipFragOKs)
		agentx_varbind_counter32(vb, ipstat.ips_fragmented);
	else if (obj == ipFragCreates)
		agentx_varbind_counter32(vb, ipstat.ips_ofragments);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_ipinhdrerrs(struct agentx_varbind *vb)
{
	u_int32_t	errors;
	struct ipstat	ipstat;

	if (mib_getipstat(&ipstat) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	errors = ipstat.ips_badsum + ipstat.ips_badvers +
	    ipstat.ips_tooshort + ipstat.ips_toosmall +
	    ipstat.ips_badhlen +  ipstat.ips_badlen +
	    ipstat.ips_badoptions + ipstat.ips_toolong +
	    ipstat.ips_badaddr;

	agentx_varbind_counter32(vb, errors);
}

void
mib_ipinaddrerrs(struct agentx_varbind *vb)
{
	u_int32_t	errors;
	struct ipstat	ipstat;

	if (mib_getipstat(&ipstat) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	errors = ipstat.ips_cantforward + ipstat.ips_badaddr;

	agentx_varbind_counter32(vb, errors);
}

void
mib_ipforwdgrams(struct agentx_varbind *vb)
{
	u_int32_t	counter;
	struct ipstat	ipstat;

	if (mib_getipstat(&ipstat) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	counter = ipstat.ips_forward + ipstat.ips_redirectsent;

	agentx_varbind_counter32(vb, counter);
}

void
mib_ipreasmtimeout(struct agentx_varbind *vb)
{
	agentx_varbind_integer(vb, IPFRAGTTL);
}

void
mib_ipreasmfails(struct agentx_varbind *vb)
{
	u_int32_t	counter;
	struct ipstat	ipstat;

	if (mib_getipstat(&ipstat) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	counter = ipstat.ips_fragdropped + ipstat.ips_fragtimeout;

	agentx_varbind_counter32(vb, counter);
}

void
mib_ipfragfails(struct agentx_varbind *vb)
{
	u_int32_t	counter;
	struct ipstat	ipstat;

	if (mib_getipstat(&ipstat) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	counter = ipstat.ips_badfrags + ipstat.ips_cantfrag;
	agentx_varbind_counter32(vb, counter);
}

void
mib_ipaddr(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	struct sockaddr_in		 addr;
	struct kif_addr			*ka;

	obj = agentx_varbind_get_object(vb);
	req = agentx_varbind_request(vb);
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_len = sizeof(addr);
	addr.sin_addr = *agentx_varbind_get_index_ipaddress(vb, ipAdEntAddrIdx);

	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (addr.sin_addr.s_addr == UINT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		addr.sin_addr.s_addr = htonl(ntohl(addr.sin_addr.s_addr) + 1);
	}
	/*
	 * XXX No consistent way to differentiate between not found and error
	 * Treat everything as not found.
	 */
	ka = kr_getnextaddr((struct sockaddr *)&addr);
	if (ka == NULL || ka->addr.sa.sa_family != AF_INET) {
		agentx_varbind_notfound(vb);
		return;
	}
	if (req == AGENTX_REQUEST_TYPE_GET) {
		if (addr.sin_addr.s_addr !=
		    ((struct sockaddr_in *)&ka->addr.sa)->sin_addr.s_addr) {
			agentx_varbind_notfound(vb);
			return;
		}
	}
	agentx_varbind_set_index_ipaddress(vb, ipAdEntAddrIdx,
	    &((struct sockaddr_in *)&ka->addr.sa)->sin_addr);

	if (obj == ipAdEntAddr)
		agentx_varbind_ipaddress(vb,
		    &((struct sockaddr_in *)&ka->addr.sa)->sin_addr);
	else if (obj == ipAdEntIfIndex)
		agentx_varbind_integer(vb, ka->if_index);
	else if (obj == ipAdEntNetMask)
		agentx_varbind_ipaddress(vb, &ka->mask.sin.sin_addr);
	else if (obj == ipAdEntBcastAddr)
		agentx_varbind_integer(vb, ka->dstbrd.sa.sa_len ? 1 : 0);
	else if (obj == ipAdEntReasmMaxSize)
		agentx_varbind_integer(vb, IP_MAXPACKET);
	else
		fatal("%s: Unexpected object", __func__);
}

void
mib_physaddr(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	struct sockaddr_in		 addr;
	struct kif			*kif;
	struct kif_arp			*ka;
	u_int32_t			 idx = 0;

	obj = agentx_varbind_get_object(vb);
	idx = agentx_varbind_get_index_integer(vb, ipNetToMediaIfIdx);
	req = agentx_varbind_request(vb);

	/* Get the IP address */
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_len = sizeof(addr);
	addr.sin_addr = *agentx_varbind_get_index_ipaddress(vb,
	    ipNetToMediaNetAddressIdx);

	if (req == AGENTX_REQUEST_TYPE_GET ||
	    req == AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE) {
		if ((ka = karp_getaddr((struct sockaddr *)&addr, idx, 0)) == NULL) {
			if (req == AGENTX_REQUEST_TYPE_GET) {
				agentx_varbind_notfound(vb);
				return;
			}
			req = AGENTX_REQUEST_TYPE_GETNEXT;
		} else {
			if (req == AGENTX_REQUEST_TYPE_GET &&
			    (idx != ka->if_index ||
			    addr.sin_addr.s_addr !=
			    ka->addr.sin.sin_addr.s_addr)) {
				agentx_varbind_notfound(vb);
				return;
			}
		}
	}
	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if ((kif = kr_getif(idx)) == NULL) {
			/* No configured interfaces */
			if (idx == 0) {
				agentx_varbind_notfound(vb);
				return;
			}
			/*
			 * It may happen that an interface with a specific index
			 * does not exist or has been removed.  Jump to the next
			 * available interface.
			 */
			kif = kr_getif(0);
 nextif:
			for (; kif != NULL; kif = kr_getnextif(kif->if_index))
				if (kif->if_index > idx &&
				    (ka = karp_first(kif->if_index)) != NULL)
					break;
			if (kif == NULL) {
				/* No more interfaces with addresses on them */
				agentx_varbind_notfound(vb);
				return;
			}
		} else {
			if (idx == 0 || addr.sin_addr.s_addr == 0)
				ka = karp_first(kif->if_index);
			else {
				/* XXX This only works on a walk. */
				ka = karp_getaddr((struct sockaddr *)&addr, idx, 1);
			}
			if (ka == NULL) {
				/* Try next interface */
				goto nextif;
			}
		}
	}
	agentx_varbind_set_index_integer(vb, ipNetToMediaIfIdx, ka->if_index);
	agentx_varbind_set_index_ipaddress(vb, ipNetToMediaNetAddressIdx,
	    &ka->addr.sin.sin_addr);

	if (obj == ipNetToMediaIfIndex)
		agentx_varbind_integer(vb, ka->if_index);
	else if (obj == ipNetToMediaPhysAddress) {
		if (bcmp(LLADDR(&ka->target.sdl), ether_zeroaddr,
		    sizeof(ether_zeroaddr)) == 0)
			agentx_varbind_nstring(vb, ether_zeroaddr,
			    sizeof(ether_zeroaddr));
		else
			agentx_varbind_nstring(vb, LLADDR(&ka->target.sdl),
			    ka->target.sdl.sdl_alen);
	} else if (obj == ipNetToMediaNetAddress)
		agentx_varbind_ipaddress(vb, &ka->addr.sin.sin_addr);
	else if (obj == ipNetToMediaType) {
		if (ka->flags & F_STATIC)
			agentx_varbind_integer(vb, 4); /* static */
		else
			agentx_varbind_integer(vb, 3); /* dynamic */
	} else
		fatal("%s: Unexpected object", __func__);
}

/*
 * Defined in IP-FORWARD-MIB.txt (rfc4292)
 */

void mib_ipfnroutes(struct agentx_varbind *);
//struct ber_oid *
//mib_ipfroutetable(struct oid *oid, struct ber_oid *o, struct ber_oid *no);
void mib_ipfroute(struct agentx_varbind *);

void
mib_ipfnroutes(struct agentx_varbind *vb)
{
	agentx_varbind_gauge32(vb, kr_routenumber());
}

#define INETADDRESSTYPE_IPV4	1
void
mib_ipfroute(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	struct kroute			*kr;
	const in_addr_t			*addr, *nhaddr;
	const uint32_t			*policy;
	size_t				 alen, plen, nlen;
	int				 af;
	int				 implied;
	u_int8_t			 prefixlen, prio, type, proto;


	obj = agentx_varbind_get_object(vb);
	req = agentx_varbind_request(vb);
	af = agentx_varbind_get_index_integer(vb, inetCidrRouteDestTypeIdx);
	addr = (const in_addr_t *)agentx_varbind_get_index_string(vb,
	    inetCidrRouteDestIdx, &alen, &implied);
	prefixlen = agentx_varbind_get_index_integer(vb,
	    inetCidrRoutePfxLenIdx);
	policy = agentx_varbind_get_index_oid(vb, inetCidrRoutePolicyIdx,
	    &plen, &implied);
	nhaddr = ((const in_addr_t *)agentx_varbind_get_index_string(vb,
	    inetCidrRouteNextHopIdx, &nlen, &implied));

	if (plen >= 2)
		prio = policy[1];
	/* Initial 2 sub-identifiers should always be the same for us */
	if (af < INETADDRESSTYPE_IPV4 ||
	    (af == INETADDRESSTYPE_IPV4 && alen < 4)) {
		if (req == AGENTX_REQUEST_TYPE_GET) {
			agentx_varbind_notfound(vb);
			return;
		}
		kr = kroute_first();
	} else if (af > INETADDRESSTYPE_IPV4 ||
		   (af == INETADDRESSTYPE_IPV4 && alen > 4)) {
		agentx_varbind_notfound(vb);
		return;
	} else {
		/* XXX This only works when requesting known values. */
		kr = kroute_getaddr(*addr, prefixlen, prio,
		    req == AGENTX_REQUEST_TYPE_GETNEXT);
		if (kr == NULL) {
			agentx_varbind_notfound(vb);
			return;
		}
		if (req == AGENTX_REQUEST_TYPE_GETNEXT)
			goto done;
		if (nlen < 4) {
			if (req == AGENTX_REQUEST_TYPE_GET) {
				agentx_varbind_notfound(vb);
				return;
			}
		} else if (nlen > 4) {
			kr = kroute_getaddr(*addr, prefixlen, prio, 1);
			if (req == AGENTX_REQUEST_TYPE_GET || kr == NULL) {
				agentx_varbind_notfound(vb);
				return;
			}
		} else {
			if (ntohl(kr->nexthop.s_addr) < ntohl(*nhaddr)) {
				if (req == AGENTX_REQUEST_TYPE_GET) {
					agentx_varbind_notfound(vb);
					return;
				}
			} else if (ntohl(kr->nexthop.s_addr) > ntohl(*nhaddr)) {
				kr = kroute_getaddr(*addr, prefixlen, prio, 1);
				if (req == AGENTX_REQUEST_TYPE_GET ||
				    kr == NULL) {
					agentx_varbind_notfound(vb);
					return;
				}
			}

		}
	}
 done:
	agentx_varbind_set_index_integer(vb, inetCidrRouteDestTypeIdx,
	    INETADDRESSTYPE_IPV4);
	agentx_varbind_set_index_nstring(vb, inetCidrRouteDestIdx,
	    (unsigned char *)&kr->prefix.s_addr, 4);
	agentx_varbind_set_index_integer(vb, inetCidrRoutePfxLenIdx,
	    kr->prefixlen);
	agentx_varbind_set_index_oid(vb, inetCidrRoutePolicyIdx,
	    AGENTX_OID(0, kr->priority));
	agentx_varbind_set_index_integer(vb, inetCidrRouteNextHopTypeIdx,
	    INETADDRESSTYPE_IPV4);
	agentx_varbind_set_index_nstring(vb, inetCidrRouteNextHopIdx,
	    (unsigned char *)&kr->nexthop.s_addr, 4);

	if (obj == inetCidrRouteIfIndex)
		agentx_varbind_integer(vb, kr->if_index);
	else if (obj == inetCidrRouteType) {
		if (kr->flags & F_REJECT)
			type = 2;
		else if (kr->flags & F_BLACKHOLE)
			type = 5;
		else if (kr->flags & F_CONNECTED)
			type = 3;
		else
			type = 4;
		agentx_varbind_integer(vb, type);
	} else if (obj == inetCidrRouteProto) {
		switch (kr->priority) {
		case RTP_CONNECTED:
			proto = 2;
			break;
		case RTP_STATIC:
			proto = 3;
			break;
		case RTP_OSPF:
			proto = 13;
			break;
		case RTP_ISIS:
			proto = 9;
			break;
		case RTP_RIP:
			proto = 8;
			break;
		case RTP_BGP:
			proto = 14;
			break;
		default:
			if (kr->flags & F_DYNAMIC)
				proto = 4;
			else
				proto = 1; /* not specified */
			break;
		}
		agentx_varbind_integer(vb, proto);
	} else if (obj == inetCidrRouteAge)
		agentx_varbind_gauge32(vb, 0);
	else if (obj == inetCidrRouteNextHopAS)
		agentx_varbind_unsigned32(vb, 0); /* unknown */
	else if (obj == inetCidrRouteMetric1)
		agentx_varbind_integer(vb, -1);
	else if (obj == inetCidrRouteMetric2)
		agentx_varbind_integer(vb, -1);
	else if (obj == inetCidrRouteMetric3)
		agentx_varbind_integer(vb, -1);
	else if (obj == inetCidrRouteMetric4)
		agentx_varbind_integer(vb, -1);
	else if (obj == inetCidrRouteMetric5)
		agentx_varbind_integer(vb, -1);
	else if (obj == inetCidrRouteStatus)
		agentx_varbind_integer(vb, 1);	/* XXX */
	else
		fatal("%s: Unexpected object", __func__);
}

/*
 * Defined in UCD-DISKIO-MIB.txt.
 */

void	mib_diskio(struct agentx_varbind *vb);

void
mib_diskio(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	u_int32_t			 idx;
	int				 mib[] = { CTL_HW, 0 };
	unsigned int			 diskcount;
	struct diskstats		*stats;
	size_t				 len;

	len = sizeof(diskcount);
	mib[1] = HW_DISKCOUNT;
	if (sysctl(mib, nitems(mib), &diskcount, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		agentx_varbind_error(vb);
		return;
	}

	obj = agentx_varbind_get_object(vb);
	req = agentx_varbind_request(vb);
	idx = agentx_varbind_get_index_integer(vb, diskIOIdx);

	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}
	if(idx < 1) {
		if (req == AGENTX_REQUEST_TYPE_GET) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx = 1;
	} else if (idx > diskcount) {
		agentx_varbind_notfound(vb);
		return;
	}
	agentx_varbind_set_index_integer(vb, diskIOIdx, idx);

	stats = calloc(diskcount, sizeof(*stats));
	if (stats == NULL) {
		log_warn("malloc");
		agentx_varbind_error(vb);
		return;
	}
	/* We know len won't overflow, otherwise calloc() would have failed. */
	len = diskcount * sizeof(*stats);
	mib[1] = HW_DISKSTATS;
	if (sysctl(mib, nitems(mib), stats, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		free(stats);
		agentx_varbind_error(vb);
		return;
	}

	if (obj == diskIOIndex)
		agentx_varbind_integer(vb, idx);
	else if (obj == diskIODevice)
		agentx_varbind_string(vb, stats[idx - 1].ds_name);
	else if (obj == diskIONRead)
		agentx_varbind_counter32(vb, 
		    (u_int32_t)stats[idx - 1].ds_rbytes);
	else if (obj == diskIONWritten)
		agentx_varbind_counter32(vb, 
		    (u_int32_t)stats[idx - 1].ds_wbytes);
	else if (obj == diskIOReads)
		agentx_varbind_counter32(vb, 
		    (u_int32_t)stats[idx - 1].ds_rxfer);
	else if (obj == diskIOWrites)
		agentx_varbind_counter32(vb, 
		    (u_int32_t)stats[idx - 1].ds_wxfer);
	else if (obj == diskIONReadX)
		agentx_varbind_counter64(vb, stats[idx - 1].ds_rbytes);
	else if (obj == diskIONWrittenX)
		agentx_varbind_counter64(vb, stats[idx - 1].ds_wbytes);
	else
		fatal("%s: Unexpected object", __func__);
	free(stats);
}

/*
 * Defined in BRIDGE-MIB.txt (rfc1493)
 *
 * This MIB is required by some NMS to accept the device because
 * the RFC says that mostly any network device has to provide this MIB... :(
 */

void	 mib_dot1basetype(struct agentx_varbind *);
void	 mib_dot1dtable(struct agentx_varbind *);

void
mib_dot1basetype(struct agentx_varbind *vb)
{
	/* srt (sourceroute + transparent) */
	agentx_varbind_integer(vb, 4);
}

void
mib_dot1dtable(struct agentx_varbind *vb)
{
	struct agentx_object		*obj;
	enum agentx_request_type	 req;
	u_int32_t			 idx = 0;
	struct kif			*kif;

	obj = agentx_varbind_get_object(vb);
	req = agentx_varbind_request(vb);
	idx = agentx_varbind_get_index_integer(vb, dot1dBasePortIdx);

	if (req == AGENTX_REQUEST_TYPE_GETNEXT) {
		if (idx == INT32_MAX) {
			agentx_varbind_notfound(vb);
			return;
		}
		idx++;
	}
	if ((kif = mib_ifget(idx)) == NULL) {
		agentx_varbind_notfound(vb);
		return;
	}
	if (req == AGENTX_REQUEST_TYPE_GET) {
		if (idx != kif->if_index) {
			agentx_varbind_notfound(vb);
			return;
		}
	}
	agentx_varbind_set_index_integer(vb, dot1dBasePortIdx, kif->if_index);

	if (obj == dot1dBasePort)
		agentx_varbind_integer(vb, kif->if_index);
	else if (obj == dot1dBasePortIfIndex)
		agentx_varbind_integer(vb, kif->if_index);
	else if (obj == dot1dBasePortCircuit)
		agentx_varbind_oid(vb, AGENTX_OID(0, 0));
	else if (obj == dot1dBasePortDelayExceededDiscards)
		agentx_varbind_counter32(vb, 0);
	else if (obj == dot1dBasePortMtuExceededDiscards)
		agentx_varbind_counter32(vb, 0);
	else
		fatal("%s: Unexpected object", __func__);
}

/*
 * Import all MIBs
 */

int
main(int argc, char *argv[])
{
	static struct snmpd conf;
	struct agentx *sa;
	struct agentx_session *sas;
	struct agentx_index *indices[6];
	struct passwd *pw;
	struct group *gr;
	char agentxsocketdir[PATH_MAX];
	int ch;
	int verbose = 0, daemonize = 1, debug = 0;
	char *context = NULL;
	const char *errstr;
	/* HOST-RESOURCES-MIB */
	struct agentx_region *host;
	struct agentx_object *hrSystemUptime, *hrSystemDate, *hrMemorySize;
	/* IF-MIB */
	struct agentx_region *ifMIB, *interfaces;
	/* OPENBSD-PF-MIB */
	struct agentx_region *pfMIBObjects;
	/* OPENBSD-SENSOR-MIB */
	struct agentx_region *sensorsMIBObjects;
	/* OPENBSD-CARP-MIB */
	struct agentx_region *carpMIBObjects;
	/* OPENBSD-MEM-MIB */
	struct agentx_region *memMIBObjects;
	/* IP-MIB */
	struct agentx_region *ip;
	/* IP-FORWARD-MIB */
	struct agentx_region *ipForward;
	/* UCD-DISKIO-MIB */
	struct agentx_region *ucdDiskIOMIB;
	/* BRIDGE-MIB */
	struct agentx_region *dot1dBridge;

	snmpd_env = &conf;
	log_init(2, LOG_DAEMON);

	agentx_log_fatal = fatalx;
	agentx_log_warn = log_warnx;
	agentx_log_info = log_info;
	agentx_log_debug = log_debug;

	while ((ch = getopt(argc, argv, "C:c:ds:vx:")) != -1) {
		switch (ch) {
		case 'C':
			if (strcmp(optarg, "filter-routes") == 0) {
				conf.sc_rtfilter = ROUTE_FILTER(RTM_NEWADDR) |
				    ROUTE_FILTER(RTM_DELADDR) |
				    ROUTE_FILTER(RTM_IFINFO) |
				    ROUTE_FILTER(RTM_IFANNOUNCE);

			}
			break;
		case 'c':
			context = optarg;
			break;
		case 'd':
			daemonize = 0;
			debug = 1;
			break;
		case 's':
			if (optarg[0] != '/')
				fatalx("agentx socket path must be absolute");
			agentxsocket = optarg;
			break;
		case 'v':
			verbose++;
			break;
		case 'x':
			/* Undocumented flag for snmpd(8) spawning */
			agentxfd = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				fatalx("invalid agentx fd: %s", errstr);
			daemonize = 0;
			break;
		default:
			fatalx("usage: snmpd_metrics [-dv] [-C option] "
			    "[-c context] [-s master]\n");
		}
	}

	if (agentxfd != -1 && !debug)
		/* Initialize syslog logging asap for snmpd */
		log_init(0, LOG_DAEMON);

	if ((pw = getpwnam("_snmpd")) == NULL)
		fatal("can't find _snmpd user");
	if ((gr = getgrnam("_agentx")) == NULL)
		fatal("can't find _agentx group");

	if (agentxfd != -1 && agentxsocket != NULL)
		fatalx("-s and -x are mutually exclusive");
	if (agentxfd == -1 && agentxsocket == NULL)
		agentxsocket = AGENTX_MASTER_PATH;

	event_init();

	if ((sa = agentx(snmp_connect, NULL)) == NULL)
		fatal("agentx");
	if ((sas = agentx_session(sa, NULL, 0, "OpenSNMPd metrics", 0)) == NULL)
		fatal("agentx_session");
	if ((sac = agentx_context(sas, context)) == NULL)
		fatal("agentx_context");

	/* kr_init requires sac */
	kr_init();
	pf_init();
	timer_init();
	pageshift_init();

	if (agentxsocket != NULL) {
		if (strlcpy(agentxsocketdir, agentxsocket,
		    sizeof(agentxsocketdir)) >= sizeof(agentxsocketdir)) {
			errno = ENAMETOOLONG;
			fatal("-s");
		}
		if (unveil(dirname(agentxsocketdir), "r") == -1)
			fatal("unveil");
	}

	/* Can't pledge: kvm_getfiles */
	if (unveil(NULL, NULL) == -1)
		fatal("unveil");

	if (setgid(gr->gr_gid) == -1)
		fatal("setgid");
	if (setuid(pw->pw_uid) == -1)
		fatal("setuid");

	/* HOST-RESOURCES-MIB */
	if ((host = agentx_region(sac, AGENTX_OID(HOST), 0)) == NULL)
		fatal("agentx_region");

	if ((hrSystemUptime = agentx_object(host, AGENTX_OID(HRSYSTEMUPTIME),
	    NULL, 0, 0, mib_hrsystemuptime)) == NULL ||
	    (hrSystemDate = agentx_object(host, AGENTX_OID(HRSYSTEMDATE),
	    NULL, 0, 0, mib_hrsystemdate)) == NULL ||
	    (hrSystemProcesses = agentx_object(host,
	    AGENTX_OID(HRSYSTEMPROCESSES), NULL, 0, 0,
	    mib_hrsystemprocs)) == NULL ||
	    (hrSystemMaxProcesses = agentx_object(host,
	    AGENTX_OID(HRSYSTEMMAXPROCESSES), NULL, 0, 0,
	    mib_hrsystemprocs)) == NULL ||
	    (hrMemorySize = agentx_object(host, AGENTX_OID(HRMEMORYSIZE),
	    NULL, 0, 0, mib_hrmemory)) == NULL)
		fatal("agentx_object");

	if ((hrStorageIdx = agentx_index_integer_dynamic(host,
	    AGENTX_OID(HRSTORAGEINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((hrStorageIndex = agentx_object(host, AGENTX_OID(HRSTORAGEINDEX),
	    &hrStorageIdx, 1, 0, mib_hrstorage)) == NULL ||
	    (hrStorageType = agentx_object(host, AGENTX_OID(HRSTORAGETYPE),
	    &hrStorageIdx, 1, 0, mib_hrstorage)) == NULL ||
	    (hrStorageDescr = agentx_object(host, AGENTX_OID(HRSTORAGEDESCR),
	    &hrStorageIdx, 1, 0, mib_hrstorage)) == NULL ||
	    (hrStorageAllocationUnits = agentx_object(host,
	    AGENTX_OID(HRSTORAGEALLOCATIONUNITS), &hrStorageIdx, 1, 0,
	    mib_hrstorage)) == NULL ||
	    (hrStorageSize = agentx_object(host, AGENTX_OID(HRSTORAGESIZE),
	    &hrStorageIdx, 1, 0, mib_hrstorage)) == NULL ||
	    (hrStorageUsed = agentx_object(host, AGENTX_OID(HRSTORAGEUSED),
	    &hrStorageIdx, 1, 0, mib_hrstorage)) == NULL ||
	    (hrStorageAllocationFailures = agentx_object(host,
	    AGENTX_OID(HRSTORAGEALLOCATIONFAILURES), &hrStorageIdx, 1, 0,
	    mib_hrstorage)) == NULL)
		fatal("agentx_object");

	if ((hrDeviceIdx = agentx_index_integer_dynamic(host,
	    AGENTX_OID(HRDEVICEINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((hrDeviceIndex = agentx_object(host, AGENTX_OID(HRDEVICEINDEX),
	    &hrDeviceIdx, 1, 0, mib_hrdevice)) == NULL ||
	    (hrDeviceType = agentx_object(host, AGENTX_OID(HRDEVICETYPE),
	    &hrDeviceIdx, 1, 0, mib_hrdevice)) == NULL ||
	    (hrDeviceDescr = agentx_object(host, AGENTX_OID(HRDEVICEDESCR),
	    &hrDeviceIdx, 1, 0, mib_hrdevice)) == NULL ||
	    (hrDeviceID = agentx_object(host, AGENTX_OID(HRDEVICEID),
	    &hrDeviceIdx, 1, 0, mib_hrdevice)) == NULL ||
	    (hrDeviceStatus = agentx_object(host, AGENTX_OID(HRDEVICESTATUS),
	    &hrDeviceIdx, 1, 0, mib_hrdevice)) == NULL ||
	    (hrDeviceErrors = agentx_object(host, AGENTX_OID(HRDEVICEERRORS),
	    &hrDeviceIdx, 1, 0, mib_hrdevice)) == NULL)
		fatal("agentx_object");
	if ((hrProcessorFrwID = agentx_object(host, AGENTX_OID(HRPROCESSORFRWID),
	    &hrDeviceIdx, 1, 0, mib_hrprocessor)) == NULL ||
	    (hrProcessorLoad = agentx_object(host, AGENTX_OID(HRPROCESSORLOAD),
	    &hrDeviceIdx, 1, 0, mib_hrprocessor)) == NULL)
		fatal("agentx_object");
	if ((hrSWRunIdx = agentx_index_integer_dynamic(host,
	    AGENTX_OID(HRSWRUNINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((hrSWRunIndex = agentx_object(host, AGENTX_OID(HRSWRUNINDEX),
	    &hrSWRunIdx, 1, 0, mib_hrswrun)) == NULL ||
	    (hrSWRunName = agentx_object(host, AGENTX_OID(HRSWRUNNAME),
	    &hrSWRunIdx, 1, 0, mib_hrswrun)) == NULL ||
	    (hrSWRunID = agentx_object(host, AGENTX_OID(HRSWRUNID),
	    &hrSWRunIdx, 1, 0, mib_hrswrun)) == NULL ||
	    (hrSWRunPath = agentx_object(host, AGENTX_OID(HRSWRUNPATH),
	    &hrSWRunIdx, 1, 0, mib_hrswrun)) == NULL ||
	    (hrSWRunParameters = agentx_object(host,
	    AGENTX_OID(HRSWRUNPARAMETERS), &hrSWRunIdx, 1, 0,
	    mib_hrswrun)) == NULL ||
	    (hrSWRunType = agentx_object(host, AGENTX_OID(HRSWRUNTYPE),
	    &hrSWRunIdx, 1, 0, mib_hrswrun)) == NULL ||
	    (hrSWRunStatus = agentx_object(host, AGENTX_OID(HRSWRUNSTATUS),
	    &hrSWRunIdx, 1, 0, mib_hrswrun)) == NULL ||
	    (hrSWRunPerfCPU = agentx_object(host, AGENTX_OID(HRSWRUNPERFCPU),
	    &hrSWRunIdx, 1, 0, mib_hrswrun)) == NULL ||
	    (hrSWRunPerfMem = agentx_object(host, AGENTX_OID(HRSWRUNPERFMEM),
	    &hrSWRunIdx, 1, 0, mib_hrswrun)) == NULL)
		fatal("agentx_object");

	/* IF-MIB */
	if ((ifMIB = agentx_region(sac, AGENTX_OID(IFMIB), 0)) == NULL ||
	    (interfaces = agentx_region(sac,
	    AGENTX_OID(INTERFACES), 0)) == NULL)
		fatal("agentx_region");

	if ((ifIdx = agentx_index_integer_dynamic(interfaces,
	    AGENTX_OID(IFINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((ifName = agentx_object(ifMIB, AGENTX_OID(IFNAME),
	    &ifIdx, 1, 0, mib_ifxtable)) == NULL ||
	    (ifInMulticastPkts = agentx_object(ifMIB,
	    AGENTX_OID(IFINMULTICASTPKTS), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifInBroadcastPkts = agentx_object(ifMIB,
	    AGENTX_OID(IFINBROADCASTPKTS), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifOutMulticastPkts = agentx_object(ifMIB,
	    AGENTX_OID(IFOUTMULTICASTPKTS), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifOutBroadcastPkts = agentx_object(ifMIB,
	    AGENTX_OID(IFOUTBROADCASTPKTS), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifHCInOctets = agentx_object(ifMIB, AGENTX_OID(IFHCINOCTETS),
	    &ifIdx, 1, 0, mib_ifxtable)) == NULL ||
	    (ifHCInUcastPkts = agentx_object(ifMIB, AGENTX_OID(IFHCINUCASTPKTS),
	    &ifIdx, 1, 0, mib_ifxtable)) == NULL ||
	    (ifHCInMulticastPkts = agentx_object(ifMIB,
	    AGENTX_OID(IFHCINMULTICASTPKTS), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifHCInBroadcastPkts = agentx_object(ifMIB,
	    AGENTX_OID(IFHCINBROADCASTPKTS), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifHCOutOctets = agentx_object(ifMIB, AGENTX_OID(IFHCOUTOCTETS),
	    &ifIdx, 1, 0, mib_ifxtable)) == NULL ||
	    (ifHCOutUcastPkts = agentx_object(ifMIB,
	    AGENTX_OID(IFHCOUTUCASTPKTS), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifHCOutMulticastPkts = agentx_object(ifMIB,
	    AGENTX_OID(IFHCOUTMULTICASTPKTS), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifHCOutBroadcastPkts = agentx_object(ifMIB,
	    AGENTX_OID(IFHCOUTBROADCASTPKTS), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifLinkUpDownTrapEnable = agentx_object(ifMIB,
	    AGENTX_OID(IFLINKUPDOWNTRAPENABLE), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifHighSpeed = agentx_object(ifMIB, AGENTX_OID(IFHIGHSPEED),
	    &ifIdx, 1, 0, mib_ifxtable)) == NULL ||
	    (ifPromiscuousMode = agentx_object(ifMIB,
	    AGENTX_OID(IFPROMISCUOUSMODE), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifConnectorPresent = agentx_object(ifMIB,
	    AGENTX_OID(IFCONNECTORPRESENT), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL ||
	    (ifAlias = agentx_object(ifMIB, AGENTX_OID(IFALIAS),
	    &ifIdx, 1, 0, mib_ifxtable)) == NULL ||
	    (ifCounterDiscontinuityTime = agentx_object(ifMIB,
	    AGENTX_OID(IFCOUNTERDISCONTINUITYTIME), &ifIdx, 1, 0,
	    mib_ifxtable)) == NULL)
		fatal("agentx_object");

	if ((ifRcvAddressAddress = agentx_index_string_dynamic(ifMIB,
	    AGENTX_OID(IFRCVADDRESSADDRESS))) == NULL)
		fatal("agentx_index_string_dynamic");
	indices[0] = ifIdx;
	indices[1] = ifRcvAddressAddress;
	if ((ifRcvAddressStatus = agentx_object(ifMIB,
	    AGENTX_OID(IFRCVADDRESSSTATUS), indices, 2, 0,
	    mib_ifrcvtable)) == NULL ||
	    (ifRcvAddressType = agentx_object(ifMIB,
	    AGENTX_OID(IFRCVADDRESSTYPE), indices, 2, 0,
	    mib_ifrcvtable)) == NULL)
		fatal("agentx_object");

	if ((ifStackLastChange = agentx_object(ifMIB,
	    AGENTX_OID(IFSTACKLASTCHANGE), NULL, 0, 0,
	    mib_ifstacklast)) == NULL)
		fatal("agentx_object");

	if ((ifNumber = agentx_object(interfaces, AGENTX_OID(IFNUMBER),
	    NULL, 0, 0, mib_ifnumber)) == NULL)
		fatal("agentx_object");

	if ((ifIndex = agentx_object(interfaces, AGENTX_OID(IFINDEX),
	    &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifDescr = agentx_object(interfaces, AGENTX_OID(IFDESCR),
	    &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifType = agentx_object(interfaces, AGENTX_OID(IFTYPE),
	    &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifMtu = agentx_object(interfaces, AGENTX_OID(IFMTU),
	    &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifSpeed = agentx_object(interfaces, AGENTX_OID(IFSPEED),
	    &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifPhysAddress = agentx_object(interfaces,
	    AGENTX_OID(IFPHYSADDRESS), &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifAdminStatus = agentx_object(interfaces,
	    AGENTX_OID(IFADMINSTATUS), &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifOperStatus = agentx_object(interfaces,
	    AGENTX_OID(IFOPERSTATUS), &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifLastChange = agentx_object(interfaces,
	    AGENTX_OID(IFLASTCHANGE), &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifInOctets = agentx_object(interfaces, AGENTX_OID(IFINOCTETS),
	    &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifInUcastPkts = agentx_object(interfaces,
	    AGENTX_OID(IFINUCASTPKTS), &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifInNUcastPkts = agentx_object(interfaces,
	    AGENTX_OID(IFINNUCASTPKTS), &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifInDiscards = agentx_object(interfaces,
	    AGENTX_OID(IFINDISCARDS), &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifInErrors = agentx_object(interfaces, AGENTX_OID(IFINERRORS),
	    &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifInUnknownProtos = agentx_object(interfaces,
	    AGENTX_OID(IFINUNKNOWNPROTOS), &ifIdx, 1, 0,
	    mib_iftable)) == NULL ||
	    (ifOutOctets = agentx_object(interfaces, AGENTX_OID(IFOUTOCTETS),
	    &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifOutUcastPkts = agentx_object(interfaces,
	    AGENTX_OID(IFOUTUCASTPKTS), &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifOutNUcastPkts = agentx_object(interfaces,
	    AGENTX_OID(IFOUTNUCASTPKTS), &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifOutDiscards = agentx_object(interfaces,
	    AGENTX_OID(IFOUTDISCARDS), &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifOutErrors = agentx_object(interfaces, AGENTX_OID(IFOUTERRORS),
	    &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifOutQLen = agentx_object(interfaces, AGENTX_OID(IFOUTQLEN),
	    &ifIdx, 1, 0, mib_iftable)) == NULL ||
	    (ifSpecific = agentx_object(interfaces, AGENTX_OID(IFSPECIFIC),
	    &ifIdx, 1, 0, mib_iftable)) == NULL)
		fatal("agentx_object");

	/* OPENBSD-PF-MIB */
	if ((pfMIBObjects = agentx_region(sac,
	    AGENTX_OID(PFMIBOBJECTS), 0)) == NULL)
		fatal("agentx_region");
	if ((pfRunning = agentx_object(pfMIBObjects, AGENTX_OID(PFRUNNING),
	    NULL, 0, 0, mib_pfinfo)) == NULL ||
	    (pfRuntime = agentx_object(pfMIBObjects, AGENTX_OID(PFRUNTIME),
	    NULL, 0, 0, mib_pfinfo)) == NULL ||
	    (pfDebug = agentx_object(pfMIBObjects, AGENTX_OID(PFDEBUG),
	    NULL, 0, 0, mib_pfinfo)) == NULL ||
	    (pfHostid = agentx_object(pfMIBObjects, AGENTX_OID(PFHOSTID),
	    NULL, 0, 0, mib_pfinfo)) == NULL)
		fatal("agentx_object");

	if ((pfCntMatch = agentx_object(pfMIBObjects, AGENTX_OID(PFCNTMATCH),
	    NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntBadOffset = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTBADOFFSET), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntFragment = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTFRAGMENT), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntShort = agentx_object(pfMIBObjects, AGENTX_OID(PFCNTSHORT),
	    NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntNormalize = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTNORMALIZE), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntMemory = agentx_object(pfMIBObjects, AGENTX_OID(PFCNTMEMORY),
	    NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntTimestamp = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTTIMESTAMP), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntCongestion = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTCONGESTION), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntIpOption = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTIPOPTION), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntProtoCksum = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTPROTOCKSUM), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntStateMismatch = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTSTATEMISMATCH), NULL, 0, 0,
	    mib_pfcounters)) == NULL ||
	    (pfCntStateInsert = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTSTATEINSERT), NULL, 0, 0,
	    mib_pfcounters)) == NULL ||
	    (pfCntStateLimit = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTSTATELIMIT), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntSrcLimit = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTSRCLIMIT), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntSynproxy = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTSYNPROXY), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntTranslate = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTTRANSLATE), NULL, 0, 0, mib_pfcounters)) == NULL ||
	    (pfCntNoRoute = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFCNTNOROUTE), NULL, 0, 0, mib_pfcounters)) == NULL)
		fatal("agentx_object");

	if ((pfStateCount = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSTATECOUNT), NULL, 0, 0, mib_pfscounters)) == NULL ||
	    (pfStateSearches = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSTATESEARCHES), NULL, 0, 0,
	    mib_pfscounters)) == NULL ||
	    (pfStateInserts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSTATEINSERTS), NULL, 0, 0, mib_pfscounters)) == NULL ||
	    (pfStateRemovals = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSTATEREMOVALS), NULL, 0, 0, mib_pfscounters)) == NULL)
		fatal("agentx_object");

	if ((pfLogIfName = agentx_object(pfMIBObjects, AGENTX_OID(PFLOGIFNAME),
	    NULL, 0, 0, mib_pflogif)) == NULL ||
	    (pfLogIfIpBytesIn = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIPBYTESIN), NULL, 0, 0, mib_pflogif)) == NULL ||
	    (pfLogIfIpBytesOut = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIPBYTESOUT), NULL, 0, 0, mib_pflogif)) == NULL ||
	    (pfLogIfIpPktsInPass = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIPPKTSINPASS), NULL, 0, 0,
	    mib_pflogif)) == NULL ||
	    (pfLogIfIpPktsInDrop = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIPPKTSINDROP), NULL, 0, 0,
	    mib_pflogif)) == NULL ||
	    (pfLogIfIpPktsOutPass = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIPPKTSOUTPASS), NULL, 0, 0,
	    mib_pflogif)) == NULL ||
	    (pfLogIfIpPktsOutDrop = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIPPKTSOUTDROP), NULL, 0, 0,
	    mib_pflogif)) == NULL ||
	    (pfLogIfIp6BytesIn = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIP6BYTESIN), NULL, 0, 0, mib_pflogif)) == NULL ||
	    (pfLogIfIp6BytesOut = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIP6BYTESOUT), NULL, 0, 0, mib_pflogif)) == NULL ||
	    (pfLogIfIp6PktsInPass = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIP6PKTSINPASS), NULL, 0, 0,
	    mib_pflogif)) == NULL ||
	    (pfLogIfIp6PktsInDrop = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIP6PKTSINDROP), NULL, 0, 0,
	    mib_pflogif)) == NULL ||
	    (pfLogIfIp6PktsOutPass = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIP6PKTSOUTPASS), NULL, 0, 0,
	    mib_pflogif)) == NULL ||
	    (pfLogIfIp6PktsOutDrop = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLOGIFIP6PKTSOUTDROP), NULL, 0, 0,
	    mib_pflogif)) == NULL)
		fatal("agentx_object");

	if ((pfSrcTrackCount = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSRCTRACKCOUNT), NULL, 0, 0, mib_pfsrctrack)) == NULL ||
	    (pfSrcTrackSearches = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSRCTRACKSEARCHES), NULL, 0, 0,
	    mib_pfsrctrack)) == NULL ||
	    (pfSrcTrackInserts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSRCTRACKINSERTS), NULL, 0, 0,
	    mib_pfsrctrack)) == NULL ||
	    (pfSrcTrackRemovals = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSRCTRACKREMOVALS), NULL, 0, 0,
	    mib_pfsrctrack)) == NULL)
		fatal("agentx_object");

	if ((pfLimitStates = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLIMITSTATES), NULL, 0, 0, mib_pflimits)) == NULL ||
	    (pfLimitSourceNodes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLIMITSOURCENODES), NULL, 0, 0,
	    mib_pflimits)) == NULL ||
	    (pfLimitFragments = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLIMITFRAGMENTS), NULL, 0, 0, mib_pflimits)) == NULL ||
	    (pfLimitMaxTables = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLIMITMAXTABLES), NULL, 0, 0, mib_pflimits)) == NULL ||
	    (pfLimitMaxTableEntries = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLIMITMAXTABLEENTRIES), NULL, 0, 0,
	    mib_pflimits)) == NULL)
		fatal("agentx_object");

	if ((pfTimeoutTcpFirst = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTTCPFIRST), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutTcpOpening = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTTCPOPENING), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutTcpEstablished = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTTCPESTABLISHED), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutTcpClosing = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTTCPCLOSING), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutTcpFinWait = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTTCPFINWAIT), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutTcpClosed = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTTCPCLOSED), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutUdpFirst = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTUDPFIRST), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutUdpSingle = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTUDPSINGLE), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutUdpMultiple = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTUDPMULTIPLE), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutIcmpFirst = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTICMPFIRST), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutIcmpError = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTICMPERROR), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutOtherFirst = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTOTHERFIRST), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutOtherSingle = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTOTHERSINGLE), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutOtherMultiple = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTOTHERMULTIPLE), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutFragment = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTFRAGMENT), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutInterval = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTINTERVAL), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutAdaptiveStart = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTADAPTIVESTART), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutAdaptiveEnd = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTADAPTIVEEND), NULL, 0, 0,
	    mib_pftimeouts)) == NULL ||
	    (pfTimeoutSrcTrack = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTIMEOUTSRCTRACK), NULL, 0, 0,
	    mib_pftimeouts)) == NULL)
		fatal("agentx_object");

	if ((pfIfNumber = agentx_object(pfMIBObjects, AGENTX_OID(PFIFNUMBER),
	    NULL, 0, 0, mib_pfifnum)) == NULL)
		fatal("agentx_object");
	if ((pfIfIdx = agentx_index_integer_dynamic(pfMIBObjects,
	    AGENTX_OID(PFIFINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((pfIfIndex = agentx_object(pfMIBObjects, AGENTX_OID(PFIFINDEX),
	    &pfIfIdx, 1, 0, mib_pfiftable)) == NULL ||
	    (pfIfDescr = agentx_object(pfMIBObjects, AGENTX_OID(PFIFDESCR),
	    &pfIfIdx, 1, 0, mib_pfiftable)) == NULL ||
	    (pfIfType = agentx_object(pfMIBObjects, AGENTX_OID(PFIFTYPE),
	    &pfIfIdx, 1, 0, mib_pfiftable)) == NULL ||
	    (pfIfRefs = agentx_object(pfMIBObjects, AGENTX_OID(PFIFREFS),
	    &pfIfIdx, 1, 0, mib_pfiftable)) == NULL ||
	    (pfIfRules = agentx_object(pfMIBObjects, AGENTX_OID(PFIFRULES),
	    &pfIfIdx, 1, 0, mib_pfiftable)) == NULL ||
	    (pfIfIn4PassPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFIN4PASSPKTS), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfIn4PassBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFIN4PASSBYTES), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfIn4BlockPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFIN4BLOCKPKTS), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfIn4BlockBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFIN4BLOCKBYTES), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfOut4PassPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFOUT4PASSPKTS), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfOut4PassBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFOUT4PASSBYTES), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfOut4BlockPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFOUT4BLOCKPKTS), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfOut4BlockBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFOUT4BLOCKBYTES), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfIn6PassPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFIN6PASSPKTS), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfIn6PassBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFIN6PASSBYTES), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfIn6BlockPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFIN6BLOCKPKTS), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfIn6BlockBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFIN6BLOCKBYTES), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfOut6PassPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFOUT6PASSPKTS), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfOut6PassBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFOUT6PASSBYTES), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfOut6BlockPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFOUT6BLOCKPKTS), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL ||
	    (pfIfOut6BlockBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFIFOUT6BLOCKBYTES), &pfIfIdx, 1, 0,
	    mib_pfiftable)) == NULL)
		fatal("agentx_object");

	if ((pfTblNumber = agentx_object(pfMIBObjects, AGENTX_OID(PFTBLNUMBER),
	    NULL, 0, 0, mib_pftablenum)) == NULL)
		fatal("agentx_object");
	if ((pfTblIdx = agentx_index_integer_dynamic(pfMIBObjects,
	    AGENTX_OID(PFTBLINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((pfTblIndex = agentx_object(pfMIBObjects, AGENTX_OID(PFTBLINDEX),
	    &pfTblIdx, 1, 0, mib_pftables)) == NULL ||
	    (pfTblName = agentx_object(pfMIBObjects, AGENTX_OID(PFTBLNAME),
	    &pfTblIdx, 1, 0, mib_pftables)) == NULL ||
	    (pfTblAddresses = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRESSES), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblAnchorRefs = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLANCHORREFS), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblRuleRefs = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLRULEREFS), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblEvalsMatch = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLEVALSMATCH), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblEvalsNoMatch = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLEVALSNOMATCH), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblInPassPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLINPASSPKTS), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblInPassBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLINPASSBYTES), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblInBlockPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLINBLOCKPKTS), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblInBlockBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLINBLOCKBYTES), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblInXPassPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLINXPASSPKTS), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblInXPassBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLINXPASSBYTES), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblOutPassPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLOUTPASSPKTS), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblOutPassBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLOUTPASSBYTES), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblOutBlockPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLOUTBLOCKPKTS), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblOutBlockBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLOUTBLOCKBYTES), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblOutXPassPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLOUTXPASSPKTS), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblOutXPassBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLOUTXPASSBYTES), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblStatsCleared = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLSTATSCLEARED), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblInMatchPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLINMATCHPKTS), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblInMatchBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLINMATCHBYTES), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblOutMatchPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLOUTMATCHPKTS), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL ||
	    (pfTblOutMatchBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLOUTMATCHBYTES), &pfTblIdx, 1, 0,
	    mib_pftables)) == NULL)
		fatal("agentx_object");

	if ((pfTblAddrTblIdx = agentx_index_integer_dynamic(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRTBLINDEX))) == NULL ||
	    (pfTblAddrNetIdx = agentx_index_ipaddress_dynamic(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRNET))) == NULL ||
	    (pfTblAddrMaskIdx = agentx_index_integer_dynamic(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRMASK))) == NULL)
		fatal("agentx_index_integer_dynamic");
	indices[0] = pfTblAddrTblIdx;
	indices[1] = pfTblAddrNetIdx;
	indices[2] = pfTblAddrMaskIdx;
	if ((pfTblAddrTblIndex = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRTBLINDEX), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrNet = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRNET), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrMask = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRMASK), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrCleared = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRCLEARED), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrInBlockPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRINBLOCKPKTS), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrInBlockBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRINBLOCKBYTES), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrInPassPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRINPASSPKTS), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrInPassBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRINPASSBYTES), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrOutBlockPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDROUTBLOCKPKTS), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrOutBlockBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDROUTBLOCKBYTES), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrOutPassPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDROUTPASSPKTS), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrOutPassBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDROUTPASSBYTES), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrInMatchPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRINMATCHPKTS), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrInMatchBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDRINMATCHBYTES), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrOutMatchPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDROUTMATCHPKTS), indices, 3, 0,
	    mib_pftableaddrs)) == NULL ||
	    (pfTblAddrOutMatchBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFTBLADDROUTMATCHBYTES), indices, 3, 0,
	    mib_pftableaddrs)) == NULL)
		fatal("agentx_object");

	if ((pfLabelNumber = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLABELNUMBER), NULL, 0, 0, mib_pflabelnum)) == NULL)
		fatal("agentx_object");
	if ((pfLabelIdx = agentx_index_integer_dynamic(pfMIBObjects,
	    AGENTX_OID(PFLABELINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((pfLabelIndex = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLABELINDEX), &pfLabelIdx, 1, 0,
	    mib_pflabels)) == NULL ||
	    (pfLabelName = agentx_object(pfMIBObjects, AGENTX_OID(PFLABELNAME),
	    &pfLabelIdx, 1, 0, mib_pflabels)) == NULL ||
	    (pfLabelEvals = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLABELEVALS), &pfLabelIdx, 1, 0,
	    mib_pflabels)) == NULL ||
	    (pfLabelPkts = agentx_object(pfMIBObjects, AGENTX_OID(PFLABELPKTS),
	    &pfLabelIdx, 1, 0, mib_pflabels)) == NULL ||
	    (pfLabelBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLABELBYTES), &pfLabelIdx, 1, 0,
	    mib_pflabels)) == NULL ||
	    (pfLabelInPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLABELINPKTS), &pfLabelIdx, 1, 0,
	    mib_pflabels)) == NULL ||
	    (pfLabelInBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLABELINBYTES), &pfLabelIdx, 1, 0,
	    mib_pflabels)) == NULL ||
	    (pfLabelOutPkts = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLABELOUTPKTS), &pfLabelIdx, 1, 0,
	    mib_pflabels)) == NULL ||
	    (pfLabelOutBytes = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLABELOUTBYTES), &pfLabelIdx, 1, 0,
	    mib_pflabels)) == NULL ||
	    (pfLabelTotalStates = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFLABELTOTALSTATES), &pfLabelIdx, 1, 0,
	    mib_pflabels)) == NULL)
		fatal("agentx_object");

	if ((pfsyncIpPktsRecv = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCIPPKTSRECV), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncIp6PktsRecv = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCIP6PKTSRECV), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncPktDiscardsForBadInterface = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCPKTDISCARDSFORBADINTERFACE), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncPktDiscardsForBadTtl = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCPKTDISCARDSFORBADTTL), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncPktShorterThanHeader = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCPKTSHORTERTHANHEADER), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncPktDiscardsForBadVersion = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCPKTDISCARDSFORBADVERSION), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncPktDiscardsForBadAction = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCPKTDISCARDSFORBADACTION), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncPktDiscardsForBadLength = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCPKTDISCARDSFORBADLENGTH), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncPktDiscardsForBadAuth = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCPKTDISCARDSFORBADAUTH), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncPktDiscardsForStaleState = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCPKTDISCARDSFORSTALESTATE), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncPktDiscardsForBadValues = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCPKTDISCARDSFORBADVALUES), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncPktDiscardsForBadState = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCPKTDISCARDSFORBADSTATE), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncIpPktsSent = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCIPPKTSSENT), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncIp6PktsSent = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCIP6PKTSSENT), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncNoMemory = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCNOMEMORY), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL ||
	    (pfsyncOutputErrors = agentx_object(pfMIBObjects,
	    AGENTX_OID(PFSYNCOUTPUTERRORS), NULL, 0, 0,
	    mib_pfsyncstats)) == NULL)
		fatal("agentx_object");

	if ((sensorsMIBObjects = agentx_region(sac,
	    AGENTX_OID(SENSORSMIBOBJECTS), 0)) == NULL)
		fatal("agentx_region");
	if ((sensorNumber = agentx_object(sensorsMIBObjects,
	    AGENTX_OID(SENSORNUMBER), NULL, 0, 0, mib_sensornum)) == NULL)
		fatal("agentx_object");
	if ((sensorIdx = agentx_index_integer_dynamic(sensorsMIBObjects,
	    AGENTX_OID(SENSORINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((sensorIndex = agentx_object(sensorsMIBObjects,
	    AGENTX_OID(SENSORINDEX), &sensorIdx, 1, 0, mib_sensors)) == NULL ||
	    (sensorDescr = agentx_object(sensorsMIBObjects,
	    AGENTX_OID(SENSORDESCR), &sensorIdx, 1, 0, mib_sensors)) == NULL ||
	    (sensorType = agentx_object(sensorsMIBObjects,
	    AGENTX_OID(SENSORTYPE), &sensorIdx, 1, 0, mib_sensors)) == NULL ||
	    (sensorDevice = agentx_object(sensorsMIBObjects,
	    AGENTX_OID(SENSORDEVICE), &sensorIdx, 1, 0, mib_sensors)) == NULL ||
	    (sensorValue = agentx_object(sensorsMIBObjects,
	    AGENTX_OID(SENSORVALUE), &sensorIdx, 1, 0, mib_sensors)) == NULL ||
	    (sensorUnits = agentx_object(sensorsMIBObjects,
	    AGENTX_OID(SENSORUNITS), &sensorIdx, 1, 0, mib_sensors)) == NULL ||
	    (sensorStatus = agentx_object(sensorsMIBObjects,
	    AGENTX_OID(SENSORSTATUS), &sensorIdx, 1, 0, mib_sensors)) == NULL)
		fatal("agentx_object");

	if ((carpMIBObjects = agentx_region(sac,
	    AGENTX_OID(CARPMIBOBJECTS), 0)) == NULL)
		fatal("agentx_region");
	if ((carpAllow = agentx_object(carpMIBObjects, AGENTX_OID(CARPALLOW),
	    NULL, 0, 0, mib_carpsysctl)) == NULL ||
	    (carpPreempt = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPPREEMPT), NULL, 0, 0, mib_carpsysctl)) == NULL ||
	    (carpLog = agentx_object(carpMIBObjects, AGENTX_OID(CARPLOG),
	    NULL, 0, 0, mib_carpsysctl)) == NULL)
		fatal("agentx_object");

	if ((carpIfNumber = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPIFNUMBER), NULL, 0, 0, mib_carpifnum)) == NULL)
		fatal("agentx_object");
	if ((carpIfIdx = agentx_index_integer_dynamic(carpMIBObjects,
	    AGENTX_OID(CARPIFINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((carpIfIndex = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPIFINDEX), &carpIfIdx, 1, 0,
	    mib_carpiftable)) == NULL ||
	    (carpIfDescr = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPIFDESCR), &carpIfIdx, 1, 0,
	    mib_carpiftable)) == NULL ||
	    (carpIfVhid = agentx_object(carpMIBObjects, AGENTX_OID(CARPIFVHID),
	    &carpIfIdx, 1, 0, mib_carpiftable)) == NULL ||
	    (carpIfDev = agentx_object(carpMIBObjects, AGENTX_OID(CARPIFDEV),
	    &carpIfIdx, 1, 0, mib_carpiftable)) == NULL ||
	    (carpIfAdvbase = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPIFADVBASE), &carpIfIdx, 1, 0,
	    mib_carpiftable)) == NULL ||
	    (carpIfAdvskew = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPIFADVSKEW), &carpIfIdx, 1, 0,
	    mib_carpiftable)) == NULL ||
	    (carpIfState = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPIFSTATE), &carpIfIdx, 1, 0,
	    mib_carpiftable)) == NULL)
		fatal("agentx_object");

	if ((carpGroupIdx = agentx_index_integer_dynamic(carpMIBObjects,
	    AGENTX_OID(CARPGROUPINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((carpGroupName = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPGROUPNAME), &carpGroupIdx, 1, 0,
	    mib_carpgrouptable)) == NULL ||
	    (carpGroupDemote = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPGROUPDEMOTE), &carpGroupIdx, 1, 0,
	    mib_carpgrouptable)) == NULL)
		fatal("agentx_object");

	if ((carpIpPktsRecv = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPIPPKTSRECV), NULL, 0, 0, mib_carpstats)) == NULL ||
	    (carpIp6PktsRecv = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPIP6PKTSRECV), NULL, 0, 0, mib_carpstats)) == NULL ||
	    (carpPktDiscardsForBadInterface = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPPKTDISCARDSFORBADINTERFACE), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpPktDiscardsForWrongTtl = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPPKTDISCARDSFORWRONGTTL), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpPktShorterThanHeader = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPPKTSHORTERTHANHEADER), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpPktDiscardsForBadChecksum = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPPKTDISCARDSFORBADCHECKSUM), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpPktDiscardsForBadVersion = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPPKTDISCARDSFORBADVERSION), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpPktDiscardsForTooShort = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPPKTDISCARDSFORTOOSHORT), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpPktDiscardsForBadAuth = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPPKTDISCARDSFORBADAUTH), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpPktDiscardsForBadVhid = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPPKTDISCARDSFORBADVHID), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpPktDiscardsForBadAddressList = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPPKTDISCARDSFORBADADDRESSLIST), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpIpPktsSent = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPIPPKTSSENT), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpIp6PktsSent = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPIP6PKTSSENT), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpNoMemory = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPNOMEMORY), NULL, 0, 0,
	    mib_carpstats)) == NULL ||
	    (carpTransitionsToMaster = agentx_object(carpMIBObjects,
	    AGENTX_OID(CARPTRANSITIONSTOMASTER), NULL, 0, 0,
	    mib_carpstats)) == NULL)
		fatal("agentx_object");

	/* OPENBSD-MEM-MIB */
	if ((memMIBObjects = agentx_region(sac,
	    AGENTX_OID(MEMMIBOBJECTS), 0)) == NULL)
		fatal("agentx_region");
	if ((memMIBVersion = agentx_object(memMIBObjects,
	    AGENTX_OID(MEMMIBVERSION), NULL, 0, 0, mib_memversion)) == NULL)
		fatal("agentx_object");
	if ((memIfName = agentx_object(memMIBObjects, AGENTX_OID(MEMIFNAME),
	    &ifIdx, 1, 0, mib_memiftable)) == NULL ||
	    (memIfLiveLocks = agentx_object(memMIBObjects,
	    AGENTX_OID(MEMIFLIVELOCKS), &ifIdx, 1, 0,
	    mib_memiftable)) == NULL)
		fatal("agentx_object");

	/* IP-MIB */
	if ((ip = agentx_region(sac, AGENTX_OID(IP), 0)) == NULL)
		fatal("agentx_region");
	if ((ipForwarding = agentx_object(ip, AGENTX_OID(IPFORWARDING),
	    NULL, 0, 0, mib_ipforwarding)) == NULL ||
	    (ipDefaultTTL = agentx_object(ip, AGENTX_OID(IPDEFAULTTTL),
	    NULL, 0, 0, mib_ipdefaultttl)) == NULL ||
	    (ipInReceives = agentx_object(ip, AGENTX_OID(IPINRECEIVES),
	    NULL, 0, 0, mib_ipstat)) == NULL ||
	    (ipInHdrErrors = agentx_object(ip, AGENTX_OID(IPINHDRERRORS),
	    NULL, 0, 0, mib_ipinhdrerrs)) == NULL ||
	    (ipInAddrErrors = agentx_object(ip, AGENTX_OID(IPINADDRERRORS),
	    NULL, 0, 0, mib_ipinaddrerrs)) == NULL ||
	    (ipForwDatagrams = agentx_object(ip, AGENTX_OID(IPFORWDATAGRAMS),
	    NULL, 0, 0, mib_ipforwdgrams)) == NULL ||
	    (ipInUnknownProtos = agentx_object(ip,
	    AGENTX_OID(IPINUNKNOWNPROTOS), NULL, 0, 0, mib_ipstat)) == NULL ||
	    (ipInDelivers = agentx_object(ip, AGENTX_OID(IPINDELIVERS),
	    NULL, 0, 0, mib_ipstat)) == NULL ||
	    (ipOutRequests = agentx_object(ip, AGENTX_OID(IPOUTREQUESTS),
	    NULL, 0, 0, mib_ipstat)) == NULL ||
	    (ipOutDiscards = agentx_object(ip, AGENTX_OID(IPOUTDISCARDS),
	    NULL, 0, 0, mib_ipstat)) == NULL ||
	    (ipOutNoRoutes = agentx_object(ip, AGENTX_OID(IPOUTNOROUTES),
	    NULL, 0, 0, mib_ipstat)) == NULL ||
	    (ipReasmTimeout = agentx_object(ip, AGENTX_OID(IPREASMTIMEOUT),
	    NULL, 0, 0, mib_ipreasmtimeout)) == NULL ||
	    (ipReasmReqds = agentx_object(ip, AGENTX_OID(IPREASMREQDS),
	    NULL, 0, 0, mib_ipstat)) == NULL ||
	    (ipReasmOKs = agentx_object(ip, AGENTX_OID(IPREASMOKS),
	    NULL, 0, 0, mib_ipstat)) == NULL ||
	    (ipReasmFails = agentx_object(ip, AGENTX_OID(IPREASMFAILS),
	    NULL, 0, 0, mib_ipreasmfails)) == NULL ||
	    (ipFragOKs = agentx_object(ip, AGENTX_OID(IPFRAGOKS),
	    NULL, 0, 0, mib_ipstat)) == NULL ||
	    (ipFragFails = agentx_object(ip, AGENTX_OID(IPFRAGFAILS),
	    NULL, 0, 0, mib_ipfragfails)) == NULL ||
	    (ipFragCreates = agentx_object(ip, AGENTX_OID(IPFRAGCREATES),
	    NULL, 0, 0, mib_ipstat)) == NULL)
		fatal("agentx_object");

	if ((ipAdEntAddrIdx = agentx_index_ipaddress_dynamic(ip,
	    AGENTX_OID(IPADENTADDR))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((ipAdEntAddr = agentx_object(ip, AGENTX_OID(IPADENTADDR),
	    &ipAdEntAddrIdx, 1, 0, mib_ipaddr)) == NULL ||
	    (ipAdEntIfIndex = agentx_object(ip, AGENTX_OID(IPADENTIFINDEX),
	    &ipAdEntAddrIdx, 1, 0, mib_ipaddr)) == NULL ||
	    (ipAdEntNetMask = agentx_object(ip, AGENTX_OID(IPADENTNETMASK),
	    &ipAdEntAddrIdx, 1, 0, mib_ipaddr)) == NULL ||
	    (ipAdEntBcastAddr = agentx_object(ip, AGENTX_OID(IPADENTBCASTADDR),
	    &ipAdEntAddrIdx, 1, 0, mib_ipaddr)) == NULL ||
	    (ipAdEntReasmMaxSize = agentx_object(ip,
	    AGENTX_OID(IPADENTREASMMAXSIZE), &ipAdEntAddrIdx, 1, 0,
	    mib_ipaddr)) == NULL)
		fatal("agentx_object");

	if ((ipNetToMediaIfIdx = agentx_index_integer_dynamic(ip,
	    AGENTX_OID(IPNETTOMEDIAIFINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((ipNetToMediaNetAddressIdx = agentx_index_ipaddress_dynamic(ip,
	    AGENTX_OID(IPNETTOMEDIANETADDRESS))) == NULL)
		fatal("agentx_index_string_dynamic");
	indices[0] = ipNetToMediaIfIdx;
	indices[1] = ipNetToMediaNetAddressIdx;
	if ((ipNetToMediaIfIndex = agentx_object(ip,
	    AGENTX_OID(IPNETTOMEDIAIFINDEX), indices, 2, 0,
	    mib_physaddr)) == NULL ||
	    (ipNetToMediaPhysAddress = agentx_object(ip,
	    AGENTX_OID(IPNETTOMEDIAPHYSADDRESS), indices, 2, 0,
	    mib_physaddr)) == NULL ||
	    (ipNetToMediaNetAddress = agentx_object(ip,
	    AGENTX_OID(IPNETTOMEDIANETADDRESS), indices, 2, 0,
	    mib_physaddr)) == NULL ||
	    (ipNetToMediaType = agentx_object(ip, AGENTX_OID(IPNETTOMEDIATYPE),
	    indices, 2, 0, mib_physaddr)) == NULL)
		fatal("agentx_object");

	if ((ipForward = agentx_region(sac, AGENTX_OID(IPFORWARD), 0)) == NULL)
		fatal("agentx_region");
	if ((inetCidrRouteNumber = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTENUMBER), NULL, 0, 0,
	    mib_ipfnroutes)) == NULL)
		fatal("agentx_object");
	if ((inetCidrRouteDestTypeIdx = agentx_index_integer_dynamic(ipForward,
	    AGENTX_OID(INETCIDRROUTEDESTTYPE))) == NULL ||
	    (inetCidrRouteDestIdx = agentx_index_string_dynamic(ipForward,
	    AGENTX_OID(INETCIDRROUTEDEST))) == NULL ||
	    (inetCidrRoutePfxLenIdx = agentx_index_integer_dynamic(ipForward,
	    AGENTX_OID(INETCIDRROUTEPFXLEN))) == NULL ||
	    (inetCidrRoutePolicyIdx = agentx_index_oid_dynamic(ipForward,
	    AGENTX_OID(INETCIDRROUTEPOLICY))) == NULL ||
	    (inetCidrRouteNextHopTypeIdx = agentx_index_integer_dynamic(
	    ipForward, AGENTX_OID(INETCIDRROUTENEXTHOPTYPE))) == NULL ||
	    (inetCidrRouteNextHopIdx = agentx_index_string_dynamic(ipForward,
	    AGENTX_OID(INETCIDRROUTENEXTHOP))) == NULL)
		fatal("agentx_index_*_dynamic");
	indices[0] = inetCidrRouteDestTypeIdx;
	indices[1] = inetCidrRouteDestIdx;
	indices[2] = inetCidrRoutePfxLenIdx;
	indices[3] = inetCidrRoutePolicyIdx;
	indices[4] = inetCidrRouteNextHopTypeIdx;
	indices[5] = inetCidrRouteNextHopIdx;
	if ((inetCidrRouteIfIndex = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTEIFINDEX), indices, 6, 0,
	    mib_ipfroute)) == NULL ||
	    (inetCidrRouteType = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTETYPE), indices, 6, 0,
	    mib_ipfroute)) == NULL ||
	    (inetCidrRouteProto = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTEPROTO), indices, 6, 0,
	    mib_ipfroute)) == NULL ||
	    (inetCidrRouteAge = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTEAGE), indices, 6, 0,
	    mib_ipfroute)) == NULL ||
	    (inetCidrRouteNextHopAS = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTENEXTHOPAS), indices, 6, 0,
	    mib_ipfroute)) == NULL ||
	    (inetCidrRouteMetric1 = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTEMETRIC1), indices, 6, 0,
	    mib_ipfroute)) == NULL ||
	    (inetCidrRouteMetric2 = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTEMETRIC2), indices, 6, 0,
	    mib_ipfroute)) == NULL ||
	    (inetCidrRouteMetric3 = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTEMETRIC3), indices, 6, 0,
	    mib_ipfroute)) == NULL ||
	    (inetCidrRouteMetric4 = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTEMETRIC4), indices, 6, 0,
	    mib_ipfroute)) == NULL ||
	    (inetCidrRouteMetric5 = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTEMETRIC5), indices, 6, 0,
	    mib_ipfroute)) == NULL ||
	    (inetCidrRouteStatus = agentx_object(ipForward,
	    AGENTX_OID(INETCIDRROUTESTATUS), indices, 6, 0,
	    mib_ipfroute)) == NULL)
		fatal("agentx_object");

	/* UCD-DISKIO-MIB */
	if ((ucdDiskIOMIB = agentx_region(sac, AGENTX_OID(UCDDISKIOMIB),
	    0)) == NULL)
		fatal("agentx_region");
	if ((diskIOIdx = agentx_index_integer_dynamic(ucdDiskIOMIB,
	    AGENTX_OID(DISKIOINDEX))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((diskIOIndex = agentx_object(ucdDiskIOMIB, AGENTX_OID(DISKIOINDEX),
	    &diskIOIdx, 1, 0, mib_diskio)) == NULL ||
	    (diskIODevice = agentx_object(ucdDiskIOMIB,
	    AGENTX_OID(DISKIODEVICE), &diskIOIdx, 1, 0, mib_diskio)) == NULL ||
	    (diskIONRead = agentx_object(ucdDiskIOMIB, AGENTX_OID(DISKIONREAD),
	    &diskIOIdx, 1, 0, mib_diskio)) == NULL ||
	    (diskIONWritten = agentx_object(ucdDiskIOMIB,
	    AGENTX_OID(DISKIONWRITTEN), &diskIOIdx, 1, 0,
	    mib_diskio)) == NULL ||
	    (diskIOReads = agentx_object(ucdDiskIOMIB,
	    AGENTX_OID(DISKIOREADS), &diskIOIdx, 1, 0, mib_diskio)) == NULL ||
	    (diskIOWrites = agentx_object(ucdDiskIOMIB,
	    AGENTX_OID(DISKIOWRITES), &diskIOIdx, 1, 0, mib_diskio)) == NULL ||
	    (diskIONReadX = agentx_object(ucdDiskIOMIB,
	    AGENTX_OID(DISKIONREADX), &diskIOIdx, 1, 0, mib_diskio)) == NULL ||
	    (diskIONWrittenX = agentx_object(ucdDiskIOMIB,
	    AGENTX_OID(DISKIONWRITTENX), &diskIOIdx, 1, 0,
	    mib_diskio)) == NULL)
		fatal("agentx_object");

	if ((dot1dBridge = agentx_region(sac, AGENTX_OID(DOT1DBRIDGE),
	    0)) == NULL)
		fatal("agentx_region");
	if ((dot1dBaseNumPorts = agentx_object(dot1dBridge,
	    AGENTX_OID(DOT1DBASENUMPORTS), NULL, 0, 0, mib_ifnumber)) == NULL ||
	    (dot1dBaseType = agentx_object(dot1dBridge,
	    AGENTX_OID(DOT1DBASETYPE), NULL, 0, 0, mib_dot1basetype)) == NULL)
		fatal("agentx_object");

	if ((dot1dBasePortIdx = agentx_index_integer_dynamic(dot1dBridge,
	    AGENTX_OID(DOT1DBASEPORT))) == NULL)
		fatal("agentx_index_integer_dynamic");
	if ((dot1dBasePort = agentx_object(dot1dBridge,
	    AGENTX_OID(DOT1DBASEPORT), &dot1dBasePortIdx, 1, 0,
	    mib_dot1dtable)) == NULL ||
	    (dot1dBasePortIfIndex = agentx_object(dot1dBridge,
	    AGENTX_OID(DOT1DBASEPORTIFINDEX), &dot1dBasePortIdx, 1, 0,
	    mib_dot1dtable)) == NULL ||
	    (dot1dBasePortCircuit = agentx_object(dot1dBridge,
	    AGENTX_OID(DOT1DBASEPORTCIRCUIT), &dot1dBasePortIdx, 1, 0,
	    mib_dot1dtable)) == NULL ||
	    (dot1dBasePortDelayExceededDiscards = agentx_object(dot1dBridge,
	    AGENTX_OID(DOT1DBASEPORTDELAYEXCEEDEDDISCARDS), &dot1dBasePortIdx,
	    1, 0, mib_dot1dtable)) == NULL ||
	    (dot1dBasePortMtuExceededDiscards = agentx_object(dot1dBridge,
	    AGENTX_OID(DOT1DBASEPORTMTUEXCEEDEDDISCARDS), &dot1dBasePortIdx,
	    1, 0, mib_dot1dtable)) == NULL)
		fatal("agentx_object");

	if (daemonize) {
		log_init(0, LOG_DAEMON);
		daemon(0, 0);
	}
	log_setverbose(verbose);

	event_dispatch();
}

#define LOG1024		 10
void
pageshift_init(void)
{
	long pagesize;

	if ((pagesize = sysconf(_SC_PAGESIZE)) == -1)
		fatal("sysconf(_SC_PAGESIZE)");
	while (pagesize > 1) {
		pageshift++;
		pagesize >>= 1;
	}
	/* we only need the amount of log(2)1024 for our conversion */
	pageshift -= LOG1024;
}

void
snmp_connect(struct agentx *sa, void *cookie, int close)
{
	static int init = 0;

	if (close) {
		event_del(&connev);
		return;
	}

	if (agentxfd != -1) {
		/* Exit if snmpd(8) leaves */
		if (init)
			exit(0);
		agentx_connect(sa, agentxfd);
		event_set(&connev, agentxfd, EV_READ | EV_PERSIST,
		    snmp_read, sa);
		event_add(&connev, NULL);
		init = 1;
	} else 
		snmp_tryconnect(-1, 0, sa);
}

void
snmp_tryconnect(int fd, short event, void *cookie)
{
	struct timeval timeout = {3, 0};
	struct agentx *sa = cookie;
	struct sockaddr_un sun;

	sun.sun_len = sizeof(sun);
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, AGENTX_MASTER_PATH, sizeof(sun.sun_path));

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ||
	    connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
		if (fd != -1)
			close(fd);
		log_warn("Failed to connect to snmpd");
		evtimer_set(&connev, snmp_tryconnect, sa);
		evtimer_add(&connev, &timeout);
		return;
	}

	event_set(&connev, fd, EV_READ | EV_PERSIST, snmp_read, sa);
	event_add(&connev, NULL);

	agentx_connect(sa, fd);
}

void
snmp_read(int fd, short event, void *cookie)
{
	struct agentx *sa = cookie;

	agentx_read(sa);
}

u_long
smi_getticks(void)
{
	return agentx_context_uptime(sac);
}
