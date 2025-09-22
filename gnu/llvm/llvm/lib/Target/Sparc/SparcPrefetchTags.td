//===- SparcPrefetchTags.td --------------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the symbolic operands permitted for various kinds of
// SPARCv9 prefetches.
//
//===----------------------------------------------------------------------===//

class PrefetchTag<string name, bits<8> op> {
  string Name = name;
  bits<8> Encoding = op;
}

def PrefetchTagsList : GenericTable {
  let FilterClass = "PrefetchTag";
  let Fields = ["Name", "Encoding"];

  let PrimaryKey = [ "Encoding" ];
  let PrimaryKeyName = "lookupPrefetchTagByEncoding";
}

def lookupPrefetchTagByName : SearchIndex {
  let Table = PrefetchTagsList;
  let Key = [ "Name" ];
}

def : PrefetchTag<"n_reads", 0x0>;
def : PrefetchTag<"one_read", 0x1>;
def : PrefetchTag<"n_writes", 0x2>;
def : PrefetchTag<"one_write", 0x3>;
def : PrefetchTag<"page", 0x4>;
def : PrefetchTag<"unified", 0x11>;
def : PrefetchTag<"n_reads_strong", 0x14>;
def : PrefetchTag<"one_read_strong", 0x15>;
def : PrefetchTag<"n_writes_strong", 0x16>;
def : PrefetchTag<"one_write_strong", 0x17>;
