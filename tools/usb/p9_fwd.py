#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import argparse
import errno
import logging
import socket
import struct
import time

import usb.core
import usb.util


def path_from_usb_dev(dev):
    """Takes a pyUSB device as argument and returns a string.
    The string is a Path representation of the position of the USB device on the USB bus tree.

    This path is used to find a USB device on the bus or all devices connected to a HUB.
    The path is made up of the number of the USB controller followed be the ports of the HUB tree."""
    if dev.port_numbers:
        dev_path = ".".join(str(i) for i in dev.port_numbers)
        return f"{dev.bus}-{dev_path}"
    return ""


HEXDUMP_FILTER = "".join(chr(x).isprintable() and chr(x) or "." for x in range(128)) + "." * 128


class Forwarder:
    @staticmethod
    def _log_hexdump(data):
        if not logging.root.isEnabledFor(logging.TRACE):
            return
        L = 16
        for c in range(0, len(data), L):
            chars = data[c : c + L]
            dump = " ".join(f"{x:02x}" for x in chars)
            printable = "".join(HEXDUMP_FILTER[x] for x in chars)
            line = f"{c:08x}  {dump:{L*3}s} |{printable:{L}s}|"
            logging.root.log(logging.TRACE, "%s", line)

    def __init__(self, server, vid, pid, path):
        self.stats = {
            "c2s packets": 0,
            "c2s bytes": 0,
            "s2c packets": 0,
            "s2c bytes": 0,
        }
        self.stats_logged = time.monotonic()

        def find_filter(dev):
            dev_path = path_from_usb_dev(dev)
            if path is not None:
                return dev_path == path
            return True

        dev = usb.core.find(idVendor=vid, idProduct=pid, custom_match=find_filter)
        if dev is None:
            raise ValueError("Device not found")

        logging.info(f"found device: {dev.bus}/{dev.address} located at {path_from_usb_dev(dev)}")

        # dev.set_configuration() is not necessary since g_multi has only one
        usb9pfs = None
        # g_multi adds 9pfs as last interface
        cfg = dev.get_active_configuration()
        for intf in cfg:
            # we have to detach the usb-storage driver from multi gadget since
            # stall option could be set, which will lead to spontaneous port
            # resets and our transfers will run dead
            if intf.bInterfaceClass == 0x08:
                if dev.is_kernel_driver_active(intf.bInterfaceNumber):
                    dev.detach_kernel_driver(intf.bInterfaceNumber)

            if intf.bInterfaceClass == 0xFF and intf.bInterfaceSubClass == 0xFF and intf.bInterfaceProtocol == 0x09:
                usb9pfs = intf
        if usb9pfs is None:
            raise ValueError("Interface not found")

        logging.info(f"claiming interface:\n{usb9pfs}")
        usb.util.claim_interface(dev, usb9pfs.bInterfaceNumber)
        ep_out = usb.util.find_descriptor(
            usb9pfs,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT,
        )
        assert ep_out is not None
        ep_in = usb.util.find_descriptor(
            usb9pfs,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN,
        )
        assert ep_in is not None
        logging.info("interface claimed")

        self.ep_out = ep_out
        self.ep_in = ep_in
        self.dev = dev

        # create and connect socket
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.connect(server)

        logging.info("connected to server")

    def c2s(self):
        """forward a request from the USB client to the TCP server"""
        data = None
        while data is None:
            try:
                logging.log(logging.TRACE, "c2s: reading")
                data = self.ep_in.read(self.ep_in.wMaxPacketSize)
            except usb.core.USBTimeoutError:
                logging.log(logging.TRACE, "c2s: reading timed out")
                continue
            except usb.core.USBError as e:
                if e.errno == errno.EIO:
                    logging.debug("c2s: reading failed with %s, retrying", repr(e))
                    time.sleep(0.5)
                    continue
                logging.error("c2s: reading failed with %s, aborting", repr(e))
                raise
        size = struct.unpack("<I", data[:4])[0]
        while len(data) < size:
            data += self.ep_in.read(size - len(data))
        logging.log(logging.TRACE, "c2s: writing")
        self._log_hexdump(data)
        self.s.send(data)
        logging.debug("c2s: forwarded %i bytes", size)
        self.stats["c2s packets"] += 1
        self.stats["c2s bytes"] += size

    def s2c(self):
        """forward a response from the TCP server to the USB client"""
        logging.log(logging.TRACE, "s2c: reading")
        data = self.s.recv(4)
        size = struct.unpack("<I", data[:4])[0]
        while len(data) < size:
            data += self.s.recv(size - len(data))
        logging.log(logging.TRACE, "s2c: writing")
        self._log_hexdump(data)
        while data:
            written = self.ep_out.write(data)
            assert written > 0
            data = data[written:]
        if size % self.ep_out.wMaxPacketSize == 0:
            logging.log(logging.TRACE, "sending zero length packet")
            self.ep_out.write(b"")
        logging.debug("s2c: forwarded %i bytes", size)
        self.stats["s2c packets"] += 1
        self.stats["s2c bytes"] += size

    def log_stats(self):
        logging.info("statistics:")
        for k, v in self.stats.items():
            logging.info(f"  {k+':':14s} {v}")

    def log_stats_interval(self, interval=5):
        if (time.monotonic() - self.stats_logged) < interval:
            return

        self.log_stats()
        self.stats_logged = time.monotonic()


def try_get_usb_str(dev, name):
    try:
        with open(f"/sys/bus/usb/devices/{dev.bus}-{dev.address}/{name}") as f:
            return f.read().strip()
    except FileNotFoundError:
        return None


def list_usb(args):
    vid, pid = [int(x, 16) for x in args.id.split(":", 1)]

    print("Bus | Addr | Manufacturer     | Product          | ID        | Path")
    print("--- | ---- | ---------------- | ---------------- | --------- | ----")
    for dev in usb.core.find(find_all=True, idVendor=vid, idProduct=pid):
        path = path_from_usb_dev(dev) or ""
        manufacturer = try_get_usb_str(dev, "manufacturer") or "unknown"
        product = try_get_usb_str(dev, "product") or "unknown"
        print(
            f"{dev.bus:3} | {dev.address:4} | {manufacturer:16} | {product:16} | {dev.idVendor:04x}:{dev.idProduct:04x} | {path:18}"
        )


def connect(args):
    vid, pid = [int(x, 16) for x in args.id.split(":", 1)]

    f = Forwarder(server=(args.server, args.port), vid=vid, pid=pid, path=args.path)

    try:
        while True:
            f.c2s()
            f.s2c()
            f.log_stats_interval()
    finally:
        f.log_stats()


def main():
    parser = argparse.ArgumentParser(
        description="Forward 9PFS requests from USB to TCP",
    )

    parser.add_argument("--id", type=str, default="1d6b:0109", help="vid:pid of target device")
    parser.add_argument("--path", type=str, required=False, help="path of target device")
    parser.add_argument("-v", "--verbose", action="count", default=0)

    subparsers = parser.add_subparsers()
    subparsers.required = True
    subparsers.dest = "command"

    parser_list = subparsers.add_parser("list", help="List all connected 9p gadgets")
    parser_list.set_defaults(func=list_usb)

    parser_connect = subparsers.add_parser(
        "connect", help="Forward messages between the usb9pfs gadget and the 9p server"
    )
    parser_connect.set_defaults(func=connect)
    connect_group = parser_connect.add_argument_group()
    connect_group.required = True
    parser_connect.add_argument("-s", "--server", type=str, default="127.0.0.1", help="server hostname")
    parser_connect.add_argument("-p", "--port", type=int, default=564, help="server port")

    args = parser.parse_args()

    logging.TRACE = logging.DEBUG - 5
    logging.addLevelName(logging.TRACE, "TRACE")

    if args.verbose >= 2:
        level = logging.TRACE
    elif args.verbose:
        level = logging.DEBUG
    else:
        level = logging.INFO
    logging.basicConfig(level=level, format="%(asctime)-15s %(levelname)-8s %(message)s")

    args.func(args)


if __name__ == "__main__":
    main()
