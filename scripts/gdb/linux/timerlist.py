# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2019 Google LLC.

import binascii
import gdb

from linux import constants
from linux import cpus
from linux import rbtree
from linux import utils

timerqueue_node_type = utils.CachedType("struct timerqueue_node").get_type()
hrtimer_type = utils.CachedType("struct hrtimer").get_type()


def ktime_get():
    """Returns the current time, but not very accurately

    We can't read the hardware timer itself to add any nanoseconds
    that need to be added since we last stored the time in the
    timekeeper. But this is probably good enough for debug purposes."""
    tk_core = gdb.parse_and_eval("&tk_core")

    return tk_core['timekeeper']['tkr_mono']['base']


def print_timer(rb_node, idx):
    timerqueue = utils.container_of(rb_node, timerqueue_node_type.pointer(),
                                    "node")
    timer = utils.container_of(timerqueue, hrtimer_type.pointer(), "node")

    function = str(timer['function']).split(" ")[1].strip("<>")
    softexpires = timer['_softexpires']
    expires = timer['node']['expires']
    now = ktime_get()

    text = " #{}: <{}>, {}, ".format(idx, timer, function)
    text += "S:{:02x}\n".format(int(timer['state']))
    text += " # expires at {}-{} nsecs [in {} to {} nsecs]\n".format(
            softexpires, expires, softexpires - now, expires - now)
    return text


def print_active_timers(base):
    curr = base['active']['rb_root']['rb_leftmost']
    idx = 0
    while curr:
        yield print_timer(curr, idx)
        curr = rbtree.rb_next(curr)
        idx += 1


def print_base(base):
    text = " .base:       {}\n".format(base.address)
    text += " .index:      {}\n".format(base['index'])

    text += " .resolution: {} nsecs\n".format(constants.LX_hrtimer_resolution)

    text += " .get_time:   {}\n".format(base['get_time'])
    if constants.LX_CONFIG_HIGH_RES_TIMERS:
        text += "  .offset:     {} nsecs\n".format(base['offset'])
    text += "active timers:\n"
    text += "".join([x for x in print_active_timers(base)])
    return text


def print_cpu(hrtimer_bases, cpu, max_clock_bases):
    cpu_base = cpus.per_cpu(hrtimer_bases, cpu)
    jiffies = gdb.parse_and_eval("jiffies_64")
    tick_sched_ptr = gdb.parse_and_eval("&tick_cpu_sched")
    ts = cpus.per_cpu(tick_sched_ptr, cpu)

    text = "cpu: {}\n".format(cpu)
    for i in range(max_clock_bases):
        text += " clock {}:\n".format(i)
        text += print_base(cpu_base['clock_base'][i])

        if constants.LX_CONFIG_HIGH_RES_TIMERS:
            fmts = [("  .{}   : {} nsecs", 'expires_next'),
                    ("  .{}    : {}", 'hres_active'),
                    ("  .{}      : {}", 'nr_events'),
                    ("  .{}     : {}", 'nr_retries'),
                    ("  .{}       : {}", 'nr_hangs'),
                    ("  .{}  : {}", 'max_hang_time')]
            text += "\n".join([s.format(f, cpu_base[f]) for s, f in fmts])
            text += "\n"

        if constants.LX_CONFIG_TICK_ONESHOT:
            TS_FLAG_STOPPED = 1 << 1
            TS_FLAG_NOHZ = 1 << 4
            text += f"  .{'nohz':15s}: {int(bool(ts['flags'] & TS_FLAG_NOHZ))}\n"
            text += f"  .{'last_tick':15s}: {ts['last_tick']}\n"
            text += f"  .{'tick_stopped':15s}: {int(bool(ts['flags'] & TS_FLAG_STOPPED))}\n"
            text += f"  .{'idle_jiffies':15s}: {ts['idle_jiffies']}\n"
            text += f"  .{'idle_calls':15s}: {ts['idle_calls']}\n"
            text += f"  .{'idle_sleeps':15s}: {ts['idle_sleeps']}\n"
            text += f"  .{'idle_entrytime':15s}: {ts['idle_entrytime']} nsecs\n"
            text += f"  .{'idle_waketime':15s}: {ts['idle_waketime']} nsecs\n"
            text += f"  .{'idle_exittime':15s}: {ts['idle_exittime']} nsecs\n"
            text += f"  .{'idle_sleeptime':15s}: {ts['idle_sleeptime']} nsecs\n"
            text += f"  .{'iowait_sleeptime':15s}: {ts['iowait_sleeptime']} nsecs\n"
            text += f"  .{'last_jiffies':15s}: {ts['last_jiffies']}\n"
            text += f"  .{'next_timer':15s}: {ts['next_timer']}\n"
            text += f"  .{'idle_expires':15s}: {ts['idle_expires']} nsecs\n"
            text += "\njiffies: {}\n".format(jiffies)

        text += "\n"

    return text


def print_tickdevice(td, cpu):
    dev = td['evtdev']
    text = "Tick Device: mode:     {}\n".format(td['mode'])
    if cpu < 0:
            text += "Broadcast device\n"
    else:
            text += "Per CPU device: {}\n".format(cpu)

    text += "Clock Event Device: "
    if dev == 0:
            text += "<NULL>\n"
            return text

    text += "{}\n".format(dev['name'])
    text += " max_delta_ns:   {}\n".format(dev['max_delta_ns'])
    text += " min_delta_ns:   {}\n".format(dev['min_delta_ns'])
    text += " mult:           {}\n".format(dev['mult'])
    text += " shift:          {}\n".format(dev['shift'])
    text += " mode:           {}\n".format(dev['state_use_accessors'])
    text += " next_event:     {} nsecs\n".format(dev['next_event'])

    text += " set_next_event: {}\n".format(dev['set_next_event'])

    members = [('set_state_shutdown', " shutdown: {}\n"),
               ('set_state_periodic', " periodic: {}\n"),
               ('set_state_oneshot', " oneshot:  {}\n"),
               ('set_state_oneshot_stopped', " oneshot stopped: {}\n"),
               ('tick_resume', " resume:   {}\n")]
    for member, fmt in members:
        if dev[member]:
            text += fmt.format(dev[member])

    text += " event_handler:  {}\n".format(dev['event_handler'])
    text += " retries:        {}\n".format(dev['retries'])

    return text


def pr_cpumask(mask):
    nr_cpu_ids = 1
    if constants.LX_NR_CPUS > 1:
        nr_cpu_ids = gdb.parse_and_eval("nr_cpu_ids")

    inf = gdb.inferiors()[0]
    bits = mask['bits']
    num_bytes = (nr_cpu_ids + 7) / 8
    buf = utils.read_memoryview(inf, bits, num_bytes).tobytes()
    buf = binascii.b2a_hex(buf)
    if type(buf) is not str:
        buf=buf.decode()

    chunks = []
    i = num_bytes
    while i > 0:
        i -= 1
        start = i * 2
        end = start + 2
        chunks.append(buf[start:end])
        if i != 0 and i % 4 == 0:
            chunks.append(',')

    extra = nr_cpu_ids % 8
    if 0 < extra <= 4:
        chunks[0] = chunks[0][0]  # Cut off the first 0

    return "".join(str(chunks))


class LxTimerList(gdb.Command):
    """Print /proc/timer_list"""

    def __init__(self):
        super(LxTimerList, self).__init__("lx-timerlist", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        hrtimer_bases = gdb.parse_and_eval("&hrtimer_bases")
        max_clock_bases = gdb.parse_and_eval("HRTIMER_MAX_CLOCK_BASES")

        text = "Timer List Version: gdb scripts\n"
        text += "HRTIMER_MAX_CLOCK_BASES: {}\n".format(
            max_clock_bases.type.fields()[max_clock_bases].enumval)
        text += "now at {} nsecs\n".format(ktime_get())

        for cpu in cpus.each_online_cpu():
            text += print_cpu(hrtimer_bases, cpu, max_clock_bases)

        if constants.LX_CONFIG_GENERIC_CLOCKEVENTS:
            if constants.LX_CONFIG_GENERIC_CLOCKEVENTS_BROADCAST:
                bc_dev = gdb.parse_and_eval("&tick_broadcast_device")
                text += print_tickdevice(bc_dev, -1)
                text += "\n"
                mask = gdb.parse_and_eval("tick_broadcast_mask")
                mask = pr_cpumask(mask)
                text += "tick_broadcast_mask: {}\n".format(mask)
                if constants.LX_CONFIG_TICK_ONESHOT:
                    mask = gdb.parse_and_eval("tick_broadcast_oneshot_mask")
                    mask = pr_cpumask(mask)
                    text += "tick_broadcast_oneshot_mask: {}\n".format(mask)
                text += "\n"

            tick_cpu_devices = gdb.parse_and_eval("&tick_cpu_device")
            for cpu in cpus.each_online_cpu():
                tick_dev = cpus.per_cpu(tick_cpu_devices, cpu)
                text += print_tickdevice(tick_dev, cpu)
                text += "\n"

        gdb.write(text)


LxTimerList()
