//===--- UnicodeNameMappingGenerator.cpp - Unicode name data generator ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is used to generate lib/Support/UnicodeNameToCodepointGenerated.cpp
// using UnicodeData.txt and NameAliases.txt available at
// https://unicode.org/Public/15.1.0/ucd/
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static const llvm::StringRef Letters =
    " _-ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// Collect names UnicodeData.txt and AliasNames.txt
// There may be multiple names per code points.
static std::unordered_multimap<char32_t, std::string>
loadDataFiles(const std::string &NamesFile, const std::string &AliasesFile) {
  std::unordered_multimap<char32_t, std::string> CollectedCharacters;
  auto FromFile = [&](const std::string &File, bool IsAliasFile = false) {
    std::ifstream InputFile(File);
    for (std::string Line; getline(InputFile, Line);) {
      if (Line.empty() || !isxdigit(Line[0]))
        continue;
      auto FirstSemiPos = Line.find(';');
      if (FirstSemiPos == std::string::npos)
        continue;
      auto SecondSemiPos = Line.find(';', FirstSemiPos + 1);
      if (SecondSemiPos == std::string::npos)
        continue;
      unsigned long long CodePoint;
      if (llvm::getAsUnsignedInteger(
              llvm::StringRef(Line.c_str(), FirstSemiPos), 16, CodePoint)) {
        continue;
      }

      std::string Name =
          Line.substr(FirstSemiPos + 1, SecondSemiPos - FirstSemiPos - 1);

      if (!Name.empty() && Name[0] == '<') {
        // Ignore ranges of characters, as their name is either absent or
        // generated.
        continue;
      }

      // Some aliases are ignored for compatibility with C++
      if (IsAliasFile) {
        std::string Kind = Line.substr(SecondSemiPos + 1);
        if (Kind != "control" && Kind != "correction" && Kind != "alternate")
          continue;
      }

      auto InsertUnique = [&](char32_t CP, std::string Name) {
        auto It = CollectedCharacters.find(CP);
        while (It != std::end(CollectedCharacters) && It->first == CP) {
          if (It->second == Name)
            return;
          ++It;
        }
        CollectedCharacters.insert({CP, std::move(Name)});
      };
      InsertUnique(CodePoint, std::move(Name));
    }
  };

  FromFile(NamesFile);
  FromFile(AliasesFile, true);
  return CollectedCharacters;
}

class Trie {
  struct Node;

public:
  // When inserting named codepoint
  // We create a node per character in the name.
  // SPARKLE becomes S <- P <- A <- R <- K <- L <- E
  // Once all  characters are inserted, the tree is compacted
  void insert(llvm::StringRef Name, char32_t Codepoint) {
    Node *N = Root.get();
    bool IsBeforeMedial = false;
    for (auto ChIt = Name.begin(); ChIt != Name.end();
         ChIt += (IsBeforeMedial ? 3 : 1)) {
      char Ch = *ChIt;
      assert(Letters.contains(Ch) && "Unexpected symbol in Unicode name");

      std::string Label(1, Ch);

      // We need to ensure a node never ends or starts by
      // a medial hyphen as this would break the
      // loose matching algorithm.
      IsBeforeMedial = llvm::isAlnum(Ch) && ChIt + 1 != Name.end() &&
                       *(ChIt + 1) == '-' && ChIt + 2 != Name.end() &&
                       llvm::isAlnum(*(ChIt + 2));
      if (IsBeforeMedial)
        Label.assign(ChIt, ChIt + 3);

      auto It = llvm::find_if(N->Children,
                              [&](const auto &C) { return C->Name == Label; });
      if (It == N->Children.end()) {
        It = N->Children.insert(It, std::make_unique<Node>(Label, N));
      }
      N = It->get();
    }
    N->Value = Codepoint;
  }

  void compact() { compact(Root.get()); }

  // This creates 2 arrays of bytes from the tree:
  // A serialized dictionary of node labels,
  // And the nodes themselves.
  // The name of each label is found by indexing into the dictionary.
  // The longest names are inserted first into the dictionary,
  // in the hope it will contain shorter labels as substring,
  // thereby reducing duplication.
  // We could theorically be more clever by trying to minimizing the size
  // of the dictionary.
  std::pair<std::string, std::vector<uint8_t>> serialize() {
    std::set<std::string> Names = this->getNameFragments();
    std::vector<std::string> Sorted(Names.begin(), Names.end());
    llvm::sort(Sorted, [](const auto &a, const auto &b) {
      return a.size() > b.size();
    });
    std::string Dict(Letters.begin(), Letters.end());
    Dict.reserve(50000);
    for (const std::string &Name : Sorted) {
      if (Name.size() <= 1)
        continue;
      if (Dict.find(Name) != std::string::npos)
        continue;
      Dict += Name;
    }

    if (Dict.size() >= std::numeric_limits<uint16_t>::max()) {
      fprintf(stderr, "Dictionary too big  to be serialized");
      exit(1);
    }

    auto Bytes = dumpIndex(Dict);
    return {Dict, Bytes};
  }

  std::set<std::string> getNameFragments() {
    std::set<std::string> Keys;
    collectKeys(Root.get(), Keys);
    return Keys;
  }

  // Maps a valid char in an Unicode character name
  // To a 6 bits index.
  static uint8_t letter(char C) {
    auto Pos = Letters.find(C);
    assert(Pos != std::string::npos &&
           "Invalid letter in Unicode character name");
    return Pos;
  }

  // clang-format off
  // +================+============+======================+=============+========+===+==============+===============+
  // | 0          | 1             | 2-7 (6)              | 8-23        | 24-44  |    | 46           | 47            |
  // +================+============+======================+=============+========+===+==============+===============+
  // | Has Value |  Has Long Name | Letter OR Name Size  | Dict Index  | Value  |    | Has Sibling  | Has Children  |
  // +----------------+------------+----------------------+-------------+--------+---+--------------+---------------+
  // clang-format on

  std::vector<uint8_t> dumpIndex(const std::string &Dict) {
    struct ChildrenOffset {
      Node *FirstChild;
      std::size_t Offset;
      bool HasValue;
    };

    // Keep track of the start of each node
    // position in the serialized data.
    std::unordered_map<Node *, int32_t> Offsets;

    // Keep track of where to write the index
    // of the first children
    std::vector<ChildrenOffset> ChildrenOffsets;
    std::unordered_map<Node *, bool> SiblingTracker;
    std::deque<Node *> AllNodes;
    std::vector<uint8_t> Bytes;
    Bytes.reserve(250'000);
    // This leading byte is used by the reading code to detect the root node.
    Bytes.push_back(0);

    auto CollectChildren = [&SiblingTracker, &AllNodes](const auto &Children) {
      for (std::size_t Index = 0; Index < Children.size(); Index++) {
        const std::unique_ptr<Node> &Child = Children[Index];
        AllNodes.push_back(Child.get());
        if (Index != Children.size() - 1)
          SiblingTracker[Child.get()] = true;
      }
    };
    CollectChildren(Root->Children);

    while (!AllNodes.empty()) {
      const std::size_t Offset = Bytes.size();
      Node *const N = AllNodes.front();
      AllNodes.pop_front();

      assert(!N->Name.empty());
      Offsets[N] = Offset;

      uint8_t FirstByte = (!!N->Value) ? 0x80 : 0;
      // Single letter node are indexed in 6 bits
      if (N->Name.size() == 1) {
        FirstByte |= letter(N->Name[0]);
        Bytes.push_back(FirstByte);
      } else {
        // Otherwise we use a 16 bits index
        FirstByte = FirstByte | uint8_t(N->Name.size()) | 0x40;
        Bytes.push_back(FirstByte);
        auto PosInDict = Dict.find(N->Name);
        assert(PosInDict != std::string::npos);
        uint8_t Low = PosInDict;
        uint8_t High = ((PosInDict >> 8) & 0xFF);
        Bytes.push_back(High);
        Bytes.push_back(Low);
      }

      const bool HasSibling = SiblingTracker.count(N) != 0;
      const bool HasChildren = N->Children.size() != 0;

      if (!!N->Value) {
        uint32_t Value = (*(N->Value) << 3);
        uint8_t H = ((Value >> 16) & 0xFF);
        uint8_t M = ((Value >> 8) & 0xFF);
        uint8_t L = (Value & 0xFF) | uint8_t(HasSibling ? 0x01 : 0) |
                    uint8_t(HasChildren ? 0x02 : 0);

        Bytes.push_back(H);
        Bytes.push_back(M);
        Bytes.push_back(L);

        if (HasChildren) {
          ChildrenOffsets.push_back(
              ChildrenOffset{N->Children[0].get(), Bytes.size(), true});
          // index of the first children
          Bytes.push_back(0x00);
          Bytes.push_back(0x00);
          Bytes.push_back(0x00);
        }
      } else {
        // When there is no value (that's most intermediate nodes)
        // Dispense of the 3 values bytes, and only store
        // 1 byte to track whether the node has sibling and children
        // + 2 bytes for the index of the first children if necessary.
        // That index also uses bytes 0-6 of the previous byte.
        uint8_t Byte =
            uint8_t(HasSibling ? 0x80 : 0) | uint8_t(HasChildren ? 0x40 : 0);
        Bytes.push_back(Byte);
        if (HasChildren) {
          ChildrenOffsets.emplace_back(
              ChildrenOffset{N->Children[0].get(), Bytes.size() - 1, false});
          Bytes.push_back(0x00);
          Bytes.push_back(0x00);
        }
      }
      CollectChildren(N->Children);
    }

    // Once all the nodes are in the inndex
    // Fill the bytes we left to indicate the position
    // of the children
    for (const ChildrenOffset &Parent : ChildrenOffsets) {
      const auto It = Offsets.find(Parent.FirstChild);
      assert(It != Offsets.end());
      std::size_t Pos = It->second;
      if (Parent.HasValue) {
        Bytes[Parent.Offset] = ((Pos >> 16) & 0xFF);
      } else {
        Bytes[Parent.Offset] =
            Bytes[Parent.Offset] | uint8_t((Pos >> 16) & 0xFF);
      }
      Bytes[Parent.Offset + 1] = ((Pos >> 8) & 0xFF);
      Bytes[Parent.Offset + 2] = Pos & 0xFF;
    }

    // Add some padding so that the deserialization code
    // doesn't try to read past the enf of the array.
    Bytes.push_back(0);
    Bytes.push_back(0);
    Bytes.push_back(0);
    Bytes.push_back(0);
    Bytes.push_back(0);
    Bytes.push_back(0);

    return Bytes;
  }

private:
  void collectKeys(Node *N, std::set<std::string> &Keys) {
    Keys.insert(N->Name);
    for (const std::unique_ptr<Node> &Child : N->Children) {
      collectKeys(Child.get(), Keys);
    }
  }

  // Merge sequences of 1-character nodes
  // This greatly reduce the total number of nodes,
  // and therefore the size of the index.
  // When the tree gets serialized, we only have 5 bytes to store the
  // size of a name. Overlong names (>32 characters) are therefore
  // kep into separate nodes
  void compact(Node *N) {
    for (auto &&Child : N->Children) {
      compact(Child.get());
    }
    if (N->Parent && N->Parent->Children.size() == 1 && !N->Parent->Value &&
        (N->Parent->Name.size() + N->Name.size() <= 32)) {
      N->Parent->Value = N->Value;
      N->Parent->Name += N->Name;
      N->Parent->Children = std::move(N->Children);
      for (std::unique_ptr<Node> &c : N->Parent->Children) {
        c->Parent = N->Parent;
      }
    }
  }
  struct Node {
    Node(std::string Name, Node *Parent = nullptr)
        : Name(Name), Parent(Parent) {}

    std::vector<std::unique_ptr<Node>> Children;
    std::string Name;
    Node *Parent = nullptr;
    std::optional<char32_t> Value;
  };

  std::unique_ptr<Node> Root = std::make_unique<Node>("");
};

extern const char *UnicodeLicense;

int main(int argc, char **argv) {
  printf("Unicode name -> codepoint mapping generator\n"
         "Usage: %s UnicodeData.txt NameAliases.txt output\n\n",
         argv[0]);
  printf("NameAliases.txt can be found at "
         "https://unicode.org/Public/15.1.0/ucd/NameAliases.txt\n"
         "UnicodeData.txt can be found at "
         "https://unicode.org/Public/15.1.0/ucd/UnicodeData.txt\n\n");

  if (argc != 4)
    return EXIT_FAILURE;

  FILE *Out = fopen(argv[3], "w");
  if (!Out) {
    printf("Error creating output file.\n");
    return EXIT_FAILURE;
  }

  Trie T;
  uint32_t NameCount = 0;
  std::size_t LongestName = 0;
  auto Entries = loadDataFiles(argv[1], argv[2]);
  for (const std::pair<const char32_t, std::string> &Entry : Entries) {
    char32_t Codepoint = Entry.first;
    const std::string &Name = Entry.second;
    // Ignore names which are not valid.
    if (Name.empty() ||
        !llvm::all_of(Name, [](char C) { return Letters.contains(C); })) {
      continue;
    }
    printf("%06x: %s\n", static_cast<unsigned int>(Codepoint), Name.c_str());
    T.insert(Name, Codepoint);
    LongestName =
        std::max(LongestName, std::size_t(llvm::count_if(Name, llvm::isAlnum)));
    NameCount++;
  }
  T.compact();

  std::pair<std::string, std::vector<uint8_t>> Data = T.serialize();
  const std::string &Dict = Data.first;
  const std::vector<uint8_t> &Tree = Data.second;

  fprintf(Out, R"(
//===------------- Support/UnicodeNameToCodepointGenerated.cpp ------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements mapping the name of a unicode code point to its value.
//
// This file was generated using %s.
// Do not edit manually.
//
//===----------------------------------------------------------------------===//
%s



#include "llvm/Support/Compiler.h"
#include <cstddef>
#include <cstdint>
)",
          argv[0], UnicodeLicense);

  fprintf(Out,
          "namespace llvm { namespace sys { namespace unicode { \n"
          "extern const char *UnicodeNameToCodepointDict;\n"
          "extern const uint8_t *UnicodeNameToCodepointIndex;\n"
          "extern const std::size_t UnicodeNameToCodepointIndexSize;\n"
          "extern const std::size_t UnicodeNameToCodepointLargestNameSize;\n");

  fprintf(Out, "const char* UnicodeNameToCodepointDict = \"%s\";\n",
          Dict.c_str());

  fprintf(Out, "uint8_t UnicodeNameToCodepointIndex_[%zu] = {\n",
          Tree.size() + 1);

  for (auto Byte : Tree) {
    fprintf(Out, "0x%02x,", Byte);
  }

  fprintf(Out, "0};");
  fprintf(Out, "const uint8_t* UnicodeNameToCodepointIndex = "
               "UnicodeNameToCodepointIndex_; \n");
  fprintf(Out, "const std::size_t UnicodeNameToCodepointIndexSize = %zu;\n",
          Tree.size() + 1);
  fprintf(Out,
          "const std::size_t UnicodeNameToCodepointLargestNameSize = %zu;\n",
          LongestName);
  fprintf(Out, "\n}}}\n");
  fclose(Out);
  printf("Generated %s: %u Files.\nIndex: %f kB, Dictionary: %f kB.\nDone\n\n",
         argv[3], NameCount, Tree.size() / 1024.0, Dict.size() / 1024.0);
}

const char *UnicodeLicense = R"(
/*
UNICODE, INC. LICENSE AGREEMENT - DATA FILES AND SOFTWARE

See Terms of Use <https://www.unicode.org/copyright.html>
for definitions of Unicode Inc.’s Data Files and Software.

NOTICE TO USER: Carefully read the following legal agreement.
BY DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING UNICODE INC.'S
DATA FILES ("DATA FILES"), AND/OR SOFTWARE ("SOFTWARE"),
YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
TERMS AND CONDITIONS OF THIS AGREEMENT.
IF YOU DO NOT AGREE, DO NOT DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE
THE DATA FILES OR SOFTWARE.

COPYRIGHT AND PERMISSION NOTICE

Copyright © 1991-2022 Unicode, Inc. All rights reserved.
Distributed under the Terms of Use in https://www.unicode.org/copyright.html.

Permission is hereby granted, free of charge, to any person obtaining
a copy of the Unicode data files and any associated documentation
(the "Data Files") or Unicode software and any associated documentation
(the "Software") to deal in the Data Files or Software
without restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, and/or sell copies of
the Data Files or Software, and to permit persons to whom the Data Files
or Software are furnished to do so, provided that either
(a) this copyright and permission notice appear with all copies
of the Data Files or Software, or
(b) this copyright and permission notice appear in associated
Documentation.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT OF THIRD PARTY RIGHTS.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS
NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL
DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THE DATA FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder
shall not be used in advertising or otherwise to promote the sale,
use or other dealings in these Data Files or Software without prior
written authorization of the copyright holder.
*/
)";
