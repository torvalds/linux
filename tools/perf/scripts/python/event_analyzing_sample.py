# event_analyzing_sample.py: general event handler in python
# SPDX-License-Identifier: GPL-2.0
#
# Current perf report is already very powerful with the annotation integrated,
# and this script is not trying to be as powerful as perf report, but
# providing end user/developer a flexible way to analyze the events other
# than trace points.
#
# The 2 database related functions in this script just show how to gather
# the basic information, and users can modify and write their own functions
# according to their specific requirement.
#
# The first function "show_general_events" just does a basic grouping for all
# generic events with the help of sqlite, and the 2nd one "show_pebs_ll" is
# for a x86 HW PMU event: PEBS with load latency data.
#

from __future__ import print_function

import os
import sys
import math
import struct
import sqlite3

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
        '/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from EventClass import *

#
# If the perf.data has a big number of samples, then the insert operation
# will be very time consuming (about 10+ minutes for 10000 samples) if the
# .db database is on disk. Move the .db file to RAM based FS to speedup
# the handling, which will cut the time down to several seconds.
#
con = sqlite3.connect("/dev/shm/perf.db")
con.isolation_level = None

def trace_begin():
        print("In trace_begin:\n")

        #
        # Will create several tables at the start, pebs_ll is for PEBS data with
        # load latency info, while gen_events is for general event.
        #
        con.execute("""
                create table if not exists gen_events (
                        name text,
                        symbol text,
                        comm text,
                        dso text
                );""")
        con.execute("""
                create table if not exists pebs_ll (
                        name text,
                        symbol text,
                        comm text,
                        dso text,
                        flags integer,
                        ip integer,
                        status integer,
                        dse integer,
                        dla integer,
                        lat integer
                );""")

#
# Create and insert event object to a database so that user could
# do more analysis with simple database commands.
#
def process_event(param_dict):
        event_attr = param_dict["attr"]
        sample     = param_dict["sample"]
        raw_buf    = param_dict["raw_buf"]
        comm       = param_dict["comm"]
        name       = param_dict["ev_name"]

        # Symbol and dso info are not always resolved
        if ("dso" in param_dict):
                dso = param_dict["dso"]
        else:
                dso = "Unknown_dso"

        if ("symbol" in param_dict):
                symbol = param_dict["symbol"]
        else:
                symbol = "Unknown_symbol"

        # Create the event object and insert it to the right table in database
        event = create_event(name, comm, dso, symbol, raw_buf)
        insert_db(event)

def insert_db(event):
        if event.ev_type == EVTYPE_GENERIC:
                con.execute("insert into gen_events values(?, ?, ?, ?)",
                                (event.name, event.symbol, event.comm, event.dso))
        elif event.ev_type == EVTYPE_PEBS_LL:
                event.ip &= 0x7fffffffffffffff
                event.dla &= 0x7fffffffffffffff
                con.execute("insert into pebs_ll values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                        (event.name, event.symbol, event.comm, event.dso, event.flags,
                                event.ip, event.status, event.dse, event.dla, event.lat))

def trace_end():
        print("In trace_end:\n")
        # We show the basic info for the 2 type of event classes
        show_general_events()
        show_pebs_ll()
        con.close()

#
# As the event number may be very big, so we can't use linear way
# to show the histogram in real number, but use a log2 algorithm.
#

def num2sym(num):
        # Each number will have at least one '#'
        snum = '#' * (int)(math.log(num, 2) + 1)
        return snum

def show_general_events():

        # Check the total record number in the table
        count = con.execute("select count(*) from gen_events")
        for t in count:
                print("There is %d records in gen_events table" % t[0])
                if t[0] == 0:
                        return

        print("Statistics about the general events grouped by thread/symbol/dso: \n")

         # Group by thread
        commq = con.execute("select comm, count(comm) from gen_events group by comm order by -count(comm)")
        print("\n%16s %8s %16s\n%s" % ("comm", "number", "histogram", "="*42))
        for row in commq:
             print("%16s %8d     %s" % (row[0], row[1], num2sym(row[1])))

        # Group by symbol
        print("\n%32s %8s %16s\n%s" % ("symbol", "number", "histogram", "="*58))
        symbolq = con.execute("select symbol, count(symbol) from gen_events group by symbol order by -count(symbol)")
        for row in symbolq:
             print("%32s %8d     %s" % (row[0], row[1], num2sym(row[1])))

        # Group by dso
        print("\n%40s %8s %16s\n%s" % ("dso", "number", "histogram", "="*74))
        dsoq = con.execute("select dso, count(dso) from gen_events group by dso order by -count(dso)")
        for row in dsoq:
             print("%40s %8d     %s" % (row[0], row[1], num2sym(row[1])))

#
# This function just shows the basic info, and we could do more with the
# data in the tables, like checking the function parameters when some
# big latency events happen.
#
def show_pebs_ll():

        count = con.execute("select count(*) from pebs_ll")
        for t in count:
                print("There is %d records in pebs_ll table" % t[0])
                if t[0] == 0:
                        return

        print("Statistics about the PEBS Load Latency events grouped by thread/symbol/dse/latency: \n")

        # Group by thread
        commq = con.execute("select comm, count(comm) from pebs_ll group by comm order by -count(comm)")
        print("\n%16s %8s %16s\n%s" % ("comm", "number", "histogram", "="*42))
        for row in commq:
             print("%16s %8d     %s" % (row[0], row[1], num2sym(row[1])))

        # Group by symbol
        print("\n%32s %8s %16s\n%s" % ("symbol", "number", "histogram", "="*58))
        symbolq = con.execute("select symbol, count(symbol) from pebs_ll group by symbol order by -count(symbol)")
        for row in symbolq:
             print("%32s %8d     %s" % (row[0], row[1], num2sym(row[1])))

        # Group by dse
        dseq = con.execute("select dse, count(dse) from pebs_ll group by dse order by -count(dse)")
        print("\n%32s %8s %16s\n%s" % ("dse", "number", "histogram", "="*58))
        for row in dseq:
             print("%32s %8d     %s" % (row[0], row[1], num2sym(row[1])))

        # Group by latency
        latq = con.execute("select lat, count(lat) from pebs_ll group by lat order by lat")
        print("\n%32s %8s %16s\n%s" % ("latency", "number", "histogram", "="*58))
        for row in latq:
             print("%32s %8d     %s" % (row[0], row[1], num2sym(row[1])))

def trace_unhandled(event_name, context, event_fields_dict):
        print (' '.join(['%s=%s'%(k,str(v))for k,v in sorted(event_fields_dict.items())]))
