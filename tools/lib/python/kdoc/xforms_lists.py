#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2026: Mauro Carvalho Chehab <mchehab@kernel.org>.

import re

from kdoc.kdoc_re import KernRe
from kdoc.c_lex import CMatch

struct_args_pattern = r'([^,)]+)'

class CTransforms:
    """
    Data class containing a long set of transformations to turn
    structure member prefixes, and macro invocations and variables
    into something we can parse and generate kdoc for.
    """

    #: Transforms for structs and unions.
    struct_xforms = [
        # Strip attributes
        (KernRe(r"__attribute__\s*\(\([a-z0-9,_\*\s\(\)]*\)\)", flags=re.I | re.S, cache=False), ' '),
        (KernRe(r'\s*__aligned\s*\([^;]*\)', re.S), ' '),
        (KernRe(r'\s*__counted_by\s*\([^;]*\)', re.S), ' '),
        (KernRe(r'\s*__counted_by_(le|be)\s*\([^;]*\)', re.S), ' '),
        (KernRe(r'\s*__guarded_by\s*\([^\)]*\)', re.S), ' '),
        (KernRe(r'\s*__pt_guarded_by\s*\([^\)]*\)', re.S), ' '),
        (KernRe(r'\s*__packed\s*', re.S), ' '),
        (KernRe(r'\s*CRYPTO_MINALIGN_ATTR', re.S), ' '),
        (KernRe(r'\s*__private', re.S), ' '),
        (KernRe(r'\s*__rcu', re.S), ' '),
        (KernRe(r'\s*____cacheline_aligned_in_smp', re.S), ' '),
        (KernRe(r'\s*____cacheline_aligned', re.S), ' '),
        (KernRe(r'\s*__cacheline_group_(begin|end)\([^\)]+\);'), ''),
        (KernRe(r'__ETHTOOL_DECLARE_LINK_MODE_MASK\s*\(([^\)]+)\)', re.S),
        r'DECLARE_BITMAP(\1, __ETHTOOL_LINK_MODE_MASK_NBITS)'),
        (KernRe(r'DECLARE_PHY_INTERFACE_MASK\s*\(([^\)]+)\)', re.S),
        r'DECLARE_BITMAP(\1, PHY_INTERFACE_MODE_MAX)'),
        (KernRe(r'DECLARE_BITMAP\s*\(' + struct_args_pattern + r',\s*' + struct_args_pattern + r'\)',
                re.S), r'unsigned long \1[BITS_TO_LONGS(\2)]'),
        (KernRe(r'DECLARE_HASHTABLE\s*\(' + struct_args_pattern + r',\s*' + struct_args_pattern + r'\)',
                re.S), r'unsigned long \1[1 << ((\2) - 1)]'),
        (KernRe(r'DECLARE_KFIFO\s*\(' + struct_args_pattern + r',\s*' + struct_args_pattern +
                r',\s*' + struct_args_pattern + r'\)', re.S), r'\2 *\1'),
        (KernRe(r'DECLARE_KFIFO_PTR\s*\(' + struct_args_pattern + r',\s*' +
                struct_args_pattern + r'\)', re.S), r'\2 *\1'),
        (KernRe(r'(?:__)?DECLARE_FLEX_ARRAY\s*\(' + struct_args_pattern + r',\s*' +
                struct_args_pattern + r'\)', re.S), r'\1 \2[]'),
        (KernRe(r'DEFINE_DMA_UNMAP_ADDR\s*\(' + struct_args_pattern + r'\)', re.S), r'dma_addr_t \1'),
        (KernRe(r'DEFINE_DMA_UNMAP_LEN\s*\(' + struct_args_pattern + r'\)', re.S), r'__u32 \1'),
        (KernRe(r'VIRTIO_DECLARE_FEATURES\(([\w_]+)\)'), r'union { u64 \1; u64 \1_array[VIRTIO_FEATURES_U64S]; }'),

        (CMatch(r"__cond_acquires"), ""),
        (CMatch(r"__cond_releases"), ""),
        (CMatch(r"__acquires"), ""),
        (CMatch(r"__releases"), ""),
        (CMatch(r"__must_hold"), ""),
        (CMatch(r"__must_not_hold"), ""),
        (CMatch(r"__must_hold_shared"), ""),
        (CMatch(r"__cond_acquires_shared"), ""),
        (CMatch(r"__acquires_shared"), ""),
        (CMatch(r"__releases_shared"), ""),

        #
        # Macro __struct_group() creates an union with an anonymous
        # and a non-anonymous struct, depending on the parameters. We only
        # need one of those at kernel-doc, as we won't be documenting the same
        # members twice.
        #
        (CMatch('struct_group'), r'struct { \2+ };'),
        (CMatch('struct_group_attr'), r'struct { \3+ };'),
        (CMatch('struct_group_tagged'), r'struct { \3+ };'),
        (CMatch('__struct_group'), r'struct { \4+ };'),

    ]

    #: Transforms for function prototypes.
    function_xforms = [
        (KernRe(r"^static +"), ""),
        (KernRe(r"^extern +"), ""),
        (KernRe(r"^asmlinkage +"), ""),
        (KernRe(r"^inline +"), ""),
        (KernRe(r"^__inline__ +"), ""),
        (KernRe(r"^__inline +"), ""),
        (KernRe(r"^__always_inline +"), ""),
        (KernRe(r"^noinline +"), ""),
        (KernRe(r"^__FORTIFY_INLINE +"), ""),
        (KernRe(r"__init +"), ""),
        (KernRe(r"__init_or_module +"), ""),
        (KernRe(r"__exit +"), ""),
        (KernRe(r"__deprecated +"), ""),
        (KernRe(r"__flatten +"), ""),
        (KernRe(r"__meminit +"), ""),
        (KernRe(r"__must_check +"), ""),
        (KernRe(r"__weak +"), ""),
        (KernRe(r"__sched +"), ""),
        (KernRe(r"_noprof"), ""),
        (KernRe(r"__always_unused *"), ""),
        (KernRe(r"__printf\s*\(\s*\d*\s*,\s*\d*\s*\) +"), ""),
        (KernRe(r"__(?:re)?alloc_size\s*\(\s*\d+\s*(?:,\s*\d+\s*)?\) +"), ""),
        (KernRe(r"__diagnose_as\s*\(\s*\S+\s*(?:,\s*\d+\s*)*\) +"), ""),
        (KernRe(r"DECL_BUCKET_PARAMS\s*\(\s*(\S+)\s*,\s*(\S+)\s*\)"), r"\1, \2"),
        (KernRe(r"__no_context_analysis\s*"), ""),
        (KernRe(r"__attribute_const__ +"), ""),
        (KernRe(r"__attribute__\s*\(\((?:[\w\s]+(?:\([^)]*\))?\s*,?)+\)\)\s+"), ""),
    ]

    #: Transforms for variable prototypes.
    var_xforms = [
        (KernRe(r"__read_mostly"), ""),
        (KernRe(r"__ro_after_init"), ""),
        (KernRe(r'\s*__guarded_by\s*\([^\)]*\)', re.S), ""),
        (KernRe(r'\s*__pt_guarded_by\s*\([^\)]*\)', re.S), ""),
        (KernRe(r"LIST_HEAD\(([\w_]+)\)"), r"struct list_head \1"),
        (KernRe(r"(?://.*)$"), ""),
        (KernRe(r"(?:/\*.*\*/)"), ""),
        (KernRe(r";$"), ""),
    ]

    #: Transforms main dictionary used at apply_transforms().
    xforms = {
        "struct": struct_xforms,
        "func": function_xforms,
        "var": var_xforms,
    }

    def apply(self, xforms_type, text):
        """
        Apply a set of transforms to a block of text.
        """
        if xforms_type not in self.xforms:
            return text

        for search, subst in self.xforms[xforms_type]:
            text = search.sub(subst, text)
        return text
