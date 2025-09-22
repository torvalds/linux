//===-- sanitizer_list_test.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_list.h"
#include "gtest/gtest.h"

namespace __sanitizer {

struct ListItem {
  ListItem *next;
};

typedef IntrusiveList<ListItem> List;

static List static_list;

static void SetList(List *l, ListItem *x = 0,
                    ListItem *y = 0, ListItem *z = 0) {
  l->clear();
  if (x) l->push_back(x);
  if (y) l->push_back(y);
  if (z) l->push_back(z);
}

static void CheckList(List *l, ListItem *i1, ListItem *i2 = 0, ListItem *i3 = 0,
                      ListItem *i4 = 0, ListItem *i5 = 0, ListItem *i6 = 0) {
  if (i1) {
    CHECK_EQ(l->front(), i1);
    l->pop_front();
  }
  if (i2) {
    CHECK_EQ(l->front(), i2);
    l->pop_front();
  }
  if (i3) {
    CHECK_EQ(l->front(), i3);
    l->pop_front();
  }
  if (i4) {
    CHECK_EQ(l->front(), i4);
    l->pop_front();
  }
  if (i5) {
    CHECK_EQ(l->front(), i5);
    l->pop_front();
  }
  if (i6) {
    CHECK_EQ(l->front(), i6);
    l->pop_front();
  }
  CHECK(l->empty());
}

TEST(SanitizerCommon, IntrusiveList) {
  ListItem items[6];
  CHECK_EQ(static_list.size(), 0);

  List l;
  l.clear();

  ListItem *x = &items[0];
  ListItem *y = &items[1];
  ListItem *z = &items[2];
  ListItem *a = &items[3];
  ListItem *b = &items[4];
  ListItem *c = &items[5];

  CHECK_EQ(l.size(), 0);
  l.push_back(x);
  CHECK_EQ(l.size(), 1);
  CHECK_EQ(l.back(), x);
  CHECK_EQ(l.front(), x);
  l.pop_front();
  CHECK(l.empty());
  l.CheckConsistency();

  l.push_front(x);
  CHECK_EQ(l.size(), 1);
  CHECK_EQ(l.back(), x);
  CHECK_EQ(l.front(), x);
  l.pop_front();
  CHECK(l.empty());
  l.CheckConsistency();

  l.push_front(x);
  l.push_front(y);
  l.push_front(z);
  CHECK_EQ(l.size(), 3);
  CHECK_EQ(l.front(), z);
  CHECK_EQ(l.back(), x);
  l.CheckConsistency();

  l.pop_front();
  CHECK_EQ(l.size(), 2);
  CHECK_EQ(l.front(), y);
  CHECK_EQ(l.back(), x);
  l.pop_front();
  l.pop_front();
  CHECK(l.empty());
  l.CheckConsistency();

  l.push_back(x);
  l.push_back(y);
  l.push_back(z);
  CHECK_EQ(l.size(), 3);
  CHECK_EQ(l.front(), x);
  CHECK_EQ(l.back(), z);
  l.CheckConsistency();

  l.pop_front();
  CHECK_EQ(l.size(), 2);
  CHECK_EQ(l.front(), y);
  CHECK_EQ(l.back(), z);
  l.pop_front();
  l.pop_front();
  CHECK(l.empty());
  l.CheckConsistency();

  l.push_back(x);
  l.push_back(y);
  l.push_back(z);
  l.extract(x, y);
  CHECK_EQ(l.size(), 2);
  CHECK_EQ(l.front(), x);
  CHECK_EQ(l.back(), z);
  l.CheckConsistency();
  l.extract(x, z);
  CHECK_EQ(l.size(), 1);
  CHECK_EQ(l.front(), x);
  CHECK_EQ(l.back(), x);
  l.CheckConsistency();
  l.pop_front();
  CHECK(l.empty());

  List l1, l2;
  l1.clear();
  l2.clear();

  l1.append_front(&l2);
  CHECK(l1.empty());
  CHECK(l2.empty());

  l1.append_back(&l2);
  CHECK(l1.empty());
  CHECK(l2.empty());

  SetList(&l1, x);
  CheckList(&l1, x);

  SetList(&l1, x, y, z);
  SetList(&l2, a, b, c);
  l1.append_back(&l2);
  CheckList(&l1, x, y, z, a, b, c);
  CHECK(l2.empty());

  SetList(&l1, x, y);
  SetList(&l2);
  l1.append_front(&l2);
  CheckList(&l1, x, y);
  CHECK(l2.empty());
}

TEST(SanitizerCommon, IntrusiveListAppendEmpty) {
  ListItem i;
  List l;
  l.clear();
  l.push_back(&i);
  List l2;
  l2.clear();
  l.append_back(&l2);
  CHECK_EQ(l.back(), &i);
  CHECK_EQ(l.front(), &i);
  CHECK_EQ(l.size(), 1);
  l.append_front(&l2);
  CHECK_EQ(l.back(), &i);
  CHECK_EQ(l.front(), &i);
  CHECK_EQ(l.size(), 1);
}

}  // namespace __sanitizer
