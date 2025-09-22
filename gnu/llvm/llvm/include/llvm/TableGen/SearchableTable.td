//===- SearchableTable.td ----------------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the key top-level classes needed to produce a reasonably
// generic table that can be binary-searched. Three types of objects can be
// defined using the classes in this file:
//
// 1. (Generic) Enums. By instantiating the GenericEnum class once, an enum with
// the name of the def is generated. It is guarded by the preprocessor define
// GET_name_DECL, where name is the name of the def.
//
// 2. (Generic) Tables and search indices. By instantiating the GenericTable
// class once, a table with the name of the instantiating def is generated and
// guarded by the GET_name_IMPL preprocessor guard.
//
// Both a primary key and additional secondary keys / search indices can also
// be defined, which result in the generation of lookup functions. Their
// declarations and definitions are all guarded by GET_name_DECL and
// GET_name_IMPL, respectively, where name is the name of the underlying table.
//
// See AArch64SystemOperands.td and its generated header for example uses.
//
//===----------------------------------------------------------------------===//

// Define a record derived from this class to generate a generic enum.
//
// The name of the record is used as the type name of the C++ enum.
class GenericEnum {
  // Name of a TableGen class. The enum will have one entry for each record
  // that derives from that class.
  string FilterClass;

  // (Optional) Name of a field that is present in all collected records and
  // contains the name of enum entries.
  //
  // If NameField is not set, the record names will be used instead.
  string NameField;

  // (Optional) Name of a field that is present in all collected records and
  // contains the numerical value of enum entries.
  //
  // If ValueField is not set, enum values will be assigned automatically,
  // starting at 0, according to a lexicographical sort of the entry names.
  string ValueField;
}

// Define a record derived from this class to generate a generic table. This
// table can have a searchable primary key, and it can also be referenced by
// external search indices.
//
// The name of the record is used as the name of the global primary array of
// entries of the table in C++.
class GenericTable {
  // Name of a class. The table will have one entry for each record that
  // derives from that class.
  string FilterClass;

  // A field of FilterClass to filter out entries. This is an optional field
  // of ``FilterClass`` which should be `bit` type. If specified, only those
  // records with this field being true will have corresponding entries in the
  // table.
  string FilterClassField = ?;

  // Name of the C++ struct/class type that holds table entries. The
  // declaration of this type is not generated automatically.
  string CppTypeName = FilterClass;

  // List of the names of fields of collected records that contain the data for
  // table entries, in the order that is used for initialization in C++.
  //
  // TableGen needs to know the type of the fields so that it can format
  // the initializers correctly. It can infer the type of bit, bits, string,
  // Intrinsic, and Instruction values. 
  //
  // For each field of the table named xxx, TableGen will look for a field
  // named TypeOf_xxx and use that as a more detailed description of the
  // type of the field. This is required for fields whose type
  // cannot be deduced automatically, such as enum fields. For example:
  //
  //   def MyEnum : GenericEnum {
  //     let FilterClass = "MyEnum";
  //     ...
  //   }
  //
  //   class MyTableEntry {
  //     MyEnum V;
  //     ...
  //   }
  //
  //   def MyTable : GenericTable {
  //     let FilterClass = "MyTableEntry";
  //     let Fields = ["V", ...];
  //     string TypeOf_V = "MyEnum";
  //   }
  //
  // If a string field was initialized with a code literal, TableGen will
  // emit the code verbatim. However, if a string field was initialized
  // in some other way, but should be interpreted as code, then a TypeOf_xxx
  // field is necessary, with a value of "code":
  //
  //     string TypeOf_Predicate = "code";
  list<string> Fields;

  // (Optional) List of fields that make up the primary key.
  list<string> PrimaryKey;

  // (Optional) Name of the primary key search function.
  string PrimaryKeyName;

  // See SearchIndex.EarlyOut
  bit PrimaryKeyEarlyOut = false;

  // If true, will generate a different function signature which will return an
  // iterator range of pointers giving the starting and end value of the range.
  // e.g. lookupSysRegByEncoding returns multiple CSRs for same encoding.
  bit PrimaryKeyReturnRange = false;
}

// Define a record derived from this class to generate an additional search
// index for a generic table that has been defined earlier.
//
// The name of the record will be used as the name of the C++ lookup function.
class SearchIndex {
  // Table that this search index refers to.
  GenericTable Table;

  // List of fields that make up the key.
  list<string> Key;

  // If true, the lookup function will check the first field of the key against
  // the minimum and maximum values in the index before entering the binary
  // search. This is convenient for tables that add extended data for a subset
  // of a larger enum-based space, e.g. extended data about a subset of
  // instructions.
  //
  // Can only be used when the first field is an integral (non-string) type.
  bit EarlyOut = false;
}

// Legacy table type with integrated enum.
class SearchableTable {
  list<string> SearchableFields;
  string EnumNameField = "Name";
  string EnumValueField;
}
