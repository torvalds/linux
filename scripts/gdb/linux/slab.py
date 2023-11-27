# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 MediaTek Inc.
#
# Authors:
#  Kuan-Ying Lee <Kuan-Ying.Lee@mediatek.com>
#

import gdb
import re
import traceback
from linux import lists, utils, stackdepot, constants, mm

SLAB_RED_ZONE       = constants.LX_SLAB_RED_ZONE
SLAB_POISON         = constants.LX_SLAB_POISON
SLAB_KMALLOC        = constants.LX_SLAB_KMALLOC
SLAB_HWCACHE_ALIGN  = constants.LX_SLAB_HWCACHE_ALIGN
SLAB_CACHE_DMA      = constants.LX_SLAB_CACHE_DMA
SLAB_CACHE_DMA32    = constants.LX_SLAB_CACHE_DMA32
SLAB_STORE_USER     = constants.LX_SLAB_STORE_USER
SLAB_PANIC          = constants.LX_SLAB_PANIC

OO_SHIFT = 16
OO_MASK = (1 << OO_SHIFT) - 1

if constants.LX_CONFIG_SLUB_DEBUG:
    slab_type = utils.CachedType("struct slab")
    slab_ptr_type = slab_type.get_type().pointer()
    kmem_cache_type = utils.CachedType("struct kmem_cache")
    kmem_cache_ptr_type = kmem_cache_type.get_type().pointer()
    freeptr_t = utils.CachedType("freeptr_t")
    freeptr_t_ptr = freeptr_t.get_type().pointer()

    track_type = gdb.lookup_type('struct track')
    track_alloc = int(gdb.parse_and_eval('TRACK_ALLOC'))
    track_free = int(gdb.parse_and_eval('TRACK_FREE'))

def slab_folio(slab):
    return slab.cast(gdb.lookup_type("struct folio").pointer())

def slab_address(slab):
    p_ops = mm.page_ops().ops
    folio = slab_folio(slab)
    return p_ops.folio_address(folio)

def for_each_object(cache, addr, slab_objects):
    p = addr
    if cache['flags'] & SLAB_RED_ZONE:
        p += int(cache['red_left_pad'])
    while p < addr + (slab_objects * cache['size']):
        yield p
        p = p + int(cache['size'])

def get_info_end(cache):
    if (cache['offset'] >= cache['inuse']):
        return cache['inuse'] + gdb.lookup_type("void").pointer().sizeof
    else:
        return cache['inuse']

def get_orig_size(cache, obj):
    if cache['flags'] & SLAB_STORE_USER and cache['flags'] & SLAB_KMALLOC:
        p = mm.page_ops().ops.kasan_reset_tag(obj)
        p += get_info_end(cache)
        p += gdb.lookup_type('struct track').sizeof * 2
        p = p.cast(utils.get_uint_type().pointer())
        return p.dereference()
    else:
        return cache['object_size']

def get_track(cache, object_pointer, alloc):
    p = object_pointer + get_info_end(cache)
    p += (alloc * track_type.sizeof)
    return p

def oo_objects(x):
    return int(x['x']) & OO_MASK

def oo_order(x):
    return int(x['x']) >> OO_SHIFT

def reciprocal_divide(a, R):
    t = (a * int(R['m'])) >> 32
    return (t + ((a - t) >> int(R['sh1']))) >> int(R['sh2'])

def __obj_to_index(cache, addr, obj):
    return reciprocal_divide(int(mm.page_ops().ops.kasan_reset_tag(obj)) - addr, cache['reciprocal_size'])

def swab64(x):
    result = (((x & 0x00000000000000ff) << 56) |   \
    ((x & 0x000000000000ff00) << 40) |   \
    ((x & 0x0000000000ff0000) << 24) |   \
    ((x & 0x00000000ff000000) <<  8) |   \
    ((x & 0x000000ff00000000) >>  8) |   \
    ((x & 0x0000ff0000000000) >> 24) |   \
    ((x & 0x00ff000000000000) >> 40) |   \
    ((x & 0xff00000000000000) >> 56))
    return result

def freelist_ptr_decode(cache, ptr, ptr_addr):
    if constants.LX_CONFIG_SLAB_FREELIST_HARDENED:
        return ptr['v'] ^ cache['random'] ^ swab64(int(ptr_addr))
    else:
        return ptr['v']

def get_freepointer(cache, obj):
    obj = mm.page_ops().ops.kasan_reset_tag(obj)
    ptr_addr = obj + cache['offset']
    p = ptr_addr.cast(freeptr_t_ptr).dereference()
    return freelist_ptr_decode(cache, p, ptr_addr)

def loc_exist(loc_track, addr, handle, waste):
    for loc in loc_track:
        if loc['addr'] == addr and loc['handle'] == handle and loc['waste'] == waste:
            return loc
    return None

def add_location(loc_track, cache, track, orig_size):
    jiffies = gdb.parse_and_eval("jiffies_64")
    age = jiffies - track['when']
    handle = 0
    waste = cache['object_size'] - int(orig_size)
    pid = int(track['pid'])
    cpuid = int(track['cpu'])
    addr = track['addr']
    if constants.LX_CONFIG_STACKDEPOT:
        handle = track['handle']

    loc = loc_exist(loc_track, addr, handle, waste)
    if loc:
        loc['count'] += 1
        if track['when']:
            loc['sum_time'] += age
            loc['min_time'] = min(loc['min_time'], age)
            loc['max_time'] = max(loc['max_time'], age)
            loc['min_pid'] = min(loc['min_pid'], pid)
            loc['max_pid'] = max(loc['max_pid'], pid)
            loc['cpus'].add(cpuid)
    else:
        loc_track.append({
            'count' : 1,
            'addr' : addr,
            'sum_time' : age,
            'min_time' : age,
            'max_time' : age,
            'min_pid' : pid,
            'max_pid' : pid,
            'handle' : handle,
            'waste' : waste,
            'cpus' : {cpuid}
            }
        )

def slabtrace(alloc, cache_name):

    def __fill_map(obj_map, cache, slab):
        p = slab['freelist']
        addr = slab_address(slab)
        while p != gdb.Value(0):
            index = __obj_to_index(cache, addr, p)
            obj_map[index] = True # free objects
            p = get_freepointer(cache, p)

    # process every slab page on the slab_list (partial and full list)
    def process_slab(loc_track, slab_list, alloc, cache):
        for slab in lists.list_for_each_entry(slab_list, slab_ptr_type, "slab_list"):
            obj_map[:] = [False] * oo_objects(cache['oo'])
            __fill_map(obj_map, cache, slab)
            addr = slab_address(slab)
            for object_pointer in for_each_object(cache, addr, slab['objects']):
                if obj_map[__obj_to_index(cache, addr, object_pointer)] == True:
                    continue
                p = get_track(cache, object_pointer, alloc)
                track = gdb.Value(p).cast(track_type.pointer())
                if alloc == track_alloc:
                    size = get_orig_size(cache, object_pointer)
                else:
                    size = cache['object_size']
                add_location(loc_track, cache, track, size)
                continue

    slab_caches = gdb.parse_and_eval("slab_caches")
    if mm.page_ops().ops.MAX_NUMNODES > 1:
        nr_node_ids = int(gdb.parse_and_eval("nr_node_ids"))
    else:
        nr_node_ids = 1

    target_cache = None
    loc_track = []

    for cache in lists.list_for_each_entry(slab_caches, kmem_cache_ptr_type, 'list'):
        if cache['name'].string() == cache_name:
            target_cache = cache
            break

    obj_map = [False] * oo_objects(target_cache['oo'])

    if target_cache['flags'] & SLAB_STORE_USER:
        for i in range(0, nr_node_ids):
            cache_node = target_cache['node'][i]
            if cache_node['nr_slabs']['counter'] == 0:
                continue
            process_slab(loc_track, cache_node['partial'], alloc, target_cache)
            process_slab(loc_track, cache_node['full'], alloc, target_cache)
    else:
        raise gdb.GdbError("SLAB_STORE_USER is not set in %s" % target_cache['name'].string())

    for loc in sorted(loc_track, key=lambda x:x['count'], reverse=True):
        if loc['addr']:
            addr = loc['addr'].cast(utils.get_ulong_type().pointer())
            gdb.write("%d %s" % (loc['count'], str(addr).split(' ')[-1]))
        else:
            gdb.write("%d <not-available>" % loc['count'])

        if loc['waste']:
            gdb.write(" waste=%d/%d" % (loc['count'] * loc['waste'], loc['waste']))

        if loc['sum_time'] != loc['min_time']:
            gdb.write(" age=%d/%d/%d" % (loc['min_time'], loc['sum_time']/loc['count'], loc['max_time']))
        else:
            gdb.write(" age=%d" % loc['min_time'])

        if loc['min_pid'] != loc['max_pid']:
            gdb.write(" pid=%d-%d" % (loc['min_pid'], loc['max_pid']))
        else:
            gdb.write(" pid=%d" % loc['min_pid'])

        if constants.LX_NR_CPUS > 1:
            nr_cpu = gdb.parse_and_eval('__num_online_cpus')['counter']
            if nr_cpu > 1:
                gdb.write(" cpus=")
                gdb.write(','.join(str(cpu) for cpu in loc['cpus']))
        gdb.write("\n")
        if constants.LX_CONFIG_STACKDEPOT:
            if loc['handle']:
                stackdepot.stack_depot_print(loc['handle'])
        gdb.write("\n")

def help():
    t = """Usage: lx-slabtrace --cache_name [cache_name] [Options]
    Options:
        --alloc
            print information of allocation trace of the allocated objects
        --free
            print information of freeing trace of the allocated objects
    Example:
        lx-slabtrace --cache_name kmalloc-1k --alloc
        lx-slabtrace --cache_name kmalloc-1k --free\n"""
    gdb.write("Unrecognized command\n")
    raise gdb.GdbError(t)

class LxSlabTrace(gdb.Command):
    """Show specific cache slabtrace"""

    def __init__(self):
        super(LxSlabTrace, self).__init__("lx-slabtrace", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        if not constants.LX_CONFIG_SLUB_DEBUG:
            raise gdb.GdbError("CONFIG_SLUB_DEBUG is not enabled")

        argv = gdb.string_to_argv(arg)
        alloc = track_alloc # default show alloc_traces

        if len(argv) == 3:
            if argv[2] == '--alloc':
                alloc = track_alloc
            elif argv[2] == '--free':
                alloc = track_free
            else:
                help()
        if len(argv) >= 2 and argv[0] == '--cache_name':
            slabtrace(alloc, argv[1])
        else:
            help()
LxSlabTrace()

def slabinfo():
    nr_node_ids = None

    if not constants.LX_CONFIG_SLUB_DEBUG:
        raise gdb.GdbError("CONFIG_SLUB_DEBUG is not enabled")

    def count_free(slab):
        total_free = 0
        for slab in lists.list_for_each_entry(slab, slab_ptr_type, 'slab_list'):
            total_free += int(slab['objects'] - slab['inuse'])
        return total_free

    gdb.write("{:^18} | {:^20} | {:^12} | {:^12} | {:^8} | {:^11} | {:^13}\n".format('Pointer', 'name', 'active_objs', 'num_objs', 'objsize', 'objperslab', 'pagesperslab'))
    gdb.write("{:-^18} | {:-^20} | {:-^12} | {:-^12} | {:-^8} | {:-^11} | {:-^13}\n".format('', '', '', '', '', '', ''))

    slab_caches = gdb.parse_and_eval("slab_caches")
    if mm.page_ops().ops.MAX_NUMNODES > 1:
        nr_node_ids = int(gdb.parse_and_eval("nr_node_ids"))
    else:
        nr_node_ids = 1

    for cache in lists.list_for_each_entry(slab_caches, kmem_cache_ptr_type, 'list'):
        nr_objs = 0
        nr_free = 0
        nr_slabs = 0
        for i in range(0, nr_node_ids):
            cache_node = cache['node'][i]
            try:
                nr_slabs += cache_node['nr_slabs']['counter']
                nr_objs = int(cache_node['total_objects']['counter'])
                nr_free = count_free(cache_node['partial'])
            except:
                raise gdb.GdbError(traceback.format_exc())
        active_objs = nr_objs - nr_free
        num_objs = nr_objs
        active_slabs = nr_slabs
        objects_per_slab = oo_objects(cache['oo'])
        cache_order = oo_order(cache['oo'])
        gdb.write("{:18s} | {:20.19s} | {:12} | {:12} | {:8} | {:11} | {:13}\n".format(hex(cache), cache['name'].string(), str(active_objs), str(num_objs), str(cache['size']), str(objects_per_slab), str(1 << cache_order)))

class LxSlabInfo(gdb.Command):
    """Show slabinfo"""

    def __init__(self):
        super(LxSlabInfo, self).__init__("lx-slabinfo", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        slabinfo()
LxSlabInfo()
