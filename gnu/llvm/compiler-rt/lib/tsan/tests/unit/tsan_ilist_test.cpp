//===-- tsan_ilist_test.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_ilist.h"

#include "gtest/gtest.h"

namespace __tsan {

struct Node {
  INode node1;
  INode node2;
};

struct Parent : Node {};

TEST(IList, Empty) {
  IList<Node, &Node::node1> list;
  Node node;

  EXPECT_TRUE(list.Empty());
  EXPECT_EQ(list.Size(), (size_t)0);
  EXPECT_EQ(list.Back(), nullptr);
  EXPECT_EQ(list.Front(), nullptr);
  EXPECT_EQ(list.PopBack(), nullptr);
  EXPECT_EQ(list.PopFront(), nullptr);
  EXPECT_FALSE(list.Queued(&node));
}

TEST(IList, OneNode) {
  IList<Node, &Node::node1> list;
  Node node;

  list.PushBack(&node);
  EXPECT_FALSE(list.Empty());
  EXPECT_EQ(list.Size(), (size_t)1);
  EXPECT_EQ(list.Back(), &node);
  EXPECT_EQ(list.Front(), &node);
  EXPECT_TRUE(list.Queued(&node));
  EXPECT_EQ(list.Prev(&node), nullptr);
  EXPECT_EQ(list.Next(&node), nullptr);

  EXPECT_EQ(list.PopFront(), &node);
  EXPECT_TRUE(list.Empty());
  EXPECT_EQ(list.Size(), (size_t)0);
  EXPECT_FALSE(list.Queued(&node));
}

TEST(IList, MultipleNodes) {
  IList<Node, &Node::node1> list;
  Node nodes[3];

  list.PushBack(&nodes[1]);
  list.PushBack(&nodes[0]);
  list.PushFront(&nodes[2]);

  EXPECT_EQ(list.Size(), (size_t)3);
  EXPECT_EQ(list.Back(), &nodes[0]);
  EXPECT_EQ(list.Front(), &nodes[2]);

  EXPECT_EQ(list.Next(&nodes[0]), nullptr);
  EXPECT_EQ(list.Prev(&nodes[0]), &nodes[1]);

  EXPECT_EQ(list.Next(&nodes[1]), &nodes[0]);
  EXPECT_EQ(list.Prev(&nodes[1]), &nodes[2]);

  EXPECT_EQ(list.Next(&nodes[2]), &nodes[1]);
  EXPECT_EQ(list.Prev(&nodes[2]), nullptr);

  EXPECT_EQ(list.PopBack(), &nodes[0]);
  EXPECT_EQ(list.PopFront(), &nodes[2]);
  EXPECT_EQ(list.PopFront(), &nodes[1]);
  EXPECT_TRUE(list.Empty());
}

TEST(IList, TwoLists) {
  IList<Node, &Node::node1> list1;
  IList<Node, &Node::node2, Parent> list2;
  Parent nodes[3];

  list1.PushBack(&nodes[2]);
  list1.PushBack(&nodes[1]);
  list1.PushBack(&nodes[0]);

  list2.PushFront(&nodes[1]);

  EXPECT_EQ(list1.Size(), (size_t)3);
  EXPECT_TRUE(list1.Queued(&nodes[0]));
  EXPECT_TRUE(list1.Queued(&nodes[1]));
  EXPECT_TRUE(list1.Queued(&nodes[2]));

  EXPECT_EQ(list2.Size(), (size_t)1);
  EXPECT_FALSE(list2.Queued(&nodes[0]));
  EXPECT_TRUE(list2.Queued(&nodes[1]));
  EXPECT_FALSE(list2.Queued(&nodes[2]));

  EXPECT_EQ(list1.Next(&nodes[1]), &nodes[0]);
  EXPECT_EQ(list1.Prev(&nodes[1]), &nodes[2]);

  EXPECT_EQ(list2.Next(&nodes[1]), nullptr);
  EXPECT_EQ(list2.Prev(&nodes[1]), nullptr);

  list1.Remove(&nodes[1]);
  EXPECT_EQ(list1.Size(), (size_t)2);
  EXPECT_FALSE(list1.Queued(&nodes[1]));
  EXPECT_EQ(list2.Size(), (size_t)1);
  EXPECT_TRUE(list2.Queued(&nodes[1]));

  EXPECT_EQ(list1.PopBack(), &nodes[0]);
  EXPECT_EQ(list1.PopBack(), &nodes[2]);
  EXPECT_EQ(list1.Size(), (size_t)0);

  EXPECT_EQ(list2.PopBack(), &nodes[1]);
  EXPECT_EQ(list2.Size(), (size_t)0);
}

}  // namespace __tsan
