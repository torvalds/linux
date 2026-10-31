#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""
Hypervisor call statistics

Copyright (C) 2018 Ravi Bangoria, IBM Corporation
Ported from tools/perf/scripts/python/powerpc-hcalls.py
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import sys
import perf

# Hypervisor call table
HCALL_TABLE = {
	4: 'H_REMOVE',
	8: 'H_ENTER',
	12: 'H_READ',
	16: 'H_CLEAR_MOD',
	20: 'H_CLEAR_REF',
	24: 'H_PROTECT',
	28: 'H_GET_TCE',
	32: 'H_PUT_TCE',
	36: 'H_SET_SPRG0',
	40: 'H_SET_DABR',
	44: 'H_PAGE_INIT',
	48: 'H_SET_ASR',
	52: 'H_ASR_ON',
	56: 'H_ASR_OFF',
	60: 'H_LOGICAL_CI_LOAD',
	64: 'H_LOGICAL_CI_STORE',
	68: 'H_LOGICAL_CACHE_LOAD',
	72: 'H_LOGICAL_CACHE_STORE',
	76: 'H_LOGICAL_ICBI',
	80: 'H_LOGICAL_DCBF',
	84: 'H_GET_TERM_CHAR',
	88: 'H_PUT_TERM_CHAR',
	92: 'H_REAL_TO_LOGICAL',
	96: 'H_HYPERVISOR_DATA',
	100: 'H_EOI',
	104: 'H_CPPR',
	108: 'H_IPI',
	112: 'H_IPOLL',
	116: 'H_XIRR',
	120: 'H_MIGRATE_DMA',
	124: 'H_PERFMON',
	220: 'H_REGISTER_VPA',
	224: 'H_CEDE',
	228: 'H_CONFER',
	232: 'H_PROD',
	236: 'H_GET_PPP',
	240: 'H_SET_PPP',
	244: 'H_PURR',
	248: 'H_PIC',
	252: 'H_REG_CRQ',
	256: 'H_FREE_CRQ',
	260: 'H_VIO_SIGNAL',
	264: 'H_SEND_CRQ',
	272: 'H_COPY_RDMA',
	276: 'H_REGISTER_LOGICAL_LAN',
	280: 'H_FREE_LOGICAL_LAN',
	284: 'H_ADD_LOGICAL_LAN_BUFFER',
	288: 'H_SEND_LOGICAL_LAN',
	292: 'H_BULK_REMOVE',
	304: 'H_MULTICAST_CTRL',
	308: 'H_SET_XDABR',
	312: 'H_STUFF_TCE',
	316: 'H_PUT_TCE_INDIRECT',
	332: 'H_CHANGE_LOGICAL_LAN_MAC',
	336: 'H_VTERM_PARTNER_INFO',
	340: 'H_REGISTER_VTERM',
	344: 'H_FREE_VTERM',
	348: 'H_RESET_EVENTS',
	352: 'H_ALLOC_RESOURCE',
	356: 'H_FREE_RESOURCE',
	360: 'H_MODIFY_QP',
	364: 'H_QUERY_QP',
	368: 'H_REREGISTER_PMR',
	372: 'H_REGISTER_SMR',
	376: 'H_QUERY_MR',
	380: 'H_QUERY_MW',
	384: 'H_QUERY_HCA',
	388: 'H_QUERY_PORT',
	392: 'H_MODIFY_PORT',
	396: 'H_DEFINE_AQP1',
	400: 'H_GET_TRACE_BUFFER',
	404: 'H_DEFINE_AQP0',
	408: 'H_RESIZE_MR',
	412: 'H_ATTACH_MCQP',
	416: 'H_DETACH_MCQP',
	420: 'H_CREATE_RPT',
	424: 'H_REMOVE_RPT',
	428: 'H_REGISTER_RPAGES',
	432: 'H_DISABLE_AND_GET',
	436: 'H_ERROR_DATA',
	440: 'H_GET_HCA_INFO',
	444: 'H_GET_PERF_COUNT',
	448: 'H_MANAGE_TRACE',
	456: 'H_GET_CPU_CHARACTERISTICS',
	468: 'H_FREE_LOGICAL_LAN_BUFFER',
	472: 'H_POLL_PENDING',
	484: 'H_QUERY_INT_STATE',
	580: 'H_ILLAN_ATTRIBUTES',
	584: 'H_ADD_LOGICAL_LAN_BUFFERS',
	592: 'H_MODIFY_HEA_QP',
	596: 'H_QUERY_HEA_QP',
	600: 'H_QUERY_HEA',
	604: 'H_QUERY_HEA_PORT',
	608: 'H_MODIFY_HEA_PORT',
	612: 'H_REG_BCMC',
	616: 'H_DEREG_BCMC',
	620: 'H_REGISTER_HEA_RPAGES',
	624: 'H_DISABLE_AND_GET_HEA',
	628: 'H_GET_HEA_INFO',
	632: 'H_ALLOC_HEA_RESOURCE',
	644: 'H_ADD_CONN',
	648: 'H_DEL_CONN',
	664: 'H_JOIN',
	672: 'H_VASI_SIGNAL',
	676: 'H_VASI_STATE',
	680: 'H_VIOCTL',
	688: 'H_ENABLE_CRQ',
	696: 'H_GET_EM_PARMS',
	720: 'H_SET_MPP',
	724: 'H_GET_MPP',
	732: 'H_REG_SUB_CRQ',
	736: 'H_FREE_SUB_CRQ',
	740: 'H_SEND_SUB_CRQ',
	744: 'H_SEND_SUB_CRQ_INDIRECT',
	748: 'H_HOME_NODE_ASSOCIATIVITY',
	756: 'H_BEST_ENERGY',
	764: 'H_XIRR_X',
	768: 'H_RANDOM',
	772: 'H_COP',
	788: 'H_GET_MPP_X',
	796: 'H_SET_MODE',
	808: 'H_BLOCK_REMOVE',
	856: 'H_CLEAR_HPT',
	864: 'H_REQUEST_VMC',
	876: 'H_RESIZE_HPT_PREPARE',
	880: 'H_RESIZE_HPT_COMMIT',
	892: 'H_REGISTER_PROC_TBL',
	896: 'H_SIGNAL_SYS_RESET',
	904: 'H_ALLOCATE_VAS_WINDOW',
	908: 'H_MODIFY_VAS_WINDOW',
	912: 'H_DEALLOCATE_VAS_WINDOW',
	916: 'H_QUERY_VAS_WINDOW',
	920: 'H_QUERY_VAS_CAPABILITIES',
	924: 'H_QUERY_NX_CAPABILITIES',
	928: 'H_GET_NX_FAULT',
	936: 'H_INT_GET_SOURCE_INFO',
	940: 'H_INT_SET_SOURCE_CONFIG',
	944: 'H_INT_GET_SOURCE_CONFIG',
	948: 'H_INT_GET_QUEUE_INFO',
	952: 'H_INT_SET_QUEUE_CONFIG',
	956: 'H_INT_GET_QUEUE_CONFIG',
	960: 'H_INT_SET_OS_REPORTING_LINE',
	964: 'H_INT_GET_OS_REPORTING_LINE',
	968: 'H_INT_ESB',
	972: 'H_INT_SYNC',
	976: 'H_INT_RESET',
	996: 'H_SCM_READ_METADATA',
	1000: 'H_SCM_WRITE_METADATA',
	1004: 'H_SCM_BIND_MEM',
	1008: 'H_SCM_UNBIND_MEM',
	1012: 'H_SCM_QUERY_BLOCK_MEM_BINDING',
	1016: 'H_SCM_QUERY_LOGICAL_MEM_BINDING',
	1020: 'H_SCM_UNBIND_ALL',
	1024: 'H_SCM_HEALTH',
	1048: 'H_SCM_PERFORMANCE_STATS',
	1052: 'H_PKS_GET_CONFIG',
	1056: 'H_PKS_SET_PASSWORD',
	1060: 'H_PKS_GEN_PASSWORD',
	1068: 'H_PKS_WRITE_OBJECT',
	1072: 'H_PKS_GEN_KEY',
	1076: 'H_PKS_READ_OBJECT',
	1080: 'H_PKS_REMOVE_OBJECT',
	1084: 'H_PKS_CONFIRM_OBJECT_FLUSHED',
	1096: 'H_RPT_INVALIDATE',
	1100: 'H_SCM_FLUSH',
	1104: 'H_GET_ENERGY_SCALE_INFO',
	1108: 'H_PKS_SIGNED_UPDATE',
	1112: 'H_HTM',
	1116: 'H_WATCHDOG',
	# Platform specific hcalls used by KVM on PowerVM
	1120: 'H_GUEST_GET_CAPABILITIES',
	1124: 'H_GUEST_SET_CAPABILITIES',
	1136: 'H_GUEST_CREATE',
	1140: 'H_GUEST_CREATE_VCPU',
	1144: 'H_GUEST_GET_STATE',
	1148: 'H_GUEST_SET_STATE',
	1152: 'H_GUEST_RUN_VCPU',
	1156: 'H_GUEST_COPY_MEMORY',
	1160: 'H_GUEST_DELETE',
	# Key wrapping hcalls
	1168: 'H_PKS_WRAP_OBJECT',
	1172: 'H_PKS_UNWRAP_OBJECT',
	# Platform-specific hcalls used by the Ultravisor
	61184: 'H_SVM_PAGE_IN',
	61188: 'H_SVM_PAGE_OUT',
	61192: 'H_SVM_INIT_START',
	61196: 'H_SVM_INIT_DONE',
	61204: 'H_SVM_INIT_ABORT',
	# Platform specific hcalls used by KVM
	61440: 'H_RTAS',
	# Platform specific hcalls used by QEMU/SLOF
	61441: 'H_LOGICAL_MEMOP',
	61442: 'H_CAS',
	61443: 'H_UPDATE_DT',
	# Platform specific hcalls provided by PHYP
	61560: 'H_GET_24X7_CATALOG_PAGE',
	61564: 'H_GET_24X7_DATA',
	61568: 'H_GET_PERF_COUNTER_INFO',
	# Platform-specific hcalls used for nested HV KVM
	63488: 'H_SET_PARTITION_TABLE',
	63492: 'H_ENTER_NESTED',
	63496: 'H_TLB_INVALIDATE',
	63500: 'H_COPY_TOFROM_GUEST',
}


class HCallAnalyzer:
    """Analyzes hypervisor calls and aggregates statistics."""

    def __init__(self, cmd_args) -> None:
        self.args = cmd_args
        # output: {opcode: {'min': min, 'max': max, 'time': time, 'cnt': cnt}}
        self.output: dict[int, dict[str, 'int | float']] = \
            defaultdict(lambda: {'time': 0, 'cnt': 0, 'min': float('inf'), 'max': 0})
        # d_enter: {cpu: {opcode: nsec}}
        self.d_enter: dict[int, dict[int, int]] = {}
        self.print_ptrn = '%-28s%10s%10s%10s%10s'

    def hcall_table_lookup(self, opcode: int) -> str:
        """Lookup hcall name by opcode."""
        return HCALL_TABLE.get(opcode, str(opcode))

    def process_event(self, sample: perf.sample_event) -> None:
        """Process a single sample event."""
        name = str(sample.evsel)
        if name not in ("evsel(powerpc:hcall_entry)", "evsel(powerpc:hcall_exit)"):
            return

        try:
            cpu = sample.sample_cpu
            opcode = sample.opcode
        except AttributeError:
            print("ERROR: tracepoint fields missed (is libtraceevent enabled?)",
                  file=sys.stderr)
            sys.exit(1)

        # sample.sample_time represents the nsecs
        time = sample.sample_time

        if opcode < 0 or cpu < 0:
            return

        if name == "evsel(powerpc:hcall_entry)":
            if cpu not in self.d_enter:
                self.d_enter[cpu] = {}
            self.d_enter[cpu][opcode] = time
        elif name == "evsel(powerpc:hcall_exit)":
            if cpu in self.d_enter and opcode in self.d_enter[cpu]:
                time_entry = self.d_enter[cpu][opcode]
                diff = time - time_entry
                del self.d_enter[cpu][opcode]

                stats = self.output[opcode]
                stats['time'] += diff
                stats['cnt'] += 1
                if diff < stats['min']:
                    stats['min'] = diff
                if diff > stats['max']:
                    stats['max'] = diff

    def print_summary(self) -> None:
        """Print aggregated statistics."""
        print(self.print_ptrn % ('hcall', 'count', 'min(ns)', 'max(ns)', 'avg(ns)'))
        print('-' * 68)

        def sort_output(opcode):
            stats = self.output[opcode]
            sort_by = getattr(self.args, 'sort', None)
            if sort_by == 'min':
                return stats['min']
            if sort_by == 'max':
                return stats['max']
            if sort_by == 'avg':
                return stats['time'] // stats['cnt']
            return stats['cnt']

        for opcode in sorted(self.output.keys(), key=sort_output, reverse=True):
            h_name = self.hcall_table_lookup(opcode)
            stats = self.output[opcode]
            time = stats['time']
            cnt = stats['cnt']
            min_t = stats['min']
            max_t = stats['max']

            # Avoid float representation for large integers if possible,
            # or use formatted strings. Legacy used time//cnt.
            avg_t = time // cnt if cnt > 0 else 0

            # If min was not updated, it remains inf, but cnt should be > 0 if in output
            if min_t == float('inf'):
                min_t = 0

            print(self.print_ptrn % (h_name, cnt, int(min_t), int(max_t), avg_t))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Hypervisor call statistics")
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    ap.add_argument("-s", "--sort", default="count",
                    choices=["count", "min", "max", "avg"], help="Sort key")
    args = ap.parse_args()

    analyzer = HCallAnalyzer(args)

    session = None
    try:
        session = perf.session(perf.data(args.input), sample=analyzer.process_event)
        session.process_events()
        analyzer.print_summary()
    except KeyboardInterrupt:
        analyzer.print_summary()
    except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError) as e:
        print(f"Error processing events: {e}")
        sys.exit(1)
    finally:
        # Break the reference cycle between perf.session and analyzer.process_event.
        session = None
