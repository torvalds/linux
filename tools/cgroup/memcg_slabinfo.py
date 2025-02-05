#!/usr/bin/env drgn
#
# Copyright (C) 2020 Roman Gushchin <guro@fb.com>
# Copyright (C) 2020 Facebook

from os import stat
import argparse
import sys

from drgn.helpers.linux import list_for_each_entry, list_empty
from drgn.helpers.linux import for_each_page
from drgn.helpers.linux.cpumask import for_each_online_cpu
from drgn.helpers.linux.percpu import per_cpu_ptr
from drgn import container_of, FaultError, Object, cast


DESC = """
This is a drgn script to provide slab statistics for memory cgroups.
It supports cgroup v2 and v1 and can emulate memory.kmem.slabinfo
interface of cgroup v1.
For drgn, visit https://github.com/osandov/drgn.
"""


MEMCGS = {}

OO_SHIFT = 16
OO_MASK = ((1 << OO_SHIFT) - 1)


def err(s):
    print('slabinfo.py: error: %s' % s, file=sys.stderr, flush=True)
    sys.exit(1)


def find_memcg_ids(css=prog['root_mem_cgroup'].css, prefix=''):
    if not list_empty(css.children.address_of_()):
        for css in list_for_each_entry('struct cgroup_subsys_state',
                                       css.children.address_of_(),
                                       'sibling'):
            name = prefix + '/' + css.cgroup.kn.name.string_().decode('utf-8')
            memcg = container_of(css, 'struct mem_cgroup', 'css')
            MEMCGS[css.cgroup.kn.id.value_()] = memcg
            find_memcg_ids(css, name)


def is_root_cache(s):
    try:
        return False if s.memcg_params.root_cache else True
    except AttributeError:
        return True


def cache_name(s):
    if is_root_cache(s):
        return s.name.string_().decode('utf-8')
    else:
        return s.memcg_params.root_cache.name.string_().decode('utf-8')


# SLUB

def oo_order(s):
    return s.oo.x >> OO_SHIFT


def oo_objects(s):
    return s.oo.x & OO_MASK


def count_partial(n, fn):
    nr_objs = 0
    for slab in list_for_each_entry('struct slab', n.partial.address_of_(),
                                    'slab_list'):
         nr_objs += fn(slab)
    return nr_objs


def count_free(slab):
    return slab.objects - slab.inuse


def slub_get_slabinfo(s, cfg):
    nr_slabs = 0
    nr_objs = 0
    nr_free = 0

    for node in range(cfg['nr_nodes']):
        n = s.node[node]
        nr_slabs += n.nr_slabs.counter.value_()
        nr_objs += n.total_objects.counter.value_()
        nr_free += count_partial(n, count_free)

    return {'active_objs': nr_objs - nr_free,
            'num_objs': nr_objs,
            'active_slabs': nr_slabs,
            'num_slabs': nr_slabs,
            'objects_per_slab': oo_objects(s),
            'cache_order': oo_order(s),
            'limit': 0,
            'batchcount': 0,
            'shared': 0,
            'shared_avail': 0}


def cache_show(s, cfg, objs):
    if cfg['allocator'] == 'SLUB':
        sinfo = slub_get_slabinfo(s, cfg)
    else:
        err('SLAB isn\'t supported yet')

    if cfg['shared_slab_pages']:
        sinfo['active_objs'] = objs
        sinfo['num_objs'] = objs

    print('%-17s %6lu %6lu %6u %4u %4d'
          ' : tunables %4u %4u %4u'
          ' : slabdata %6lu %6lu %6lu' % (
              cache_name(s), sinfo['active_objs'], sinfo['num_objs'],
              s.size, sinfo['objects_per_slab'], 1 << sinfo['cache_order'],
              sinfo['limit'], sinfo['batchcount'], sinfo['shared'],
              sinfo['active_slabs'], sinfo['num_slabs'],
              sinfo['shared_avail']))


def detect_kernel_config():
    cfg = {}

    cfg['nr_nodes'] = prog['nr_online_nodes'].value_()

    if prog.type('struct kmem_cache').members[1].name == 'flags':
        cfg['allocator'] = 'SLUB'
    elif prog.type('struct kmem_cache').members[1].name == 'batchcount':
        cfg['allocator'] = 'SLAB'
    else:
        err('Can\'t determine the slab allocator')

    cfg['shared_slab_pages'] = False
    try:
        if prog.type('struct obj_cgroup'):
            cfg['shared_slab_pages'] = True
    except:
        pass

    return cfg


def for_each_slab(prog):
    PGSlab = ~prog.constant('PG_slab')

    for page in for_each_page(prog):
        try:
            if page.page_type.value_() == PGSlab:
                yield cast('struct slab *', page)
        except FaultError:
            pass


def main():
    parser = argparse.ArgumentParser(description=DESC,
                                     formatter_class=
                                     argparse.RawTextHelpFormatter)
    parser.add_argument('cgroup', metavar='CGROUP',
                        help='Target memory cgroup')
    args = parser.parse_args()

    try:
        cgroup_id = stat(args.cgroup).st_ino
        find_memcg_ids()
        memcg = MEMCGS[cgroup_id]
    except KeyError:
        err('Can\'t find the memory cgroup')

    cfg = detect_kernel_config()

    print('# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab>'
          ' : tunables <limit> <batchcount> <sharedfactor>'
          ' : slabdata <active_slabs> <num_slabs> <sharedavail>')

    if cfg['shared_slab_pages']:
        obj_cgroups = set()
        stats = {}
        caches = {}

        # find memcg pointers belonging to the specified cgroup
        obj_cgroups.add(memcg.objcg.value_())
        for ptr in list_for_each_entry('struct obj_cgroup',
                                       memcg.objcg_list.address_of_(),
                                       'list'):
            obj_cgroups.add(ptr.value_())

        # look over all slab folios and look for objects belonging
        # to the given memory cgroup
        for slab in for_each_slab(prog):
            objcg_vec_raw = slab.memcg_data.value_()
            if objcg_vec_raw == 0:
                continue
            cache = slab.slab_cache
            if not cache:
                continue
            addr = cache.value_()
            caches[addr] = cache
            # clear the lowest bit to get the true obj_cgroups
            objcg_vec = Object(prog, 'struct obj_cgroup **',
                               value=objcg_vec_raw & ~1)

            if addr not in stats:
                stats[addr] = 0

            for i in range(oo_objects(cache)):
                if objcg_vec[i].value_() in obj_cgroups:
                    stats[addr] += 1

        for addr in caches:
            if stats[addr] > 0:
                cache_show(caches[addr], cfg, stats[addr])

    else:
        for s in list_for_each_entry('struct kmem_cache',
                                     memcg.kmem_caches.address_of_(),
                                     'memcg_params.kmem_caches_node'):
            cache_show(s, cfg, None)


main()
