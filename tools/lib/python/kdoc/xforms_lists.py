#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2026: Mauro Carvalho Chehab <mchehab@kernel.org>.

import re

from kdoc.kdoc_re import KernRe
from kdoc.c_lex import CMatch, CTokenizer

struct_args_pattern = r"([^,)]+)"


class CTransforms:
    """
    Data class containing a long set of transformations to turn
    structure member prefixes, and macro invocations and variables
    into something we can parse and generate kdoc for.
    """

    #
    # NOTE:
    #      Due to performance reasons, place CMatch rules before KernRe,
    #      as this avoids running the C parser every time.
    #

    #: Transforms for structs and unions.
    struct_xforms = [
        (CMatch("__attribute__"), ""),
        (CMatch("__aligned"), ""),
        (CMatch("__counted_by"), ""),
        (CMatch("__counted_by_(le|be)"), ""),
        (CMatch("__guarded_by"), ""),
        (CMatch("__pt_guarded_by"), ""),
        (CMatch("__packed"), ""),
        (CMatch("CRYPTO_MINALIGN_ATTR"), ""),
        (CMatch("__private"), ""),
        (CMatch("__rcu"), ""),
        (CMatch("____cacheline_aligned_in_smp"), ""),
        (CMatch("____cacheline_aligned"), ""),
        (CMatch("__cacheline_group_(?:begin|end)"), ""),
        (CMatch("__ETHTOOL_DECLARE_LINK_MODE_MASK"), r"DECLARE_BITMAP(\1, __ETHTOOL_LINK_MODE_MASK_NBITS)"),
        (CMatch("DECLARE_PHY_INTERFACE_MASK",),r"DECLARE_BITMAP(\1, PHY_INTERFACE_MODE_MAX)"),
        (CMatch("DECLARE_BITMAP"), r"unsigned long \1[BITS_TO_LONGS(\2)]"),
        (CMatch("DECLARE_HASHTABLE"), r"unsigned long \1[1 << ((\2) - 1)]"),
        (CMatch("DECLARE_KFIFO"), r"\2 *\1"),
        (CMatch("DECLARE_KFIFO_PTR"), r"\2 *\1"),
        (CMatch("(?:__)?DECLARE_FLEX_ARRAY"), r"\1 \2[]"),
        (CMatch("DEFINE_DMA_UNMAP_ADDR"), r"dma_addr_t \1"),
        (CMatch("DEFINE_DMA_UNMAP_LEN"), r"__u32 \1"),
        (CMatch("VIRTIO_DECLARE_FEATURES"), r"union { u64 \1; u64 \1_array[VIRTIO_FEATURES_U64S]; }"),
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

        #
        # Macro __struct_group() creates an union with an anonymous
        # and a non-anonymous struct, depending on the parameters. We only
        # need one of those at kernel-doc, as we won't be documenting the same
        # members twice.
        #
        (CMatch("struct_group"), r"struct { \2+ };"),
        (CMatch("struct_group_attr"), r"struct { \3+ };"),
        (CMatch("struct_group_tagged"), r"struct { \3+ };"),
        (CMatch("__struct_group"), r"struct { \4+ };"),
    ]

    #: Transforms for function prototypes.
    function_xforms = [
        (CMatch("static"), ""),
        (CMatch("extern"), ""),
        (CMatch("asmlinkage"), ""),
        (CMatch("inline"), ""),
        (CMatch("__inline__"), ""),
        (CMatch("__inline"), ""),
        (CMatch("__always_inline"), ""),
        (CMatch("noinline"), ""),
        (CMatch("__FORTIFY_INLINE"), ""),
        (CMatch("__init"), ""),
        (CMatch("__init_or_module"), ""),
        (CMatch("__exit"), ""),
        (CMatch("__deprecated"), ""),
        (CMatch("__flatten"), ""),
        (CMatch("__meminit"), ""),
        (CMatch("__must_check"), ""),
        (CMatch("__weak"), ""),
        (CMatch("__sched"), ""),
        (CMatch("__always_unused"), ""),
        (CMatch("__printf"), ""),
        (CMatch("__(?:re)?alloc_size"), ""),
        (CMatch("__diagnose_as"), ""),
        (CMatch("DECL_BUCKET_PARAMS"), r"\1, \2"),
        (CMatch("__no_context_analysis"), ""),
        (CMatch("__attribute_const__"), ""),
        (CMatch("__attribute__"), ""),

        #
        # HACK: this is similar to process_export() hack. It is meant to
        # drop _noproof from function name. See for instance:
        # ahash_request_alloc kernel-doc declaration at include/crypto/hash.h.
        #
        (KernRe("_noprof"), ""),
    ]

    #: Transforms for variable prototypes.
    var_xforms = [
        (CMatch("__read_mostly"), ""),
        (CMatch("__ro_after_init"), ""),
        (CMatch("__guarded_by"), ""),
        (CMatch("__pt_guarded_by"), ""),
        (CMatch("LIST_HEAD"), r"struct list_head \1"),

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

    def apply(self, xforms_type, source):
        """
        Apply a set of transforms to a block of source.

        As tokenizer is used here, this function also remove comments
        at the end.
        """
        if xforms_type not in self.xforms:
            return source

        if isinstance(source, str):
            source = CTokenizer(source)

        for search, subst in self.xforms[xforms_type]:
            #
            # KernRe only accept strings.
            #
            if isinstance(search, KernRe):
                source = str(source)

            source = search.sub(subst, source)
        return str(source)
