# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

from collections import namedtuple
from enum import Enum
import functools
import os
import random
import socket
import struct
from struct import Struct
import sys
import yaml
import ipaddress
import uuid
import queue
import selectors
import time

from .nlspec import SpecFamily

#
# Generic Netlink code which should really be in some library, but I can't quickly find one.
#


class Netlink:
    # Netlink socket
    SOL_NETLINK = 270

    NETLINK_ADD_MEMBERSHIP = 1
    NETLINK_CAP_ACK = 10
    NETLINK_EXT_ACK = 11
    NETLINK_GET_STRICT_CHK = 12

    # Netlink message
    NLMSG_ERROR = 2
    NLMSG_DONE = 3

    NLM_F_REQUEST = 1
    NLM_F_ACK = 4
    NLM_F_ROOT = 0x100
    NLM_F_MATCH = 0x200

    NLM_F_REPLACE = 0x100
    NLM_F_EXCL = 0x200
    NLM_F_CREATE = 0x400
    NLM_F_APPEND = 0x800

    NLM_F_CAPPED = 0x100
    NLM_F_ACK_TLVS = 0x200

    NLM_F_DUMP = NLM_F_ROOT | NLM_F_MATCH

    NLA_F_NESTED = 0x8000
    NLA_F_NET_BYTEORDER = 0x4000

    NLA_TYPE_MASK = NLA_F_NESTED | NLA_F_NET_BYTEORDER

    # Genetlink defines
    NETLINK_GENERIC = 16

    GENL_ID_CTRL = 0x10

    # nlctrl
    CTRL_CMD_GETFAMILY = 3

    CTRL_ATTR_FAMILY_ID = 1
    CTRL_ATTR_FAMILY_NAME = 2
    CTRL_ATTR_MAXATTR = 5
    CTRL_ATTR_MCAST_GROUPS = 7

    CTRL_ATTR_MCAST_GRP_NAME = 1
    CTRL_ATTR_MCAST_GRP_ID = 2

    # Extack types
    NLMSGERR_ATTR_MSG = 1
    NLMSGERR_ATTR_OFFS = 2
    NLMSGERR_ATTR_COOKIE = 3
    NLMSGERR_ATTR_POLICY = 4
    NLMSGERR_ATTR_MISS_TYPE = 5
    NLMSGERR_ATTR_MISS_NEST = 6

    # Policy types
    NL_POLICY_TYPE_ATTR_TYPE = 1
    NL_POLICY_TYPE_ATTR_MIN_VALUE_S = 2
    NL_POLICY_TYPE_ATTR_MAX_VALUE_S = 3
    NL_POLICY_TYPE_ATTR_MIN_VALUE_U = 4
    NL_POLICY_TYPE_ATTR_MAX_VALUE_U = 5
    NL_POLICY_TYPE_ATTR_MIN_LENGTH = 6
    NL_POLICY_TYPE_ATTR_MAX_LENGTH = 7
    NL_POLICY_TYPE_ATTR_POLICY_IDX = 8
    NL_POLICY_TYPE_ATTR_POLICY_MAXTYPE = 9
    NL_POLICY_TYPE_ATTR_BITFIELD32_MASK = 10
    NL_POLICY_TYPE_ATTR_PAD = 11
    NL_POLICY_TYPE_ATTR_MASK = 12

    AttrType = Enum('AttrType', ['flag', 'u8', 'u16', 'u32', 'u64',
                                  's8', 's16', 's32', 's64',
                                  'binary', 'string', 'nul-string',
                                  'nested', 'nested-array',
                                  'bitfield32', 'sint', 'uint'])

class NlError(Exception):
  def __init__(self, nl_msg):
    self.nl_msg = nl_msg
    self.error = -nl_msg.error

  def __str__(self):
    return f"Netlink error: {os.strerror(self.error)}\n{self.nl_msg}"


class ConfigError(Exception):
    pass


class NlAttr:
    ScalarFormat = namedtuple('ScalarFormat', ['native', 'big', 'little'])
    type_formats = {
        'u8' : ScalarFormat(Struct('B'), Struct("B"),  Struct("B")),
        's8' : ScalarFormat(Struct('b'), Struct("b"),  Struct("b")),
        'u16': ScalarFormat(Struct('H'), Struct(">H"), Struct("<H")),
        's16': ScalarFormat(Struct('h'), Struct(">h"), Struct("<h")),
        'u32': ScalarFormat(Struct('I'), Struct(">I"), Struct("<I")),
        's32': ScalarFormat(Struct('i'), Struct(">i"), Struct("<i")),
        'u64': ScalarFormat(Struct('Q'), Struct(">Q"), Struct("<Q")),
        's64': ScalarFormat(Struct('q'), Struct(">q"), Struct("<q"))
    }

    def __init__(self, raw, offset):
        self._len, self._type = struct.unpack("HH", raw[offset : offset + 4])
        self.type = self._type & ~Netlink.NLA_TYPE_MASK
        self.is_nest = self._type & Netlink.NLA_F_NESTED
        self.payload_len = self._len
        self.full_len = (self.payload_len + 3) & ~3
        self.raw = raw[offset + 4 : offset + self.payload_len]

    @classmethod
    def get_format(cls, attr_type, byte_order=None):
        format = cls.type_formats[attr_type]
        if byte_order:
            return format.big if byte_order == "big-endian" \
                else format.little
        return format.native

    def as_scalar(self, attr_type, byte_order=None):
        format = self.get_format(attr_type, byte_order)
        return format.unpack(self.raw)[0]

    def as_auto_scalar(self, attr_type, byte_order=None):
        if len(self.raw) != 4 and len(self.raw) != 8:
            raise Exception(f"Auto-scalar len payload be 4 or 8 bytes, got {len(self.raw)}")
        real_type = attr_type[0] + str(len(self.raw) * 8)
        format = self.get_format(real_type, byte_order)
        return format.unpack(self.raw)[0]

    def as_strz(self):
        return self.raw.decode('ascii')[:-1]

    def as_bin(self):
        return self.raw

    def as_c_array(self, type):
        format = self.get_format(type)
        return [ x[0] for x in format.iter_unpack(self.raw) ]

    def __repr__(self):
        return f"[type:{self.type} len:{self._len}] {self.raw}"


class NlAttrs:
    def __init__(self, msg, offset=0):
        self.attrs = []

        while offset < len(msg):
            attr = NlAttr(msg, offset)
            offset += attr.full_len
            self.attrs.append(attr)

    def __iter__(self):
        yield from self.attrs

    def __repr__(self):
        msg = ''
        for a in self.attrs:
            if msg:
                msg += '\n'
            msg += repr(a)
        return msg


class NlMsg:
    def __init__(self, msg, offset, attr_space=None):
        self.hdr = msg[offset : offset + 16]

        self.nl_len, self.nl_type, self.nl_flags, self.nl_seq, self.nl_portid = \
            struct.unpack("IHHII", self.hdr)

        self.raw = msg[offset + 16 : offset + self.nl_len]

        self.error = 0
        self.done = 0

        extack_off = None
        if self.nl_type == Netlink.NLMSG_ERROR:
            self.error = struct.unpack("i", self.raw[0:4])[0]
            self.done = 1
            extack_off = 20
        elif self.nl_type == Netlink.NLMSG_DONE:
            self.error = struct.unpack("i", self.raw[0:4])[0]
            self.done = 1
            extack_off = 4

        self.extack = None
        if self.nl_flags & Netlink.NLM_F_ACK_TLVS and extack_off:
            self.extack = dict()
            extack_attrs = NlAttrs(self.raw[extack_off:])
            for extack in extack_attrs:
                if extack.type == Netlink.NLMSGERR_ATTR_MSG:
                    self.extack['msg'] = extack.as_strz()
                elif extack.type == Netlink.NLMSGERR_ATTR_MISS_TYPE:
                    self.extack['miss-type'] = extack.as_scalar('u32')
                elif extack.type == Netlink.NLMSGERR_ATTR_MISS_NEST:
                    self.extack['miss-nest'] = extack.as_scalar('u32')
                elif extack.type == Netlink.NLMSGERR_ATTR_OFFS:
                    self.extack['bad-attr-offs'] = extack.as_scalar('u32')
                elif extack.type == Netlink.NLMSGERR_ATTR_POLICY:
                    self.extack['policy'] = self._decode_policy(extack.raw)
                else:
                    if 'unknown' not in self.extack:
                        self.extack['unknown'] = []
                    self.extack['unknown'].append(extack)

            if attr_space:
                # We don't have the ability to parse nests yet, so only do global
                if 'miss-type' in self.extack and 'miss-nest' not in self.extack:
                    miss_type = self.extack['miss-type']
                    if miss_type in attr_space.attrs_by_val:
                        spec = attr_space.attrs_by_val[miss_type]
                        self.extack['miss-type'] = spec['name']
                        if 'doc' in spec:
                            self.extack['miss-type-doc'] = spec['doc']

    def _decode_policy(self, raw):
        policy = {}
        for attr in NlAttrs(raw):
            if attr.type == Netlink.NL_POLICY_TYPE_ATTR_TYPE:
                type = attr.as_scalar('u32')
                policy['type'] = Netlink.AttrType(type).name
            elif attr.type == Netlink.NL_POLICY_TYPE_ATTR_MIN_VALUE_S:
                policy['min-value'] = attr.as_scalar('s64')
            elif attr.type == Netlink.NL_POLICY_TYPE_ATTR_MAX_VALUE_S:
                policy['max-value'] = attr.as_scalar('s64')
            elif attr.type == Netlink.NL_POLICY_TYPE_ATTR_MIN_VALUE_U:
                policy['min-value'] = attr.as_scalar('u64')
            elif attr.type == Netlink.NL_POLICY_TYPE_ATTR_MAX_VALUE_U:
                policy['max-value'] = attr.as_scalar('u64')
            elif attr.type == Netlink.NL_POLICY_TYPE_ATTR_MIN_LENGTH:
                policy['min-length'] = attr.as_scalar('u32')
            elif attr.type == Netlink.NL_POLICY_TYPE_ATTR_MAX_LENGTH:
                policy['max-length'] = attr.as_scalar('u32')
            elif attr.type == Netlink.NL_POLICY_TYPE_ATTR_BITFIELD32_MASK:
                policy['bitfield32-mask'] = attr.as_scalar('u32')
            elif attr.type == Netlink.NL_POLICY_TYPE_ATTR_MASK:
                policy['mask'] = attr.as_scalar('u64')
        return policy

    def cmd(self):
        return self.nl_type

    def __repr__(self):
        msg = f"nl_len = {self.nl_len} ({len(self.raw)}) nl_flags = 0x{self.nl_flags:x} nl_type = {self.nl_type}"
        if self.error:
            msg += '\n\terror: ' + str(self.error)
        if self.extack:
            msg += '\n\textack: ' + repr(self.extack)
        return msg


class NlMsgs:
    def __init__(self, data, attr_space=None):
        self.msgs = []

        offset = 0
        while offset < len(data):
            msg = NlMsg(data, offset, attr_space=attr_space)
            offset += msg.nl_len
            self.msgs.append(msg)

    def __iter__(self):
        yield from self.msgs


genl_family_name_to_id = None


def _genl_msg(nl_type, nl_flags, genl_cmd, genl_version, seq=None):
    # we prepend length in _genl_msg_finalize()
    if seq is None:
        seq = random.randint(1, 1024)
    nlmsg = struct.pack("HHII", nl_type, nl_flags, seq, 0)
    genlmsg = struct.pack("BBH", genl_cmd, genl_version, 0)
    return nlmsg + genlmsg


def _genl_msg_finalize(msg):
    return struct.pack("I", len(msg) + 4) + msg


def _genl_load_families():
    with socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, Netlink.NETLINK_GENERIC) as sock:
        sock.setsockopt(Netlink.SOL_NETLINK, Netlink.NETLINK_CAP_ACK, 1)

        msg = _genl_msg(Netlink.GENL_ID_CTRL,
                        Netlink.NLM_F_REQUEST | Netlink.NLM_F_ACK | Netlink.NLM_F_DUMP,
                        Netlink.CTRL_CMD_GETFAMILY, 1)
        msg = _genl_msg_finalize(msg)

        sock.send(msg, 0)

        global genl_family_name_to_id
        genl_family_name_to_id = dict()

        while True:
            reply = sock.recv(128 * 1024)
            nms = NlMsgs(reply)
            for nl_msg in nms:
                if nl_msg.error:
                    print("Netlink error:", nl_msg.error)
                    return
                if nl_msg.done:
                    return

                gm = GenlMsg(nl_msg)
                fam = dict()
                for attr in NlAttrs(gm.raw):
                    if attr.type == Netlink.CTRL_ATTR_FAMILY_ID:
                        fam['id'] = attr.as_scalar('u16')
                    elif attr.type == Netlink.CTRL_ATTR_FAMILY_NAME:
                        fam['name'] = attr.as_strz()
                    elif attr.type == Netlink.CTRL_ATTR_MAXATTR:
                        fam['maxattr'] = attr.as_scalar('u32')
                    elif attr.type == Netlink.CTRL_ATTR_MCAST_GROUPS:
                        fam['mcast'] = dict()
                        for entry in NlAttrs(attr.raw):
                            mcast_name = None
                            mcast_id = None
                            for entry_attr in NlAttrs(entry.raw):
                                if entry_attr.type == Netlink.CTRL_ATTR_MCAST_GRP_NAME:
                                    mcast_name = entry_attr.as_strz()
                                elif entry_attr.type == Netlink.CTRL_ATTR_MCAST_GRP_ID:
                                    mcast_id = entry_attr.as_scalar('u32')
                            if mcast_name and mcast_id is not None:
                                fam['mcast'][mcast_name] = mcast_id
                if 'name' in fam and 'id' in fam:
                    genl_family_name_to_id[fam['name']] = fam


class GenlMsg:
    def __init__(self, nl_msg):
        self.nl = nl_msg
        self.genl_cmd, self.genl_version, _ = struct.unpack_from("BBH", nl_msg.raw, 0)
        self.raw = nl_msg.raw[4:]

    def cmd(self):
        return self.genl_cmd

    def __repr__(self):
        msg = repr(self.nl)
        msg += f"\tgenl_cmd = {self.genl_cmd} genl_ver = {self.genl_version}\n"
        for a in self.raw_attrs:
            msg += '\t\t' + repr(a) + '\n'
        return msg


class NetlinkProtocol:
    def __init__(self, family_name, proto_num):
        self.family_name = family_name
        self.proto_num = proto_num

    def _message(self, nl_type, nl_flags, seq=None):
        if seq is None:
            seq = random.randint(1, 1024)
        nlmsg = struct.pack("HHII", nl_type, nl_flags, seq, 0)
        return nlmsg

    def message(self, flags, command, version, seq=None):
        return self._message(command, flags, seq)

    def _decode(self, nl_msg):
        return nl_msg

    def decode(self, ynl, nl_msg, op):
        msg = self._decode(nl_msg)
        if op is None:
            op = ynl.rsp_by_value[msg.cmd()]
        fixed_header_size = ynl._struct_size(op.fixed_header)
        msg.raw_attrs = NlAttrs(msg.raw, fixed_header_size)
        return msg

    def get_mcast_id(self, mcast_name, mcast_groups):
        if mcast_name not in mcast_groups:
            raise Exception(f'Multicast group "{mcast_name}" not present in the spec')
        return mcast_groups[mcast_name].value

    def msghdr_size(self):
        return 16


class GenlProtocol(NetlinkProtocol):
    def __init__(self, family_name):
        super().__init__(family_name, Netlink.NETLINK_GENERIC)

        global genl_family_name_to_id
        if genl_family_name_to_id is None:
            _genl_load_families()

        self.genl_family = genl_family_name_to_id[family_name]
        self.family_id = genl_family_name_to_id[family_name]['id']

    def message(self, flags, command, version, seq=None):
        nlmsg = self._message(self.family_id, flags, seq)
        genlmsg = struct.pack("BBH", command, version, 0)
        return nlmsg + genlmsg

    def _decode(self, nl_msg):
        return GenlMsg(nl_msg)

    def get_mcast_id(self, mcast_name, mcast_groups):
        if mcast_name not in self.genl_family['mcast']:
            raise Exception(f'Multicast group "{mcast_name}" not present in the family')
        return self.genl_family['mcast'][mcast_name]

    def msghdr_size(self):
        return super().msghdr_size() + 4


class SpaceAttrs:
    SpecValuesPair = namedtuple('SpecValuesPair', ['spec', 'values'])

    def __init__(self, attr_space, attrs, outer = None):
        outer_scopes = outer.scopes if outer else []
        inner_scope = self.SpecValuesPair(attr_space, attrs)
        self.scopes = [inner_scope] + outer_scopes

    def lookup(self, name):
        for scope in self.scopes:
            if name in scope.spec:
                if name in scope.values:
                    return scope.values[name]
                spec_name = scope.spec.yaml['name']
                raise Exception(
                    f"No value for '{name}' in attribute space '{spec_name}'")
        raise Exception(f"Attribute '{name}' not defined in any attribute-set")


#
# YNL implementation details.
#


class YnlFamily(SpecFamily):
    def __init__(self, def_path, schema=None, process_unknown=False,
                 recv_size=0):
        super().__init__(def_path, schema)

        self.include_raw = False
        self.process_unknown = process_unknown

        try:
            if self.proto == "netlink-raw":
                self.nlproto = NetlinkProtocol(self.yaml['name'],
                                               self.yaml['protonum'])
            else:
                self.nlproto = GenlProtocol(self.yaml['name'])
        except KeyError:
            raise Exception(f"Family '{self.yaml['name']}' not supported by the kernel")

        self._recv_dbg = False
        # Note that netlink will use conservative (min) message size for
        # the first dump recv() on the socket, our setting will only matter
        # from the second recv() on.
        self._recv_size = recv_size if recv_size else 131072
        # Netlink will always allocate at least PAGE_SIZE - sizeof(skb_shinfo)
        # for a message, so smaller receive sizes will lead to truncation.
        # Note that the min size for other families may be larger than 4k!
        if self._recv_size < 4000:
            raise ConfigError()

        self.sock = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, self.nlproto.proto_num)
        self.sock.setsockopt(Netlink.SOL_NETLINK, Netlink.NETLINK_CAP_ACK, 1)
        self.sock.setsockopt(Netlink.SOL_NETLINK, Netlink.NETLINK_EXT_ACK, 1)
        self.sock.setsockopt(Netlink.SOL_NETLINK, Netlink.NETLINK_GET_STRICT_CHK, 1)

        self.async_msg_ids = set()
        self.async_msg_queue = queue.Queue()

        for msg in self.msgs.values():
            if msg.is_async:
                self.async_msg_ids.add(msg.rsp_value)

        for op_name, op in self.ops.items():
            bound_f = functools.partial(self._op, op_name)
            setattr(self, op.ident_name, bound_f)


    def ntf_subscribe(self, mcast_name):
        mcast_id = self.nlproto.get_mcast_id(mcast_name, self.mcast_groups)
        self.sock.bind((0, 0))
        self.sock.setsockopt(Netlink.SOL_NETLINK, Netlink.NETLINK_ADD_MEMBERSHIP,
                             mcast_id)

    def set_recv_dbg(self, enabled):
        self._recv_dbg = enabled

    def _recv_dbg_print(self, reply, nl_msgs):
        if not self._recv_dbg:
            return
        print("Recv: read", len(reply), "bytes,",
              len(nl_msgs.msgs), "messages", file=sys.stderr)
        for nl_msg in nl_msgs:
            print("  ", nl_msg, file=sys.stderr)

    def _encode_enum(self, attr_spec, value):
        enum = self.consts[attr_spec['enum']]
        if enum.type == 'flags' or attr_spec.get('enum-as-flags', False):
            scalar = 0
            if isinstance(value, str):
                value = [value]
            for single_value in value:
                scalar += enum.entries[single_value].user_value(as_flags = True)
            return scalar
        else:
            return enum.entries[value].user_value()

    def _get_scalar(self, attr_spec, value):
        try:
            return int(value)
        except (ValueError, TypeError) as e:
            if 'enum' not in attr_spec:
                raise e
        return self._encode_enum(attr_spec, value)

    def _add_attr(self, space, name, value, search_attrs):
        try:
            attr = self.attr_sets[space][name]
        except KeyError:
            raise Exception(f"Space '{space}' has no attribute '{name}'")
        nl_type = attr.value

        if attr.is_multi and isinstance(value, list):
            attr_payload = b''
            for subvalue in value:
                attr_payload += self._add_attr(space, name, subvalue, search_attrs)
            return attr_payload

        if attr["type"] == 'nest':
            nl_type |= Netlink.NLA_F_NESTED
            attr_payload = b''
            sub_space = attr['nested-attributes']
            sub_attrs = SpaceAttrs(self.attr_sets[sub_space], value, search_attrs)
            for subname, subvalue in value.items():
                attr_payload += self._add_attr(sub_space, subname, subvalue, sub_attrs)
        elif attr["type"] == 'flag':
            if not value:
                # If value is absent or false then skip attribute creation.
                return b''
            attr_payload = b''
        elif attr["type"] == 'string':
            attr_payload = str(value).encode('ascii') + b'\x00'
        elif attr["type"] == 'binary':
            if isinstance(value, bytes):
                attr_payload = value
            elif isinstance(value, str):
                attr_payload = bytes.fromhex(value)
            elif isinstance(value, dict) and attr.struct_name:
                attr_payload = self._encode_struct(attr.struct_name, value)
            else:
                raise Exception(f'Unknown type for binary attribute, value: {value}')
        elif attr['type'] in NlAttr.type_formats or attr.is_auto_scalar:
            scalar = self._get_scalar(attr, value)
            if attr.is_auto_scalar:
                attr_type = attr["type"][0] + ('32' if scalar.bit_length() <= 32 else '64')
            else:
                attr_type = attr["type"]
            format = NlAttr.get_format(attr_type, attr.byte_order)
            attr_payload = format.pack(scalar)
        elif attr['type'] in "bitfield32":
            scalar_value = self._get_scalar(attr, value["value"])
            scalar_selector = self._get_scalar(attr, value["selector"])
            attr_payload = struct.pack("II", scalar_value, scalar_selector)
        elif attr['type'] == 'sub-message':
            msg_format = self._resolve_selector(attr, search_attrs)
            attr_payload = b''
            if msg_format.fixed_header:
                attr_payload += self._encode_struct(msg_format.fixed_header, value)
            if msg_format.attr_set:
                if msg_format.attr_set in self.attr_sets:
                    nl_type |= Netlink.NLA_F_NESTED
                    sub_attrs = SpaceAttrs(msg_format.attr_set, value, search_attrs)
                    for subname, subvalue in value.items():
                        attr_payload += self._add_attr(msg_format.attr_set,
                                                       subname, subvalue, sub_attrs)
                else:
                    raise Exception(f"Unknown attribute-set '{msg_format.attr_set}'")
        else:
            raise Exception(f'Unknown type at {space} {name} {value} {attr["type"]}')

        pad = b'\x00' * ((4 - len(attr_payload) % 4) % 4)
        return struct.pack('HH', len(attr_payload) + 4, nl_type) + attr_payload + pad

    def _decode_enum(self, raw, attr_spec):
        enum = self.consts[attr_spec['enum']]
        if enum.type == 'flags' or attr_spec.get('enum-as-flags', False):
            i = 0
            value = set()
            while raw:
                if raw & 1:
                    value.add(enum.entries_by_val[i].name)
                raw >>= 1
                i += 1
        else:
            value = enum.entries_by_val[raw].name
        return value

    def _decode_binary(self, attr, attr_spec):
        if attr_spec.struct_name:
            decoded = self._decode_struct(attr.raw, attr_spec.struct_name)
        elif attr_spec.sub_type:
            decoded = attr.as_c_array(attr_spec.sub_type)
        else:
            decoded = attr.as_bin()
            if attr_spec.display_hint:
                decoded = self._formatted_string(decoded, attr_spec.display_hint)
        return decoded

    def _decode_array_attr(self, attr, attr_spec):
        decoded = []
        offset = 0
        while offset < len(attr.raw):
            item = NlAttr(attr.raw, offset)
            offset += item.full_len

            if attr_spec["sub-type"] == 'nest':
                subattrs = self._decode(NlAttrs(item.raw), attr_spec['nested-attributes'])
                decoded.append({ item.type: subattrs })
            elif attr_spec["sub-type"] == 'binary':
                subattrs = item.as_bin()
                if attr_spec.display_hint:
                    subattrs = self._formatted_string(subattrs, attr_spec.display_hint)
                decoded.append(subattrs)
            elif attr_spec["sub-type"] in NlAttr.type_formats:
                subattrs = item.as_scalar(attr_spec['sub-type'], attr_spec.byte_order)
                if attr_spec.display_hint:
                    subattrs = self._formatted_string(subattrs, attr_spec.display_hint)
                decoded.append(subattrs)
            else:
                raise Exception(f'Unknown {attr_spec["sub-type"]} with name {attr_spec["name"]}')
        return decoded

    def _decode_nest_type_value(self, attr, attr_spec):
        decoded = {}
        value = attr
        for name in attr_spec['type-value']:
            value = NlAttr(value.raw, 0)
            decoded[name] = value.type
        subattrs = self._decode(NlAttrs(value.raw), attr_spec['nested-attributes'])
        decoded.update(subattrs)
        return decoded

    def _decode_unknown(self, attr):
        if attr.is_nest:
            return self._decode(NlAttrs(attr.raw), None)
        else:
            return attr.as_bin()

    def _rsp_add(self, rsp, name, is_multi, decoded):
        if is_multi == None:
            if name in rsp and type(rsp[name]) is not list:
                rsp[name] = [rsp[name]]
                is_multi = True
            else:
                is_multi = False

        if not is_multi:
            rsp[name] = decoded
        elif name in rsp:
            rsp[name].append(decoded)
        else:
            rsp[name] = [decoded]

    def _resolve_selector(self, attr_spec, search_attrs):
        sub_msg = attr_spec.sub_message
        if sub_msg not in self.sub_msgs:
            raise Exception(f"No sub-message spec named {sub_msg} for {attr_spec.name}")
        sub_msg_spec = self.sub_msgs[sub_msg]

        selector = attr_spec.selector
        value = search_attrs.lookup(selector)
        if value not in sub_msg_spec.formats:
            raise Exception(f"No message format for '{value}' in sub-message spec '{sub_msg}'")

        spec = sub_msg_spec.formats[value]
        return spec

    def _decode_sub_msg(self, attr, attr_spec, search_attrs):
        msg_format = self._resolve_selector(attr_spec, search_attrs)
        decoded = {}
        offset = 0
        if msg_format.fixed_header:
            decoded.update(self._decode_struct(attr.raw, msg_format.fixed_header));
            offset = self._struct_size(msg_format.fixed_header)
        if msg_format.attr_set:
            if msg_format.attr_set in self.attr_sets:
                subdict = self._decode(NlAttrs(attr.raw, offset), msg_format.attr_set)
                decoded.update(subdict)
            else:
                raise Exception(f"Unknown attribute-set '{attr_space}' when decoding '{attr_spec.name}'")
        return decoded

    def _decode(self, attrs, space, outer_attrs = None):
        rsp = dict()
        if space:
            attr_space = self.attr_sets[space]
            search_attrs = SpaceAttrs(attr_space, rsp, outer_attrs)

        for attr in attrs:
            try:
                attr_spec = attr_space.attrs_by_val[attr.type]
            except (KeyError, UnboundLocalError):
                if not self.process_unknown:
                    raise Exception(f"Space '{space}' has no attribute with value '{attr.type}'")
                attr_name = f"UnknownAttr({attr.type})"
                self._rsp_add(rsp, attr_name, None, self._decode_unknown(attr))
                continue

            try:
                if attr_spec["type"] == 'nest':
                    subdict = self._decode(NlAttrs(attr.raw), attr_spec['nested-attributes'], search_attrs)
                    decoded = subdict
                elif attr_spec["type"] == 'string':
                    decoded = attr.as_strz()
                elif attr_spec["type"] == 'binary':
                    decoded = self._decode_binary(attr, attr_spec)
                elif attr_spec["type"] == 'flag':
                    decoded = True
                elif attr_spec.is_auto_scalar:
                    decoded = attr.as_auto_scalar(attr_spec['type'], attr_spec.byte_order)
                elif attr_spec["type"] in NlAttr.type_formats:
                    decoded = attr.as_scalar(attr_spec['type'], attr_spec.byte_order)
                    if 'enum' in attr_spec:
                        decoded = self._decode_enum(decoded, attr_spec)
                    elif attr_spec.display_hint:
                        decoded = self._formatted_string(decoded, attr_spec.display_hint)
                elif attr_spec["type"] == 'indexed-array':
                    decoded = self._decode_array_attr(attr, attr_spec)
                elif attr_spec["type"] == 'bitfield32':
                    value, selector = struct.unpack("II", attr.raw)
                    if 'enum' in attr_spec:
                        value = self._decode_enum(value, attr_spec)
                        selector = self._decode_enum(selector, attr_spec)
                    decoded = {"value": value, "selector": selector}
                elif attr_spec["type"] == 'sub-message':
                    decoded = self._decode_sub_msg(attr, attr_spec, search_attrs)
                elif attr_spec["type"] == 'nest-type-value':
                    decoded = self._decode_nest_type_value(attr, attr_spec)
                else:
                    if not self.process_unknown:
                        raise Exception(f'Unknown {attr_spec["type"]} with name {attr_spec["name"]}')
                    decoded = self._decode_unknown(attr)

                self._rsp_add(rsp, attr_spec["name"], attr_spec.is_multi, decoded)
            except:
                print(f"Error decoding '{attr_spec.name}' from '{space}'")
                raise

        return rsp

    def _decode_extack_path(self, attrs, attr_set, offset, target):
        for attr in attrs:
            try:
                attr_spec = attr_set.attrs_by_val[attr.type]
            except KeyError:
                raise Exception(f"Space '{attr_set.name}' has no attribute with value '{attr.type}'")
            if offset > target:
                break
            if offset == target:
                return '.' + attr_spec.name

            if offset + attr.full_len <= target:
                offset += attr.full_len
                continue
            if attr_spec['type'] != 'nest':
                raise Exception(f"Can't dive into {attr.type} ({attr_spec['name']}) for extack")
            offset += 4
            subpath = self._decode_extack_path(NlAttrs(attr.raw),
                                               self.attr_sets[attr_spec['nested-attributes']],
                                               offset, target)
            if subpath is None:
                return None
            return '.' + attr_spec.name + subpath

        return None

    def _decode_extack(self, request, op, extack):
        if 'bad-attr-offs' not in extack:
            return

        msg = self.nlproto.decode(self, NlMsg(request, 0, op.attr_set), op)
        offset = self.nlproto.msghdr_size() + self._struct_size(op.fixed_header)
        path = self._decode_extack_path(msg.raw_attrs, op.attr_set, offset,
                                        extack['bad-attr-offs'])
        if path:
            del extack['bad-attr-offs']
            extack['bad-attr'] = path

    def _struct_size(self, name):
        if name:
            members = self.consts[name].members
            size = 0
            for m in members:
                if m.type in ['pad', 'binary']:
                    if m.struct:
                        size += self._struct_size(m.struct)
                    else:
                        size += m.len
                else:
                    format = NlAttr.get_format(m.type, m.byte_order)
                    size += format.size
            return size
        else:
            return 0

    def _decode_struct(self, data, name):
        members = self.consts[name].members
        attrs = dict()
        offset = 0
        for m in members:
            value = None
            if m.type == 'pad':
                offset += m.len
            elif m.type == 'binary':
                if m.struct:
                    len = self._struct_size(m.struct)
                    value = self._decode_struct(data[offset : offset + len],
                                                m.struct)
                    offset += len
                else:
                    value = data[offset : offset + m.len]
                    offset += m.len
            else:
                format = NlAttr.get_format(m.type, m.byte_order)
                [ value ] = format.unpack_from(data, offset)
                offset += format.size
            if value is not None:
                if m.enum:
                    value = self._decode_enum(value, m)
                elif m.display_hint:
                    value = self._formatted_string(value, m.display_hint)
                attrs[m.name] = value
        return attrs

    def _encode_struct(self, name, vals):
        members = self.consts[name].members
        attr_payload = b''
        for m in members:
            value = vals.pop(m.name) if m.name in vals else None
            if m.type == 'pad':
                attr_payload += bytearray(m.len)
            elif m.type == 'binary':
                if m.struct:
                    if value is None:
                        value = dict()
                    attr_payload += self._encode_struct(m.struct, value)
                else:
                    if value is None:
                        attr_payload += bytearray(m.len)
                    else:
                        attr_payload += bytes.fromhex(value)
            else:
                if value is None:
                    value = 0
                format = NlAttr.get_format(m.type, m.byte_order)
                attr_payload += format.pack(value)
        return attr_payload

    def _formatted_string(self, raw, display_hint):
        if display_hint == 'mac':
            formatted = ':'.join('%02x' % b for b in raw)
        elif display_hint == 'hex':
            if isinstance(raw, int):
                formatted = hex(raw)
            else:
                formatted = bytes.hex(raw, ' ')
        elif display_hint in [ 'ipv4', 'ipv6' ]:
            formatted = format(ipaddress.ip_address(raw))
        elif display_hint == 'uuid':
            formatted = str(uuid.UUID(bytes=raw))
        else:
            formatted = raw
        return formatted

    def handle_ntf(self, decoded):
        msg = dict()
        if self.include_raw:
            msg['raw'] = decoded
        op = self.rsp_by_value[decoded.cmd()]
        attrs = self._decode(decoded.raw_attrs, op.attr_set.name)
        if op.fixed_header:
            attrs.update(self._decode_struct(decoded.raw, op.fixed_header))

        msg['name'] = op['name']
        msg['msg'] = attrs
        self.async_msg_queue.put(msg)

    def check_ntf(self):
        while True:
            try:
                reply = self.sock.recv(self._recv_size, socket.MSG_DONTWAIT)
            except BlockingIOError:
                return

            nms = NlMsgs(reply)
            self._recv_dbg_print(reply, nms)
            for nl_msg in nms:
                if nl_msg.error:
                    print("Netlink error in ntf!?", os.strerror(-nl_msg.error))
                    print(nl_msg)
                    continue
                if nl_msg.done:
                    print("Netlink done while checking for ntf!?")
                    continue

                decoded = self.nlproto.decode(self, nl_msg, None)
                if decoded.cmd() not in self.async_msg_ids:
                    print("Unexpected msg id while checking for ntf", decoded)
                    continue

                self.handle_ntf(decoded)

    def poll_ntf(self, duration=None):
        start_time = time.time()
        selector = selectors.DefaultSelector()
        selector.register(self.sock, selectors.EVENT_READ)

        while True:
            try:
                yield self.async_msg_queue.get_nowait()
            except queue.Empty:
                if duration is not None:
                    timeout = start_time + duration - time.time()
                    if timeout <= 0:
                        return
                else:
                    timeout = None
                events = selector.select(timeout)
                if events:
                    self.check_ntf()

    def operation_do_attributes(self, name):
      """
      For a given operation name, find and return a supported
      set of attributes (as a dict).
      """
      op = self.find_operation(name)
      if not op:
        return None

      return op['do']['request']['attributes'].copy()

    def _encode_message(self, op, vals, flags, req_seq):
        nl_flags = Netlink.NLM_F_REQUEST | Netlink.NLM_F_ACK
        for flag in flags or []:
            nl_flags |= flag

        msg = self.nlproto.message(nl_flags, op.req_value, 1, req_seq)
        if op.fixed_header:
            msg += self._encode_struct(op.fixed_header, vals)
        search_attrs = SpaceAttrs(op.attr_set, vals)
        for name, value in vals.items():
            msg += self._add_attr(op.attr_set.name, name, value, search_attrs)
        msg = _genl_msg_finalize(msg)
        return msg

    def _ops(self, ops):
        reqs_by_seq = {}
        req_seq = random.randint(1024, 65535)
        payload = b''
        for (method, vals, flags) in ops:
            op = self.ops[method]
            msg = self._encode_message(op, vals, flags, req_seq)
            reqs_by_seq[req_seq] = (op, msg, flags)
            payload += msg
            req_seq += 1

        self.sock.send(payload, 0)

        done = False
        rsp = []
        op_rsp = []
        while not done:
            reply = self.sock.recv(self._recv_size)
            nms = NlMsgs(reply, attr_space=op.attr_set)
            self._recv_dbg_print(reply, nms)
            for nl_msg in nms:
                if nl_msg.nl_seq in reqs_by_seq:
                    (op, req_msg, req_flags) = reqs_by_seq[nl_msg.nl_seq]
                    if nl_msg.extack:
                        self._decode_extack(req_msg, op, nl_msg.extack)
                else:
                    op = None
                    req_flags = []

                if nl_msg.error:
                    raise NlError(nl_msg)
                if nl_msg.done:
                    if nl_msg.extack:
                        print("Netlink warning:")
                        print(nl_msg)

                    if Netlink.NLM_F_DUMP in req_flags:
                        rsp.append(op_rsp)
                    elif not op_rsp:
                        rsp.append(None)
                    elif len(op_rsp) == 1:
                        rsp.append(op_rsp[0])
                    else:
                        rsp.append(op_rsp)
                    op_rsp = []

                    del reqs_by_seq[nl_msg.nl_seq]
                    done = len(reqs_by_seq) == 0
                    break

                decoded = self.nlproto.decode(self, nl_msg, op)

                # Check if this is a reply to our request
                if nl_msg.nl_seq not in reqs_by_seq or decoded.cmd() != op.rsp_value:
                    if decoded.cmd() in self.async_msg_ids:
                        self.handle_ntf(decoded)
                        continue
                    else:
                        print('Unexpected message: ' + repr(decoded))
                        continue

                rsp_msg = self._decode(decoded.raw_attrs, op.attr_set.name)
                if op.fixed_header:
                    rsp_msg.update(self._decode_struct(decoded.raw, op.fixed_header))
                op_rsp.append(rsp_msg)

        return rsp

    def _op(self, method, vals, flags=None, dump=False):
        req_flags = flags or []
        if dump:
            req_flags.append(Netlink.NLM_F_DUMP)

        ops = [(method, vals, req_flags)]
        return self._ops(ops)[0]

    def do(self, method, vals, flags=None):
        return self._op(method, vals, flags)

    def dump(self, method, vals):
        return self._op(method, vals, dump=True)

    def do_multi(self, ops):
        return self._ops(ops)
