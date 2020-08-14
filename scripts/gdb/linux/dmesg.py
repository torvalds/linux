#
# gdb helper commands and functions for Linux kernel debugging
#
#  kernel log buffer dump
#
# Copyright (c) Siemens AG, 2011, 2012
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb
import sys

from linux import utils

printk_info_type = utils.CachedType("struct printk_info")
prb_data_blk_lpos_type = utils.CachedType("struct prb_data_blk_lpos")
prb_desc_type = utils.CachedType("struct prb_desc")
prb_desc_ring_type = utils.CachedType("struct prb_desc_ring")
prb_data_ring_type = utils.CachedType("struct prb_data_ring")
printk_ringbuffer_type = utils.CachedType("struct printk_ringbuffer")
atomic_long_type = utils.CachedType("atomic_long_t")

class LxDmesg(gdb.Command):
    """Print Linux kernel log buffer."""

    def __init__(self):
        super(LxDmesg, self).__init__("lx-dmesg", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        inf = gdb.inferiors()[0]

        # read in prb structure
        prb_addr = int(str(gdb.parse_and_eval("(void *)'printk.c'::prb")).split()[0], 16)
        sz = printk_ringbuffer_type.get_type().sizeof
        prb = utils.read_memoryview(inf, prb_addr, sz).tobytes()

        # read in descriptor ring structure
        off = printk_ringbuffer_type.get_type()['desc_ring'].bitpos // 8
        addr = prb_addr + off
        sz = prb_desc_ring_type.get_type().sizeof
        desc_ring = utils.read_memoryview(inf, addr, sz).tobytes()

        # read in descriptor array
        off = prb_desc_ring_type.get_type()['count_bits'].bitpos // 8
        desc_ring_count = 1 << utils.read_u32(desc_ring, off)
        desc_sz = prb_desc_type.get_type().sizeof
        off = prb_desc_ring_type.get_type()['descs'].bitpos // 8
        addr = utils.read_ulong(desc_ring, off)
        descs = utils.read_memoryview(inf, addr, desc_sz * desc_ring_count).tobytes()

        # read in text data ring structure
        off = printk_ringbuffer_type.get_type()['text_data_ring'].bitpos // 8
        addr = prb_addr + off
        sz = prb_data_ring_type.get_type().sizeof
        text_data_ring = utils.read_memoryview(inf, addr, sz).tobytes()

        # read in text data
        off = prb_data_ring_type.get_type()['size_bits'].bitpos // 8
        text_data_sz = 1 << utils.read_u32(text_data_ring, off)
        off = prb_data_ring_type.get_type()['data'].bitpos // 8
        addr = utils.read_ulong(text_data_ring, off)
        text_data = utils.read_memoryview(inf, addr, text_data_sz).tobytes()

        counter_off = atomic_long_type.get_type()['counter'].bitpos // 8

        sv_off = prb_desc_type.get_type()['state_var'].bitpos // 8

        off = prb_desc_type.get_type()['text_blk_lpos'].bitpos // 8
        begin_off = off + (prb_data_blk_lpos_type.get_type()['begin'].bitpos // 8)
        next_off = off + (prb_data_blk_lpos_type.get_type()['next'].bitpos // 8)

        off = prb_desc_type.get_type()['info'].bitpos // 8
        ts_off = off + printk_info_type.get_type()['ts_nsec'].bitpos // 8
        len_off = off + printk_info_type.get_type()['text_len'].bitpos // 8

        # definitions from kernel/printk/printk_ringbuffer.h
        desc_sv_bits = utils.get_long_type().sizeof * 8
        desc_committed_mask = 1 << (desc_sv_bits - 1)
        desc_reuse_mask = 1 << (desc_sv_bits - 2)
        desc_flags_mask = desc_committed_mask | desc_reuse_mask
        desc_id_mask = ~desc_flags_mask

        # read in tail and head descriptor ids
        off = prb_desc_ring_type.get_type()['tail_id'].bitpos // 8
        tail_id = utils.read_u64(desc_ring, off + counter_off)
        off = prb_desc_ring_type.get_type()['head_id'].bitpos // 8
        head_id = utils.read_u64(desc_ring, off + counter_off)

        did = tail_id
        while True:
            ind = did % desc_ring_count
            desc_off = desc_sz * ind

            # skip non-committed record
            state = utils.read_u64(descs, desc_off + sv_off + counter_off) & desc_flags_mask
            if state != desc_committed_mask:
                if did == head_id:
                    break
                did = (did + 1) & desc_id_mask
                continue

            begin = utils.read_ulong(descs, desc_off + begin_off) % text_data_sz
            end = utils.read_ulong(descs, desc_off + next_off) % text_data_sz

            # handle data-less record
            if begin & 1 == 1:
                text = ""
            else:
                # handle wrapping data block
                if begin > end:
                    begin = 0

                # skip over descriptor id
                text_start = begin + utils.get_long_type().sizeof

                text_len = utils.read_u16(descs, desc_off + len_off)

                # handle truncated message
                if end - text_start < text_len:
                    text_len = end - text_start

                text = text_data[text_start:text_start + text_len].decode(
                    encoding='utf8', errors='replace')

            time_stamp = utils.read_u64(descs, desc_off + ts_off)

            for line in text.splitlines():
                msg = u"[{time:12.6f}] {line}\n".format(
                    time=time_stamp / 1000000000.0,
                    line=line)
                # With python2 gdb.write will attempt to convert unicode to
                # ascii and might fail so pass an utf8-encoded str instead.
                if sys.hexversion < 0x03000000:
                    msg = msg.encode(encoding='utf8', errors='replace')
                gdb.write(msg)

            if did == head_id:
                break
            did = (did + 1) & desc_id_mask


LxDmesg()
