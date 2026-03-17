#!/usr/bin/env python3

"""
Unit tests for struct/union member extractor class.
"""


import os
import re
import unittest
import sys

from unittest.mock import MagicMock

SRC_DIR = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(SRC_DIR, "../lib/python"))

from kdoc.kdoc_parser import trim_private_members
from unittest_helper import run_unittest

#
# List of tests.
#
# The code will dynamically generate one test for each key on this dictionary.
#

#: Tests to check if CTokenizer is handling properly public/private comments.
TESTS_PRIVATE = {
    #
    # Simplest case: no private. Ensure that trimming won't affect struct
    #
    "no private": {
        "source": """
            struct foo {
                int a;
                int b;
                int c;
            };
        """,
        "trimmed": """
            struct foo {
                int a;
                int b;
                int c;
            };
        """,
    },

    #
    # Play "by the books" by always having a public in place
    #

    "balanced_private": {
        "source": """
            struct foo {
                int a;
                /* private: */
                int b;
                /* public: */
                int c;
            };
        """,
        "trimmed": """
            struct foo {
                int a;
                int c;
            };
        """,
    },

    "balanced_non_greddy_private": {
        "source": """
            struct foo {
                int a;
                /* private: */
                int b;
                /* public: */
                int c;
                /* private: */
                int d;
                /* public: */
                int e;

            };
        """,
        "trimmed": """
            struct foo {
                int a;
                int c;
                int e;
            };
        """,
    },

    "balanced_inner_private": {
        "source": """
            struct foo {
                struct {
                    int a;
                    /* private: ignore below */
                    int b;
                /* public: but this should not be ignored */
                };
                int b;
            };
        """,
        "trimmed": """
            struct foo {
                struct {
                    int a;
                };
                int b;
            };
        """,
    },

    #
    # Test what happens if there's no public after private place
    #

    "unbalanced_private": {
        "source": """
            struct foo {
                int a;
                /* private: */
                int b;
                int c;
            };
        """,
        "trimmed": """
            struct foo {
                int a;
            };
        """,
    },

    "unbalanced_inner_private": {
        "source": """
            struct foo {
                struct {
                    int a;
                    /* private: ignore below */
                    int b;
                /* but this should not be ignored */
                };
                int b;
            };
        """,
        "trimmed": """
            struct foo {
                struct {
                    int a;
                };
                int b;
            };
        """,
    },

    "unbalanced_struct_group_tagged_with_private": {
        "source": """
            struct page_pool_params {
                struct_group_tagged(page_pool_params_fast, fast,
                        unsigned int    order;
                        unsigned int    pool_size;
                        int             nid;
                        struct device   *dev;
                        struct napi_struct *napi;
                        enum dma_data_direction dma_dir;
                        unsigned int    max_len;
                        unsigned int    offset;
                };
                struct_group_tagged(page_pool_params_slow, slow,
                        struct net_device *netdev;
                        unsigned int queue_idx;
                        unsigned int    flags;
                        /* private: used by test code only */
                        void (*init_callback)(netmem_ref netmem, void *arg);
                        void *init_arg;
                };
            };
        """,
        "trimmed": """
            struct page_pool_params {
                struct_group_tagged(page_pool_params_fast, fast,
                        unsigned int    order;
                        unsigned int    pool_size;
                        int             nid;
                        struct device   *dev;
                        struct napi_struct *napi;
                        enum dma_data_direction dma_dir;
                        unsigned int    max_len;
                        unsigned int    offset;
                };
                struct_group_tagged(page_pool_params_slow, slow,
                        struct net_device *netdev;
                        unsigned int queue_idx;
                        unsigned int    flags;
                };
            };
        """,
    },

    "unbalanced_two_struct_group_tagged_first_with_private": {
        "source": """
            struct page_pool_params {
                struct_group_tagged(page_pool_params_slow, slow,
                        struct net_device *netdev;
                        unsigned int queue_idx;
                        unsigned int    flags;
                        /* private: used by test code only */
                        void (*init_callback)(netmem_ref netmem, void *arg);
                        void *init_arg;
                };
                struct_group_tagged(page_pool_params_fast, fast,
                        unsigned int    order;
                        unsigned int    pool_size;
                        int             nid;
                        struct device   *dev;
                        struct napi_struct *napi;
                        enum dma_data_direction dma_dir;
                        unsigned int    max_len;
                        unsigned int    offset;
                };
            };
        """,
        "trimmed": """
            struct page_pool_params {
                struct_group_tagged(page_pool_params_slow, slow,
                        struct net_device *netdev;
                        unsigned int queue_idx;
                        unsigned int    flags;
                };
                struct_group_tagged(page_pool_params_fast, fast,
                        unsigned int    order;
                        unsigned int    pool_size;
                        int             nid;
                        struct device   *dev;
                        struct napi_struct *napi;
                        enum dma_data_direction dma_dir;
                        unsigned int    max_len;
                        unsigned int    offset;
                };
            };
        """,
    },
    "unbalanced_without_end_of_line": {
        "source": """ \
            struct page_pool_params { \
                struct_group_tagged(page_pool_params_slow, slow, \
                        struct net_device *netdev; \
                        unsigned int queue_idx; \
                        unsigned int    flags;
                        /* private: used by test code only */
                        void (*init_callback)(netmem_ref netmem, void *arg); \
                        void *init_arg; \
                }; \
                struct_group_tagged(page_pool_params_fast, fast, \
                        unsigned int    order; \
                        unsigned int    pool_size; \
                        int             nid; \
                        struct device   *dev; \
                        struct napi_struct *napi; \
                        enum dma_data_direction dma_dir; \
                        unsigned int    max_len; \
                        unsigned int    offset; \
                }; \
            };
        """,
        "trimmed": """
            struct page_pool_params {
                struct_group_tagged(page_pool_params_slow, slow,
                        struct net_device *netdev;
                        unsigned int queue_idx;
                        unsigned int    flags;
                };
                struct_group_tagged(page_pool_params_fast, fast,
                        unsigned int    order;
                        unsigned int    pool_size;
                        int             nid;
                        struct device   *dev;
                        struct napi_struct *napi;
                        enum dma_data_direction dma_dir;
                        unsigned int    max_len;
                        unsigned int    offset;
                };
            };
        """,
    },
}


class TestPublicPrivate(unittest.TestCase):
    """
    Main test class. Populated dynamically at runtime.
    """

    def setUp(self):
        self.maxDiff = None

    def add_test(cls, name, source, trimmed):
        """
        Dynamically add a test to the class
        """
        def test(cls):
            result = trim_private_members(source)

            result = re.sub(r"\s++", " ", result).strip()
            expected = re.sub(r"\s++", " ", trimmed).strip()

            msg = f"failed when parsing this source:\n" + source

            cls.assertEqual(result, expected, msg=msg)

        test.__name__ = f'test {name}'

        setattr(TestPublicPrivate, test.__name__, test)


#
# Populate TestPublicPrivate class
#
test_class = TestPublicPrivate()
for name, test in TESTS_PRIVATE.items():
    test_class.add_test(name, test["source"], test["trimmed"])


#
# main
#
if __name__ == "__main__":
    run_unittest(__file__)
