# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 MediaTek Inc.
#
# Authors:
#  Kuan-Ying Lee <Kuan-Ying.Lee@mediatek.com>
#

import gdb
from linux import utils, stackdepot, constants, mm

if constants.LX_CONFIG_PAGE_OWNER:
    page_ext_t = utils.CachedType('struct page_ext')
    page_owner_t = utils.CachedType('struct page_owner')

    PAGE_OWNER_STACK_DEPTH = 16
    PAGE_EXT_OWNER = constants.LX_PAGE_EXT_OWNER
    PAGE_EXT_INVALID = 0x1
    PAGE_EXT_OWNER_ALLOCATED = constants.LX_PAGE_EXT_OWNER_ALLOCATED

def help():
    t = """Usage: lx-dump-page-owner [Option]
    Option:
        --pfn [Decimal pfn]
    Example:
        lx-dump-page-owner --pfn 655360\n"""
    gdb.write("Unrecognized command\n")
    raise gdb.GdbError(t)

class DumpPageOwner(gdb.Command):
    """Dump page owner"""

    min_pfn = None
    max_pfn = None
    p_ops = None
    migrate_reason_names = None

    def __init__(self):
        super(DumpPageOwner, self).__init__("lx-dump-page-owner", gdb.COMMAND_SUPPORT)

    def invoke(self, args, from_tty):
        if not constants.LX_CONFIG_PAGE_OWNER:
            raise gdb.GdbError('CONFIG_PAGE_OWNER does not enable')

        page_owner_inited = gdb.parse_and_eval('page_owner_inited')
        if page_owner_inited['key']['enabled']['counter'] != 0x1:
            raise gdb.GdbError('page_owner_inited is not enabled')

        self.p_ops = mm.page_ops().ops
        self.get_page_owner_info()
        argv = gdb.string_to_argv(args)
        if len(argv) == 0:
              self.read_page_owner()
        elif len(argv) == 2:
            if argv[0] == "--pfn":
                pfn = int(argv[1])
                self.read_page_owner_by_addr(self.p_ops.pfn_to_page(pfn))
            else:
                help()
        else:
            help()

    def get_page_owner_info(self):
        self.min_pfn = int(gdb.parse_and_eval("min_low_pfn"))
        self.max_pfn = int(gdb.parse_and_eval("max_pfn"))
        self.page_ext_size = int(gdb.parse_and_eval("page_ext_size"))
        self.migrate_reason_names = gdb.parse_and_eval('migrate_reason_names')

    def page_ext_invalid(self, page_ext):
        if page_ext == gdb.Value(0):
            return True
        if page_ext.cast(utils.get_ulong_type()) & PAGE_EXT_INVALID == PAGE_EXT_INVALID:
            return True
        return False

    def get_entry(self, base, index):
        return (base.cast(utils.get_ulong_type()) + self.page_ext_size * index).cast(page_ext_t.get_type().pointer())

    def lookup_page_ext(self, page):
        pfn = self.p_ops.page_to_pfn(page)
        section = self.p_ops.pfn_to_section(pfn)
        page_ext = section["page_ext"]
        if self.page_ext_invalid(page_ext):
            return gdb.Value(0)
        return self.get_entry(page_ext, pfn)

    def page_ext_get(self, page):
        page_ext = self.lookup_page_ext(page)
        if page_ext != gdb.Value(0):
            return page_ext
        else:
            return gdb.Value(0)

    def get_page_owner(self, page_ext):
        addr = page_ext.cast(utils.get_ulong_type()) + gdb.parse_and_eval("page_owner_ops")["offset"].cast(utils.get_ulong_type())
        return addr.cast(page_owner_t.get_type().pointer())

    def read_page_owner_by_addr(self, struct_page_addr):
        page = gdb.Value(struct_page_addr).cast(utils.get_page_type().pointer())
        pfn = self.p_ops.page_to_pfn(page)

        if pfn < self.min_pfn or pfn > self.max_pfn or (not self.p_ops.pfn_valid(pfn)):
            gdb.write("pfn is invalid\n")
            return

        page = self.p_ops.pfn_to_page(pfn)
        page_ext = self.page_ext_get(page)

        if page_ext == gdb.Value(0):
            gdb.write("page_ext is null\n")
            return

        if not (page_ext['flags'] & (1 << PAGE_EXT_OWNER)):
            gdb.write("page_owner flag is invalid\n")
            raise gdb.GdbError('page_owner info is not present (never set?)\n')

        if mm.test_bit(PAGE_EXT_OWNER_ALLOCATED, page_ext['flags'].address):
            gdb.write('page_owner tracks the page as allocated\n')
        else:
            gdb.write('page_owner tracks the page as freed\n')

        if not (page_ext['flags'] & (1 << PAGE_EXT_OWNER_ALLOCATED)):
            gdb.write("page_owner is not allocated\n")

        try:
            page_owner = self.get_page_owner(page_ext)
            gdb.write("Page last allocated via order %d, gfp_mask: 0x%x, pid: %d, tgid: %d (%s), ts %u ns, free_ts %u ns\n" %\
                    (page_owner["order"], page_owner["gfp_mask"],\
                    page_owner["pid"], page_owner["tgid"], page_owner["comm"],\
                    page_owner["ts_nsec"], page_owner["free_ts_nsec"]))
            gdb.write("PFN: %d, Flags: 0x%x\n" % (pfn, page['flags']))
            if page_owner["handle"] == 0:
                gdb.write('page_owner allocation stack trace missing\n')
            else:
                stackdepot.stack_depot_print(page_owner["handle"])

            if page_owner["free_handle"] == 0:
                gdb.write('page_owner free stack trace missing\n')
            else:
                gdb.write('page last free stack trace:\n')
                stackdepot.stack_depot_print(page_owner["free_handle"])
            if page_owner['last_migrate_reason'] != -1:
                gdb.write('page has been migrated, last migrate reason: %s\n' % self.migrate_reason_names[page_owner['last_migrate_reason']])
        except:
            gdb.write("\n")

    def read_page_owner(self):
        pfn = self.min_pfn

        # Find a valid PFN or the start of a MAX_ORDER_NR_PAGES area
        while ((not self.p_ops.pfn_valid(pfn)) and (pfn & (self.p_ops.MAX_ORDER_NR_PAGES - 1))) != 0:
            pfn += 1

        while pfn < self.max_pfn:
            #
            # If the new page is in a new MAX_ORDER_NR_PAGES area,
            # validate the area as existing, skip it if not
            #
            if ((pfn & (self.p_ops.MAX_ORDER_NR_PAGES - 1)) == 0) and (not self.p_ops.pfn_valid(pfn)):
                pfn += (self.p_ops.MAX_ORDER_NR_PAGES - 1)
                continue;

            page = self.p_ops.pfn_to_page(pfn)
            page_ext = self.page_ext_get(page)
            if page_ext == gdb.Value(0):
                pfn += 1
                continue

            if not (page_ext['flags'] & (1 << PAGE_EXT_OWNER)):
                pfn += 1
                continue
            if not (page_ext['flags'] & (1 << PAGE_EXT_OWNER_ALLOCATED)):
                pfn += 1
                continue

            try:
                page_owner = self.get_page_owner(page_ext)
                gdb.write("Page allocated via order %d, gfp_mask: 0x%x, pid: %d, tgid: %d (%s), ts %u ns, free_ts %u ns\n" %\
                        (page_owner["order"], page_owner["gfp_mask"],\
                        page_owner["pid"], page_owner["tgid"], page_owner["comm"],\
                        page_owner["ts_nsec"], page_owner["free_ts_nsec"]))
                gdb.write("PFN: %d, Flags: 0x%x\n" % (pfn, page['flags']))
                stackdepot.stack_depot_print(page_owner["handle"])
                pfn += (1 << page_owner["order"])
                continue
            except:
                gdb.write("\n")
            pfn += 1

DumpPageOwner()
