#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Display a process of packets and processed time.
It helps us to investigate networking or network device.

Ported from tools/perf/scripts/python/netdev-times.py
"""

from __future__ import annotations
import argparse
from collections import defaultdict
import sys
from typing import Optional
import perf

# Format for displaying rx packet processing
PF_IRQ_ENTRY = "  irq_entry(+%.3fmsec irq=%d:%s)"
PF_SOFT_ENTRY = "  softirq_entry(+%.3fmsec)"
PF_NAPI_POLL = "  napi_poll_exit(+%.3fmsec %s)"
PF_JOINT = "         |"
PF_WJOINT = "         |            |"
PF_NET_RECV = "         |---netif_receive_skb(+%.3fmsec skb=%x len=%d)"
PF_NET_RX = "         |---netif_rx(+%.3fmsec skb=%x)"
PF_CPY_DGRAM = "         |      skb_copy_datagram_iovec(+%.3fmsec %d:%s)"
PF_KFREE_SKB = "         |      kfree_skb(+%.3fmsec location=%x)"
PF_CONS_SKB = "         |      consume_skb(+%.3fmsec)"


class NetDevTimesAnalyzer:
    """Analyzes network device events and prints charts."""

    def __init__(self, cfg: argparse.Namespace):
        self.args = cfg
        self.session: Optional[perf.session] = None
        self.show_tx = cfg.tx or (not cfg.tx and not cfg.rx)
        self.show_rx = cfg.rx or (not cfg.tx and not cfg.rx)
        self.dev = cfg.dev
        self.debug = cfg.debug
        self.buffer_budget = 65536
        self.irq_dic: dict[int, list[dict]] = defaultdict(list)
        self.net_rx_dic: dict[int, dict] = {}
        self.receive_hunk_list: list[dict] = []
        self.rx_skb_list: list[dict] = []
        self.tx_queue_list: list[dict] = []
        self.tx_xmit_list: list[dict] = []
        self.tx_free_list: list[dict] = []

        self.buffer_budget = 65536
        self.of_count_rx_skb_list = 0
        self.of_count_tx_queue_list = 0
        self.of_count_tx_xmit_list = 0

    def diff_msec(self, src: int, dst: int) -> float:
        """Calculate a time interval(msec) from src(nsec) to dst(nsec)."""
        return (dst - src) / 1000000.0

    def print_transmit(self, hunk: dict) -> None:
        """Display a process of transmitting a packet."""
        if self.dev and hunk['dev'].find(self.dev) < 0:
            return
        queue_t_sec = hunk['queue_t'] // 1000000000
        queue_t_usec = hunk['queue_t'] % 1000000000 // 1000
        print(f"{hunk['dev']:7s} {hunk['len']:5d} "
              f"{queue_t_sec:6d}.{queue_t_usec:06d}sec "
              f"{self.diff_msec(hunk['queue_t'], hunk['xmit_t']):12.3f}msec      "
              f"{self.diff_msec(hunk['xmit_t'], hunk['free_t']):12.3f}msec")

    def print_receive(self, hunk: dict) -> None:
        """Display a process of received packets and interrupts."""
        show_hunk = False
        irq_list = hunk['irq_list']
        if not irq_list:
            return
        cpu = irq_list[0]['cpu']
        base_t = irq_list[0]['irq_ent_t']

        if self.dev:
            for irq in irq_list:
                if irq['name'].find(self.dev) >= 0:
                    show_hunk = True
                    break
        else:
            show_hunk = True

        if not show_hunk:
            return

        base_t_sec = base_t // 1000000000
        base_t_usec = base_t % 1000000000 // 1000
        print(f"{base_t_sec}.{base_t_usec:06d}sec cpu={cpu}")
        for irq in irq_list:
            print(PF_IRQ_ENTRY %
                  (self.diff_msec(base_t, irq['irq_ent_t']),
                   irq['irq'], irq['name']))
            print(PF_JOINT)
            irq_event_list = irq['event_list']
            for irq_event in irq_event_list:
                if irq_event['event'] == 'netif_rx':
                    print(PF_NET_RX %
                          (self.diff_msec(base_t, irq_event['time']),
                           irq_event['skbaddr']))
                    print(PF_JOINT)

        print(PF_SOFT_ENTRY % self.diff_msec(base_t, hunk['sirq_ent_t']))
        print(PF_JOINT)
        event_list = hunk['event_list']
        for i, event in enumerate(event_list):
            if event['event_name'] == 'napi_poll':
                print(PF_NAPI_POLL %
                      (self.diff_msec(base_t, event['event_t']),
                       event['dev']))
                if i == len(event_list) - 1:
                    print("")
                else:
                    print(PF_JOINT)
            else:
                print(PF_NET_RECV %
                      (self.diff_msec(base_t, event['event_t']),
                       event['skbaddr'],
                       event['len']))
                if 'handle' in event:
                    print(PF_WJOINT)
                    if event['handle'] == "kfree_skb":
                        print(PF_KFREE_SKB %
                              (self.diff_msec(base_t, event['comm_t']),
                               event['location']))
                    elif event['handle'] == "consume_skb":
                        print(PF_CONS_SKB %
                              self.diff_msec(base_t, event['comm_t']))
                    elif event['handle'] == "skb_copy_datagram_iovec":
                        print(PF_CPY_DGRAM %
                              (self.diff_msec(base_t, event['comm_t']),
                               event['pid'], event['comm']))
                print(PF_JOINT)

    def handle_irq_handler_entry(self, event: dict) -> None:
        """Handle irq:irq_handler_entry event."""
        time = event['time']
        cpu = event['cpu']
        irq = event['irq']
        irq_name = event['irq_name']
        irq_record = {'irq': irq, 'name': irq_name, 'cpu': cpu,
                      'irq_ent_t': time, 'event_list': []}
        self.irq_dic[cpu].append(irq_record)

    def handle_irq_handler_exit(self, event: dict) -> None:
        """Handle irq:irq_handler_exit event."""
        time = event['time']
        cpu = event['cpu']
        irq = event['irq']
        if cpu not in self.irq_dic or not self.irq_dic[cpu]:
            return
        if irq != self.irq_dic[cpu][-1]['irq']:
            return
        irq_record = self.irq_dic[cpu].pop()
        irq_record['irq_ext_t'] = time
        # if an irq doesn't include NET_RX softirq, drop.
        if irq_record['event_list']:
            self.irq_dic[cpu].append(irq_record)

    def handle_irq_softirq_raise(self, event: dict) -> None:
        """Handle irq:softirq_raise event."""
        time = event['time']
        cpu = event['cpu']
        if cpu not in self.irq_dic or not self.irq_dic[cpu]:
            return
        irq_record = self.irq_dic[cpu].pop()
        irq_record['event_list'].append({'time': time, 'event': 'sirq_raise'})
        self.irq_dic[cpu].append(irq_record)

    def handle_irq_softirq_entry(self, event: dict) -> None:
        """Handle irq:softirq_entry event."""
        time = event['time']
        cpu = event['cpu']
        self.net_rx_dic[cpu] = {'sirq_ent_t': time, 'event_list': []}

    def handle_irq_softirq_exit(self, event: dict) -> None:
        """Handle irq:softirq_exit event."""
        time = event['time']
        cpu = event['cpu']
        irq_list = None
        event_list = None
        sirq_ent_t = None

        if cpu in self.irq_dic:
            irq_list = self.irq_dic[cpu]
            del self.irq_dic[cpu]
        if cpu in self.net_rx_dic:
            sirq_ent_t = self.net_rx_dic[cpu]['sirq_ent_t']
            event_list = self.net_rx_dic[cpu]['event_list']
            del self.net_rx_dic[cpu]
        if not irq_list or event_list is None or sirq_ent_t is None:
            return
        rec_data = {'sirq_ent_t': sirq_ent_t, 'sirq_ext_t': time,
                    'irq_list': irq_list, 'event_list': event_list}
        self.receive_hunk_list.append(rec_data)

    def handle_napi_poll(self, event: dict) -> None:
        """Handle napi:napi_poll event."""
        time = event['time']
        cpu = event['cpu']
        dev_name = event['dev_name']
        work = event['work']
        budget = event['budget']
        if cpu in self.net_rx_dic:
            event_list = self.net_rx_dic[cpu]['event_list']
            rec_data = {'event_name': 'napi_poll',
                        'dev': dev_name, 'event_t': time,
                        'work': work, 'budget': budget}
            event_list.append(rec_data)

    def handle_netif_rx(self, event: dict) -> None:
        """Handle net:netif_rx event."""
        time = event['time']
        cpu = event['cpu']
        skbaddr = event['skbaddr']
        skblen = event['skblen']
        dev_name = event['dev_name']
        if cpu not in self.irq_dic or not self.irq_dic[cpu]:
            return
        irq_record = self.irq_dic[cpu].pop()
        irq_record['event_list'].append({'time': time, 'event': 'netif_rx',
                                         'skbaddr': skbaddr, 'skblen': skblen,
                                         'dev_name': dev_name})
        self.irq_dic[cpu].append(irq_record)

    def handle_netif_receive_skb(self, event: dict) -> None:
        """Handle net:netif_receive_skb event."""
        time = event['time']
        cpu = event['cpu']
        skbaddr = event['skbaddr']
        skblen = event['skblen']
        if cpu in self.net_rx_dic:
            rec_data = {'event_name': 'netif_receive_skb',
                        'event_t': time, 'skbaddr': skbaddr, 'len': skblen}
            event_list = self.net_rx_dic[cpu]['event_list']
            event_list.append(rec_data)
            self.rx_skb_list.insert(0, rec_data)
            if len(self.rx_skb_list) > self.buffer_budget:
                self.rx_skb_list.pop()
                self.of_count_rx_skb_list += 1

    def handle_net_dev_queue(self, event: dict) -> None:
        """Handle net:net_dev_queue event."""
        time = event['time']
        skbaddr = event['skbaddr']
        skblen = event['skblen']
        dev_name = event['dev_name']
        skb = {'dev': dev_name, 'skbaddr': skbaddr, 'len': skblen, 'queue_t': time}
        self.tx_queue_list.insert(0, skb)
        if len(self.tx_queue_list) > self.buffer_budget:
            self.tx_queue_list.pop()
            self.of_count_tx_queue_list += 1

    def handle_net_dev_xmit(self, event: dict) -> None:
        """Handle net:net_dev_xmit event."""
        time = event['time']
        skbaddr = event['skbaddr']
        rc = event['rc']
        if rc == 0:  # NETDEV_TX_OK
            for i, skb in enumerate(self.tx_queue_list):
                if skb['skbaddr'] == skbaddr:
                    skb['xmit_t'] = time
                    self.tx_xmit_list.insert(0, skb)
                    del self.tx_queue_list[i]
                    if len(self.tx_xmit_list) > self.buffer_budget:
                        self.tx_xmit_list.pop()
                        self.of_count_tx_xmit_list += 1
                    return

    def handle_kfree_skb(self, event: dict) -> None:
        """Handle skb:kfree_skb event."""
        time = event['time']
        skbaddr = event['skbaddr']
        comm = event['comm']
        pid = event['pid']
        location = event['location']
        for i, skb in enumerate(self.tx_queue_list):
            if skb['skbaddr'] == skbaddr:
                del self.tx_queue_list[i]
                return
        for i, skb in enumerate(self.tx_xmit_list):
            if skb['skbaddr'] == skbaddr:
                skb['free_t'] = time
                self.tx_free_list.append(skb)
                del self.tx_xmit_list[i]
                return
        for i, rec_data in enumerate(self.rx_skb_list):
            if rec_data['skbaddr'] == skbaddr:
                rec_data.update({'handle': "kfree_skb",
                                 'comm': comm, 'pid': pid, 'comm_t': time, 'location': location})
                del self.rx_skb_list[i]
                return

    def handle_consume_skb(self, event: dict) -> None:
        """Handle skb:consume_skb event."""
        time = event['time']
        skbaddr = event['skbaddr']
        for i, skb in enumerate(self.tx_xmit_list):
            if skb['skbaddr'] == skbaddr:
                skb['free_t'] = time
                self.tx_free_list.append(skb)
                del self.tx_xmit_list[i]
                return
        for i, rec_data in enumerate(self.rx_skb_list):
            if rec_data['skbaddr'] == skbaddr:
                rec_data.update({'handle': "consume_skb", 'comm_t': time})
                del self.rx_skb_list[i]
                return

    def handle_skb_copy_datagram_iovec(self, event: dict) -> None:
        """Handle skb:skb_copy_datagram_iovec event."""
        time = event['time']
        skbaddr = event['skbaddr']
        comm = event['comm']
        pid = event['pid']
        for i, rec_data in enumerate(self.rx_skb_list):
            if skbaddr == rec_data['skbaddr']:
                rec_data.update({'handle': "skb_copy_datagram_iovec",
                                 'comm': comm, 'pid': pid, 'comm_t': time})
                del self.rx_skb_list[i]
                return

    def print_summary(self) -> None:
        """Print charts."""

        # display receive hunks
        if self.show_rx:
            for hunk in self.receive_hunk_list:
                self.print_receive(hunk)

        # display transmit hunks
        if self.show_tx:
            print("   dev    len      Qdisc        "
                  "       netdevice             free")
            for hunk in self.tx_free_list:
                self.print_transmit(hunk)

        if self.debug:
            print("debug buffer status")
            print("----------------------------")
            print(f"xmit Qdisc:remain:{len(self.tx_queue_list)} "
                  f"overflow:{self.of_count_tx_queue_list}")
            print(f"xmit netdevice:remain:{len(self.tx_xmit_list)} "
                  f"overflow:{self.of_count_tx_xmit_list}")
            print(f"receive:remain:{len(self.rx_skb_list)} "
                  f"overflow:{self.of_count_rx_skb_list}")

    def handle_single_event(self, event: dict) -> None:
        """Handle a single processed event."""
        name = event['name']
        if name == 'irq:softirq_exit':
            self.handle_irq_softirq_exit(event)
        elif name == 'irq:softirq_entry':
            self.handle_irq_softirq_entry(event)
        elif name == 'irq:softirq_raise':
            self.handle_irq_softirq_raise(event)
        elif name == 'irq:irq_handler_entry':
            self.handle_irq_handler_entry(event)
        elif name == 'irq:irq_handler_exit':
            self.handle_irq_handler_exit(event)
        elif name == 'napi:napi_poll':
            self.handle_napi_poll(event)
        elif name == 'net:netif_receive_skb':
            self.handle_netif_receive_skb(event)
        elif name == 'net:netif_rx':
            self.handle_netif_rx(event)
        elif name == 'skb:skb_copy_datagram_iovec':
            self.handle_skb_copy_datagram_iovec(event)
        elif name == 'net:net_dev_queue':
            self.handle_net_dev_queue(event)
        elif name == 'net:net_dev_xmit':
            self.handle_net_dev_xmit(event)
        elif name == 'skb:kfree_skb':
            self.handle_kfree_skb(event)
        elif name == 'skb:consume_skb':
            self.handle_consume_skb(event)

    def process_event(self, sample: perf.sample_event) -> None:
        """Process events directly on-the-fly."""
        name = str(sample.evsel)
        ev_name = name[6:-1] if name.startswith("evsel(") else name
        pid = sample.sample_pid
        if hasattr(self, 'session') and self.session:
            try:
                thread = self.session.find_thread(sample.sample_pid, sample.sample_tid)
                comm = (thread.comm() if thread else None) or "[unknown]"
            except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError):
                comm = "[unknown]"
        else:
            comm = "Unknown"
        event_data = {
            'name': ev_name,
            'time': sample.sample_time,
            'cpu': sample.sample_cpu,
            'pid': pid,
            'comm': comm,
        }

        # Extract specific fields based on event type
        if ev_name.startswith("irq:softirq_"):
            event_data['vec'] = getattr(sample, "vec", 0)
            # Filter for NET_RX
            if event_data['vec'] != 3:  # NET_RX_SOFTIRQ is usually 3
                return
        elif ev_name == "irq:irq_handler_entry":
            event_data['irq'] = getattr(sample, "irq", -1)
            event_data['irq_name'] = getattr(sample, "name", "[unknown]")
        elif ev_name == "irq:irq_handler_exit":
            event_data['irq'] = getattr(sample, "irq", -1)
            event_data['ret'] = getattr(sample, "ret", 0)
        elif ev_name == "napi:napi_poll":
            event_data['napi'] = getattr(sample, "napi", 0)
            event_data['dev_name'] = getattr(sample, "dev_name", "[unknown]")
            event_data['work'] = getattr(sample, "work", 0)
            event_data['budget'] = getattr(sample, "budget", 0)
        elif ev_name in ("net:netif_receive_skb", "net:netif_rx",
                         "net:net_dev_queue"):
            event_data['skbaddr'] = getattr(sample, "skbaddr", 0)
            event_data['skblen'] = getattr(sample, "len", 0)
            event_data['dev_name'] = getattr(sample, "name", "[unknown]")
        elif ev_name == "net:net_dev_xmit":
            event_data['skbaddr'] = getattr(sample, "skbaddr", 0)
            event_data['skblen'] = getattr(sample, "len", 0)
            event_data['rc'] = getattr(sample, "rc", 0)
            event_data['dev_name'] = getattr(sample, "name", "[unknown]")
        elif ev_name == "skb:kfree_skb":
            event_data['skbaddr'] = getattr(sample, "skbaddr", 0)
            event_data['location'] = getattr(sample, "location", 0)
            event_data['protocol'] = getattr(sample, "protocol", 0)
            event_data['reason'] = getattr(sample, "reason", 0)
        elif ev_name == "skb:consume_skb":
            event_data['skbaddr'] = getattr(sample, "skbaddr", 0)
            event_data['location'] = getattr(sample, "location", 0)
        elif ev_name == "skb:skb_copy_datagram_iovec":
            event_data['skbaddr'] = getattr(sample, "skbaddr", 0)
            event_data['skblen'] = getattr(sample, "len", 0)

        self.handle_single_event(event_data)


NetdevTimes = NetDevTimesAnalyzer


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Display a process of packets and processed time.")
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    ap.add_argument("--tx", action="store_true", help="show only tx chart")
    ap.add_argument("--rx", action="store_true", help="show only rx chart")
    ap.add_argument("--dev", default=None, help="show only specified device")
    ap.add_argument("--debug", action="store_true",
                    help="work with debug mode. It shows buffer status.")
    ap.add_argument("positionals", nargs="*",
                    help="optional positional arguments (tx, rx, dev=<name>, debug)")
    args, unknown_args = ap.parse_known_args()

    parsed_args = argparse.Namespace(
        tx=args.tx, rx=args.rx, dev=args.dev, debug=args.debug, input=args.input
    )

    for arg in list(args.positionals) + unknown_args:
        if not arg or not isinstance(arg, str):
            continue
        if arg in ('tx', '--tx'):
            parsed_args.tx = True
        elif arg in ('rx', '--rx'):
            parsed_args.rx = True
        elif arg.startswith('dev='):
            parsed_args.dev = arg[4:]
        elif arg.startswith('--dev='):
            parsed_args.dev = arg[6:]
        elif arg in ('debug', '--debug'):
            parsed_args.debug = True

    analyzer = NetDevTimesAnalyzer(parsed_args)

    try:
        try:
            analyzer.session = perf.session(
                perf.data(parsed_args.input), sample=analyzer.process_event
            )
            try:
                analyzer.session.process_events()
            except KeyboardInterrupt:
                pass
        finally:
            # Break the reference cycle between analyzer.session and the bound
            # analyzer.process_event callback so the C perf.session object is freed.
            analyzer.session = None
        analyzer.print_summary()
    except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError) as e:
        print(f"Error processing events: {e}")
        sys.exit(1)
