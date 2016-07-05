#
# Copyright(C) 2016 Linaro Limited. All rights reserved.
# Author: Tor Jeremiassen <tor.jeremiassen@linaro.org>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published by
# the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program.  If not, see <http://www.gnu.org/licenses/>.
#

import os
import sys

sys.path.append(os.environ['PERF_EXEC_PATH'] + \
                '/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *

def trace_begin():
        pass;

def trace_end():
        pass

def process_event(t):

        sample = t['sample']

        print "range:",format(sample['ip'],"x"),"-",format(sample['addr'],"x")

def trace_unhandled(event_name, context, event_fields_dict):
		print ' '.join(['%s=%s'%(k,str(v))for k,v in sorted(event_fields_dict.items())])

def print_header(event_name, cpu, secs, nsecs, pid, comm):
        print "print_header"
	print "%-20s %5u %05u.%09u %8u %-20s " % \
	(event_name, cpu, secs, nsecs, pid, comm),
