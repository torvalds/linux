#!/usr/bin/env python

import os
import re
import sys

def _write_message(kind, message):
    import inspect, os, sys

    # Get the file/line where this message was generated.
    f = inspect.currentframe()
    # Step out of _write_message, and then out of wrapper.
    f = f.f_back.f_back
    file,line,_,_,_ = inspect.getframeinfo(f)
    location = '%s:%d' % (os.path.basename(file), line)

    print >>sys.stderr, '%s: %s: %s' % (location, kind, message)

note = lambda message: _write_message('note', message)
warning = lambda message: _write_message('warning', message)
error = lambda message: (_write_message('error', message), sys.exit(1))

def re_full_match(pattern, str):
    m = re.match(pattern, str)
    if m and m.end() != len(str):
        m = None
    return m

def parse_time(value):
    minutes,value = value.split(':',1)
    if '.' in value:
        seconds,fseconds = value.split('.',1)
    else:
        seconds = value
    return int(minutes) * 60 + int(seconds) + float('.'+fseconds)

def extractExecutable(command):
    """extractExecutable - Given a string representing a command line, attempt
    to extract the executable path, even if it includes spaces."""

    # Split into potential arguments.
    args = command.split(' ')

    # Scanning from the beginning, try to see if the first N args, when joined,
    # exist. If so that's probably the executable.
    for i in range(1,len(args)):
        cmd = ' '.join(args[:i])
        if os.path.exists(cmd):
            return cmd

    # Otherwise give up and return the first "argument".
    return args[0]

class Struct:
    def __init__(self, **kwargs):
        self.fields = kwargs.keys()
        self.__dict__.update(kwargs)

    def __repr__(self):
        return 'Struct(%s)' % ', '.join(['%s=%r' % (k,getattr(self,k))
                                         for k in self.fields])

kExpectedPSFields = [('PID', int, 'pid'),
                     ('USER', str, 'user'),
                     ('COMMAND', str, 'command'),
                     ('%CPU', float, 'cpu_percent'),
                     ('TIME', parse_time, 'cpu_time'),
                     ('VSZ', int, 'vmem_size'),
                     ('RSS', int, 'rss')]
def getProcessTable():
    import subprocess
    p = subprocess.Popen(['ps', 'aux'], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    out,err = p.communicate()
    res = p.wait()
    if p.wait():
        error('unable to get process table')
    elif err.strip():
        error('unable to get process table: %s' % err)

    lns = out.split('\n')
    it = iter(lns)
    header = it.next().split()
    numRows = len(header)

    # Make sure we have the expected fields.
    indexes = []
    for field in kExpectedPSFields:
        try:
            indexes.append(header.index(field[0]))
        except:
            if opts.debug:
                raise
            error('unable to get process table, no %r field.' % field[0])

    table = []
    for i,ln in enumerate(it):
        if not ln.strip():
            continue

        fields = ln.split(None, numRows - 1)
        if len(fields) != numRows:
            warning('unable to process row: %r' % ln)
            continue

        record = {}
        for field,idx in zip(kExpectedPSFields, indexes):
            value = fields[idx]
            try:
                record[field[2]] = field[1](value)
            except:
                if opts.debug:
                    raise
                warning('unable to process %r in row: %r' % (field[0], ln))
                break
        else:
            # Add our best guess at the executable.
            record['executable'] = extractExecutable(record['command'])
            table.append(Struct(**record))

    return table

def getSignalValue(name):
    import signal
    if name.startswith('SIG'):
        value = getattr(signal, name)
        if value and isinstance(value, int):
            return value
    error('unknown signal: %r' % name)

import signal
kSignals = {}
for name in dir(signal):
    if name.startswith('SIG') and name == name.upper() and name.isalpha():
        kSignals[name[3:]] = getattr(signal, name)

def main():
    global opts
    from optparse import OptionParser, OptionGroup
    parser = OptionParser("usage: %prog [options] {pid}*")

    # FIXME: Add -NNN and -SIGNAME options.

    parser.add_option("-s", "", dest="signalName",
                      help="Name of the signal to use (default=%default)",
                      action="store", default='INT',
                      choices=kSignals.keys())
    parser.add_option("-l", "", dest="listSignals",
                      help="List known signal names",
                      action="store_true", default=False)

    parser.add_option("-n", "--dry-run", dest="dryRun",
                      help="Only print the actions that would be taken",
                      action="store_true", default=False)
    parser.add_option("-v", "--verbose", dest="verbose",
                      help="Print more verbose output",
                      action="store_true", default=False)
    parser.add_option("", "--debug", dest="debug",
                      help="Enable debugging output",
                      action="store_true", default=False)
    parser.add_option("", "--force", dest="force",
                      help="Perform the specified commands, even if it seems like a bad idea",
                      action="store_true", default=False)

    inf = float('inf')
    group = OptionGroup(parser, "Process Filters")
    group.add_option("", "--name", dest="execName", metavar="REGEX",
                      help="Kill processes whose name matches the given regexp",
                      action="store", default=None)
    group.add_option("", "--exec", dest="execPath", metavar="REGEX",
                      help="Kill processes whose executable matches the given regexp",
                      action="store", default=None)
    group.add_option("", "--user", dest="userName", metavar="REGEX",
                      help="Kill processes whose user matches the given regexp",
                      action="store", default=None)
    group.add_option("", "--min-cpu", dest="minCPU", metavar="PCT",
                      help="Kill processes with CPU usage >= PCT",
                      action="store", type=float, default=None)
    group.add_option("", "--max-cpu", dest="maxCPU", metavar="PCT",
                      help="Kill processes with CPU usage <= PCT",
                      action="store", type=float, default=inf)
    group.add_option("", "--min-mem", dest="minMem", metavar="N",
                      help="Kill processes with virtual size >= N (MB)",
                      action="store", type=float, default=None)
    group.add_option("", "--max-mem", dest="maxMem", metavar="N",
                      help="Kill processes with virtual size <= N (MB)",
                      action="store", type=float, default=inf)
    group.add_option("", "--min-rss", dest="minRSS", metavar="N",
                      help="Kill processes with RSS >= N",
                      action="store", type=float, default=None)
    group.add_option("", "--max-rss", dest="maxRSS", metavar="N",
                      help="Kill processes with RSS <= N",
                      action="store", type=float, default=inf)
    group.add_option("", "--min-time", dest="minTime", metavar="N",
                      help="Kill processes with CPU time >= N (seconds)",
                      action="store", type=float, default=None)
    group.add_option("", "--max-time", dest="maxTime", metavar="N",
                      help="Kill processes with CPU time <= N (seconds)",
                      action="store", type=float, default=inf)
    parser.add_option_group(group)

    (opts, args) = parser.parse_args()

    if opts.listSignals:
        items = [(v,k) for k,v in kSignals.items()]
        items.sort()
        for i in range(0, len(items), 4):
            print '\t'.join(['%2d) SIG%s' % (k,v)
                             for k,v in items[i:i+4]])
        sys.exit(0)

    # Figure out the signal to use.
    signal = kSignals[opts.signalName]
    signalValueName = str(signal)
    if opts.verbose:
        name = dict((v,k) for k,v in kSignals.items()).get(signal,None)
        if name:
            signalValueName = name
            note('using signal %d (SIG%s)' % (signal, name))
        else:
            note('using signal %d' % signal)

    # Get the pid list to consider.
    pids = set()
    for arg in args:
        try:
            pids.add(int(arg))
        except:
            parser.error('invalid positional argument: %r' % arg)

    filtered = ps = getProcessTable()

    # Apply filters.
    if pids:
        filtered = [p for p in filtered
                    if p.pid in pids]
    if opts.execName is not None:
        filtered = [p for p in filtered
                    if re_full_match(opts.execName,
                                     os.path.basename(p.executable))]
    if opts.execPath is not None:
        filtered = [p for p in filtered
                    if re_full_match(opts.execPath, p.executable)]
    if opts.userName is not None:
        filtered = [p for p in filtered
                    if re_full_match(opts.userName, p.user)]
    filtered = [p for p in filtered
                if opts.minCPU <= p.cpu_percent <= opts.maxCPU]
    filtered = [p for p in filtered
                if opts.minMem <= float(p.vmem_size) / (1<<20) <= opts.maxMem]
    filtered = [p for p in filtered
                if opts.minRSS <= p.rss <= opts.maxRSS]
    filtered = [p for p in filtered
                if opts.minTime <= p.cpu_time <= opts.maxTime]

    if len(filtered) == len(ps):
        if not opts.force and not opts.dryRun:
            error('refusing to kill all processes without --force')

    if not filtered:
        warning('no processes selected')

    for p in filtered:
        if opts.verbose:
            note('kill(%r, %s) # (user=%r, executable=%r, CPU=%2.2f%%, time=%r, vmem=%r, rss=%r)' %
                 (p.pid, signalValueName, p.user, p.executable, p.cpu_percent, p.cpu_time, p.vmem_size, p.rss))
        if not opts.dryRun:
            try:
                os.kill(p.pid, signal)
            except OSError:
                if opts.debug:
                    raise
                warning('unable to kill PID: %r' % p.pid)

if __name__ == '__main__':
    main()
