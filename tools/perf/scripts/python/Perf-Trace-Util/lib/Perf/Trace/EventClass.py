# EventClass.py
# SPDX-License-Identifier: GPL-2.0
#
# This is a library defining some events types classes, which could
# be used by other scripts to analyzing the perf samples.
#
# Currently there are just a few classes defined for examples,
# PerfEvent is the base class for all perf event sample, PebsEvent
# is a HW base Intel x86 PEBS event, and user could add more SW/HW
# event classes based on requirements.

import struct

# Event types, user could add more here
EVTYPE_GENERIC  = 0
EVTYPE_PEBS     = 1     # Basic PEBS event
EVTYPE_PEBS_LL  = 2     # PEBS event with load latency info
EVTYPE_IBS      = 3

#
# Currently we don't have good way to tell the event type, but by
# the size of raw buffer, raw PEBS event with load latency data's
# size is 176 bytes, while the pure PEBS event's size is 144 bytes.
#
def create_event(name, comm, dso, symbol, raw_buf):
        if (len(raw_buf) == 144):
                event = PebsEvent(name, comm, dso, symbol, raw_buf)
        elif (len(raw_buf) == 176):
                event = PebsNHM(name, comm, dso, symbol, raw_buf)
        else:
                event = PerfEvent(name, comm, dso, symbol, raw_buf)

        return event

class PerfEvent(object):
        event_num = 0
        def __init__(self, name, comm, dso, symbol, raw_buf, ev_type=EVTYPE_GENERIC):
                self.name       = name
                self.comm       = comm
                self.dso        = dso
                self.symbol     = symbol
                self.raw_buf    = raw_buf
                self.ev_type    = ev_type
                PerfEvent.event_num += 1

        def show(self):
                print "PMU event: name=%12s, symbol=%24s, comm=%8s, dso=%12s" % (self.name, self.symbol, self.comm, self.dso)

#
# Basic Intel PEBS (Precise Event-based Sampling) event, whose raw buffer
# contains the context info when that event happened: the EFLAGS and
# linear IP info, as well as all the registers.
#
class PebsEvent(PerfEvent):
        pebs_num = 0
        def __init__(self, name, comm, dso, symbol, raw_buf, ev_type=EVTYPE_PEBS):
                tmp_buf=raw_buf[0:80]
                flags, ip, ax, bx, cx, dx, si, di, bp, sp = struct.unpack('QQQQQQQQQQ', tmp_buf)
                self.flags = flags
                self.ip    = ip
                self.ax    = ax
                self.bx    = bx
                self.cx    = cx
                self.dx    = dx
                self.si    = si
                self.di    = di
                self.bp    = bp
                self.sp    = sp

                PerfEvent.__init__(self, name, comm, dso, symbol, raw_buf, ev_type)
                PebsEvent.pebs_num += 1
                del tmp_buf

#
# Intel Nehalem and Westmere support PEBS plus Load Latency info which lie
# in the four 64 bit words write after the PEBS data:
#       Status: records the IA32_PERF_GLOBAL_STATUS register value
#       DLA:    Data Linear Address (EIP)
#       DSE:    Data Source Encoding, where the latency happens, hit or miss
#               in L1/L2/L3 or IO operations
#       LAT:    the actual latency in cycles
#
class PebsNHM(PebsEvent):
        pebs_nhm_num = 0
        def __init__(self, name, comm, dso, symbol, raw_buf, ev_type=EVTYPE_PEBS_LL):
                tmp_buf=raw_buf[144:176]
                status, dla, dse, lat = struct.unpack('QQQQ', tmp_buf)
                self.status = status
                self.dla = dla
                self.dse = dse
                self.lat = lat

                PebsEvent.__init__(self, name, comm, dso, symbol, raw_buf, ev_type)
                PebsNHM.pebs_nhm_num += 1
                del tmp_buf
