#!/usr/bin/python3

#
# trace-cmd record -e hrtimer_start -e hrtimer_cancel -e hrtimer_expire_entry -- $cmd
#

from tracecmd import *

def load_kallsyms(file_path='/proc/kallsyms'):
    """
    Parses /proc/kallsyms into a dictionary.
    Returns: { address_int: symbol_name }
    """
    kallsyms_map = {}

    try:
        with open(file_path, 'r') as f:
            for line in f:
                # The format is: [address] [type] [name] [module]
                parts = line.split()
                if len(parts) < 3:
                    continue

                addr = int(parts[0], 16)
                name = parts[2]

                kallsyms_map[addr] = name

    except PermissionError:
        print(f"Error: Permission denied reading {file_path}. Try running with sudo.")
    except FileNotFoundError:
        print(f"Error: {file_path} not found.")

    return kallsyms_map

ksyms = load_kallsyms()

# pending[timer_ptr] = {'ts': timestamp, 'comm': comm}
pending = {}

# histograms[comm][bucket] = count
histograms = {}

class OnlineHarmonicMean:
    def __init__(self):
        self.n = 0          # Count of elements
        self.S = 0.0        # Cumulative sum of reciprocals

    def update(self, x):
        if x == 0:
            raise ValueError("Harmonic mean is undefined for zero.")

        self.n += 1
        self.S += 1.0 / x
        return self.n / self.S

    @property
    def mean(self):
        return self.n / self.S if self.n > 0 else 0

ohms = {}

def handle_start(record):
    func_name = ksyms[record.num_field("function")]
    if "rseq_slice_expired" in func_name:
        timer_ptr = record.num_field("hrtimer")
        pending[timer_ptr] = {
            'ts': record.ts,
            'comm': record.comm
        }
    return None

def handle_cancel(record):
    timer_ptr = record.num_field("hrtimer")

    if timer_ptr in pending:
        start_data = pending.pop(timer_ptr)
        duration_ns = record.ts - start_data['ts']
        duration_us = duration_ns // 1000

        comm = start_data['comm']

        if comm not in ohms:
            ohms[comm] = OnlineHarmonicMean()

        ohms[comm].update(duration_ns)

        if comm not in histograms:
            histograms[comm] = {}

        histograms[comm][duration_us] = histograms[comm].get(duration_us, 0) + 1
    return None

def handle_expire(record):
    timer_ptr = record.num_field("hrtimer")

    if timer_ptr in pending:
        start_data = pending.pop(timer_ptr)
        comm = start_data['comm']

        if comm not in histograms:
            histograms[comm] = {}

        # Record -1 bucket for expired (failed to cancel)
        histograms[comm][-1] = histograms[comm].get(-1, 0) + 1
    return None

if __name__ == "__main__":
    t = Trace("trace.dat")
    for cpu in range(0, t.cpus):
        ev = t.read_event(cpu)
        while ev:
            if "hrtimer_start" in ev.name:
                handle_start(ev)
            if "hrtimer_cancel" in ev.name:
                handle_cancel(ev)
            if "hrtimer_expire_entry" in ev.name:
                handle_expire(ev)

            ev = t.read_event(cpu)

    print("\n" + "="*40)
    print("RSEQ SLICE HISTOGRAM (us)")
    print("="*40)
    for comm, buckets in histograms.items():
        print(f"\nTask: {comm}    Mean: {ohms[comm].mean:.3f} ns")
        print(f"  {'Latency (us)':<15} | {'Count'}")
        print(f"  {'-'*30}")
        # Sort buckets numerically, putting -1 at the top
        for bucket in sorted(buckets.keys()):
            label = "EXPIRED" if bucket == -1 else f"{bucket} us"
            print(f"  {label:<15} | {buckets[bucket]}")
