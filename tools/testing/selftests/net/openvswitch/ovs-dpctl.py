#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

# Controls the openvswitch module.  Part of the kselftest suite, but
# can be used for some diagnostic purpose as well.

import argparse
import errno
import ipaddress
import logging
import math
import multiprocessing
import re
import socket
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
    from pyroute2.netlink.event import EventSocket
    from pyroute2.netlink.exceptions import NetlinkError
    from pyroute2.netlink.generic import GenericNetlinkSocket
    from pyroute2.netlink.nlsocket import Marshal
    import pyroute2
    import pyroute2.iproute

except ModuleNotFoundError:
    print("Need to install the python pyroute2 package >= 0.6.")
    sys.exit(1)


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

UINT32_MAX = 0xFFFFFFFF

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

    m = None
    if defmask is not None:
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
                    "Flag %s set once, cannot be set in multiples" % flag
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

    if not mac_str:
        mac_str = mask_str = "00:00:00:00:00:00"
    elif not mask_str:
        mask_str = "FF:FF:FF:FF:FF:FF"

    return to_bytes(mac_str), to_bytes(mask_str)

def convert_ipv4(data):
    ip, _, mask = data.partition('/')

    if not ip:
        ip = mask = 0
    elif not mask:
        mask = 0xFFFFFFFF
    elif mask.isdigit():
        mask = (0xFFFFFFFF << (32 - int(mask))) & 0xFFFFFFFF

    return int(ipaddress.IPv4Address(ip)), int(ipaddress.IPv4Address(mask))

def convert_ipv6(data):
    ip, _, mask = data.partition('/')

    if not ip:
        ip = mask = 0
    elif not mask:
        mask = 'ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff'
    elif mask.isdigit():
        mask = ipaddress.IPv6Network("::/" + mask).hostmask

    return ipaddress.IPv6Address(ip).packed, ipaddress.IPv6Address(mask).packed

def convert_int(size):
    def convert_int_sized(data):
        value, _, mask = data.partition('/')

        if not value:
            return 0, 0
        elif not mask:
            return int(value, 0), pow(2, size) - 1
        else:
            return int(value, 0), int(mask, 0)

    return convert_int_sized

def parse_starts_block(block_str, scanstr, returnskipped, scanregex=False):
    if scanregex:
        m = re.search(scanstr, block_str)
        if m is None:
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
    block_str, fieldstr, scanfmt, convert, masked=False, defval=None
):
    if fieldstr and not block_str.startswith(fieldstr):
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
    if m is None:
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


def parse_attrs(actstr, attr_desc):
    """Parses the given action string and returns a list of netlink
    attributes based on a list of attribute descriptions.

    Each element in the attribute description list is a tuple such as:
        (name, attr_name, parse_func)
    where:
        name: is the string representing the attribute
        attr_name: is the name of the attribute as defined in the uAPI.
        parse_func: is a callable accepting a string and returning either
            a single object (the parsed attribute value) or a tuple of
            two values (the parsed attribute value and the remaining string)

    Returns a list of attributes and the remaining string.
    """
    def parse_attr(actstr, key, func):
        actstr = actstr[len(key) :]

        if not func:
            return None, actstr

        delim = actstr[0]
        actstr = actstr[1:]

        if delim == "=":
            pos = strcspn(actstr, ",)")
            ret = func(actstr[:pos])
        else:
            ret = func(actstr)

        if isinstance(ret, tuple):
            (datum, actstr) = ret
        else:
            datum = ret
            actstr = actstr[strcspn(actstr, ",)"):]

        if delim == "(":
            if not actstr or actstr[0] != ")":
                raise ValueError("Action contains unbalanced parentheses")

            actstr = actstr[1:]

        actstr = actstr[strspn(actstr, ", ") :]

        return datum, actstr

    attrs = []
    attr_desc = list(attr_desc)
    while actstr and actstr[0] != ")" and attr_desc:
        found = False
        for i, (key, attr, func) in enumerate(attr_desc):
            if actstr.startswith(key):
                datum, actstr = parse_attr(actstr, key, func)
                attrs.append([attr, datum])
                found = True
                del attr_desc[i]

        if not found:
            raise ValueError("Unknown attribute: '%s'" % actstr)

        actstr = actstr[strspn(actstr, ", ") :]

    if actstr[0] != ")":
        raise ValueError("Action string contains extra garbage or has "
                         "unbalanced parenthesis: '%s'" % actstr)

    return attrs, actstr[1:]


class ovs_dp_msg(genlmsg):
    # include the OVS version
    # We need a custom header rather than just being able to rely on
    # genlmsg because fields ends up not expressing everything correctly
    # if we use the canonical example of setting fields = (('customfield',),)
    fields = genlmsg.fields + (("dpifindex", "I"),)


class ovsactions(nla):
    nla_flags = NLA_F_NESTED

    nla_map = (
        ("OVS_ACTION_ATTR_UNSPEC", "none"),
        ("OVS_ACTION_ATTR_OUTPUT", "uint32"),
        ("OVS_ACTION_ATTR_USERSPACE", "userspace"),
        ("OVS_ACTION_ATTR_SET", "ovskey"),
        ("OVS_ACTION_ATTR_PUSH_VLAN", "none"),
        ("OVS_ACTION_ATTR_POP_VLAN", "flag"),
        ("OVS_ACTION_ATTR_SAMPLE", "sample"),
        ("OVS_ACTION_ATTR_RECIRC", "uint32"),
        ("OVS_ACTION_ATTR_HASH", "none"),
        ("OVS_ACTION_ATTR_PUSH_MPLS", "none"),
        ("OVS_ACTION_ATTR_POP_MPLS", "flag"),
        ("OVS_ACTION_ATTR_SET_MASKED", "ovskey"),
        ("OVS_ACTION_ATTR_CT", "ctact"),
        ("OVS_ACTION_ATTR_TRUNC", "uint32"),
        ("OVS_ACTION_ATTR_PUSH_ETH", "none"),
        ("OVS_ACTION_ATTR_POP_ETH", "flag"),
        ("OVS_ACTION_ATTR_CT_CLEAR", "flag"),
        ("OVS_ACTION_ATTR_PUSH_NSH", "none"),
        ("OVS_ACTION_ATTR_POP_NSH", "flag"),
        ("OVS_ACTION_ATTR_METER", "none"),
        ("OVS_ACTION_ATTR_CLONE", "recursive"),
        ("OVS_ACTION_ATTR_CHECK_PKT_LEN", "none"),
        ("OVS_ACTION_ATTR_ADD_MPLS", "none"),
        ("OVS_ACTION_ATTR_DEC_TTL", "none"),
        ("OVS_ACTION_ATTR_DROP", "uint32"),
        ("OVS_ACTION_ATTR_PSAMPLE", "psample"),
    )

    class psample(nla):
        nla_flags = NLA_F_NESTED

        nla_map = (
            ("OVS_PSAMPLE_ATTR_UNSPEC", "none"),
            ("OVS_PSAMPLE_ATTR_GROUP", "uint32"),
            ("OVS_PSAMPLE_ATTR_COOKIE", "array(uint8)"),
        )

        def dpstr(self, more=False):
            args = "group=%d" % self.get_attr("OVS_PSAMPLE_ATTR_GROUP")

            cookie = self.get_attr("OVS_PSAMPLE_ATTR_COOKIE")
            if cookie:
                args += ",cookie(%s)" % \
                        "".join(format(x, "02x") for x in cookie)

            return "psample(%s)" % args

        def parse(self, actstr):
            desc = (
                ("group", "OVS_PSAMPLE_ATTR_GROUP", int),
                ("cookie", "OVS_PSAMPLE_ATTR_COOKIE",
                    lambda x: list(bytearray.fromhex(x)))
            )

            attrs, actstr = parse_attrs(actstr, desc)

            for attr in attrs:
                self["attrs"].append(attr)

            return actstr

    class sample(nla):
        nla_flags = NLA_F_NESTED

        nla_map = (
            ("OVS_SAMPLE_ATTR_UNSPEC", "none"),
            ("OVS_SAMPLE_ATTR_PROBABILITY", "uint32"),
            ("OVS_SAMPLE_ATTR_ACTIONS", "ovsactions"),
        )

        def dpstr(self, more=False):
            args = []

            args.append("sample={:.2f}%".format(
                100 * self.get_attr("OVS_SAMPLE_ATTR_PROBABILITY") /
                UINT32_MAX))

            actions = self.get_attr("OVS_SAMPLE_ATTR_ACTIONS")
            if actions:
                args.append("actions(%s)" % actions.dpstr(more))

            return "sample(%s)" % ",".join(args)

        def parse(self, actstr):
            def parse_nested_actions(actstr):
                subacts = ovsactions()
                parsed_len = subacts.parse(actstr)
                return subacts, actstr[parsed_len :]

            def percent_to_rate(percent):
                percent = float(percent.strip('%'))
                return int(math.floor(UINT32_MAX * (percent / 100.0) + .5))

            desc = (
                ("sample", "OVS_SAMPLE_ATTR_PROBABILITY", percent_to_rate),
                ("actions", "OVS_SAMPLE_ATTR_ACTIONS", parse_nested_actions),
            )
            attrs, actstr = parse_attrs(actstr, desc)

            for attr in attrs:
                self["attrs"].append(attr)

            return actstr

    class ctact(nla):
        nla_flags = NLA_F_NESTED

        nla_map = (
            ("OVS_CT_ATTR_NONE", "none"),
            ("OVS_CT_ATTR_COMMIT", "flag"),
            ("OVS_CT_ATTR_ZONE", "uint16"),
            ("OVS_CT_ATTR_MARK", "none"),
            ("OVS_CT_ATTR_LABELS", "none"),
            ("OVS_CT_ATTR_HELPER", "asciiz"),
            ("OVS_CT_ATTR_NAT", "natattr"),
            ("OVS_CT_ATTR_FORCE_COMMIT", "flag"),
            ("OVS_CT_ATTR_EVENTMASK", "uint32"),
            ("OVS_CT_ATTR_TIMEOUT", "asciiz"),
        )

        class natattr(nla):
            nla_flags = NLA_F_NESTED

            nla_map = (
                ("OVS_NAT_ATTR_NONE", "none"),
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
                    print_str += "XXX-unknown-nat"

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

            if self.get_attr("OVS_CT_ATTR_COMMIT") is not None:
                print_str += "commit,"
            if self.get_attr("OVS_CT_ATTR_ZONE") is not None:
                print_str += "zone=%d," % self.get_attr("OVS_CT_ATTR_ZONE")
            if self.get_attr("OVS_CT_ATTR_HELPER") is not None:
                print_str += "helper=%s," % self.get_attr("OVS_CT_ATTR_HELPER")
            if self.get_attr("OVS_CT_ATTR_NAT") is not None:
                print_str += self.get_attr("OVS_CT_ATTR_NAT").dpstr(more)
                print_str += ","
            if self.get_attr("OVS_CT_ATTR_FORCE_COMMIT") is not None:
                print_str += "force,"
            if self.get_attr("OVS_CT_ATTR_EVENTMASK") is not None:
                print_str += "emask=0x%X," % self.get_attr(
                    "OVS_CT_ATTR_EVENTMASK"
                )
            if self.get_attr("OVS_CT_ATTR_TIMEOUT") is not None:
                print_str += "timeout=%s" % self.get_attr(
                    "OVS_CT_ATTR_TIMEOUT"
                )
            print_str += ")"
            return print_str

    class userspace(nla):
        nla_flags = NLA_F_NESTED

        nla_map = (
            ("OVS_USERSPACE_ATTR_UNUSED", "none"),
            ("OVS_USERSPACE_ATTR_PID", "uint32"),
            ("OVS_USERSPACE_ATTR_USERDATA", "array(uint8)"),
            ("OVS_USERSPACE_ATTR_EGRESS_TUN_PORT", "uint32"),
        )

        def dpstr(self, more=False):
            print_str = "userspace("
            if self.get_attr("OVS_USERSPACE_ATTR_PID") is not None:
                print_str += "pid=%d," % self.get_attr(
                    "OVS_USERSPACE_ATTR_PID"
                )
            if self.get_attr("OVS_USERSPACE_ATTR_USERDATA") is not None:
                print_str += "userdata="
                for f in self.get_attr("OVS_USERSPACE_ATTR_USERDATA"):
                    print_str += "%x." % f
            if self.get_attr("OVS_USERSPACE_ATTR_EGRESS_TUN_PORT") is not None:
                print_str += "egress_tun_port=%d" % self.get_attr(
                    "OVS_USERSPACE_ATTR_EGRESS_TUN_PORT"
                )
            print_str += ")"
            return print_str

        def parse(self, actstr):
            attrs_desc = (
                ("pid", "OVS_USERSPACE_ATTR_PID", int),
                ("userdata", "OVS_USERSPACE_ATTR_USERDATA",
                    lambda x: list(bytearray.fromhex(x))),
                ("egress_tun_port", "OVS_USERSPACE_ATTR_EGRESS_TUN_PORT", int)
            )

            attrs, actstr = parse_attrs(actstr, attrs_desc)
            for attr in attrs:
                self["attrs"].append(attr)

            return actstr

    def dpstr(self, more=False):
        print_str = ""

        for field in self["attrs"]:
            if field[1] == "none" or self.get_attr(field[0]) is None:
                continue
            if print_str != "":
                print_str += ","

            if field[0] == "OVS_ACTION_ATTR_OUTPUT":
                print_str += "%d" % int(self.get_attr(field[0]))
            elif field[0] == "OVS_ACTION_ATTR_RECIRC":
                print_str += "recirc(0x%x)" % int(self.get_attr(field[0]))
            elif field[0] == "OVS_ACTION_ATTR_TRUNC":
                print_str += "trunc(%d)" % int(self.get_attr(field[0]))
            elif field[0] == "OVS_ACTION_ATTR_DROP":
                print_str += "drop(%d)" % int(self.get_attr(field[0]))
            elif field[0] == "OVS_ACTION_ATTR_CT_CLEAR":
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
                elif field[0] == "OVS_ACTION_ATTR_SET" or \
                     field[0] == "OVS_ACTION_ATTR_SET_MASKED":
                    print_str += "set"
                    field = datum
                    mask = None
                    if field[0] == "OVS_ACTION_ATTR_SET_MASKED":
                        print_str += "_masked"
                        field = datum[0]
                        mask = datum[1]
                    print_str += "("
                    print_str += field.dpstr(mask, more)
                    print_str += ")"
                else:
                    try:
                        print_str += datum.dpstr(more)
                    except:
                        print_str += "{ATTR: %s not decoded}" % field[0]

        return print_str

    def parse(self, actstr):
        totallen = len(actstr)
        while len(actstr) != 0:
            parsed = False
            parencount = 0
            if actstr.startswith("drop"):
                # If no reason is provided, the implicit drop is used (i.e no
                # action). If some reason is given, an explicit action is used.
                reason = None
                if actstr.startswith("drop("):
                    parencount += 1

                    actstr, reason = parse_extract_field(
                        actstr,
                        "drop(",
                        r"([0-9]+)",
                        lambda x: int(x, 0),
                        False,
                        None,
                    )

                if reason is not None:
                    self["attrs"].append(["OVS_ACTION_ATTR_DROP", reason])
                    parsed = True
                else:
                    actstr = actstr[len("drop"): ]
                    return (totallen - len(actstr))

            elif parse_starts_block(actstr, r"^(\d+)", False, True):
                actstr, output = parse_extract_field(
                    actstr, None, r"(\d+)", lambda x: int(x), False, "0"
                )
                self["attrs"].append(["OVS_ACTION_ATTR_OUTPUT", output])
                parsed = True
            elif parse_starts_block(actstr, "recirc(", False):
                actstr, recircid = parse_extract_field(
                    actstr,
                    "recirc(",
                    r"([0-9a-fA-Fx]+)",
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
                    self["attrs"].append([flat_act[1], True])
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
            elif parse_starts_block(actstr, "set(", False):
                parencount += 1
                k = ovskey()
                actstr = actstr[len("set("):]
                actstr = k.parse(actstr, None)
                self["attrs"].append(("OVS_ACTION_ATTR_SET", k))
                if not actstr.startswith(")"):
                    actstr = ")" + actstr
                parsed = True
            elif parse_starts_block(actstr, "set_masked(", False):
                parencount += 1
                k = ovskey()
                m = ovskey()
                actstr = actstr[len("set_masked("):]
                actstr = k.parse(actstr, m)
                self["attrs"].append(("OVS_ACTION_ATTR_SET_MASKED", [k, m]))
                if not actstr.startswith(")"):
                    actstr = ")" + actstr
                parsed = True
            elif parse_starts_block(actstr, "ct(", False):
                parencount += 1
                actstr = actstr[len("ct(") :]
                ctact = ovsactions.ctact()

                for scan in (
                    ("commit", "OVS_CT_ATTR_COMMIT", None),
                    ("force_commit", "OVS_CT_ATTR_FORCE_COMMIT", None),
                    ("zone", "OVS_CT_ATTR_ZONE", int),
                    ("mark", "OVS_CT_ATTR_MARK", int),
                    ("helper", "OVS_CT_ATTR_HELPER", lambda x, y: str(x)),
                    ("timeout", "OVS_CT_ATTR_TIMEOUT", lambda x, y: str(x)),
                ):
                    if actstr.startswith(scan[0]):
                        actstr = actstr[len(scan[0]) :]
                        if scan[2] is not None:
                            if actstr[0] != "=":
                                raise ValueError("Invalid ct attr")
                            actstr = actstr[1:]
                            pos = strcspn(actstr, ",)")
                            datum = scan[2](actstr[:pos], 0)
                            ctact["attrs"].append([scan[1], datum])
                            actstr = actstr[pos:]
                        else:
                            ctact["attrs"].append([scan[1], None])
                        actstr = actstr[strspn(actstr, ", ") :]
                    # it seems strange to put this here, but nat() is a complex
                    # sub-action and this lets it sit anywhere in the ct() action
                    if actstr.startswith("nat"):
                        actstr = actstr[3:]
                        natact = ovsactions.ctact.natattr()

                        if actstr.startswith("("):
                            parencount += 1
                            t = None
                            actstr = actstr[1:]
                            if actstr.startswith("src"):
                                t = "OVS_NAT_ATTR_SRC"
                                actstr = actstr[3:]
                            elif actstr.startswith("dst"):
                                t = "OVS_NAT_ATTR_DST"
                                actstr = actstr[3:]

                            actstr, ip_block_min = parse_extract_field(
                                actstr, "=", r"([0-9a-fA-F\.]+)", str, False
                            )
                            actstr, ip_block_max = parse_extract_field(
                                actstr, "-", r"([0-9a-fA-F\.]+)", str, False
                            )

                            actstr, proto_min = parse_extract_field(
                                actstr, ":", r"(\d+)", int, False
                            )
                            actstr, proto_max = parse_extract_field(
                                actstr, "-", r"(\d+)", int, False
                            )

                            if t is not None:
                                natact["attrs"].append([t, None])

                                if ip_block_min is not None:
                                    natact["attrs"].append(
                                        ["OVS_NAT_ATTR_IP_MIN", ip_block_min]
                                    )
                                if ip_block_max is not None:
                                    natact["attrs"].append(
                                        ["OVS_NAT_ATTR_IP_MAX", ip_block_max]
                                    )
                                if proto_min is not None:
                                    natact["attrs"].append(
                                        ["OVS_NAT_ATTR_PROTO_MIN", proto_min]
                                    )
                                if proto_max is not None:
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
                                    natact["attrs"].append([natscan[1], None])
                                    actstr = actstr[strspn(actstr, ", ") :]

                        ctact["attrs"].append(["OVS_CT_ATTR_NAT", natact])
                        actstr = actstr[strspn(actstr, ", ") :]

                self["attrs"].append(["OVS_ACTION_ATTR_CT", ctact])
                parsed = True

            elif parse_starts_block(actstr, "sample(", False):
                sampleact = self.sample()
                actstr = sampleact.parse(actstr[len("sample(") : ])
                self["attrs"].append(["OVS_ACTION_ATTR_SAMPLE", sampleact])
                parsed = True

            elif parse_starts_block(actstr, "psample(", False):
                psampleact = self.psample()
                actstr = psampleact.parse(actstr[len("psample(") : ])
                self["attrs"].append(["OVS_ACTION_ATTR_PSAMPLE", psampleact])
                parsed = True

            elif parse_starts_block(actstr, "userspace(", False):
                uact = self.userspace()
                actstr = uact.parse(actstr[len("userspace(") : ])
                self["attrs"].append(["OVS_ACTION_ATTR_USERSPACE", uact])
                parsed = True

            elif parse_starts_block(actstr, "trunc(", False):
                parencount += 1
                actstr, val = parse_extract_field(
                    actstr,
                    "trunc(",
                    r"([0-9]+)",
                    int,
                    False,
                    None,
                )
                self["attrs"].append(["OVS_ACTION_ATTR_TRUNC", val])
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

            if not parsed:
                raise ValueError("Action str: '%s' not supported" % actstr)

        return (totallen - len(actstr))


class ovskey(nla):
    nla_flags = NLA_F_NESTED
    nla_map = (
        ("OVS_KEY_ATTR_UNSPEC", "none"),
        ("OVS_KEY_ATTR_ENCAP", "none"),
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
        ("OVS_KEY_ATTR_TUNNEL", "ovs_key_tunnel"),
        ("OVS_KEY_ATTR_SCTP", "ovs_key_sctp"),
        ("OVS_KEY_ATTR_TCP_FLAGS", "be16"),
        ("OVS_KEY_ATTR_DP_HASH", "uint32"),
        ("OVS_KEY_ATTR_RECIRC_ID", "uint32"),
        ("OVS_KEY_ATTR_MPLS", "array(ovs_key_mpls)"),
        ("OVS_KEY_ATTR_CT_STATE", "uint32"),
        ("OVS_KEY_ATTR_CT_ZONE", "uint16"),
        ("OVS_KEY_ATTR_CT_MARK", "uint32"),
        ("OVS_KEY_ATTR_CT_LABELS", "none"),
        ("OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV4", "ovs_key_ct_tuple_ipv4"),
        ("OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV6", "ovs_key_ct_tuple_ipv6"),
        ("OVS_KEY_ATTR_NSH", "none"),
        ("OVS_KEY_ATTR_PACKET_TYPE", "none"),
        ("OVS_KEY_ATTR_ND_EXTENSIONS", "none"),
        ("OVS_KEY_ATTR_TUNNEL_INFO", "none"),
        ("OVS_KEY_ATTR_IPV6_EXTENSIONS", "none"),
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
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
            if not flowstr.startswith(self.proto_str):
                return None, None

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

        def dpstr(self, masked=None, more=False):
            outstr = self.proto_str + "("
            first = False
            for f in self.fields_map:
                if first:
                    outstr += ","
                if masked is None:
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
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
                lambda x: ipaddress.IPv6Address(x).packed if x else 0,
                convert_ipv6,
            ),
            (
                "dst",
                "dst",
                lambda x: str(ipaddress.IPv6Address(x)),
                lambda x: ipaddress.IPv6Address(x).packed if x else 0,
                convert_ipv6,
            ),
            ("label", "label", "%d", lambda x: int(x) if x else 0),
            ("proto", "proto", "%d", lambda x: int(x) if x else 0),
            ("tclass", "tclass", "%d", lambda x: int(x) if x else 0),
            ("hlimit", "hlimit", "%d", lambda x: int(x) if x else 0),
            ("frag", "frag", "%d", lambda x: int(x) if x else 0),
        )

        def __init__(
            self,
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
                convert_ipv6,
            ),
            ("sll", "sll", macstr, lambda x: int.from_bytes(x, "big")),
            ("tll", "tll", macstr, lambda x: int.from_bytes(x, "big")),
        )

        def __init__(
            self,
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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
                convert_ipv6,
            ),
            (
                "dst",
                "dst",
                lambda x: str(ipaddress.IPv6Address(x)),
                convert_ipv6,
            ),
            ("tp_src", "tp_src", "%d", int),
            ("tp_dst", "tp_dst", "%d", int),
            ("proto", "proto", "%d", int),
        )

        def __init__(
            self,
            data=None,
            offset=None,
            parent=None,
            length=None,
            init=None,
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

    class ovs_key_tunnel(nla):
        nla_flags = NLA_F_NESTED

        nla_map = (
            ("OVS_TUNNEL_KEY_ATTR_ID", "be64"),
            ("OVS_TUNNEL_KEY_ATTR_IPV4_SRC", "ipaddr"),
            ("OVS_TUNNEL_KEY_ATTR_IPV4_DST", "ipaddr"),
            ("OVS_TUNNEL_KEY_ATTR_TOS", "uint8"),
            ("OVS_TUNNEL_KEY_ATTR_TTL", "uint8"),
            ("OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT", "flag"),
            ("OVS_TUNNEL_KEY_ATTR_CSUM", "flag"),
            ("OVS_TUNNEL_KEY_ATTR_OAM", "flag"),
            ("OVS_TUNNEL_KEY_ATTR_GENEVE_OPTS", "array(uint32)"),
            ("OVS_TUNNEL_KEY_ATTR_TP_SRC", "be16"),
            ("OVS_TUNNEL_KEY_ATTR_TP_DST", "be16"),
            ("OVS_TUNNEL_KEY_ATTR_VXLAN_OPTS", "none"),
            ("OVS_TUNNEL_KEY_ATTR_IPV6_SRC", "ipaddr"),
            ("OVS_TUNNEL_KEY_ATTR_IPV6_DST", "ipaddr"),
            ("OVS_TUNNEL_KEY_ATTR_PAD", "none"),
            ("OVS_TUNNEL_KEY_ATTR_ERSPAN_OPTS", "none"),
            ("OVS_TUNNEL_KEY_ATTR_IPV4_INFO_BRIDGE", "flag"),
        )

        def parse(self, flowstr, mask=None):
            if not flowstr.startswith("tunnel("):
                return None, None

            k = ovskey.ovs_key_tunnel()
            if mask is not None:
                mask = ovskey.ovs_key_tunnel()

            flowstr = flowstr[len("tunnel("):]

            v6_address = None

            fields = [
                ("tun_id=", r"(\d+)", int, "OVS_TUNNEL_KEY_ATTR_ID",
                 0xffffffffffffffff, None, None),

                ("src=", r"([0-9a-fA-F\.]+)", str,
                 "OVS_TUNNEL_KEY_ATTR_IPV4_SRC", "255.255.255.255", "0.0.0.0",
                 False),
                ("dst=", r"([0-9a-fA-F\.]+)", str,
                 "OVS_TUNNEL_KEY_ATTR_IPV4_DST", "255.255.255.255", "0.0.0.0",
                 False),

                ("ipv6_src=", r"([0-9a-fA-F:]+)", str,
                 "OVS_TUNNEL_KEY_ATTR_IPV6_SRC",
                 "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "::", True),
                ("ipv6_dst=", r"([0-9a-fA-F:]+)", str,
                 "OVS_TUNNEL_KEY_ATTR_IPV6_DST",
                 "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "::", True),

                ("tos=", r"(\d+)", int, "OVS_TUNNEL_KEY_ATTR_TOS", 255, 0,
                 None),
                ("ttl=", r"(\d+)", int, "OVS_TUNNEL_KEY_ATTR_TTL", 255, 0,
                 None),

                ("tp_src=", r"(\d+)", int, "OVS_TUNNEL_KEY_ATTR_TP_SRC",
                 65535, 0, None),
                ("tp_dst=", r"(\d+)", int, "OVS_TUNNEL_KEY_ATTR_TP_DST",
                 65535, 0, None),
            ]

            forced_include = ["OVS_TUNNEL_KEY_ATTR_TTL"]

            for prefix, regex, typ, attr_name, mask_val, default_val, v46_flag in fields:
                flowstr, value = parse_extract_field(flowstr, prefix, regex, typ, False)
                if not attr_name:
                    raise Exception("Bad list value in tunnel fields")

                if value is None and attr_name in forced_include:
                    value = default_val
                    mask_val = default_val

                if value is not None:
                    if v46_flag is not None:
                        if v6_address is None:
                            v6_address = v46_flag
                        if v46_flag != v6_address:
                            raise ValueError("Cannot mix v6 and v4 addresses")
                    k["attrs"].append([attr_name, value])
                    if mask is not None:
                        mask["attrs"].append([attr_name, mask_val])
                else:
                    if v46_flag is not None:
                        if v6_address is None or v46_flag != v6_address:
                            continue
                    if mask is not None:
                        mask["attrs"].append([attr_name, default_val])

            if k["attrs"][0][0] != "OVS_TUNNEL_KEY_ATTR_ID":
                raise ValueError("Needs a tunid set")

            if flowstr.startswith("flags("):
                flowstr = flowstr[len("flags("):]
                flagspos = flowstr.find(")")
                flags = flowstr[:flagspos]
                flowstr = flowstr[flagspos + 1:]

                flag_attrs = {
                    "df": "OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT",
                    "csum": "OVS_TUNNEL_KEY_ATTR_CSUM",
                    "oam": "OVS_TUNNEL_KEY_ATTR_OAM"
                }

                for flag in flags.split("|"):
                    if flag in flag_attrs:
                        k["attrs"].append([flag_attrs[flag], True])
                        if mask is not None:
                            mask["attrs"].append([flag_attrs[flag], True])

            flowstr = flowstr[strspn(flowstr, ", ") :]
            return flowstr, k, mask

        def dpstr(self, mask=None, more=False):
            print_str = "tunnel("

            flagsattrs = []
            for k in self["attrs"]:
                noprint = False
                if k[0] == "OVS_TUNNEL_KEY_ATTR_ID":
                    print_str += "tun_id=%d" % k[1]
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_IPV4_SRC":
                    print_str += "src=%s" % k[1]
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_IPV4_DST":
                    print_str += "dst=%s" % k[1]
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_IPV6_SRC":
                    print_str += "ipv6_src=%s" % k[1]
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_IPV6_DST":
                    print_str += "ipv6_dst=%s" % k[1]
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_TOS":
                    print_str += "tos=%d" % k[1]
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_TTL":
                    print_str += "ttl=%d" % k[1]
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_TP_SRC":
                    print_str += "tp_src=%d" % k[1]
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_TP_DST":
                    print_str += "tp_dst=%d" % k[1]
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT":
                    noprint = True
                    flagsattrs.append("df")
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_CSUM":
                    noprint = True
                    flagsattrs.append("csum")
                elif k[0] == "OVS_TUNNEL_KEY_ATTR_OAM":
                    noprint = True
                    flagsattrs.append("oam")

                if not noprint:
                    print_str += ","

            if len(flagsattrs):
                print_str += "flags(" + "|".join(flagsattrs) + ")"
            print_str += ")"
            return print_str

    class ovs_key_mpls(nla):
        fields = (("lse", ">I"),)

    def parse(self, flowstr, mask=None):
        for field in (
            ("OVS_KEY_ATTR_PRIORITY", "skb_priority", intparse),
            ("OVS_KEY_ATTR_SKB_MARK", "skb_mark", intparse),
            ("OVS_KEY_ATTR_RECIRC_ID", "recirc_id", intparse),
            ("OVS_KEY_ATTR_TUNNEL", "tunnel", ovskey.ovs_key_tunnel),
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
                lambda x: parse_flags(x, None),
            ),
        ):
            fld = field[1] + "("
            if not flowstr.startswith(fld):
                continue

            if not isinstance(field[2], types.FunctionType):
                nk = field[2]()
                flowstr, k, m = nk.parse(flowstr, field[2])
            else:
                flowstr = flowstr[len(fld) :]
                flowstr, k, m = field[2](flowstr)

            if m and mask is not None:
                mask["attrs"].append([field[0], m])
            self["attrs"].append([field[0], k])

            flowstr = flowstr[strspn(flowstr, "), ") :]

        return flowstr

    def dpstr(self, mask=None, more=False):
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
                "OVS_KEY_ATTR_TUNNEL",
                "tunnel",
                None,
                False,
                False,
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
                None,
                None,
                False,
                False,
            ),
            (
                "OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV6",
                None,
                None,
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
            ("OVS_KEY_ATTR_ETHERNET", None, None, False, False),
            (
                "OVS_KEY_ATTR_ETHERTYPE",
                "eth_type",
                "0x%04x",
                lambda x: int(x) == 0xFFFF,
                True,
            ),
            ("OVS_KEY_ATTR_IPV4", None, None, False, False),
            ("OVS_KEY_ATTR_IPV6", None, None, False, False),
            ("OVS_KEY_ATTR_ARP", None, None, False, False),
            ("OVS_KEY_ATTR_TCP", None, None, False, False),
            (
                "OVS_KEY_ATTR_TCP_FLAGS",
                "tcp_flags",
                "0x%04x",
                lambda x: False,
                True,
            ),
            ("OVS_KEY_ATTR_UDP", None, None, False, False),
            ("OVS_KEY_ATTR_SCTP", None, None, False, False),
            ("OVS_KEY_ATTR_ICMP", None, None, False, False),
            ("OVS_KEY_ATTR_ICMPV6", None, None, False, False),
            ("OVS_KEY_ATTR_ND", None, None, False, False),
        ):
            v = self.get_attr(field[0])
            if v is not None:
                m = None if mask is None else mask.get_attr(field[0])
                if field[4] is False:
                    print_str += v.dpstr(m, more)
                    print_str += ","
                else:
                    if m is None or field[3](m):
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
            ("OVS_PACKET_ATTR_UNSPEC", "none"),
            ("OVS_PACKET_ATTR_PACKET", "array(uint8)"),
            ("OVS_PACKET_ATTR_KEY", "ovskey"),
            ("OVS_PACKET_ATTR_ACTIONS", "ovsactions"),
            ("OVS_PACKET_ATTR_USERDATA", "none"),
            ("OVS_PACKET_ATTR_EGRESS_TUN_KEY", "none"),
            ("OVS_PACKET_ATTR_UNUSED1", "none"),
            ("OVS_PACKET_ATTR_UNUSED2", "none"),
            ("OVS_PACKET_ATTR_PROBE", "none"),
            ("OVS_PACKET_ATTR_MRU", "uint16"),
            ("OVS_PACKET_ATTR_LEN", "uint32"),
            ("OVS_PACKET_ATTR_HASH", "uint64"),
        )

    def __init__(self):
        GenericNetlinkSocket.__init__(self)
        self.bind(OVS_PACKET_FAMILY, OvsPacket.ovs_packet_msg)

    def upcall_handler(self, up=None):
        print("listening on upcall packet handler:", self.epid)
        while True:
            try:
                msgs = self.get()
                for msg in msgs:
                    if not up:
                        continue
                    if msg["cmd"] == OvsPacket.OVS_PACKET_CMD_MISS:
                        up.miss(msg)
                    elif msg["cmd"] == OvsPacket.OVS_PACKET_CMD_ACTION:
                        up.action(msg)
                    elif msg["cmd"] == OvsPacket.OVS_PACKET_CMD_EXECUTE:
                        up.execute(msg)
                    else:
                        print("Unknown cmd: %d" % msg["cmd"])
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
            ("OVS_DP_ATTR_UNSPEC", "none"),
            ("OVS_DP_ATTR_NAME", "asciiz"),
            ("OVS_DP_ATTR_UPCALL_PID", "array(uint32)"),
            ("OVS_DP_ATTR_STATS", "dpstats"),
            ("OVS_DP_ATTR_MEGAFLOW_STATS", "megaflowstats"),
            ("OVS_DP_ATTR_USER_FEATURES", "uint32"),
            ("OVS_DP_ATTR_PAD", "none"),
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
            if ne.code == errno.ENODEV:
                reply = None
            else:
                raise ne

        return reply

    def create(
        self, dpname, shouldUpcall=False, versionStr=None, p=OvsPacket()
    ):
        msg = OvsDatapath.dp_cmd_msg()
        msg["cmd"] = OVS_DP_CMD_NEW
        if versionStr is None:
            msg["version"] = OVS_DATAPATH_VERSION
        else:
            msg["version"] = int(versionStr.split(":")[0], 0)
        msg["reserved"] = 0
        msg["dpifindex"] = 0
        msg["attrs"].append(["OVS_DP_ATTR_NAME", dpname])

        dpfeatures = 0
        if versionStr is not None and versionStr.find(":") != -1:
            dpfeatures = int(versionStr.split(":")[1], 0)
        else:
            if versionStr is None or versionStr.find(":") == -1:
                dpfeatures |= OvsDatapath.OVS_DP_F_DISPATCH_UPCALL_PER_CPU
                dpfeatures &= ~OvsDatapath.OVS_DP_F_VPORT_PIDS

            nproc = multiprocessing.cpu_count()
            procarray = []
            for i in range(1, nproc):
                procarray += [int(p.epid)]
            msg["attrs"].append(["OVS_DP_ATTR_UPCALL_PID", procarray])
        msg["attrs"].append(["OVS_DP_ATTR_USER_FEATURES", dpfeatures])
        if not shouldUpcall:
            msg["attrs"].append(["OVS_DP_ATTR_UPCALL_PID", [0]])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST | NLM_F_ACK
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == errno.EEXIST:
                reply = None
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
            if ne.code == errno.ENODEV:
                reply = None
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
            ("OVS_VPORT_ATTR_UNSPEC", "none"),
            ("OVS_VPORT_ATTR_PORT_NO", "uint32"),
            ("OVS_VPORT_ATTR_TYPE", "uint32"),
            ("OVS_VPORT_ATTR_NAME", "asciiz"),
            ("OVS_VPORT_ATTR_OPTIONS", "vportopts"),
            ("OVS_VPORT_ATTR_UPCALL_PID", "array(uint32)"),
            ("OVS_VPORT_ATTR_STATS", "vportstats"),
            ("OVS_VPORT_ATTR_PAD", "none"),
            ("OVS_VPORT_ATTR_IFINDEX", "uint32"),
            ("OVS_VPORT_ATTR_NETNSID", "uint32"),
        )

        class vportopts(nla):
            nla_map = (
                ("OVS_TUNNEL_ATTR_UNSPEC", "none"),
                ("OVS_TUNNEL_ATTR_DST_PORT", "uint16"),
                ("OVS_TUNNEL_ATTR_EXTENSION", "none"),
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
        raise ValueError("Unknown vport type:%d" % vport_type)

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
        raise ValueError("Unknown vport type: '%s'" % vport_type)

    def __init__(self, packet=OvsPacket()):
        GenericNetlinkSocket.__init__(self)
        self.bind(OVS_VPORT_FAMILY, OvsVport.ovs_vport_msg)
        self.upcall_packet = packet

    def info(self, vport_name, dpifindex=0, portno=None):
        msg = OvsVport.ovs_vport_msg()

        msg["cmd"] = OVS_VPORT_CMD_GET
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = dpifindex

        if portno is None:
            msg["attrs"].append(["OVS_VPORT_ATTR_NAME", vport_name])
        else:
            msg["attrs"].append(["OVS_VPORT_ATTR_PORT_NO", portno])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == errno.ENODEV:
                reply = None
            else:
                raise ne
        return reply

    def attach(self, dpindex, vport_ifname, ptype, dport, lwt):
        msg = OvsVport.ovs_vport_msg()

        msg["cmd"] = OVS_VPORT_CMD_NEW
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = dpindex
        port_type = OvsVport.str_to_type(ptype)

        msg["attrs"].append(["OVS_VPORT_ATTR_NAME", vport_ifname])
        msg["attrs"].append(
            ["OVS_VPORT_ATTR_UPCALL_PID", [self.upcall_packet.epid]]
        )

        TUNNEL_DEFAULTS = [("geneve", 6081),
                           ("vxlan", 4789)]

        for tnl in TUNNEL_DEFAULTS:
            if ptype == tnl[0]:
                if not dport:
                    dport = tnl[1]

                if not lwt:
                    vportopt = OvsVport.ovs_vport_msg.vportopts()
                    vportopt["attrs"].append(
                        ["OVS_TUNNEL_ATTR_DST_PORT", socket.htons(dport)]
                    )
                    msg["attrs"].append(
                        ["OVS_VPORT_ATTR_OPTIONS", vportopt]
                    )
                else:
                    port_type = OvsVport.OVS_VPORT_TYPE_NETDEV
                    ipr = pyroute2.iproute.IPRoute()

                    if tnl[0] == "geneve":
                        ipr.link("add", ifname=vport_ifname, kind=tnl[0],
                                 geneve_port=dport,
                                 geneve_collect_metadata=True,
                                 geneve_udp_zero_csum6_rx=1)
                    elif tnl[0] == "vxlan":
                        ipr.link("add", ifname=vport_ifname, kind=tnl[0],
                                 vxlan_learning=0, vxlan_collect_metadata=1,
                                 vxlan_udp_zero_csum6_rx=1, vxlan_port=dport)
                break
        msg["attrs"].append(["OVS_VPORT_ATTR_TYPE", port_type])

        try:
            reply = self.nlm_request(
                msg, msg_type=self.prid, msg_flags=NLM_F_REQUEST | NLM_F_ACK
            )
            reply = reply[0]
        except NetlinkError as ne:
            if ne.code == errno.EEXIST:
                reply = None
            else:
                raise ne
        return reply

    def reset_upcall(self, dpindex, vport_ifname, p=None):
        msg = OvsVport.ovs_vport_msg()

        msg["cmd"] = OVS_VPORT_CMD_SET
        msg["version"] = OVS_DATAPATH_VERSION
        msg["reserved"] = 0
        msg["dpifindex"] = dpindex
        msg["attrs"].append(["OVS_VPORT_ATTR_NAME", vport_ifname])

        if p == None:
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
            if ne.code == errno.ENODEV:
                reply = None
            else:
                raise ne
        return reply

    def upcall_handler(self, handler=None):
        self.upcall_packet.upcall_handler(handler)


class OvsFlow(GenericNetlinkSocket):
    class ovs_flow_msg(ovs_dp_msg):
        nla_map = (
            ("OVS_FLOW_ATTR_UNSPEC", "none"),
            ("OVS_FLOW_ATTR_KEY", "ovskey"),
            ("OVS_FLOW_ATTR_ACTIONS", "ovsactions"),
            ("OVS_FLOW_ATTR_STATS", "flowstats"),
            ("OVS_FLOW_ATTR_TCP_FLAGS", "uint8"),
            ("OVS_FLOW_ATTR_USED", "uint64"),
            ("OVS_FLOW_ATTR_CLEAR", "none"),
            ("OVS_FLOW_ATTR_MASK", "ovskey"),
            ("OVS_FLOW_ATTR_PROBE", "none"),
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
            if ufid is not None:
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
            keymsg = None
            if key_field is not None:
                keymsg = key_field

            mask_field = self.get_attr("OVS_FLOW_ATTR_MASK")
            maskmsg = None
            if mask_field is not None:
                maskmsg = mask_field

            acts_field = self.get_attr("OVS_FLOW_ATTR_ACTIONS")
            actsmsg = None
            if acts_field is not None:
                actsmsg = acts_field

            print_str = ""

            if more:
                print_str += ufid_str + ","

            if keymsg is not None:
                print_str += keymsg.dpstr(maskmsg, more)

            stats = self.get_attr("OVS_FLOW_ATTR_STATS")
            if stats is None:
                print_str += " packets:0, bytes:0,"
            else:
                print_str += " packets:%d, bytes:%d," % (
                    stats["packets"],
                    stats["bytes"],
                )

            used = self.get_attr("OVS_FLOW_ATTR_USED")
            print_str += " used:"
            if used is None:
                print_str += "never,"
            else:
                used_time = int(used)
                cur_time_sec = time.clock_gettime(time.CLOCK_MONOTONIC)
                used_time = (cur_time_sec * 1000) - used_time
                print_str += "{}s,".format(used_time / 1000)

            print_str += " actions:"
            if (
                actsmsg is None
                or "attrs" not in actsmsg
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

    def dump(self, dpifindex, flowspec=None):
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
        if flowspec is None:
            msg_flags |= NLM_F_DUMP
        rep = None

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
        keystr = "(none)"
        key_field = packetmsg.get_attr("OVS_PACKET_ATTR_KEY")
        if key_field is not None:
            keystr = key_field.dpstr(None, True)

        pktdata = packetmsg.get_attr("OVS_PACKET_ATTR_PACKET")
        pktpres = "yes" if pktdata is not None else "no"

        print("MISS upcall[%d/%s]: %s" % (seq, pktpres, keystr), flush=True)

    def execute(self, packetmsg):
        print("userspace execute command", flush=True)

    def action(self, packetmsg):
        print("userspace action command", flush=True)


class psample_sample(genlmsg):
    nla_map = (
        ("PSAMPLE_ATTR_IIFINDEX", "none"),
        ("PSAMPLE_ATTR_OIFINDEX", "none"),
        ("PSAMPLE_ATTR_ORIGSIZE", "none"),
        ("PSAMPLE_ATTR_SAMPLE_GROUP", "uint32"),
        ("PSAMPLE_ATTR_GROUP_SEQ", "none"),
        ("PSAMPLE_ATTR_SAMPLE_RATE", "uint32"),
        ("PSAMPLE_ATTR_DATA", "array(uint8)"),
        ("PSAMPLE_ATTR_GROUP_REFCOUNT", "none"),
        ("PSAMPLE_ATTR_TUNNEL", "none"),
        ("PSAMPLE_ATTR_PAD", "none"),
        ("PSAMPLE_ATTR_OUT_TC", "none"),
        ("PSAMPLE_ATTR_OUT_TC_OCC", "none"),
        ("PSAMPLE_ATTR_LATENCY", "none"),
        ("PSAMPLE_ATTR_TIMESTAMP", "none"),
        ("PSAMPLE_ATTR_PROTO", "none"),
        ("PSAMPLE_ATTR_USER_COOKIE", "array(uint8)"),
    )

    def dpstr(self):
        fields = []
        data = ""
        for (attr, value) in self["attrs"]:
            if attr == "PSAMPLE_ATTR_SAMPLE_GROUP":
                fields.append("group:%d" % value)
            if attr == "PSAMPLE_ATTR_SAMPLE_RATE":
                fields.append("rate:%d" % value)
            if attr == "PSAMPLE_ATTR_USER_COOKIE":
                value = "".join(format(x, "02x") for x in value)
                fields.append("cookie:%s" % value)
            if attr == "PSAMPLE_ATTR_DATA" and len(value) > 0:
                data = "data:%s" % "".join(format(x, "02x") for x in value)

        return ("%s %s" % (",".join(fields), data)).strip()


class psample_msg(Marshal):
    PSAMPLE_CMD_SAMPLE = 0
    PSAMPLE_CMD_GET_GROUP = 1
    PSAMPLE_CMD_NEW_GROUP = 2
    PSAMPLE_CMD_DEL_GROUP = 3
    PSAMPLE_CMD_SET_FILTER = 4
    msg_map = {PSAMPLE_CMD_SAMPLE: psample_sample}


class PsampleEvent(EventSocket):
    genl_family = "psample"
    mcast_groups = ["packets"]
    marshal_class = psample_msg

    def read_samples(self):
        print("listening for psample events", flush=True)
        while True:
            try:
                for msg in self.get():
                    print(msg.dpstr(), flush=True)
            except NetlinkError as ne:
                raise ne


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

    if user_features is not None:
        print("  features: 0x%X" % user_features)

    # port print out
    for iface in ndb.interfaces:
        rep = vpl.info(iface.ifname, ifindex)
        if rep is not None:
            opts = ""
            vpo = rep.get_attr("OVS_VPORT_ATTR_OPTIONS")
            if vpo:
                dpo = vpo.get_attr("OVS_TUNNEL_ATTR_DST_PORT")
                if dpo:
                    opts += " tnl-dport:%s" % socket.ntohs(dpo)
            print(
                "  port %d: %s (%s%s)"
                % (
                    rep.get_attr("OVS_VPORT_ATTR_PORT_NO"),
                    rep.get_attr("OVS_VPORT_ATTR_NAME"),
                    OvsVport.type_to_str(rep.get_attr("OVS_VPORT_ATTR_TYPE")),
                    opts,
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
    subparsers = parser.add_subparsers(dest="subcommand")

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
        choices=["netdev", "internal", "geneve", "vxlan"],
        help="Interface type (default netdev)",
    )
    addifcmd.add_argument(
        "-p",
        "--dport",
        type=int,
        default=0,
        help="Destination port (0 for default)"
    )
    addifcmd.add_argument(
        "-l",
        "--lwt",
        type=bool,
        default=True,
        help="Use LWT infrastructure instead of vport (default true)."
    )
    delifcmd = subparsers.add_parser("del-if")
    delifcmd.add_argument("dpname", help="Datapath Name")
    delifcmd.add_argument("delif", help="Interface name for adding")
    delifcmd.add_argument("-d",
                          "--dellink",
                          type=bool, default=False,
                          help="Delete the link as well.")

    dumpflcmd = subparsers.add_parser("dump-flows")
    dumpflcmd.add_argument("dumpdp", help="Datapath Name")

    addflcmd = subparsers.add_parser("add-flow")
    addflcmd.add_argument("flbr", help="Datapath name")
    addflcmd.add_argument("flow", help="Flow specification")
    addflcmd.add_argument("acts", help="Flow actions")

    delfscmd = subparsers.add_parser("del-flows")
    delfscmd.add_argument("flsbr", help="Datapath name")

    subparsers.add_parser("psample-events")

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

    if args.subcommand == "psample-events":
        PsampleEvent().read_samples()

    if hasattr(args, "showdp"):
        found = False
        for iface in ndb.interfaces:
            rep = None
            if args.showdp is None:
                rep = ovsdp.info(iface.ifname, 0)
            elif args.showdp == iface.ifname:
                rep = ovsdp.info(iface.ifname, 0)

            if rep is not None:
                found = True
                print_ovsdp_full(rep, iface.index, ndb, ovsvp)

        if not found:
            msg = "No DP found"
            if args.showdp is not None:
                msg += ":'%s'" % args.showdp
            print(msg)
    elif hasattr(args, "adddp"):
        rep = ovsdp.create(args.adddp, args.upcall, args.versioning, ovspk)
        if rep is None:
            print("DP '%s' already exists" % args.adddp)
        else:
            print("DP '%s' added" % args.adddp)
        if args.upcall:
            ovspk.upcall_handler(ovsflow)
    elif hasattr(args, "deldp"):
        ovsdp.destroy(args.deldp)
    elif hasattr(args, "addif"):
        rep = ovsdp.info(args.dpname, 0)
        if rep is None:
            print("DP '%s' not found." % args.dpname)
            return 1
        dpindex = rep["dpifindex"]
        rep = ovsvp.attach(rep["dpifindex"], args.addif, args.ptype,
                           args.dport, args.lwt)
        msg = "vport '%s'" % args.addif
        if rep and rep["header"]["error"] is None:
            msg += " added."
        else:
            msg += " failed to add."
        if args.upcall:
            if rep is None:
                rep = ovsvp.reset_upcall(dpindex, args.addif, ovspk)
            ovsvp.upcall_handler(ovsflow)
    elif hasattr(args, "delif"):
        rep = ovsdp.info(args.dpname, 0)
        if rep is None:
            print("DP '%s' not found." % args.dpname)
            return 1
        rep = ovsvp.detach(rep["dpifindex"], args.delif)
        msg = "vport '%s'" % args.delif
        if rep and rep["header"]["error"] is None:
            msg += " removed."
        else:
            msg += " failed to remove."
        if args.dellink:
            ipr = pyroute2.iproute.IPRoute()
            ipr.link("del", index=ipr.link_lookup(ifname=args.delif)[0])
    elif hasattr(args, "dumpdp"):
        rep = ovsdp.info(args.dumpdp, 0)
        if rep is None:
            print("DP '%s' not found." % args.dumpdp)
            return 1
        rep = ovsflow.dump(rep["dpifindex"])
        for flow in rep:
            print(flow.dpstr(True if args.verbose > 0 else False))
    elif hasattr(args, "flbr"):
        rep = ovsdp.info(args.flbr, 0)
        if rep is None:
            print("DP '%s' not found." % args.flbr)
            return 1
        flow = OvsFlow.ovs_flow_msg()
        flow.parse(args.flow, args.acts, rep["dpifindex"])
        ovsflow.add_flow(rep["dpifindex"], flow)
    elif hasattr(args, "flsbr"):
        rep = ovsdp.info(args.flsbr, 0)
        if rep is None:
            print("DP '%s' not found." % args.flsbr)
        ovsflow.del_flows(rep["dpifindex"])

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
