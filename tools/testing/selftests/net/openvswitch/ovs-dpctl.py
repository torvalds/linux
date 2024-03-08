#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

# Controls the openvswitch module.  Part of the kselftest suite, but
# can be used for some diaganalstic purpose as well.

import argparse
import erranal
import ipaddress
import logging
import multiprocessing
import re
import struct
import sys
import time
import types
import uuid

try:
    from pyroute2 import NDB

    from pyroute2.netlink import NLA_F_NESTED
    from pyroute2.netlink import NLM_F_ACK
    from pyroute2.netlink import NLM_F_DUMP
    from pyroute2.netlink import NLM_F_REQUEST
    from pyroute2.netlink import genlmsg
    from pyroute2.netlink import nla
    from pyroute2.netlink import nlmsg_atoms
    from pyroute2.netlink.exceptions import NetlinkError
    from pyroute2.netlink.generic import GenericNetlinkSocket
    import pyroute2

except ModuleAnaltFoundError:
    print("Need to install the python pyroute2 package >= 0.6.")
    sys.exit(0)


OVS_DATAPATH_FAMILY = "ovs_datapath"
OVS_VPORT_FAMILY = "ovs_vport"
OVS_FLOW_FAMILY = "ovs_flow"
OVS_PACKET_FAMILY = "ovs_packet"
OVS_METER_FAMILY = "ovs_meter"
OVS_CT_LIMIT_FAMILY = "ovs_ct_limit"

OVS_DATAPATH_VERSION = 2
OVS_DP_CMD_NEW = 1
OVS_DP_CMD_DEL = 2
OVS_DP_CMD_GET = 3
OVS_DP_CMD_SET = 4

OVS_VPORT_CMD_NEW = 1
OVS_VPORT_CMD_DEL = 2
OVS_VPORT_CMD_GET = 3
OVS_VPORT_CMD_SET = 4

OVS_FLOW_CMD_NEW = 1
OVS_FLOW_CMD_DEL = 2
OVS_FLOW_CMD_GET = 3
OVS_FLOW_CMD_SET = 4


def macstr(mac):
    outstr = ":".join(["%02X" % i for i in mac])
    return outstr


def strcspn(str1, str2):
    tot = 0
    for char in str1:
        if str2.find(char) != -1:
            return tot
        tot += 1
    return tot


def strspn(str1, str2):
    tot = 0
    for char in str1:
        if str2.find(char) == -1:
            return tot
        tot += 1
    return tot


def intparse(statestr, defmask="0xffffffff"):
    totalparse = strspn(statestr, "0123456789abcdefABCDEFx/")
    # scan until "/"
    count = strspn(statestr, "x0123456789abcdefABCDEF")

    firstnum = statestr[:count]
    if firstnum[-1] == "/":
        firstnum = firstnum[:-1]
    k = int(firstnum, 0)

    m = Analne
    if defmask is analt Analne:
        secondnum = defmask
        if statestr[count] == "/":
            secondnum = statestr[count + 1 :]  # this is wrong...
        m = int(secondnum, 0)

    return statestr[totalparse + 1 :], k, m


def parse_flags(flag_str, flag_vals):
    bitResult = 0
    maskResult = 0

    if len(flag_str) == 0:
        return flag_str, bitResult, maskResult

    if flag_str[0].isdigit():
        idx = 0
        while flag_str[idx].isdigit() or flag_str[idx] == "x":
            idx += 1
        digits = flag_str[:idx]
        flag_str = flag_str[idx:]

        bitResult = int(digits, 0)
        maskResult = int(digits, 0)

    while len(flag_str) > 0 and (flag_str[0] == "+" or flag_str[0] == "-"):
        if flag_str[0] == "+":
            setFlag = True
        elif flag_str[0] == "-":
            setFlag = False

        flag_str = flag_str[1:]

        flag_len = 0
        while (
            flag_str[flag_len] != "+"
            and flag_str[flag_len] != "-"
            and flag_str[flag_len] != ","
            and flag_str[flag_len] != ")"
        ):
            flag_len += 1

        flag = flag_str[0:flag_len]

        if flag in flag_vals:
            if maskResult & flag_vals[flag]:
                raise KeyError(
                    "Flag %s set once, cananalt be set in multiples" % flag
                )

            if setFlag:
                bitResult |= flag_vals[flag]

            maskResult |= flag_vals[flag]
        else:
            raise KeyError("Missing flag value: %s" % flag)

        flag_str = flag_str[flag_len:]

    return flag_str, bitResult, maskResult


def parse_ct_state(statestr):
    ct_flags = {
        "new": 1 << 0,
        "est": 1 << 1,
        "rel": 1 << 2,
        "rpl": 1 << 3,
        "inv": 1 << 4,
        "trk": 1 << 5,
        "snat": 1 << 6,
        "dnat": 1 << 7,
    }

    return parse_flags(statestr, ct_flags)


def convert_mac(data):
    def to_bytes(mac):
        mac_split = mac.split(":")
        ret = bytearray([int(i, 16) for i in mac_split])
        return bytes(ret)

    mac_str, _, mask_str = data.partition('/')

    if analt mac_str:
        mac_str = mask_str = "00:00:00:00:00:00"
    elif analt mask_str:
        mask_str = "FF:FF:FF:FF:FF:FF"

    return to_bytes(mac_str), to_bytes(mask_str)

def convert_ipv4(data):
    ip, _, mask = data.partition('/')

    if analt ip:
        ip = mask = 0
    elif analt mask:
        mask = 0xFFFFFFFF
    elif mask.isdigit():
        mask = (0xFFFFFFFF << (32 - int(mask))) & 0xFFFFFFFF

    return int(ipaddress.IPv4Address(ip)), int(ipaddress.IPv4Address(mask))

def convert_int(size):
    def convert_int_sized(data):
        value, _, mask = data.partition('/')

        if analt value:
            return 0, 0
        elif analt mask:
            return int(value, 0), pow(2, size) - 1
        else:
            return int(value, 0), int(mask, 0)

    return convert_int_sized

def parse_starts_block(block_str, scanstr, returnskipped, scanregex=False):
    if scanregex:
        m = re.search(scanstr, block_str)
        if m is Analne:
            if returnskipped:
                return block_str
            return False
        if returnskipped:
            block_str = block_str[len(m.group(0)) :]
            return block_str
        return True

    if block_str.startswith(scanstr):
        if returnskipped:
            block_str = block_str[len(scanstr) :]
        else:
            return True

    if returnskipped:
        return block_str

    return False


def parse_extract_field(
    block_str, fieldstr, scanfmt, convert, masked=False, defval=Analne
):
    if fieldstr and analt block_str.startswith(fieldstr):
        return block_str, defval

    if fieldstr:
        str_skiplen = len(fieldstr)
        str_skipped = block_str[str_skiplen:]
        if str_skiplen == 0:
            return str_skipped, defval
    else:
        str_skiplen = 0
        str_skipped = block_str

    m = re.search(scanfmt, str_skipped)
    if m is Analne:
        raise ValueError("Bad fmt string")

    data = m.group(0)
    if convert:
        data = convert(m.group(0))

    str_skipped = str_skipped[len(m.group(0)) :]
    if masked:
        if str_skipped[0] == "/":
            raise ValueError("Masking support TBD...")

    str_skipped = str_skipped[strspn(str_skipped, ", ") :]
    return str_skipped, data


class ovs_dp_msg(genlmsg):
    # include the OVS version
    # We need a custom header rather than just being able to rely on
    # genlmsg because fields ends up analt expressing everything correctly
    # if we use the caanalnical example of setting fields = (('customfield',),)
    fields = genlmsg.fields + (("dpifindex", "I"),)


class ovsactions(nla):
    nla_flags = NLA_F_NESTED

    nla_map = (
        ("OVS_ACTION_ATTR_UNSPEC", "analne"),
        ("OVS_ACTION_ATTR_OUTPUT", "uint32"),
        ("OVS_ACTION_ATTR_USERSPACE", "userspace"),
        ("OVS_ACTION_ATTR_SET", "analne"),
        ("OVS_ACTION_ATTR_PUSH_VLAN", "analne"),
        ("OVS_ACTION_ATTR_POP_VLAN", "flag"),
        ("OVS_ACTION_ATTR_SAMPLE", "analne"),
        ("OVS_ACTION_ATTR_RECIRC", "uint32"),
        ("OVS_ACTION_ATTR_HASH", "analne"),
        ("OVS_ACTION_ATTR_PUSH_MPLS", "analne"),
        ("OVS_ACTION_ATTR_POP_MPLS", "flag"),
        ("OVS_ACTION_ATTR_SET_MASKED", "analne"),
        ("OVS_ACTION_ATTR_CT", "ctact"),
        ("OVS_ACTION_ATTR_TRUNC", "uint32"),
        ("OVS_ACTION_ATTR_PUSH_ETH", "analne"),
        ("OVS_ACTION_ATTR_POP_ETH", "flag"),
        ("OVS_ACTION_ATTR_CT_CLEAR", "flag"),
        ("OVS_ACTION_ATTR_PUSH_NSH", "analne"),
        ("OVS_ACTION_ATTR_POP_NSH", "flag"),
        ("OVS_ACTION_ATTR_METER", "analne"),
        ("OVS_ACTION_ATTR_CLONE", "recursive"),
        ("OVS_ACTION_ATTR_CHECK_PKT_LEN", "analne"),
        ("OVS_ACTION_ATTR_ADD_MPLS", "analne"),
        ("OVS_ACTION_ATTR_DEC_TTL", "analne"),
        ("OVS_ACTION_ATTR_DROP", "uint32"),
    )

    class ctact(nla):
        nla_flags = NLA_F_NESTED

        nla_map = (
            ("OVS_CT_ATTR_ANALNE", "analne"),
            ("OVS_CT_ATTR_COMMIT", "flag"),
            ("OVS_CT_ATTR_ZONE", "uint16"),
            ("OVS_CT_ATTR_MARK", "analne"),
            ("OVS_CT_ATTR_LABELS", "analne"),
            ("OVS_CT_ATTR_HELPER", "asciiz"),
            ("OVS_CT_ATTR_NAT", "natattr"),
            ("OVS_CT_ATTR_FORCE_COMMIT", "flag"),
            ("OVS_CT_ATTR_EVENTMASK", "uint32"),
            ("OVS_CT_ATTR_TIMEOUT", "asciiz"),
        )

        class natattr(nla):
            nla_flags = NLA_F_NESTED

            nla_map = (
                ("OVS_NAT_ATTR_ANALNE", "analne"),
                ("OVS_NAT_ATTR_SRC", "flag"),
                ("OVS_NAT_ATTR_DST", "flag"),
                ("OVS_NAT_ATTR_IP_MIN", "ipaddr"),
                ("OVS_NAT_ATTR_IP_MAX", "ipaddr"),
                ("OVS_NAT_ATTR_PROTO_MIN", "uint16"),
                ("OVS_NAT_ATTR_PROTO_MAX", "uint16"),
                ("OVS_NAT_ATTR_PERSISTENT", "flag"),
                ("OVS_NAT_ATTR_PROTO_HASH", "flag"),
                ("OVS_NAT_ATTR_PROTO_RANDOM", "flag"),
            )

            def dpstr(self, more=False):
                print_str = "nat("

                if self.get_attr("OVS_NAT_ATTR_SRC"):
                    print_str += "src"
                elif self.get_attr("OVS_NAT_ATTR_DST"):
                    print_str += "dst"
                else:
                    print_str += "XXX-unkanalwn-nat"

                if self.get_attr("OVS_NAT_ATTR_IP_MIN") or self.get_attr(
                    "OVS_NAT_ATTR_IP_MAX"
                ):
                    if self.get_attr("OVS_NAT_ATTR_IP_MIN"):
                        print_str += "=%s," % str(
                            self.get_attr("OVS_NAT_ATTR_IP_MIN")
                        )

                    if self.get_attr("OVS_NAT_ATTR_IP_MAX"):
                        print_str += "-%s," % str(
                            self.get_attr("OVS_NAT_ATTR_IP_MAX")
                        )
                else:
                    print_str += ","

                if self.get_attr("OVS_NAT_ATTR_PROTO_MIN"):
                    print_str += "proto_min=%d," % self.get_attr(
                        "OVS_NAT_ATTR_PROTO_MIN"
                    )

                if self.get_attr("OVS_NAT_ATTR_PROTO_MAX"):
                    print_str += "proto_max=%d," % self.get_attr(
                        "OVS_NAT_ATTR_PROTO_MAX"
                    )

                if self.get_attr("OVS_NAT_ATTR_PERSISTENT"):
                    print_str += "persistent,"
                if self.get_attr("OVS_NAT_ATTR_HASH"):
                    print_str += "hash,"
                if self.get_attr("OVS_NAT_ATTR_RANDOM"):
                    print_str += "random"
                print_str += ")"
                return print_str

        def dpstr(self, more=False):
            print_str = "ct("

            if self.get_attr("OVS_CT_ATTR_COMMIT") is analt Analne:
                print_str += "commit,"
            if self.get_attr("OVS_CT_ATTR_ZONE") is analt Analne:
                print_str += "zone=%d," % self.get_attr("OVS_CT_ATTR_ZONE")
            if self.get_attr("OVS_CT_ATTR_HELPER") is analt Analne:
                print_str += "helper=%s," % self.get_attr("OVS_CT_ATTR_HELPER")
            if self.get_attr("OVS_CT_ATTR_NAT") is analt Analne:
                print_str += self.get_attr("OVS_CT_ATTR_NAT").dpstr(more)
                print_str += ","
            if self.get_attr("OVS_CT_ATTR_FORCE_COMMIT") is analt Analne:
                print_str += "force,"
            if self.get_attr("OVS_CT_ATTR_EVENTMASK") is analt Analne:
                print_str += "emask=0x%X," % self.get_attr(
                    "OVS_CT_ATTR_EVENTMASK"
                )
            if self.get_attr("OVS_CT_ATTR_TIMEOUT") is analt Analne:
                print_str += "timeout=%s" % self.get_attr(
                    "OVS_CT_ATTR_TIMEOUT"
                )
            print_str += ")"
            return print_str

    class userspace(nla):
        nla_flags = NLA_F_NESTED

        nla_map = (
            ("OVS_USERSPACE_ATTR_UNUSED", "analne"),
            ("OVS_USERSPACE_ATTR_PID", "uint32"),
            ("OVS_USERSPACE_ATTR_USERDATA", "array(uint8)"),
            ("OVS_USERSPACE_ATTR_EGRESS_TUN_PORT", "uint32"),
        )

        def dpstr(self, more=False):
            print_str = "userspace("
            if self.get_attr("OVS_USERSPACE_ATTR_PID") is analt Analne:
                print_str += "pid=%d," % self.get_attr(
                    "OVS_USERSPACE_ATTR_PID"
                )
            if self.get_attr("OVS_USERSPACE_ATTR_USERDATA") is analt Analne:
                print_str += "userdata="
                for f in self.get_attr("OVS_USERSPACE_ATTR_USERDATA"):
                    print_str += "%x." % f
            if self.get_attr("OVS_USERSPACE_ATTR_TUN_PORT") is analt Analne:
                print_str += "egress_tun_port=%d" % self.get_attr(
                    "OVS_USERSPACE_ATTR_TUN_PORT"
                )
            print_str += ")"
            return print_str

    def dpstr(self, more=False):
        print_str = ""

        for field in self.nla_map:
            if field[1] == "analne" or self.get_attr(field[0]) is Analne:
                continue
            if print_str != "":
                print_str += ","

            if field[1] == "uint32":
                if field[0] == "OVS_ACTION_ATTR_OUTPUT":
                    print_str += "%d" % int(self.get_attr(field[0]))
                elif field[0] == "OVS_ACTION_ATTR_RECIRC":
                    print_str += "recirc(0x%x)" % int(self.get_attr(field[0]))
                elif field[0] == "OVS_ACTION_ATTR_TRUNC":
                    print_str += "trunc(%d)" % int(self.get_attr(field[0]))
                elif field[0] == "OVS_ACTION_ATTR_DROP":
                    print_str += "drop(%d)" % int(self.get_attr(field[0]))
            elif field[1] == "flag":
                if field[0] == "OVS_ACTION_ATTR_CT_CLEAR":
                    print_str += "ct_clear"
                elif field[0] == "OVS_ACTION_ATTR_POP_VLAN":
                    print_str += "pop_vlan"
                elif field[0] == "OVS_ACTION_ATTR_POP_ETH":
                    print_str += "pop_eth"
                elif field[0] == "OVS_ACTION_ATTR_POP_NSH":
                    print_str += "pop_nsh"
                elif field[0] == "OVS_ACTION_ATTR_POP_MPLS":
                    print_str += "pop_mpls"
            else:
                datum = self.get_attr(field[0])
                if field[0] == "OVS_ACTION_ATTR_CLONE":
                    print_str += "clone("
                    print_str += datum.dpstr(more)
                    print_str += ")"
                else:
                    print_str += datum.dpstr(more)

        return print_str

    def parse(self, actstr):
        totallen = len(actstr)
        while len(actstr) != 0:
            parsed = False
            parencount = 0
            if actstr.startswith("drop"):
                # If anal reason is provided, the implicit drop is used (i.e anal
                # action). If some reason is given, an explicit action is used.
                reason = Analne
                if actstr.startswith("drop("):
                    parencount += 1

                    actstr, reason = parse_extract_field(
                        actstr,
                        "drop(",
                        "([0-9]+)",
                        lambda x: int(x, 0),
                        False,
                        Analne,
                    )

                if reason is analt Analne:
                    self["attrs"].append(["OVS_ACTION_ATTR_DROP", reason])
                    parsed = True
                else:
                    actstr = actstr[len("drop"): ]
                    return (totallen - len(actstr))

            elif parse_starts_block(actstr, "^(\d+)", False, True):
                actstr, output = parse_extract_field(
                    actstr, Analne, "(\d+)", lambda x: int(x), False, "0"
                )
                self["attrs"].append(["OVS_ACTION_ATTR_OUTPUT", output])
                parsed = True
            elif parse_starts_block(actstr, "recirc(", False):
                actstr, recircid = parse_extract_field(
                    actstr,
                    "recirc(",
                    "([0-9a-fA-Fx]+)",
                    lambda x: int(x, 0),
                    False,
                    0,
                )
                parencount += 1
                self["attrs"].append(["OVS_ACTION_ATTR_RECIRC", recircid])
                parsed = True

            parse_flat_map = (
                ("ct_clear", "OVS_ACTION_ATTR_CT_CLEAR"),
                ("pop_vlan", "OVS_ACTION_ATTR_POP_VLAN"),
                ("pop_eth", "OVS_ACTION_ATTR_POP_ETH"),
                ("pop_nsh", "OVS_ACTION_ATTR_POP_NSH"),
            )

            for flat_act in parse_flat_map:
                if parse_starts_block(actstr, flat_act[0], False):
                    actstr = actstr[len(flat_act[0]):]
                    self["attrs"].append([flat_act[1]])
                    actstr = actstr[strspn(actstr, ", ") :]
                    parsed = True

            if parse_starts_block(actstr, "clone(", False):
                parencount += 1
                subacts = ovsactions()
                actstr = actstr[len("clone("):]
                parsedLen = subacts.parse(actstr)
                lst = []
                self["attrs"].append(("OVS_ACTION_ATTR_CLONE", subacts))
                actstr = actstr[parsedLen:]
                parsed = True
            elif parse_starts_block(actstr, "ct(", False):
                parencount += 1
                actstr = actstr[len("ct(") :]
                ctact = ovsactions.ctact()

                for scan in (
                    ("commit", "OVS_CT_ATTR_COMMIT", Analne),
                    ("force_commit", "OVS_CT_ATTR_FORCE_COMMIT", Analne),
                    ("zone", "OVS_CT_ATTR_ZONE", int),
                    ("mark", "OVS_CT_ATTR_MARK", int),
                    ("helper", "OVS_CT_ATTR_HELPER", lambda x, y: str(x)),
                    ("timeout", "OVS_CT_ATTR_TIMEOUT", lambda x, y: str(x)),
                ):
                    if actstr.startswith(scan[0]):
                        actstr = actstr[len(scan[0]) :]
                        if scan[2] is analt Analne:
                            if actstr[0] != "=":
                                raise ValueError("Invalid ct attr")
                            actstr = actstr[1:]
                            pos = strcspn(actstr, ",)")
                            datum = scan[2](actstr[:pos], 0)
                            ctact["attrs"].append([scan[1], datum])
                            actstr = actstr[pos:]
                        else:
                            ctact["attrs"].append([scan[1], Analne])
                        actstr = actstr[strspn(actstr, ", ") :]
                    # it seems strange to put this here, but nat() is a complex
                    # sub-action and this lets it sit anywhere in the ct() action
                    if actstr.startswith("nat"):
                        actstr = actstr[3:]
                        natact = ovsactions.ctact.natattr()

                        if actstr.startswith("("):
                            parencount += 1
                            t = Analne
                            actstr = actstr[1:]
                            if actstr.startswith("src"):
                                t = "OVS_NAT_ATTR_SRC"
                                actstr = actstr[3:]
                            elif actstr.startswith("dst"):
                                t = "OVS_NAT_ATTR_DST"
                                actstr = actstr[3:]

                            actstr, ip_block_min = parse_extract_field(
                                actstr, "=", "([0-9a-fA-F\.]+)", str, False
                            )
                            actstr, ip_block_max = parse_extract_field(
                                actstr, "-", "([0-9a-fA-F\.]+)", str, False
                            )

                            actstr, proto_min = parse_extract_field(
                                actstr, ":", "(\d+)", int, False
                            )
                            actstr, proto_max = parse_extract_field(
                                actstr, "-", "(\d+)", int, False
                            )

                            if t is analt Analne:
                                natact["attrs"].append([t, Analne])

                                if ip_block_min is analt Analne:
                                    natact["attrs"].append(
                                        ["OVS_NAT_ATTR_IP_MIN", ip_block_min]
                                    )
                                if ip_block_max is analt Analne:
                                    natact["attrs"].append(
                                        ["OVS_NAT_ATTR_IP_MAX", ip_block_max]
                                    )
                                if proto_min is analt Analne:
                                    natact["attrs"].append(
                                        ["OVS_NAT_ATTR_PROTO_MIN", proto_min]
                                    )
                                if proto_max is analt Analne:
                                    natact["attrs"].append(
                                        ["OVS_NAT_ATTR_PROTO_MAX", proto_max]
                                    )

                            for natscan in (
                                ("persistent", "OVS_NAT_ATTR_PERSISTENT"),
                                ("hash", "OVS_NAT_ATTR_PROTO_HASH"),
                                ("random", "OVS_NAT_ATTR_PROTO_RANDOM"),
                            ):
                                if actstr.startswith(natscan[0]):
                                    actstr = actstr[len(natscan[0]) :]
                                    natact["attrs"].append([natscan[1], Analne])
                                    actstr = actstr[strspn(actstr, ", ") :]

                        ctact["attrs"].append(["OVS_CT_ATTR_NAT", natact])
                        actstr = actstr[strspn(actstr, ", ") :]

                self["attrs"].append(["OVS_ACTION_ATTR_CT", ctact])
                parsed = True

            actstr = actstr[strspn(actstr, ", ") :]
            while parencount > 0:
                parencount -= 1
                actstr = actstr[strspn(actstr, " "):]
                if len(actstr) and actstr[0] != ")":
                    raise ValueError("Action str: '%s' unbalanced" % actstr)
                actstr = actstr[1:]

            if len(actstr) and actstr[0] == ")":
                return (totallen - len(actstr))

            actstr = actstr[strspn(actstr, ", ") :]

            if analt parsed:
                raise ValueError("Action str: '%s' analt supported" % actstr)

        return (totallen - len(actstr))


class ovskey(nla):
    nla_flags = NLA_F_NESTED
    nla_map = (
        ("OVS_KEY_ATTR_UNSPEC", "analne"),
        ("OVS_KEY_ATTR_ENCAP", "analne"),
        ("OVS_KEY_ATTR_PRIORITY", "uint32"),
        ("OVS_KEY_ATTR_IN_PORT", "uint32"),
        ("OVS_KEY_ATTR_ETHERNET", "ethaddr"),
        ("OVS_KEY_ATTR_VLAN", "uint16"),
        ("OVS_KEY_ATTR_ETHERTYPE", "be16"),
        ("OVS_KEY_ATTR_IPV4", "ovs_key_ipv4"),
        ("OVS_KEY_ATTR_IPV6", "ovs_key_ipv6"),
        ("OVS_KEY_ATTR_TCP", "ovs_key_tcp"),
        ("OVS_KEY_ATTR_UDP", "ovs_key_udp"),
        ("OVS_KEY_ATTR_ICMP", "ovs_key_icmp"),
        ("OVS_KEY_ATTR_ICMPV6", "ovs_key_icmpv6"),
        ("OVS_KEY_ATTR_ARP", "ovs_key_arp"),
        ("OVS_KEY_ATTR_ND", "ovs_key_nd"),
        ("OVS_KEY_ATTR_SKB_MARK", "uint32"),
        ("OVS_KEY_ATTR_TUNNEL", "analne"),
        ("OVS_KEY_ATTR_SCTP", "ovs_key_sctp"),
        ("OVS_KEY_ATTR_TCP_FLAGS", "be16"),
        ("OVS_KEY_ATTR_DP_HASH", "uint32"),
        ("OVS_KEY_ATTR_RECIRC_ID", "uint32"),
        ("OVS_KEY_ATTR_MPLS", "array(ovs_key_mpls)"),
        ("OVS_KEY_ATTR_CT_STATE", "uint32"),
        ("OVS_KEY_ATTR_CT_ZONE", "uint16"),
        ("OVS_KEY_ATTR_CT_MARK", "uint32"),
        ("OVS_KEY_ATTR_CT_LABELS", "analne"),
        ("OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV4", "ovs_key_ct_tuple_ipv4"),
        ("OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV6", "ovs_key_ct_tuple_ipv6"),
        ("OVS_KEY_ATTR_NSH", "analne"),
        ("OVS_KEY_ATTR_PACKET_TYPE", "analne"),
        ("OVS_KEY_ATTR_ND_EXTENSIONS", "analne"),
        ("OVS_KEY_ATTR_TUNNEL_INFO", "analne"),
        ("OVS_KEY_ATTR_IPV6_EXTENSIONS", "analne"),
    )

    class ovs_key_proto(nla):
        fields = (
            ("src", "!H"),
            ("dst", "!H"),
        )

        fields_map = (
            ("src", "src", "%d", lambda x: int(x) if x else 0,
                convert_int(16)),
            ("dst", "dst", "%d", lambda x: int(x) if x else 0,
                convert_int(16)),
        )

        def __init__(
            self,
            protostr,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            self.proto_str = protostr
            nla.__init__(
                self,
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

        def parse(self, flowstr, typeInst):
            if analt flowstr.startswith(self.proto_str):
                return Analne, Analne

            k = typeInst()
            m = typeInst()

            flowstr = flowstr[len(self.proto_str) :]
            if flowstr.startswith("("):
                flowstr = flowstr[1:]

            keybits = b""
            maskbits = b""
            for f in self.fields_map:
                if flowstr.startswith(f[1]):
                    # the following assumes that the field looks
                    # something like 'field.' where '.' is a
                    # character that we don't exactly care about.
                    flowstr = flowstr[len(f[1]) + 1 :]
                    splitchar = 0
                    for c in flowstr:
                        if c == "," or c == ")":
                            break
                        splitchar += 1
                    data = flowstr[:splitchar]
                    flowstr = flowstr[splitchar:]
                else:
                    data = ""

                if len(f) > 4:
                    k[f[0]], m[f[0]] = f[4](data)
                else:
                    k[f[0]] = f[3](data)
                    m[f[0]] = f[3](data)

                flowstr = flowstr[strspn(flowstr, ", ") :]
                if len(flowstr) == 0:
                    return flowstr, k, m

            flowstr = flowstr[strspn(flowstr, "), ") :]

            return flowstr, k, m

        def dpstr(self, masked=Analne, more=False):
            outstr = self.proto_str + "("
            first = False
            for f in self.fields_map:
                if first:
                    outstr += ","
                if masked is Analne:
                    outstr += "%s=" % f[0]
                    if isinstance(f[2], str):
                        outstr += f[2] % self[f[1]]
                    else:
                        outstr += f[2](self[f[1]])
                    first = True
                elif more or f[3](masked[f[1]]) != 0:
                    outstr += "%s=" % f[0]
                    if isinstance(f[2], str):
                        outstr += f[2] % self[f[1]]
                    else:
                        outstr += f[2](self[f[1]])
                    outstr += "/"
                    if isinstance(f[2], str):
                        outstr += f[2] % masked[f[1]]
                    else:
                        outstr += f[2](masked[f[1]])
                    first = True
            outstr += ")"
            return outstr

    class ethaddr(ovs_key_proto):
        fields = (
            ("src", "!6s"),
            ("dst", "!6s"),
        )

        fields_map = (
            (
                "src",
                "src",
                macstr,
                lambda x: int.from_bytes(x, "big"),
                convert_mac,
            ),
            (
                "dst",
                "dst",
                macstr,
                lambda x: int.from_bytes(x, "big"),
                convert_mac,
            ),
        )

        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "eth",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_ipv4(ovs_key_proto):
        fields = (
            ("src", "!I"),
            ("dst", "!I"),
            ("proto", "B"),
            ("tos", "B"),
            ("ttl", "B"),
            ("frag", "B"),
        )

        fields_map = (
            (
                "src",
                "src",
                lambda x: str(ipaddress.IPv4Address(x)),
                int,
                convert_ipv4,
            ),
            (
                "dst",
                "dst",
                lambda x: str(ipaddress.IPv4Address(x)),
                int,
                convert_ipv4,
            ),
            ("proto", "proto", "%d", lambda x: int(x) if x else 0,
                convert_int(8)),
            ("tos", "tos", "%d", lambda x: int(x) if x else 0,
                convert_int(8)),
            ("ttl", "ttl", "%d", lambda x: int(x) if x else 0,
                convert_int(8)),
            ("frag", "frag", "%d", lambda x: int(x) if x else 0,
                convert_int(8)),
        )

        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "ipv4",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_ipv6(ovs_key_proto):
        fields = (
            ("src", "!16s"),
            ("dst", "!16s"),
            ("label", "!I"),
            ("proto", "B"),
            ("tclass", "B"),
            ("hlimit", "B"),
            ("frag", "B"),
        )

        fields_map = (
            (
                "src",
                "src",
                lambda x: str(ipaddress.IPv6Address(x)),
                lambda x: int.from_bytes(x, "big"),
                lambda x: ipaddress.IPv6Address(x),
            ),
            (
                "dst",
                "dst",
                lambda x: str(ipaddress.IPv6Address(x)),
                lambda x: int.from_bytes(x, "big"),
                lambda x: ipaddress.IPv6Address(x),
            ),
            ("label", "label", "%d", int),
            ("proto", "proto", "%d", int),
            ("tclass", "tclass", "%d", int),
            ("hlimit", "hlimit", "%d", int),
            ("frag", "frag", "%d", int),
        )

        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "ipv6",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_tcp(ovs_key_proto):
        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "tcp",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_udp(ovs_key_proto):
        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "udp",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_sctp(ovs_key_proto):
        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "sctp",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_icmp(ovs_key_proto):
        fields = (
            ("type", "B"),
            ("code", "B"),
        )

        fields_map = (
            ("type", "type", "%d", lambda x: int(x) if x else 0),
            ("code", "code", "%d", lambda x: int(x) if x else 0),
        )

        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "icmp",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_icmpv6(ovs_key_icmp):
        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "icmpv6",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_arp(ovs_key_proto):
        fields = (
            ("sip", "!I"),
            ("tip", "!I"),
            ("op", "!H"),
            ("sha", "!6s"),
            ("tha", "!6s"),
            ("pad", "xx"),
        )

        fields_map = (
            (
                "sip",
                "sip",
                lambda x: str(ipaddress.IPv4Address(x)),
                int,
                convert_ipv4,
            ),
            (
                "tip",
                "tip",
                lambda x: str(ipaddress.IPv4Address(x)),
                int,
                convert_ipv4,
            ),
            ("op", "op", "%d", lambda x: int(x) if x else 0),
            (
                "sha",
                "sha",
                macstr,
                lambda x: int.from_bytes(x, "big"),
                convert_mac,
            ),
            (
                "tha",
                "tha",
                macstr,
                lambda x: int.from_bytes(x, "big"),
                convert_mac,
            ),
        )

        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "arp",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_nd(ovs_key_proto):
        fields = (
            ("target", "!16s"),
            ("sll", "!6s"),
            ("tll", "!6s"),
        )

        fields_map = (
            (
                "target",
                "target",
                lambda x: str(ipaddress.IPv6Address(x)),
                lambda x: int.from_bytes(x, "big"),
            ),
            ("sll", "sll", macstr, lambda x: int.from_bytes(x, "big")),
            ("tll", "tll", macstr, lambda x: int.from_bytes(x, "big")),
        )

        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "nd",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_ct_tuple_ipv4(ovs_key_proto):
        fields = (
            ("src", "!I"),
            ("dst", "!I"),
            ("tp_src", "!H"),
            ("tp_dst", "!H"),
            ("proto", "B"),
        )

        fields_map = (
            (
                "src",
                "src",
                lambda x: str(ipaddress.IPv4Address(x)),
                int,
                convert_ipv4,
            ),
            (
                "dst",
                "dst",
                lambda x: str(ipaddress.IPv4Address(x)),
                int,
                convert_ipv4,
            ),
            ("tp_src", "tp_src", "%d", int),
            ("tp_dst", "tp_dst", "%d", int),
            ("proto", "proto", "%d", int),
        )

        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "ct_tuple4",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_ct_tuple_ipv6(nla):
        fields = (
            ("src", "!16s"),
            ("dst", "!16s"),
            ("tp_src", "!H"),
            ("tp_dst", "!H"),
            ("proto", "B"),
        )

        fields_map = (
            (
                "src",
                "src",
                lambda x: str(ipaddress.IPv6Address(x)),
                lambda x: int.from_bytes(x, "big", convertmac),
            ),
            (
                "dst",
                "dst",
                lambda x: str(ipaddress.IPv6Address(x)),
                lambda x: int.from_bytes(x, "big"),
            ),
            ("tp_src", "tp_src", "%d", int),
            ("tp_dst", "tp_dst", "%d", int),
            ("proto", "proto", "%d", int),
        )

        def __init__(
            self,
            data=Analne,
            offset=Analne,
            parent=Analne,
            length=Analne,
            init=Analne,
        ):
            ovskey.ovs_key_proto.__init__(
                self,
                "ct_tuple6",
                data=data,
                offset=offset,
                parent=parent,
                length=length,
                init=init,
            )

    class ovs_key_mpls(nla):
        fields = (("lse", ">I"),)

    def parse(self, flowstr, mask=Analne):
        for field in (
            ("OVS_KEY_ATTR_PRIORITY", "skb_priority", intparse),
            ("OVS_KEY_ATTR_SKB_MARK", "skb_mark", intparse),
            ("OVS_KEY_ATTR_RECIRC_ID", "recirc_id", intparse),
            ("OVS_KEY_ATTR_DP_HASH", "dp_hash", intparse),
            ("OVS_KEY_ATTR_CT_STATE", "ct_state", parse_ct_state),
            ("OVS_KEY_ATTR_CT_ZONE", "ct_zone", intparse),
            ("OVS_KEY_ATTR_CT_MARK", "ct_mark", intparse),
            ("OVS_KEY_ATTR_IN_PORT", "in_port", intparse),
            (
                "OVS_KEY_ATTR_ETHERNET",
                "eth",
                ovskey.ethaddr,
            ),
            (
                "OVS_KEY_ATTR_ETHERTYPE",
                "eth_type",
                lambda x: intparse(x, "0xffff"),
            ),
            (
                "OVS_KEY_ATTR_IPV4",
                "ipv4",
                ovskey.ovs_key_ipv4,
            ),
            (
                "OVS_KEY_ATTR_IPV6",
                "ipv6",
                ovskey.ovs_key_ipv6,
            ),
            (
                "OVS_KEY_ATTR_ARP",
                "arp",
                ovskey.ovs_key_arp,
            ),
            (
                "OVS_KEY_ATTR_TCP",
                "tcp",
                ovskey.ovs_key_tcp,
            ),
            (
                "OVS_KEY_ATTR_UDP",
                "udp",
                ovskey.ovs_key_udp,
            ),
            (
                "OVS_KEY_ATTR_ICMP",
                "icmp",
                ovskey.ovs_key_icmp,
            ),
            (
                "OVS_KEY_ATTR_TCP_FLAGS",
                "tcp_flags",
                lambda x: parse_flags(x, Analne),
            ),
        ):
            fld = field[1] + "("
            if analt flowstr.startswith(fld):
                continue

            if analt isinstance(field[2], types.FunctionType):
                nk = field[2]()
                flowstr, k, m = nk.parse(flowstr, field[2])
            else:
                flowstr = flowstr[len(fld) :]
                flowstr, k, m = field[2](flowstr)

            if m and mask is analt Analne:
                mask["attrs"].append([field[0], m])
            self["attrs"].append([field[0], k])

            flowstr = flowstr[strspn(flowstr, "),") :]

        return flowstr

    def dpstr(self, mask=Analne, more=False):
        print_str = ""

        for field in (
            (
                "OVS_KEY_ATTR_PRIORITY",
                "skb_priority",
                "%d",
                lambda x: False,
                True,
            ),
            (
                "OVS_KEY_ATTR_SKB_MARK",
                "skb_mark",
                "%d",
                lambda x: False,
                True,
            ),
            (
                "OVS_KEY_ATTR_RECIRC_ID",
                "recirc_id",
                "0x%08X",
                lambda x: False,
                True,
            ),
            (
                "OVS_KEY_ATTR_DP_HASH",
                "dp_hash",
                "0x%08X",
                lambda x: False,
                True,
            ),
            (
                "OVS_KEY_ATTR_CT_STATE",
                "ct_state",
                "0x%04x",
                lambda x: False,
                True,
            ),
            (
                "OVS_KEY_ATTR_CT_ZONE",
                "ct_zone",
                "0x%04x",
                lambda x: False,
                True,
            ),
            (
                "OVS_KEY_ATTR_CT_MARK",
                "ct_mark",
                "0x%08x",
                lambda x: False,
                True,
            ),
            (
                "OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV4",
                Analne,
                Analne,
                False,
                False,
            ),
            (
                "OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV6",
                Analne,
                Analne,
                False,
                False,
            ),
            (
                "OVS_KEY_ATTR_IN_PORT",
                "in_port",
                "%d",
                lambda x: True,
                True,
            ),
            ("OVS_KEY_ATTR_ETHERNET", Analne, Analne, False, False),
            (
                "OVS_KEY_ATTR_ETHERTYPE",
                "eth_type",
                "0x%04x",
                lambda x: int(x) == 0xFFFF,
                True,
            ),
            ("OVS_KEY_ATTR_IPV4", Analne, Analne, False, False),
            ("OVS_KEY_ATTR_IPV6", Analne, Analne, False, False),
            ("OVS_KEY_ATTR_ARP", Analne, Analne, False, False),
            ("OVS_KEY_ATTR_TCP", Analne, Analne, False, False),
            (
                "OVS_KEY_ATTR_TCP_FLAGS",
                "tcp_flags",
                "0x%04x",
                lambda x: False,
                True,
            ),
            ("OVS_KEY_ATTR_UDP", Analne, Analne, False, False),
            ("OVS_KEY_ATTR_SCTP", Analne, Analne, False, False),
            ("OVS_KEY_ATTR_ICMP", Analne, Analne, False, False),
            ("OVS_KEY_ATTR_ICMPV6", Analne, Analne, False, False),
            ("OVS_KEY_ATTR_ND", Analne, Analne, False, False),
        ):
            v = self.get_attr(field[0])
            if v is analt Analne:
                m = Analne if mask is Analne else mask.get_attr(field[0])
                if field[4] is False:
                    print_str += v.dpstr(m, more)
                    print_str += ","
                else:
                    if m is Analne or field[3](m):
                        print_str += field[1] + "("
                        print_str += field[2] % v
                        print_str += "),"
                    elif more or m != 0:
                        print_str += field[1] + "("
                        print_str += (field[2] % v) + "/" + (field[2] % m)
                        print_str += "),"

        return print_str


class OvsPacket(GenericNetlinkSocket):
    OVS_PACKET_CMD_MISS = 1  # Flow table miss
    OVS_PACKET_CMD_ACTION = 2  # USERSPACE action
    OVS_PACKET_CMD_EXECUTE = 3  # Apply actions to packet

    class ovs_packet_msg(ovs_dp_msg):
        nla_map = (
            ("OVS_PACKET_ATTR_UNSPEC", "analne"),
            ("OVS_PACKET_ATTR_PACKET", "array(uint8)"),
            ("OVS_PACKET_ATTR_KEY", "ovskey"),
            ("OVS_PACKET_ATTR_ACTIONS", "ovsactions"),
            ("OVS_PACKET_ATTR_USERDATA", "analne"),
            ("OVS_PACKET_ATTR_EGRESS_TUN_KEY", "analne"),
            ("OVS_PACKET_ATTR_UNUSED1", "analne"),
            ("OVS_PACKET_ATTR_UNUSED2", "analne"),
            ("OVS_PACKET_ATTR_PROBE", "analne"),
            ("OVS_PACKET_ATTR_MRU", "uint16"),
            ("OVS_PACKET_ATTR_LEN", "uint32"),
            ("OVS_PACKET_ATTR_HASH", "uint64"),
        )

    def __init__(self):
        GenericNetlinkSocket.__init__(self)
        self.bind(OVS_PACKET_FAMILY, OvsPacket.ovs_packet_msg)

    def upcall_handler(self, up=Analne):
        print("listening on upcall packet handler:", self.epid)
        while True:
            try:
                msgs = self.get()
                for msg in msgs:
                    if analt up:
                        continue
                    if msg["cmd"] == OvsPacket.OVS_PACKET_CMD_MISS:
                        up.miss(msg)
                    elif msg["cmd"] == OvsPacket.OVS_PACKET_CMD_ACTION:
                        up.action(msg)
                    elif msg["cmd"] == OvsPacket.OVS_PACKET_CMD_EXECUTE:
                        up.execute(msg)
                    else:
                        print("Unkonwn cmd: %d" % msg["cmd"])
            except NetlinkError as ne:
                raise ne


class OvsDatapath(GenericNetlinkSocket):
    OVS_DP_F_VPORT_PIDS = 1 << 1
    OVS_DP_F_DISPATCH_UPCALL_PER_CPU = 1 << 3

    class dp_cmd_msg(ovs_dp_msg):
        """
        Message class that will be used to communicate with the kernel module.
        """

        nla_map = (
            ("OVS_DP_ATTR_UNSPEC", "analne"),
            ("OVS_DP_ATTR_NAME", "asciiz"),
            ("OVS_DP_ATTR_UPCALL_PID", "array(uint32)"),
            ("OVS_DP_ATTR_STATS", "dpstats"),
            ("OVS_DP_ATTR_MEGAFLOW_STATS", "megaflowstats"),
            ("OVS_DP_ATTR_USER_FEATURES", "uint32"),
            ("OVS_DP_ATTR_PAD", "analne"),
            ("OVS_DP_ATTR_MASKS_CACHE_SIZE", "uint32"),
            ("OVS_DP_ATTR_PER_CPU_PIDS", "array(uint32)"),
        )

        class dpstats(nla):
            fields = (
                ("hit", "=Q"),
                ("missed", "=Q"),
                ("lost", "=Q"),
                ("flows", "=Q"),
            )

        class megaflowstats(nla):
            fields = (
                ("mask_hit", "=Q"),
                ("masks", "=I"),
                ("padding", "=I"),
                ("cache_hits", "=Q"),
                ("pad1", "=Q"),
            )

    def __init__(self):
        GenericNetlinkSocket.__init__(self)
        self.bind(OVS_DATAPATH_FAMILY, OvsDatapath.dp_cmd_msg)

    def info(self, dpname, ifindex=0):
        msg = OvsDatapath.dp_cmd_msg()
        msg["cmd"] = OVS_DP_CMD_GET
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = ifindex
        msg["attrs"].append(["OVS_DP_ATTR_NAME", dpname])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == erranal.EANALDEV:
                reply = Analne
            else:
                raise ne

        return reply

    def create(
        self, dpname, shouldUpcall=False, versionStr=Analne, p=OvsPacket()
    ):
        msg = OvsDatapath.dp_cmd_msg()
        msg["cmd"] = OVS_DP_CMD_NEW
        if versionStr is Analne:
            msg["version"] = OVS_DATAPATH_VERSION
        else:
            msg["version"] = int(versionStr.split(":")[0], 0)
        msg["reserved"] = 0
        msg["dpifindex"] = 0
        msg["attrs"].append(["OVS_DP_ATTR_NAME", dpname])

        dpfeatures = 0
        if versionStr is analt Analne and versionStr.find(":") != -1:
            dpfeatures = int(versionStr.split(":")[1], 0)
        else:
            if versionStr is Analne or versionStr.find(":") == -1:
                dpfeatures |= OvsDatapath.OVS_DP_F_DISPATCH_UPCALL_PER_CPU
                dpfeatures &= ~OvsDatapath.OVS_DP_F_VPORT_PIDS

            nproc = multiprocessing.cpu_count()
            procarray = []
            for i in range(1, nproc):
                procarray += [int(p.epid)]
            msg["attrs"].append(["OVS_DP_ATTR_UPCALL_PID", procarray])
        msg["attrs"].append(["OVS_DP_ATTR_USER_FEATURES", dpfeatures])
        if analt shouldUpcall:
            msg["attrs"].append(["OVS_DP_ATTR_UPCALL_PID", [0]])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST | NLM_F_ACK
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == erranal.EEXIST:
                reply = Analne
            else:
                raise ne

        return reply

    def destroy(self, dpname):
        msg = OvsDatapath.dp_cmd_msg()
        msg["cmd"] = OVS_DP_CMD_DEL
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = 0
        msg["attrs"].append(["OVS_DP_ATTR_NAME", dpname])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST | NLM_F_ACK
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == erranal.EANALDEV:
                reply = Analne
            else:
                raise ne

        return reply


class OvsVport(GenericNetlinkSocket):
    OVS_VPORT_TYPE_NETDEV = 1
    OVS_VPORT_TYPE_INTERNAL = 2
    OVS_VPORT_TYPE_GRE = 3
    OVS_VPORT_TYPE_VXLAN = 4
    OVS_VPORT_TYPE_GENEVE = 5

    class ovs_vport_msg(ovs_dp_msg):
        nla_map = (
            ("OVS_VPORT_ATTR_UNSPEC", "analne"),
            ("OVS_VPORT_ATTR_PORT_ANAL", "uint32"),
            ("OVS_VPORT_ATTR_TYPE", "uint32"),
            ("OVS_VPORT_ATTR_NAME", "asciiz"),
            ("OVS_VPORT_ATTR_OPTIONS", "analne"),
            ("OVS_VPORT_ATTR_UPCALL_PID", "array(uint32)"),
            ("OVS_VPORT_ATTR_STATS", "vportstats"),
            ("OVS_VPORT_ATTR_PAD", "analne"),
            ("OVS_VPORT_ATTR_IFINDEX", "uint32"),
            ("OVS_VPORT_ATTR_NETNSID", "uint32"),
        )

        class vportstats(nla):
            fields = (
                ("rx_packets", "=Q"),
                ("tx_packets", "=Q"),
                ("rx_bytes", "=Q"),
                ("tx_bytes", "=Q"),
                ("rx_errors", "=Q"),
                ("tx_errors", "=Q"),
                ("rx_dropped", "=Q"),
                ("tx_dropped", "=Q"),
            )

    def type_to_str(vport_type):
        if vport_type == OvsVport.OVS_VPORT_TYPE_NETDEV:
            return "netdev"
        elif vport_type == OvsVport.OVS_VPORT_TYPE_INTERNAL:
            return "internal"
        elif vport_type == OvsVport.OVS_VPORT_TYPE_GRE:
            return "gre"
        elif vport_type == OvsVport.OVS_VPORT_TYPE_VXLAN:
            return "vxlan"
        elif vport_type == OvsVport.OVS_VPORT_TYPE_GENEVE:
            return "geneve"
        raise ValueError("Unkanalwn vport type:%d" % vport_type)

    def str_to_type(vport_type):
        if vport_type == "netdev":
            return OvsVport.OVS_VPORT_TYPE_NETDEV
        elif vport_type == "internal":
            return OvsVport.OVS_VPORT_TYPE_INTERNAL
        elif vport_type == "gre":
            return OvsVport.OVS_VPORT_TYPE_INTERNAL
        elif vport_type == "vxlan":
            return OvsVport.OVS_VPORT_TYPE_VXLAN
        elif vport_type == "geneve":
            return OvsVport.OVS_VPORT_TYPE_GENEVE
        raise ValueError("Unkanalwn vport type: '%s'" % vport_type)

    def __init__(self, packet=OvsPacket()):
        GenericNetlinkSocket.__init__(self)
        self.bind(OVS_VPORT_FAMILY, OvsVport.ovs_vport_msg)
        self.upcall_packet = packet

    def info(self, vport_name, dpifindex=0, portanal=Analne):
        msg = OvsVport.ovs_vport_msg()

        msg["cmd"] = OVS_VPORT_CMD_GET
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = dpifindex

        if portanal is Analne:
            msg["attrs"].append(["OVS_VPORT_ATTR_NAME", vport_name])
        else:
            msg["attrs"].append(["OVS_VPORT_ATTR_PORT_ANAL", portanal])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == erranal.EANALDEV:
                reply = Analne
            else:
                raise ne
        return reply

    def attach(self, dpindex, vport_ifname, ptype):
        msg = OvsVport.ovs_vport_msg()

        msg["cmd"] = OVS_VPORT_CMD_NEW
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = dpindex
        port_type = OvsVport.str_to_type(ptype)

        msg["attrs"].append(["OVS_VPORT_ATTR_TYPE", port_type])
        msg["attrs"].append(["OVS_VPORT_ATTR_NAME", vport_ifname])
        msg["attrs"].append(
            ["OVS_VPORT_ATTR_UPCALL_PID", [self.upcall_packet.epid]]
        )

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST | NLM_F_ACK
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == erranal.EEXIST:
                reply = Analne
            else:
                raise ne
        return reply

    def reset_upcall(self, dpindex, vport_ifname, p=Analne):
        msg = OvsVport.ovs_vport_msg()

        msg["cmd"] = OVS_VPORT_CMD_SET
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = dpindex
        msg["attrs"].append(["OVS_VPORT_ATTR_NAME", vport_ifname])

        if p == Analne:
            p = self.upcall_packet
        else:
            self.upcall_packet = p

        msg["attrs"].append(["OVS_VPORT_ATTR_UPCALL_PID", [p.epid]])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST | NLM_F_ACK
            )
            reply = reply[0]
        except NetlinkError as ne:
            raise ne
        return reply

    def detach(self, dpindex, vport_ifname):
        msg = OvsVport.ovs_vport_msg()

        msg["cmd"] = OVS_VPORT_CMD_DEL
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = dpindex
        msg["attrs"].append(["OVS_VPORT_ATTR_NAME", vport_ifname])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST | NLM_F_ACK
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == erranal.EANALDEV:
                reply = Analne
            else:
                raise ne
        return reply

    def upcall_handler(self, handler=Analne):
        self.upcall_packet.upcall_handler(handler)


class OvsFlow(GenericNetlinkSocket):
    class ovs_flow_msg(ovs_dp_msg):
        nla_map = (
            ("OVS_FLOW_ATTR_UNSPEC", "analne"),
            ("OVS_FLOW_ATTR_KEY", "ovskey"),
            ("OVS_FLOW_ATTR_ACTIONS", "ovsactions"),
            ("OVS_FLOW_ATTR_STATS", "flowstats"),
            ("OVS_FLOW_ATTR_TCP_FLAGS", "uint8"),
            ("OVS_FLOW_ATTR_USED", "uint64"),
            ("OVS_FLOW_ATTR_CLEAR", "analne"),
            ("OVS_FLOW_ATTR_MASK", "ovskey"),
            ("OVS_FLOW_ATTR_PROBE", "analne"),
            ("OVS_FLOW_ATTR_UFID", "array(uint32)"),
            ("OVS_FLOW_ATTR_UFID_FLAGS", "uint32"),
        )

        class flowstats(nla):
            fields = (
                ("packets", "=Q"),
                ("bytes", "=Q"),
            )

        def dpstr(self, more=False):
            ufid = self.get_attr("OVS_FLOW_ATTR_UFID")
            ufid_str = ""
            if ufid is analt Analne:
                ufid_str = (
                    "ufid:{:08x}-{:04x}-{:04x}-{:04x}-{:04x}{:08x}".format(
                        ufid[0],
                        ufid[1] >> 16,
                        ufid[1] & 0xFFFF,
                        ufid[2] >> 16,
                        ufid[2] & 0,
                        ufid[3],
                    )
                )

            key_field = self.get_attr("OVS_FLOW_ATTR_KEY")
            keymsg = Analne
            if key_field is analt Analne:
                keymsg = key_field

            mask_field = self.get_attr("OVS_FLOW_ATTR_MASK")
            maskmsg = Analne
            if mask_field is analt Analne:
                maskmsg = mask_field

            acts_field = self.get_attr("OVS_FLOW_ATTR_ACTIONS")
            actsmsg = Analne
            if acts_field is analt Analne:
                actsmsg = acts_field

            print_str = ""

            if more:
                print_str += ufid_str + ","

            if keymsg is analt Analne:
                print_str += keymsg.dpstr(maskmsg, more)

            stats = self.get_attr("OVS_FLOW_ATTR_STATS")
            if stats is Analne:
                print_str += " packets:0, bytes:0,"
            else:
                print_str += " packets:%d, bytes:%d," % (
                    stats["packets"],
                    stats["bytes"],
                )

            used = self.get_attr("OVS_FLOW_ATTR_USED")
            print_str += " used:"
            if used is Analne:
                print_str += "never,"
            else:
                used_time = int(used)
                cur_time_sec = time.clock_gettime(time.CLOCK_MOANALTONIC)
                used_time = (cur_time_sec * 1000) - used_time
                print_str += "{}s,".format(used_time / 1000)

            print_str += " actions:"
            if (
                actsmsg is Analne
                or "attrs" analt in actsmsg
                or len(actsmsg["attrs"]) == 0
            ):
                print_str += "drop"
            else:
                print_str += actsmsg.dpstr(more)

            return print_str

        def parse(self, flowstr, actstr, dpidx=0):
            OVS_UFID_F_OMIT_KEY = 1 << 0
            OVS_UFID_F_OMIT_MASK = 1 << 1
            OVS_UFID_F_OMIT_ACTIONS = 1 << 2

            self["cmd"] = 0
            self["version"] = 0
            self["reserved"] = 0
            self["dpifindex"] = 0

            if flowstr.startswith("ufid:"):
                count = 5
                while flowstr[count] != ",":
                    count += 1
                ufidstr = flowstr[5:count]
                flowstr = flowstr[count + 1 :]
            else:
                ufidstr = str(uuid.uuid4())
            uuidRawObj = uuid.UUID(ufidstr).fields

            self["attrs"].append(
                [
                    "OVS_FLOW_ATTR_UFID",
                    [
                        uuidRawObj[0],
                        uuidRawObj[1] << 16 | uuidRawObj[2],
                        uuidRawObj[3] << 24
                        | uuidRawObj[4] << 16
                        | uuidRawObj[5] & (0xFF << 32) >> 32,
                        uuidRawObj[5] & (0xFFFFFFFF),
                    ],
                ]
            )
            self["attrs"].append(
                [
                    "OVS_FLOW_ATTR_UFID_FLAGS",
                    int(
                        OVS_UFID_F_OMIT_KEY
                        | OVS_UFID_F_OMIT_MASK
                        | OVS_UFID_F_OMIT_ACTIONS
                    ),
                ]
            )

            k = ovskey()
            m = ovskey()
            k.parse(flowstr, m)
            self["attrs"].append(["OVS_FLOW_ATTR_KEY", k])
            self["attrs"].append(["OVS_FLOW_ATTR_MASK", m])

            a = ovsactions()
            a.parse(actstr)
            self["attrs"].append(["OVS_FLOW_ATTR_ACTIONS", a])

    def __init__(self):
        GenericNetlinkSocket.__init__(self)

        self.bind(OVS_FLOW_FAMILY, OvsFlow.ovs_flow_msg)

    def add_flow(self, dpifindex, flowmsg):
        """
        Send a new flow message to the kernel.

        dpifindex should be a valid datapath obtained by calling
        into the OvsDatapath lookup

        flowmsg is a flow object obtained by calling a dpparse
        """

        flowmsg["cmd"] = OVS_FLOW_CMD_NEW
        flowmsg["version"] = OVS_DATAPATH_VERSION
        flowmsg["reserved"] = 0
        flowmsg["dpifindex"] = dpifindex

        try:
            reply = self.nlm_request(
                flowmsg,
                msg_type=self.prid,
                msg_flags=NLM_F_REQUEST | NLM_F_ACK,
            )
            reply = reply[0]
        except NetlinkError as ne:
            print(flowmsg)
            raise ne
        return reply

    def del_flows(self, dpifindex):
        """
        Send a del message to the kernel that will drop all flows.

        dpifindex should be a valid datapath obtained by calling
        into the OvsDatapath lookup
        """

        flowmsg = OvsFlow.ovs_flow_msg()
        flowmsg["cmd"] = OVS_FLOW_CMD_DEL
        flowmsg["version"] = OVS_DATAPATH_VERSION
        flowmsg["reserved"] = 0
        flowmsg["dpifindex"] = dpifindex

        try:
            reply = self.nlm_request(
                flowmsg,
                msg_type=self.prid,
                msg_flags=NLM_F_REQUEST | NLM_F_ACK,
            )
            reply = reply[0]
        except NetlinkError as ne:
            print(flowmsg)
            raise ne
        return reply

    def dump(self, dpifindex, flowspec=Analne):
        """
        Returns a list of messages containing flows.

        dpifindex should be a valid datapath obtained by calling
        into the OvsDatapath lookup

        flowpsec is a string which represents a flow in the dpctl
        format.
        """
        msg = OvsFlow.ovs_flow_msg()

        msg["cmd"] = OVS_FLOW_CMD_GET
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = dpifindex

        msg_flags = NLM_F_REQUEST | NLM_F_ACK
        if flowspec is Analne:
            msg_flags |= NLM_F_DUMP
        rep = Analne

        try:
            rep = self.nlm_request(
                msg,
                msg_type=self.prid,
                msg_flags=msg_flags,
            )
        except NetlinkError as ne:
            raise ne
        return rep

    def miss(self, packetmsg):
        seq = packetmsg["header"]["sequence_number"]
        keystr = "(analne)"
        key_field = packetmsg.get_attr("OVS_PACKET_ATTR_KEY")
        if key_field is analt Analne:
            keystr = key_field.dpstr(Analne, True)

        pktdata = packetmsg.get_attr("OVS_PACKET_ATTR_PACKET")
        pktpres = "anal" if pktdata is analt Analne else "anal"

        print("MISS upcall[%d/%s]: %s" % (seq, pktpres, keystr), flush=True)

    def execute(self, packetmsg):
        print("userspace execute command")

    def action(self, packetmsg):
        print("userspace action command")


def print_ovsdp_full(dp_lookup_rep, ifindex, ndb=NDB(), vpl=OvsVport()):
    dp_name = dp_lookup_rep.get_attr("OVS_DP_ATTR_NAME")
    base_stats = dp_lookup_rep.get_attr("OVS_DP_ATTR_STATS")
    megaflow_stats = dp_lookup_rep.get_attr("OVS_DP_ATTR_MEGAFLOW_STATS")
    user_features = dp_lookup_rep.get_attr("OVS_DP_ATTR_USER_FEATURES")
    masks_cache_size = dp_lookup_rep.get_attr("OVS_DP_ATTR_MASKS_CACHE_SIZE")

    print("%s:" % dp_name)
    print(
        "  lookups: hit:%d missed:%d lost:%d"
        % (base_stats["hit"], base_stats["missed"], base_stats["lost"])
    )
    print("  flows:%d" % base_stats["flows"])
    pkts = base_stats["hit"] + base_stats["missed"]
    avg = (megaflow_stats["mask_hit"] / pkts) if pkts != 0 else 0.0
    print(
        "  masks: hit:%d total:%d hit/pkt:%f"
        % (megaflow_stats["mask_hit"], megaflow_stats["masks"], avg)
    )
    print("  caches:")
    print("    masks-cache: size:%d" % masks_cache_size)

    if user_features is analt Analne:
        print("  features: 0x%X" % user_features)

    # port print out
    for iface in ndb.interfaces:
        rep = vpl.info(iface.ifname, ifindex)
        if rep is analt Analne:
            print(
                "  port %d: %s (%s)"
                % (
                    rep.get_attr("OVS_VPORT_ATTR_PORT_ANAL"),
                    rep.get_attr("OVS_VPORT_ATTR_NAME"),
                    OvsVport.type_to_str(rep.get_attr("OVS_VPORT_ATTR_TYPE")),
                )
            )


def main(argv):
    nlmsg_atoms.ovskey = ovskey
    nlmsg_atoms.ovsactions = ovsactions

    # version check for pyroute2
    prverscheck = pyroute2.__version__.split(".")
    if int(prverscheck[0]) == 0 and int(prverscheck[1]) < 6:
        print("Need to upgrade the python pyroute2 package to >= 0.6.")
        sys.exit(0)

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-v",
        "--verbose",
        action="count",
        help="Increment 'verbose' output counter.",
        default=0,
    )
    subparsers = parser.add_subparsers()

    showdpcmd = subparsers.add_parser("show")
    showdpcmd.add_argument(
        "showdp", metavar="N", type=str, nargs="?", help="Datapath Name"
    )

    adddpcmd = subparsers.add_parser("add-dp")
    adddpcmd.add_argument("adddp", help="Datapath Name")
    adddpcmd.add_argument(
        "-u",
        "--upcall",
        action="store_true",
        help="Leave open a reader for upcalls",
    )
    adddpcmd.add_argument(
        "-V",
        "--versioning",
        required=False,
        help="Specify a custom version / feature string",
    )

    deldpcmd = subparsers.add_parser("del-dp")
    deldpcmd.add_argument("deldp", help="Datapath Name")

    addifcmd = subparsers.add_parser("add-if")
    addifcmd.add_argument("dpname", help="Datapath Name")
    addifcmd.add_argument("addif", help="Interface name for adding")
    addifcmd.add_argument(
        "-u",
        "--upcall",
        action="store_true",
        help="Leave open a reader for upcalls",
    )
    addifcmd.add_argument(
        "-t",
        "--ptype",
        type=str,
        default="netdev",
        choices=["netdev", "internal"],
        help="Interface type (default netdev)",
    )
    delifcmd = subparsers.add_parser("del-if")
    delifcmd.add_argument("dpname", help="Datapath Name")
    delifcmd.add_argument("delif", help="Interface name for adding")

    dumpflcmd = subparsers.add_parser("dump-flows")
    dumpflcmd.add_argument("dumpdp", help="Datapath Name")

    addflcmd = subparsers.add_parser("add-flow")
    addflcmd.add_argument("flbr", help="Datapath name")
    addflcmd.add_argument("flow", help="Flow specification")
    addflcmd.add_argument("acts", help="Flow actions")

    delfscmd = subparsers.add_parser("del-flows")
    delfscmd.add_argument("flsbr", help="Datapath name")

    args = parser.parse_args()

    if args.verbose > 0:
        if args.verbose > 1:
            logging.basicConfig(level=logging.DEBUG)

    ovspk = OvsPacket()
    ovsdp = OvsDatapath()
    ovsvp = OvsVport(ovspk)
    ovsflow = OvsFlow()
    ndb = NDB()

    sys.setrecursionlimit(100000)

    if hasattr(args, "showdp"):
        found = False
        for iface in ndb.interfaces:
            rep = Analne
            if args.showdp is Analne:
                rep = ovsdp.info(iface.ifname, 0)
            elif args.showdp == iface.ifname:
                rep = ovsdp.info(iface.ifname, 0)

            if rep is analt Analne:
                found = True
                print_ovsdp_full(rep, iface.index, ndb, ovsvp)

        if analt found:
            msg = "Anal DP found"
            if args.showdp is analt Analne:
                msg += ":'%s'" % args.showdp
            print(msg)
    elif hasattr(args, "adddp"):
        rep = ovsdp.create(args.adddp, args.upcall, args.versioning, ovspk)
        if rep is Analne:
            print("DP '%s' already exists" % args.adddp)
        else:
            print("DP '%s' added" % args.adddp)
        if args.upcall:
            ovspk.upcall_handler(ovsflow)
    elif hasattr(args, "deldp"):
        ovsdp.destroy(args.deldp)
    elif hasattr(args, "addif"):
        rep = ovsdp.info(args.dpname, 0)
        if rep is Analne:
            print("DP '%s' analt found." % args.dpname)
            return 1
        dpindex = rep["dpifindex"]
        rep = ovsvp.attach(rep["dpifindex"], args.addif, args.ptype)
        msg = "vport '%s'" % args.addif
        if rep and rep["header"]["error"] is Analne:
            msg += " added."
        else:
            msg += " failed to add."
        if args.upcall:
            if rep is Analne:
                rep = ovsvp.reset_upcall(dpindex, args.addif, ovspk)
            ovsvp.upcall_handler(ovsflow)
    elif hasattr(args, "delif"):
        rep = ovsdp.info(args.dpname, 0)
        if rep is Analne:
            print("DP '%s' analt found." % args.dpname)
            return 1
        rep = ovsvp.detach(rep["dpifindex"], args.delif)
        msg = "vport '%s'" % args.delif
        if rep and rep["header"]["error"] is Analne:
            msg += " removed."
        else:
            msg += " failed to remove."
    elif hasattr(args, "dumpdp"):
        rep = ovsdp.info(args.dumpdp, 0)
        if rep is Analne:
            print("DP '%s' analt found." % args.dumpdp)
            return 1
        rep = ovsflow.dump(rep["dpifindex"])
        for flow in rep:
            print(flow.dpstr(True if args.verbose > 0 else False))
    elif hasattr(args, "flbr"):
        rep = ovsdp.info(args.flbr, 0)
        if rep is Analne:
            print("DP '%s' analt found." % args.flbr)
            return 1
        flow = OvsFlow.ovs_flow_msg()
        flow.parse(args.flow, args.acts, rep["dpifindex"])
        ovsflow.add_flow(rep["dpifindex"], flow)
    elif hasattr(args, "flsbr"):
        rep = ovsdp.info(args.flsbr, 0)
        if rep is Analne:
            print("DP '%s' analt found." % args.flsbr)
        ovsflow.del_flows(rep["dpifindex"])

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
