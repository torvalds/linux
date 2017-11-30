#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0

data    = {}
times   = []
threads = []
cpus    = []

def get_key(time, event, cpu, thread):
    return "%d-%s-%d-%d" % (time, event, cpu, thread)

def store_key(time, cpu, thread):
    if (time not in times):
        times.append(time)

    if (cpu not in cpus):
        cpus.append(cpu)

    if (thread not in threads):
        threads.append(thread)

def store(time, event, cpu, thread, val, ena, run):
    #print "event %s cpu %d, thread %d, time %d, val %d, ena %d, run %d" % \
    #      (event, cpu, thread, time, val, ena, run)

    store_key(time, cpu, thread)
    key = get_key(time, event, cpu, thread)
    data[key] = [ val, ena, run]

def get(time, event, cpu, thread):
    key = get_key(time, event, cpu, thread)
    return data[key][0]

def stat__cycles_k(cpu, thread, time, val, ena, run):
    store(time, "cycles", cpu, thread, val, ena, run);

def stat__instructions_k(cpu, thread, time, val, ena, run):
    store(time, "instructions", cpu, thread, val, ena, run);

def stat__cycles_u(cpu, thread, time, val, ena, run):
    store(time, "cycles", cpu, thread, val, ena, run);

def stat__instructions_u(cpu, thread, time, val, ena, run):
    store(time, "instructions", cpu, thread, val, ena, run);

def stat__cycles(cpu, thread, time, val, ena, run):
    store(time, "cycles", cpu, thread, val, ena, run);

def stat__instructions(cpu, thread, time, val, ena, run):
    store(time, "instructions", cpu, thread, val, ena, run);

def stat__interval(time):
    for cpu in cpus:
        for thread in threads:
            cyc = get(time, "cycles", cpu, thread)
            ins = get(time, "instructions", cpu, thread)
            cpi = 0

            if ins != 0:
                cpi = cyc/float(ins)

            print "%15f: cpu %d, thread %d -> cpi %f (%d/%d)" % (time/(float(1000000000)), cpu, thread, cpi, cyc, ins)

def trace_end():
    pass
# XXX trace_end callback could be used as an alternative place
#     to compute same values as in the script above:
#
#    for time in times:
#        for cpu in cpus:
#            for thread in threads:
#                cyc = get(time, "cycles", cpu, thread)
#                ins = get(time, "instructions", cpu, thread)
#
#                if ins != 0:
#                    cpi = cyc/float(ins)
#
#                print "time %.9f, cpu %d, thread %d -> cpi %f" % (time/(float(1000000000)), cpu, thread, cpi)
