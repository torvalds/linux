#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2026: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=C0413,R0904


"""
Unit tests for kernel-doc CMatch.
"""

import os
import re
import sys
import unittest


# Import Python modules

SRC_DIR = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(SRC_DIR, "../lib/python"))

from kdoc.c_lex import CMatch
from kdoc.kdoc_re import KernRe
from unittest_helper import run_unittest

#
# Override unittest.TestCase to better compare diffs ignoring whitespaces
#
class TestCaseDiff(unittest.TestCase):
    """
    Disable maximum limit on diffs and add a method to better
    handle diffs with whitespace differences.
    """

    @classmethod
    def setUpClass(cls):
        """Ensure that there won't be limit for diffs"""
        cls.maxDiff = None


#
# Tests doing with different macros
#

class TestSearch(TestCaseDiff):
    """
    Test search mechanism
    """

    def test_search_acquires_simple(self):
        line = "__acquires(ctx) foo();"
        result = ", ".join(CMatch("__acquires").search(line))
        self.assertEqual(result, "__acquires(ctx)")

    def test_search_acquires_multiple(self):
        line = "__acquires(ctx) __acquires(other) bar();"
        result = ", ".join(CMatch("__acquires").search(line))
        self.assertEqual(result, "__acquires(ctx), __acquires(other)")

    def test_search_acquires_nested_paren(self):
        line = "__acquires((ctx1, ctx2)) baz();"
        result = ", ".join(CMatch("__acquires").search(line))
        self.assertEqual(result, "__acquires((ctx1, ctx2))")

    def test_search_must_hold(self):
        line = "__must_hold(&lock) do_something();"
        result = ", ".join(CMatch("__must_hold").search(line))
        self.assertEqual(result, "__must_hold(&lock)")

    def test_search_must_hold_shared(self):
        line = "__must_hold_shared(RCU) other();"
        result = ", ".join(CMatch("__must_hold_shared").search(line))
        self.assertEqual(result, "__must_hold_shared(RCU)")

    def test_search_no_false_positive(self):
        line = "call__acquires(foo);  // should stay intact"
        result = ", ".join(CMatch(r"__acquires").search(line))
        self.assertEqual(result, "")

    def test_search_no_macro_remains(self):
        line = "do_something_else();"
        result = ", ".join(CMatch("__acquires").search(line))
        self.assertEqual(result, "")

    def test_search_no_function(self):
        line = "something"
        result = ", ".join(CMatch(line).search(line))
        self.assertEqual(result, "")

#
# Override unittest.TestCase to better compare diffs ignoring whitespaces
#
class TestCaseDiff(unittest.TestCase):
    """
    Disable maximum limit on diffs and add a method to better
    handle diffs with whitespace differences.
    """

    @classmethod
    def setUpClass(cls):
        """Ensure that there won't be limit for diffs"""
        cls.maxDiff = None

    def assertLogicallyEqual(self, a, b):
        """
        Compare two results ignoring multiple whitespace differences.

        This is useful to check more complex matches picked from examples.
        On a plus side, we also don't need to use dedent.
        Please notice that line breaks still need to match. We might
        remove it at the regex, but this way, checking the diff is easier.
        """
        a = re.sub(r"[\t ]+", " ", a.strip())
        b = re.sub(r"[\t ]+", " ", b.strip())

        a = re.sub(r"\s+\n", "\n", a)
        b = re.sub(r"\s+\n", "\n", b)

        a = re.sub(" ;", ";", a)
        b = re.sub(" ;", ";", b)

        self.assertEqual(a, b)

#
# Tests doing with different macros
#

class TestSubMultipleMacros(TestCaseDiff):
    """
    Tests doing with different macros.

    Here, we won't use assertLogicallyEqual. Instead, we'll check if each
    of the expected patterns are present at the answer.
    """

    def test_acquires_simple(self):
        """Simple replacement test with __acquires"""
        line = "__acquires(ctx) foo();"
        result = CMatch(r"__acquires").sub("REPLACED", line)

        self.assertEqual("REPLACED foo();", result)

    def test_acquires_multiple(self):
        """Multiple __acquires"""
        line = "__acquires(ctx) __acquires(other) bar();"
        result = CMatch(r"__acquires").sub("REPLACED", line)

        self.assertEqual("REPLACED REPLACED bar();", result)

    def test_acquires_nested_paren(self):
        """__acquires with nested pattern"""
        line = "__acquires((ctx1, ctx2)) baz();"
        result = CMatch(r"__acquires").sub("REPLACED", line)

        self.assertEqual("REPLACED baz();", result)

    def test_must_hold(self):
        """__must_hold with a pointer"""
        line = "__must_hold(&lock) do_something();"
        result = CMatch(r"__must_hold").sub("REPLACED", line)

        self.assertNotIn("__must_hold(", result)
        self.assertIn("do_something();", result)

    def test_must_hold_shared(self):
        """__must_hold with an upercase defined value"""
        line = "__must_hold_shared(RCU) other();"
        result = CMatch(r"__must_hold_shared").sub("REPLACED", line)

        self.assertNotIn("__must_hold_shared(", result)
        self.assertIn("other();", result)

    def test_no_false_positive(self):
        """
        Ensure that unrelated text containing similar patterns is preserved
        """
        line = "call__acquires(foo);  // should stay intact"
        result = CMatch(r"\b__acquires").sub("REPLACED", line)

        self.assertLogicallyEqual(result, "call__acquires(foo);")

    def test_mixed_macros(self):
        """Add a mix of macros"""
        line = "__acquires(ctx) __releases(ctx) __must_hold(&lock) foo();"

        result = CMatch(r"__acquires").sub("REPLACED", line)
        result = CMatch(r"__releases").sub("REPLACED", result)
        result = CMatch(r"__must_hold").sub("REPLACED", result)

        self.assertNotIn("__acquires(", result)
        self.assertNotIn("__releases(", result)
        self.assertNotIn("__must_hold(", result)

        self.assertIn("foo();", result)

    def test_no_macro_remains(self):
        """Ensures that unmatched macros are untouched"""
        line = "do_something_else();"
        result = CMatch(r"__acquires").sub("REPLACED", line)

        self.assertEqual(result, line)

    def test_no_function(self):
        """Ensures that no functions will remain untouched"""
        line = "something"
        result = CMatch(line).sub("REPLACED", line)

        self.assertEqual(result, line)

#
# Check if the diff is logically equivalent. To simplify, the tests here
# use a single macro name for all replacements.
#

class TestSubSimple(TestCaseDiff):
    """
    Test argument replacements.

    Here, the function name can be anything. So, we picked __attribute__(),
    to mimic a macro found at the Kernel, but none of the replacements her
    has any relationship with the Kernel usage.
    """

    MACRO = "__attribute__"

    @classmethod
    def setUpClass(cls):
        """Define a CMatch to be used for all tests"""
        cls.matcher = CMatch(cls.MACRO)

    def test_sub_with_capture(self):
        """Test all arguments replacement with a single arg"""
        line = f"{self.MACRO}(&ctx)\nfoo();"

        result = self.matcher.sub(r"ACQUIRED(\0)", line)

        self.assertLogicallyEqual("ACQUIRED(&ctx)\nfoo();", result)

    def test_sub_zero_placeholder(self):
        """Test all arguments replacement with a multiple args"""
        line = f"{self.MACRO}(arg1, arg2)\nbar();"

        result = self.matcher.sub(r"REPLACED(\0)", line)

        self.assertLogicallyEqual("REPLACED(arg1, arg2)\nbar();", result)

    def test_sub_single_placeholder(self):
        """Single replacement rule for \1"""
        line = f"{self.MACRO}(ctx, boo)\nfoo();"
        result = self.matcher.sub(r"ACQUIRED(\1)", line)

        self.assertLogicallyEqual("ACQUIRED(ctx)\nfoo();", result)

    def test_sub_multiple_placeholders(self):
        """Replacement rule for both \1 and \2"""
        line = f"{self.MACRO}(arg1, arg2)\nbar();"
        result = self.matcher.sub(r"REPLACE(\1, \2)", line)

        self.assertLogicallyEqual("REPLACE(arg1, arg2)\nbar();", result)

    def test_sub_mixed_placeholders(self):
        """Replacement rule for \0, \1 and additional text"""
        line = f"{self.MACRO}(foo, bar)\nbaz();"
        result = self.matcher.sub(r"ALL(\0) FIRST(\1)", line)

        self.assertLogicallyEqual("ALL(foo, bar) FIRST(foo)\nbaz();", result)

    def test_sub_no_placeholder(self):
        """Replacement without placeholders"""
        line = f"{self.MACRO}(arg)\nfoo();"
        result = self.matcher.sub(r"NO_BACKREFS()", line)

        self.assertLogicallyEqual("NO_BACKREFS()\nfoo();", result)

    def test_sub_count_parameter(self):
        """Verify that the algorithm stops after the requested count"""
        line = f"{self.MACRO}(a1) x();\n{self.MACRO}(a2) y();"
        result = self.matcher.sub(r"ONLY_FIRST(\1) ", line, count=1)

        self.assertLogicallyEqual(f"ONLY_FIRST(a1) x();\n{self.MACRO}(a2) y();",
                                  result)

    def test_strip_multiple_acquires(self):
        """Check if spaces between removed delimiters will be dropped"""
        line = f"int {self.MACRO}(1)  {self.MACRO}(2 )   {self.MACRO}(3) foo;"
        result = self.matcher.sub("", line)

        self.assertLogicallyEqual(result, "int foo;")

    def test_rise_early_greedy(self):
        line = f"{self.MACRO}(a, b, c, d);"
        sub = r"\1, \2+, \3"

        with self.assertRaises(ValueError):
            result = self.matcher.sub(sub, line)

    def test_rise_multiple_greedy(self):
        line = f"{self.MACRO}(a, b, c, d);"
        sub = r"\1, \2+, \3+"

        with self.assertRaises(ValueError):
            result = self.matcher.sub(sub, line)

#
# Test replacements with slashrefs
#


class TestSubWithLocalXforms(TestCaseDiff):
    """
    Test diferent usecase patterns found at the Kernel.

    Here, replacements using both CMatch and KernRe can be tested,
    as it will import the actual replacement rules used by kernel-doc.
    """

    struct_xforms = [
        (CMatch("__attribute__"), ' '),
        (CMatch('__aligned'), ' '),
        (CMatch('__counted_by'), ' '),
        (CMatch('__counted_by_(le|be)'), ' '),
        (CMatch('__guarded_by'), ' '),
        (CMatch('__pt_guarded_by'), ' '),

        (CMatch('__cacheline_group_(begin|end)'), ''),

        (CMatch('struct_group'), r'\2'),
        (CMatch('struct_group_attr'), r'\3'),
        (CMatch('struct_group_tagged'), r'struct \1 { \3+ } \2;'),
        (CMatch('__struct_group'), r'\4'),

        (CMatch('__ETHTOOL_DECLARE_LINK_MODE_MASK'), r'DECLARE_BITMAP(\1, __ETHTOOL_LINK_MODE_MASK_NBITS)'),
        (CMatch('DECLARE_PHY_INTERFACE_MASK',), r'DECLARE_BITMAP(\1, PHY_INTERFACE_MODE_MAX)'),
        (CMatch('DECLARE_BITMAP'), r'unsigned long \1[BITS_TO_LONGS(\2)]'),

        (CMatch('DECLARE_HASHTABLE'), r'unsigned long \1[1 << ((\2) - 1)]'),
        (CMatch('DECLARE_KFIFO'), r'\2 *\1'),
        (CMatch('DECLARE_KFIFO_PTR'), r'\2 *\1'),
        (CMatch('(?:__)?DECLARE_FLEX_ARRAY'), r'\1 \2[]'),
        (CMatch('DEFINE_DMA_UNMAP_ADDR'), r'dma_addr_t \1'),
        (CMatch('DEFINE_DMA_UNMAP_LEN'), r'__u32 \1'),
        (CMatch('VIRTIO_DECLARE_FEATURES'), r'union { u64 \1; u64 \1_array[VIRTIO_FEATURES_U64S]; }'),
    ]

    function_xforms = [
        (CMatch('__printf'), ""),
        (CMatch('__(?:re)?alloc_size'), ""),
        (CMatch("__diagnose_as"), ""),
        (CMatch("DECL_BUCKET_PARAMS"), r"\1, \2"),

        (CMatch("__cond_acquires"), ""),
        (CMatch("__cond_releases"), ""),
        (CMatch("__acquires"), ""),
        (CMatch("__releases"), ""),
        (CMatch("__must_hold"), ""),
        (CMatch("__must_not_hold"), ""),
        (CMatch("__must_hold_shared"), ""),
        (CMatch("__cond_acquires_shared"), ""),
        (CMatch("__acquires_shared"), ""),
        (CMatch("__releases_shared"), ""),
        (CMatch("__attribute__"), ""),
    ]

    var_xforms = [
        (CMatch('__guarded_by'), ""),
        (CMatch('__pt_guarded_by'), ""),
        (CMatch("LIST_HEAD"), r"struct list_head \1"),
    ]

    #: Transforms main dictionary used at apply_transforms().
    xforms = {
        "struct": struct_xforms,
        "func": function_xforms,
        "var": var_xforms,
    }

    @classmethod
    def apply_transforms(cls, xform_type, text):
        """
        Mimic the behavior of kdoc_parser.apply_transforms() method.

        For each element of STRUCT_XFORMS, apply apply_transforms.

        There are two parameters:

        - ``xform_type``
            Can be ``func``, ``struct`` or ``var``;
        - ``text``
            The text where the sub patterns from CTransforms will be applied.
        """
        for search, subst in cls.xforms.get(xform_type):
            text = search.sub(subst, text)

        return text.strip()

        cls.matcher = CMatch(r"struct_group[\w\_]*")

    def test_struct_group(self):
        """
        Test struct_group using a pattern from
        drivers/net/ethernet/asix/ax88796c_main.h.
        """
        line = """
            struct tx_pkt_info {
                    struct_group(tx_overhead,
                            struct tx_sop_header sop;
                            struct tx_segment_header seg;
                    );
                    struct tx_eop_header eop;
                    u16 pkt_len;
                    u16 seq_num;
            };
        """
        expected = """
            struct tx_pkt_info {
                    struct tx_sop_header sop;
                    struct tx_segment_header seg;
                    struct tx_eop_header eop;
                    u16 pkt_len;
                    u16 seq_num;
            };
        """

        result = self.apply_transforms("struct", line)
        self.assertLogicallyEqual(result, expected)

    def test_struct_group_attr(self):
        """
        Test two struct_group_attr using patterns from fs/smb/client/cifspdu.h.
        """
        line = """
            typedef struct smb_com_open_rsp {
                struct smb_hdr hdr;     /* wct = 34 BB */
                __u8 AndXCommand;
                __u8 AndXReserved;
                __le16 AndXOffset;
                __u8 OplockLevel;
                __u16 Fid;
                __le32 CreateAction;
                struct_group_attr(common_attributes,,
                    __le64 CreationTime;
                    __le64 LastAccessTime;
                    __le64 LastWriteTime;
                    __le64 ChangeTime;
                    __le32 FileAttributes;
                );
                __le64 AllocationSize;
                __le64 EndOfFile;
                __le16 FileType;
                __le16 DeviceState;
                __u8 DirectoryFlag;
                __u16 ByteCount;        /* bct = 0 */
            } OPEN_RSP;
            typedef struct {
                struct_group_attr(common_attributes,,
                    __le64 CreationTime;
                    __le64 LastAccessTime;
                    __le64 LastWriteTime;
                    __le64 ChangeTime;
                    __le32 Attributes;
                );
                __u32 Pad1;
                __le64 AllocationSize;
                __le64 EndOfFile;
                __le32 NumberOfLinks;
                __u8 DeletePending;
                __u8 Directory;
                __u16 Pad2;
                __le32 EASize;
                __le32 FileNameLength;
                union {
                    char __pad;
                    DECLARE_FLEX_ARRAY(char, FileName);
                };
            } FILE_ALL_INFO;       /* level 0x107 QPathInfo */
        """
        expected = """
            typedef struct smb_com_open_rsp {
                struct smb_hdr hdr;
                __u8 AndXCommand;
                __u8 AndXReserved;
                __le16 AndXOffset;
                __u8 OplockLevel;
                __u16 Fid;
                __le32 CreateAction;
                __le64 CreationTime;
                __le64 LastAccessTime;
                __le64 LastWriteTime;
                __le64 ChangeTime;
                __le32 FileAttributes;
                __le64 AllocationSize;
                __le64 EndOfFile;
                __le16 FileType;
                __le16 DeviceState;
                __u8 DirectoryFlag;
                __u16 ByteCount;
            } OPEN_RSP;
        typedef struct {
            __le64 CreationTime;
            __le64 LastAccessTime;
            __le64 LastWriteTime;
            __le64 ChangeTime;
            __le32 Attributes;
            __u32 Pad1;
            __le64 AllocationSize;
            __le64 EndOfFile;
            __le32 NumberOfLinks;
            __u8 DeletePending;
            __u8 Directory;
            __u16 Pad2;
            __le32 EASize;
            __le32 FileNameLength;
            union {
                char __pad;
                char FileName[];
            };
        } FILE_ALL_INFO;
        """

        result = self.apply_transforms("struct", line)
        self.assertLogicallyEqual(result, expected)

    def test_raw_struct_group(self):
        """
        Test a __struct_group pattern from include/uapi/cxl/features.h.
        """
        line = """
            struct cxl_mbox_get_sup_feats_out {
                __struct_group(cxl_mbox_get_sup_feats_out_hdr, hdr, /* empty */,
                    __le16 num_entries;
                    __le16 supported_feats;
                    __u8 reserved[4];
                );
                struct cxl_feat_entry ents[] __counted_by_le(num_entries);
            } __attribute__ ((__packed__));
        """
        expected = """
            struct cxl_mbox_get_sup_feats_out {
                __le16 num_entries;
                __le16 supported_feats;
                __u8 reserved[4];
                struct cxl_feat_entry ents[];
            };
        """

        result = self.apply_transforms("struct", line)
        self.assertLogicallyEqual(result, expected)

    def test_raw_struct_group_tagged(self):
        r"""
        Test cxl_regs with struct_group_tagged patterns from drivers/cxl/cxl.h.

        NOTE:

            This one has actually a violation from what kernel-doc would
            expect: Kernel-doc regex expects only 3 members, but this is
            actually defined as::

                #define struct_group_tagged(TAG, NAME, MEMBERS...)

            The replace expression there is::

                struct \1 { \3 } \2;

            but it should be really something like::

                struct \1 { \3 \4 \5 \6 \7 \8 ... } \2;

            a later fix would be needed to address it.

        """
        line = """
            struct cxl_regs {
                struct_group_tagged(cxl_component_regs, component,
                    void __iomem *hdm_decoder;
                    void __iomem *ras;
                );


                /* This is actually a violation: too much commas */
                struct_group_tagged(cxl_device_regs, device_regs,
                    void __iomem *status, *mbox, *memdev;
                );

                struct_group_tagged(cxl_pmu_regs, pmu_regs,
                    void __iomem *pmu;
                );

                struct_group_tagged(cxl_rch_regs, rch_regs,
                    void __iomem *dport_aer;
                );

                struct_group_tagged(cxl_rcd_regs, rcd_regs,
                    void __iomem *rcd_pcie_cap;
                );
            };
        """
        expected = """
        struct cxl_regs {
            struct cxl_component_regs {
                void __iomem *hdm_decoder;
                void __iomem *ras;
            } component;

            struct cxl_device_regs {
                void __iomem *status, *mbox, *memdev;
            } device_regs;

            struct cxl_pmu_regs {
                void __iomem *pmu;
            } pmu_regs;

            struct cxl_rch_regs {
                void __iomem *dport_aer;
            } rch_regs;

            struct cxl_rcd_regs {
                void __iomem *rcd_pcie_cap;
            } rcd_regs;
        };
        """

        result = self.apply_transforms("struct", line)
        self.assertLogicallyEqual(result, expected)

    def test_struct_group_tagged_with_private(self):
        """
        Replace struct_group_tagged with private, using the same regex
        for the replacement as what happens in xforms_lists.py.

        As the private removal happens outside NestedGroup class, we manually
        dropped the remaining part of the struct, to simulate what happens
        at kdoc_parser.

        Taken from include/net/page_pool/types.h
        """
        line = """
            struct page_pool_params {
                struct_group_tagged(page_pool_params_slow, slow,
                                    struct net_device *netdev;
                                    unsigned int queue_idx;
                                    unsigned int    flags;
                                    /* private: only under "slow" struct */
                                    unsigned int ignored;
                );
                /* Struct below shall not be ignored */
                struct_group_tagged(page_pool_params_fast, fast,
                                    unsigned int    order;
                                    unsigned int    pool_size;
                                    int             nid;
                                    struct device   *dev;
                                    struct napi_struct *napi;
                                    enum dma_data_direction dma_dir;
                                    unsigned int    max_len;
                                    unsigned int    offset;
                );
            };
        """
        expected = """
            struct page_pool_params {
                struct page_pool_params_slow {
                    struct net_device *netdev;
                    unsigned int queue_idx;
                    unsigned int    flags;
                } slow;
                struct page_pool_params_fast {
                    unsigned int order;
                    unsigned int    pool_size;
                    int             nid;
                    struct device   *dev;
                    struct napi_struct *napi;
                    enum dma_data_direction dma_dir;
                    unsigned int    max_len;
                    unsigned int    offset;
                } fast;
            };
        """

        result = self.apply_transforms("struct", line)
        self.assertLogicallyEqual(result, expected)

    def test_struct_kcov(self):
        """
        """
        line = """
            struct kcov {
                refcount_t              refcount;
                spinlock_t              lock;
                enum kcov_mode          mode __guarded_by(&lock);
                unsigned int            size __guarded_by(&lock);
                void                    *area __guarded_by(&lock);
                struct task_struct      *t __guarded_by(&lock);
                bool                    remote;
                unsigned int            remote_size;
                int                     sequence;
            };
        """
        expected = """
        """

        result = self.apply_transforms("struct", line)
        self.assertLogicallyEqual(result, expected)


    def test_struct_kcov(self):
        """
        Test a struct from kernel/kcov.c.
        """
        line = """
            struct kcov {
                refcount_t              refcount;
                spinlock_t              lock;
                enum kcov_mode          mode __guarded_by(&lock);
                unsigned int            size __guarded_by(&lock);
                void                    *area __guarded_by(&lock);
                struct task_struct      *t __guarded_by(&lock);
                bool                    remote;
                unsigned int            remote_size;
                int                     sequence;
            };
        """
        expected = """
            struct kcov {
                refcount_t              refcount;
                spinlock_t              lock;
                enum kcov_mode          mode;
                unsigned int            size;
                void                    *area;
                struct task_struct      *t;
                bool                    remote;
                unsigned int            remote_size;
                int                     sequence;
            };
        """

        result = self.apply_transforms("struct", line)
        self.assertLogicallyEqual(result, expected)

    def test_vars_stackdepot(self):
        """
        Test guarded_by on vars from lib/stackdepot.c.
        """
        line = """
            size_t pool_offset __guarded_by(&pool_lock) = DEPOT_POOL_SIZE;
            __guarded_by(&pool_lock) LIST_HEAD(free_stacks);
            void **stack_pools __pt_guarded_by(&pool_lock);
        """
        expected = """
            size_t pool_offset = DEPOT_POOL_SIZE;
            struct list_head free_stacks;
            void **stack_pools;
        """

        result = self.apply_transforms("var", line)
        self.assertLogicallyEqual(result, expected)

    def test_functions_with_acquires_and_releases(self):
        """
        Test guarded_by on vars from lib/stackdepot.c.
        """
        line = """
            bool prepare_report_consumer(unsigned long *flags,
                                         const struct access_info *ai,
                                         struct other_info *other_info) \
                                        __cond_acquires(true, &report_lock);

            int tcp_sigpool_start(unsigned int id, struct tcp_sigpool *c) \
                                  __cond_acquires(0, RCU_BH);

            bool undo_report_consumer(unsigned long *flags,
                                      const struct access_info *ai,
                                      struct other_info *other_info) \
                                     __cond_releases(true, &report_lock);

            void debugfs_enter_cancellation(struct file *file,
                                            struct debugfs_cancellation *c) \
                                           __acquires(cancellation);

            void debugfs_leave_cancellation(struct file *file,
                                            struct debugfs_cancellation *c) \
                                           __releases(cancellation);

            acpi_cpu_flags acpi_os_acquire_lock(acpi_spinlock lockp) \
                                               __acquires(lockp);

            void acpi_os_release_lock(acpi_spinlock lockp,
                                      acpi_cpu_flags not_used) \
                                     __releases(lockp)
        """
        expected = """
            bool prepare_report_consumer(unsigned long *flags,
                                         const struct access_info *ai,
                                         struct other_info *other_info);

            int tcp_sigpool_start(unsigned int id, struct tcp_sigpool *c);

            bool undo_report_consumer(unsigned long *flags,
                                      const struct access_info *ai,
                                      struct other_info *other_info);

            void debugfs_enter_cancellation(struct file *file,
                                            struct debugfs_cancellation *c);

            void debugfs_leave_cancellation(struct file *file,
                                            struct debugfs_cancellation *c);

            acpi_cpu_flags acpi_os_acquire_lock(acpi_spinlock lockp);

            void acpi_os_release_lock(acpi_spinlock lockp,
                                      acpi_cpu_flags not_used)
        """

        result = self.apply_transforms("func", line)
        self.assertLogicallyEqual(result, expected)

#
# Run all tests
#
if __name__ == "__main__":
    run_unittest(__file__)
