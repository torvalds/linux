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

from kdoc.c_lex import CToken, CTokenizer
from unittest_helper import run_unittest

#
# List of tests.
#
# The code will dynamically generate one test for each key on this dictionary.
#
def tokens_to_list(tokens):
    tuples = []

    for tok in tokens:
        if tok.kind == CToken.SPACE:
            continue

        tuples += [(tok.kind, tok.value, tok.level)]

    return tuples


def make_tokenizer_test(name, data):
    """
    Create a test named ``name`` using parameters given by ``data`` dict.
    """

    def test(self):
        """In-lined lambda-like function to run the test"""

        #
        # Check if logger is working
        #
        if "log_msg" in data:
            with self.assertLogs() as cm:
                tokenizer = CTokenizer(data["source"])

                msg_found = False
                for result in cm.output:
                    if data["log_msg"] in result:
                        msg_found = True

                self.assertTrue(msg_found, f"Missing log {data['log_msg']}")

            return

        #
        # Check if tokenizer is producing expected results
        #
        tokens = CTokenizer(data["source"]).tokens

        result = tokens_to_list(tokens)
        expected = tokens_to_list(data["expected"])

        self.assertEqual(result, expected, msg=f"{name}")

    return test

#: Tokenizer tests.
TESTS_TOKENIZER = {
    "__run__": make_tokenizer_test,

    "basic_tokens": {
        "source": """
            int a; // comment
            float b = 1.23;
        """,
        "expected": [
            CToken(CToken.NAME, "int"),
            CToken(CToken.NAME, "a"),
            CToken(CToken.ENDSTMT, ";"),
            CToken(CToken.COMMENT, "// comment"),
            CToken(CToken.NAME, "float"),
            CToken(CToken.NAME, "b"),
            CToken(CToken.OP, "="),
            CToken(CToken.NUMBER, "1.23"),
            CToken(CToken.ENDSTMT, ";"),
        ],
    },

    "depth_counters": {
        "source": """
            struct X {
                int arr[10];
                func(a[0], (b + c));
            }
        """,
        "expected": [
            CToken(CToken.STRUCT, "struct"),
            CToken(CToken.NAME, "X"),
            CToken(CToken.BEGIN, "{", brace_level=1),

            CToken(CToken.NAME, "int", brace_level=1),
            CToken(CToken.NAME, "arr", brace_level=1),
            CToken(CToken.BEGIN, "[", brace_level=1, bracket_level=1),
            CToken(CToken.NUMBER, "10", brace_level=1, bracket_level=1),
            CToken(CToken.END, "]", brace_level=1),
            CToken(CToken.ENDSTMT, ";", brace_level=1),
            CToken(CToken.NAME, "func", brace_level=1),
            CToken(CToken.BEGIN, "(", brace_level=1, paren_level=1),
            CToken(CToken.NAME, "a", brace_level=1, paren_level=1),
            CToken(CToken.BEGIN, "[", brace_level=1, paren_level=1, bracket_level=1),
            CToken(CToken.NUMBER, "0", brace_level=1, paren_level=1, bracket_level=1),
            CToken(CToken.END, "]", brace_level=1, paren_level=1),
            CToken(CToken.PUNC, ",", brace_level=1, paren_level=1),
            CToken(CToken.BEGIN, "(", brace_level=1, paren_level=2),
            CToken(CToken.NAME, "b", brace_level=1, paren_level=2),
            CToken(CToken.OP, "+", brace_level=1, paren_level=2),
            CToken(CToken.NAME, "c", brace_level=1, paren_level=2),
            CToken(CToken.END, ")", brace_level=1, paren_level=1),
            CToken(CToken.END, ")", brace_level=1),
            CToken(CToken.ENDSTMT, ";", brace_level=1),
            CToken(CToken.END, "}"),
        ],
    },

    "mismatch_error": {
        "source": "int a$ = 5;",          # $ is illegal
        "log_msg": "Unexpected token",
    },
}

def make_private_test(name, data):
    """
    Create a test named ``name`` using parameters given by ``data`` dict.
    """

    def test(self):
        """In-lined lambda-like function to run the test"""
        tokens = CTokenizer(data["source"])
        result = str(tokens)

        #
        # Avoid whitespace false positives
        #
        result = re.sub(r"\s++", " ", result).strip()
        expected = re.sub(r"\s++", " ", data["trimmed"]).strip()

        msg = f"failed when parsing this source:\n{data['source']}"
        self.assertEqual(result, expected, msg=msg)

    return test

#: Tests to check if CTokenizer is handling properly public/private comments.
TESTS_PRIVATE = {
    #
    # Simplest case: no private. Ensure that trimming won't affect struct
    #
    "__run__": make_private_test,
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

#: Dict containing all test groups fror CTokenizer
TESTS = {
    "TestPublicPrivate": TESTS_PRIVATE,
    "TestTokenizer": TESTS_TOKENIZER,
}

def setUp(self):
    self.maxDiff = None

def build_test_class(group_name, table):
    """
    Dynamically creates a class instance using type() as a generator
    for a new class derivated from unittest.TestCase.

    We're opting to do it inside a function to avoid the risk of
    changing the globals() dictionary.
    """

    class_dict = {
        "setUp": setUp
    }

    run = table["__run__"]

    for test_name, data in table.items():
        if test_name == "__run__":
            continue

        class_dict[f"test_{test_name}"] = run(test_name, data)

    cls = type(group_name, (unittest.TestCase,), class_dict)

    return cls.__name__, cls

#
# Create classes and add them to the global dictionary
#
for group, table in TESTS.items():
    t = build_test_class(group, table)
    globals()[t[0]] = t[1]

#
# main
#
if __name__ == "__main__":
    run_unittest(__file__)
