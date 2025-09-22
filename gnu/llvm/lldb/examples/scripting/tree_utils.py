"""
# ===-- tree_utils.py ---------------------------------------*- Python -*-===//
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===---------------------------------------------------------------------===//

tree_utils.py  - A set of functions for examining binary
search trees, based on the example search tree defined in
dictionary.c.  These functions contain calls to LLDB API
functions, and assume that the LLDB Python module has been
imported.

For a thorough explanation of how the DFS function works, and
for more information about dictionary.c go to
http://lldb.llvm.org/scripting.html
"""


def DFS(root, word, cur_path):
    """
    Recursively traverse a binary search tree containing
    words sorted alphabetically, searching for a particular
    word in the tree.  Also maintains a string representing
    the path from the root of the tree to the current node.
    If the word is found in the tree, return the path string.
    Otherwise return an empty string.

    This function assumes the binary search tree is
    the one defined in dictionary.c  It uses LLDB API
    functions to examine and traverse the tree nodes.
    """

    # Get pointer field values out of node 'root'

    root_word_ptr = root.GetChildMemberWithName("word")
    left_child_ptr = root.GetChildMemberWithName("left")
    right_child_ptr = root.GetChildMemberWithName("right")

    # Get the word out of the word pointer and strip off
    # surrounding quotes (added by call to GetSummary).

    root_word = root_word_ptr.GetSummary()
    end = len(root_word) - 1
    if root_word[0] == '"' and root_word[end] == '"':
        root_word = root_word[1:end]
    end = len(root_word) - 1
    if root_word[0] == "'" and root_word[end] == "'":
        root_word = root_word[1:end]

    # Main depth first search

    if root_word == word:
        return cur_path
    elif word < root_word:
        # Check to see if left child is NULL

        if left_child_ptr.GetValue() is None:
            return ""
        else:
            cur_path = cur_path + "L"
            return DFS(left_child_ptr, word, cur_path)
    else:
        # Check to see if right child is NULL

        if right_child_ptr.GetValue() is None:
            return ""
        else:
            cur_path = cur_path + "R"
            return DFS(right_child_ptr, word, cur_path)


def tree_size(root):
    """
    Recursively traverse a binary search tree, counting
    the nodes in the tree.  Returns the final count.

    This function assumes the binary search tree is
    the one defined in dictionary.c  It uses LLDB API
    functions to examine and traverse the tree nodes.
    """
    if root.GetValue is None:
        return 0

    if int(root.GetValue(), 16) == 0:
        return 0

    left_size = tree_size(root.GetChildAtIndex(1))
    right_size = tree_size(root.GetChildAtIndex(2))

    total_size = left_size + right_size + 1
    return total_size


def print_tree(root):
    """
    Recursively traverse a binary search tree, printing out
    the words at the nodes in alphabetical order (the
    search order for the binary tree).

    This function assumes the binary search tree is
    the one defined in dictionary.c  It uses LLDB API
    functions to examine and traverse the tree nodes.
    """
    if (root.GetChildAtIndex(1).GetValue() is not None) and (
        int(root.GetChildAtIndex(1).GetValue(), 16) != 0
    ):
        print_tree(root.GetChildAtIndex(1))

    print(root.GetChildAtIndex(0).GetSummary())

    if (root.GetChildAtIndex(2).GetValue() is not None) and (
        int(root.GetChildAtIndex(2).GetValue(), 16) != 0
    ):
        print_tree(root.GetChildAtIndex(2))
