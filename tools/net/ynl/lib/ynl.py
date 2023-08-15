# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

from collections import namedtuple
import functools
import os
import random
import socket
import struct
from struct import Struct
import yaml
import ipaddress
import uuid

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

    # Netlink message
    NLMSG_ERROR = 2
    NLMSG_DONE = 3

    NLM_F_REQUEST = 1
    NLM_F_ACK = 4
    NLM_F_ROOT = 0x100
    NLM_F_MATCH = 0x200
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


class NlError(Exception):
  def __init__(self, nl_msg):
    self.nl_msg = nl_msg

  def __str__(self):
    return f"Netlink error: {os.strerror(-self.nl_msg.error)}\n{self.nl_msg}"


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
        self._len, self._type = struct.unpack("HH", raw[offset:offset + 4])
        self.type = self._type & ~Netlink.NLA_TYPE_MASK
        self.payload_len = self._len
        self.full_len = (self.payload_len + 3) & ~3
        self.raw = raw[offset + 4:offset + self.payload_len]

    @classmethod
    def get_format(cls, attr_type, byte_order=None):
        format = cls.type_formats[attr_type]
        if byte_order:
            return format.big if byte_order == "big-endian" \
                else format.little
        return format.native

    @classmethod
    def formatted_string(cls, raw, display_hint):
        if display_hint == 'mac':
            formatted = ':'.join('%02x' % b for b in raw)
        elif display_hint == 'hex':
            formatted = bytes.hex(raw, ' ')
        elif display_hint in [ 'ipv4', 'ipv6' ]:
            formatted = format(ipaddress.ip_address(raw))
        elif display_hint == 'uuid':
            formatted = str(uuid.UUID(bytes=raw))
        else:
            formatted = raw
        return formatted

    def as_scalar(self, attr_type, byte_order=None):
        format = self.get_format(attr_type, byte_order)
        return format.unpack(self.raw)[0]

    def as_strz(self):
        return self.raw.decode('ascii')[:-1]

    def as_bin(self):
        return self.raw

    def as_c_array(self, type):
        format = self.get_format(type)
        return [ x[0] for x in format.iter_unpack(self.raw) ]

    def as_struct(self, members):
        value = dict()
        offset = 0
        for m in members:
            # TODO: handle non-scalar members
            if m.type == 'binary':
                decoded = self.raw[offset:offset+m['len']]
                offset += m['len']
            elif m.type in NlAttr.type_formats:
                format = self.get_format(m.type, m.byte_order)
                [ decoded ] = format.unpack_from(self.raw, offset)
                offset += format.size
            if m.display_hint:
                decoded = self.formatted_string(decoded, m.display_hint)
            value[m.name] = decoded
        return value

    def __repr__(self):
        return f"[type:{self.type} len:{self._len}] {self.raw}"


class NlAttrs:
    def __init__(self, msg):
        self.attrs = []

        offset = 0
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
        self.hdr = msg[offset:offset + 16]

        self.nl_len, self.nl_type, self.nl_flags, self.nl_seq, self.nl_portid = \
            struct.unpack("IHHII", self.hdr)

        self.raw = msg[offset + 16:offset + self.nl_len]

        self.error = 0
        self.done = 0

        extack_off = None
        if self.nl_type == Netlink.NLMSG_ERROR:
            self.error = struct.unpack("i", self.raw[0:4])[0]
            self.done = 1
            extack_off = 20
        elif self.nl_type == Netlink.NLMSG_DONE:
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
                        desc = spec['name']
                        if 'doc' in spec:
                            desc += f" ({spec['doc']})"
                        self.extack['miss-type'] = desc

    def __repr__(self):
        msg = f"nl_len = {self.nl_len} ({len(self.raw)}) nl_flags = 0x{self.nl_flags:x} nl_type = {self.nl_type}\n"
        if self.error:
            msg += '\terror: ' + str(self.error)
        if self.extack:
            msg += '\textack: ' + repr(self.extack)
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
                for attr in gm.raw_attrs:
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
    def __init__(self, nl_msg, fixed_header_members=[]):
        self.nl = nl_msg

        self.hdr = nl_msg.raw[0:4]
        offset = 4

        self.genl_cmd, self.genl_version, _ = struct.unpack("BBH", self.hdr)

        self.fixed_header_attrs = dict()
        for m in fixed_header_members:
            format = NlAttr.get_format(m.type, m.byte_order)
            decoded = format.unpack_from(nl_msg.raw, offset)
            offset += format.size
            self.fixed_header_attrs[m.name] = decoded[0]

        self.raw = nl_msg.raw[offset:]
        self.raw_attrs = NlAttrs(self.raw)

    def __repr__(self):
        msg = repr(self.nl)
        msg += f"\tgenl_cmd = {self.genl_cmd} genl_ver = {self.genl_version}\n"
        for a in self.raw_attrs:
            msg += '\t\t' + repr(a) + '\n'
        return msg


class GenlFamily:
    def __init__(self, family_name):
        self.family_name = family_name

        global genl_family_name_to_id
        if genl_family_name_to_id is None:
            _genl_load_families()

        self.genl_family = genl_family_name_to_id[family_name]
        self.family_id = genl_family_name_to_id[family_name]['id']


#
# YNL implementation details.
#


class YnlFamily(SpecFamily):
    def __init__(self, def_path, schema=None):
        super().__init__(def_path, schema)

        self.include_raw = False

        self.sock = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, Netlink.NETLINK_GENERIC)
        self.sock.setsockopt(Netlink.SOL_NETLINK, Netlink.NETLINK_CAP_ACK, 1)
        self.sock.setsockopt(Netlink.SOL_NETLINK, Netlink.NETLINK_EXT_ACK, 1)

        self.async_msg_ids = set()
        self.async_msg_queue = []

        for msg in self.msgs.values():
            if msg.is_async:
                self.async_msg_ids.add(msg.rsp_value)

        for op_name, op in self.ops.items():
            bound_f = functools.partial(self._op, op_name)
            setattr(self, op.ident_name, bound_f)

        try:
            self.family = GenlFamily(self.yaml['name'])
        except KeyError:
            raise Exception(f"Family '{self.yaml['name']}' not supported by the kernel")

    def ntf_subscribe(self, mcast_name):
        if mcast_name not in self.family.genl_family['mcast']:
            raise Exception(f'Multicast group "{mcast_name}" not present in the family')

        self.sock.bind((0, 0))
        self.sock.setsockopt(Netlink.SOL_NETLINK, Netlink.NETLINK_ADD_MEMBERSHIP,
                             self.family.genl_family['mcast'][mcast_name])

    def _add_attr(self, space, name, value):
        attr = self.attr_sets[space][name]
        nl_type = attr.value
        if attr["type"] == 'nest':
            nl_type |= Netlink.NLA_F_NESTED
            attr_payload = b''
            for subname, subvalue in value.items():
                attr_payload += self._add_attr(attr['nested-attributes'], subname, subvalue)
        elif attr["type"] == 'flag':
            attr_payload = b''
        elif attr["type"] == 'string':
            attr_payload = str(value).encode('ascii') + b'\x00'
        elif attr["type"] == 'binary':
            attr_payload = bytes.fromhex(value)
        elif attr['type'] in NlAttr.type_formats:
            format = NlAttr.get_format(attr['type'], attr.byte_order)
            attr_payload = format.pack(int(value))
        else:
            raise Exception(f'Unknown type at {space} {name} {value} {attr["type"]}')

        pad = b'\x00' * ((4 - len(attr_payload) % 4) % 4)
        return struct.pack('HH', len(attr_payload) + 4, nl_type) + attr_payload + pad

    def _decode_enum(self, raw, attr_spec):
        enum = self.consts[attr_spec['enum']]
        if 'enum-as-flags' in attr_spec and attr_spec['enum-as-flags']:
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
            members = self.consts[attr_spec.struct_name]
            decoded = attr.as_struct(members)
            for m in members:
                if m.enum:
                    decoded[m.name] = self._decode_enum(decoded[m.name], m)
        elif attr_spec.sub_type:
            decoded = attr.as_c_array(attr_spec.sub_type)
        else:
            decoded = attr.as_bin()
            if attr_spec.display_hint:
                decoded = NlAttr.formatted_string(decoded, attr_spec.display_hint)
        return decoded

    def _decode(self, attrs, space):
        attr_space = self.attr_sets[space]
        rsp = dict()
        for attr in attrs:
            attr_spec = attr_space.attrs_by_val[attr.type]
            if attr_spec["type"] == 'nest':
                subdict = self._decode(NlAttrs(attr.raw), attr_spec['nested-attributes'])
                decoded = subdict
            elif attr_spec["type"] == 'string':
                decoded = attr.as_strz()
            elif attr_spec["type"] == 'binary':
                decoded = self._decode_binary(attr, attr_spec)
            elif attr_spec["type"] == 'flag':
                decoded = True
            elif attr_spec["type"] in NlAttr.type_formats:
                decoded = attr.as_scalar(attr_spec['type'], attr_spec.byte_order)
            else:
                raise Exception(f'Unknown {attr_spec["type"]} with name {attr_spec["name"]}')

            if 'enum' in attr_spec:
                decoded = self._decode_enum(decoded, attr_spec)

            if not attr_spec.is_multi:
                rsp[attr_spec['name']] = decoded
            elif attr_spec.name in rsp:
                rsp[attr_spec.name].append(decoded)
            else:
                rsp[attr_spec.name] = [decoded]

        return rsp

    def _decode_extack_path(self, attrs, attr_set, offset, target):
        for attr in attrs:
            attr_spec = attr_set.attrs_by_val[attr.type]
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

    def _decode_extack(self, request, attr_space, extack):
        if 'bad-attr-offs' not in extack:
            return

        genl_req = GenlMsg(NlMsg(request, 0, attr_space=attr_space))
        path = self._decode_extack_path(genl_req.raw_attrs, attr_space,
                                        20, extack['bad-attr-offs'])
        if path:
            del extack['bad-attr-offs']
            extack['bad-attr'] = path

    def handle_ntf(self, nl_msg, genl_msg):
        msg = dict()
        if self.include_raw:
            msg['nlmsg'] = nl_msg
            msg['genlmsg'] = genl_msg
        op = self.rsp_by_value[genl_msg.genl_cmd]
        msg['name'] = op['name']
        msg['msg'] = self._decode(genl_msg.raw_attrs, op.attr_set.name)
        self.async_msg_queue.append(msg)

    def check_ntf(self):
        while True:
            try:
                reply = self.sock.recv(128 * 1024, socket.MSG_DONTWAIT)
            except BlockingIOError:
                return

            nms = NlMsgs(reply)
            for nl_msg in nms:
                if nl_msg.error:
                    print("Netlink error in ntf!?", os.strerror(-nl_msg.error))
                    print(nl_msg)
                    continue
                if nl_msg.done:
                    print("Netlink done while checking for ntf!?")
                    continue

                gm = GenlMsg(nl_msg)
                if gm.genl_cmd not in self.async_msg_ids:
                    print("Unexpected msg id done while checking for ntf", gm)
                    continue

                self.handle_ntf(nl_msg, gm)

    def operation_do_attributes(self, name):
      """
      For a given operation name, find and return a supported
      set of attributes (as a dict).
      """
      op = self.find_operation(name)
      if not op:
        return None

      return op['do']['request']['attributes'].copy()

    def _op(self, method, vals, dump=False):
        op = self.ops[method]

        nl_flags = Netlink.NLM_F_REQUEST | Netlink.NLM_F_ACK
        if dump:
            nl_flags |= Netlink.NLM_F_DUMP

        req_seq = random.randint(1024, 65535)
        msg = _genl_msg(self.family.family_id, nl_flags, op.req_value, 1, req_seq)
        fixed_header_members = []
        if op.fixed_header:
            fixed_header_members = self.consts[op.fixed_header].members
            for m in fixed_header_members:
                value = vals.pop(m.name) if m.name in vals else 0
                format = NlAttr.get_format(m.type, m.byte_order)
                msg += format.pack(value)
        for name, value in vals.items():
            msg += self._add_attr(op.attr_set.name, name, value)
        msg = _genl_msg_finalize(msg)

        self.sock.send(msg, 0)

        done = False
        rsp = []
        while not done:
            reply = self.sock.recv(128 * 1024)
            nms = NlMsgs(reply, attr_space=op.attr_set)
            for nl_msg in nms:
                if nl_msg.extack:
                    self._decode_extack(msg, op.attr_set, nl_msg.extack)

                if nl_msg.error:
                    raise NlError(nl_msg)
                if nl_msg.done:
                    if nl_msg.extack:
                        print("Netlink warning:")
                        print(nl_msg)
                    done = True
                    break

                gm = GenlMsg(nl_msg, fixed_header_members)
                # Check if this is a reply to our request
                if nl_msg.nl_seq != req_seq or gm.genl_cmd != op.rsp_value:
                    if gm.genl_cmd in self.async_msg_ids:
                        self.handle_ntf(nl_msg, gm)
                        continue
                    else:
                        print('Unexpected message: ' + repr(gm))
                        continue

                rsp_msg = self._decode(gm.raw_attrs, op.attr_set.name)
                rsp_msg.update(gm.fixed_header_attrs)
                rsp.append(rsp_msg)

        if not rsp:
            return None
        if not dump and len(rsp) == 1:
            return rsp[0]
        return rsp

    def do(self, method, vals):
        return self._op(method, vals)

    def dump(self, method, vals):
        return self._op(method, vals, dump=True)
