//===-- MsgPackDocument.cpp - MsgPack Document --------------------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file implements a class that exposes a simple in-memory representation
/// of a document of MsgPack objects, that can be read from MsgPack, written to
/// MsgPack, and inspected and modified in memory. This is intended to be a
/// lighter-weight (in terms of memory allocations) replacement for
/// MsgPackTypes.
///
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MsgPackDocument.h"
#include "llvm/BinaryFormat/MsgPackWriter.h"

using namespace llvm;
using namespace msgpack;

// Convert this DocNode into an empty array.
void DocNode::convertToArray() { *this = getDocument()->getArrayNode(); }

// Convert this DocNode into an empty map.
void DocNode::convertToMap() { *this = getDocument()->getMapNode(); }

/// Find the key in the MapDocNode.
DocNode::MapTy::iterator MapDocNode::find(StringRef S) {
  return find(getDocument()->getNode(S));
}

/// Member access for MapDocNode. The string data must remain valid for the
/// lifetime of the Document.
DocNode &MapDocNode::operator[](StringRef S) {
  return (*this)[getDocument()->getNode(S)];
}

/// Member access for MapDocNode.
DocNode &MapDocNode::operator[](DocNode Key) {
  assert(!Key.isEmpty());
  DocNode &N = (*Map)[Key];
  if (N.isEmpty()) {
    // Ensure a new element has its KindAndDoc initialized.
    N = getDocument()->getEmptyNode();
  }
  return N;
}

/// Member access for MapDocNode for integer key.
DocNode &MapDocNode::operator[](int Key) {
  return (*this)[getDocument()->getNode(Key)];
}
DocNode &MapDocNode::operator[](unsigned Key) {
  return (*this)[getDocument()->getNode(Key)];
}
DocNode &MapDocNode::operator[](int64_t Key) {
  return (*this)[getDocument()->getNode(Key)];
}
DocNode &MapDocNode::operator[](uint64_t Key) {
  return (*this)[getDocument()->getNode(Key)];
}

/// Array element access. This extends the array if necessary.
DocNode &ArrayDocNode::operator[](size_t Index) {
  if (size() <= Index) {
    // Ensure new elements have their KindAndDoc initialized.
    Array->resize(Index + 1, getDocument()->getEmptyNode());
  }
  return (*Array)[Index];
}

// Convenience assignment operators. This only works if the destination
// DocNode has an associated Document, i.e. it was not constructed using the
// default constructor. The string one does not copy, so the string must
// remain valid for the lifetime of the Document. Use fromString to avoid
// that restriction.
DocNode &DocNode::operator=(StringRef Val) {
  *this = getDocument()->getNode(Val);
  return *this;
}
DocNode &DocNode::operator=(MemoryBufferRef Val) {
  *this = getDocument()->getNode(Val);
  return *this;
}
DocNode &DocNode::operator=(bool Val) {
  *this = getDocument()->getNode(Val);
  return *this;
}
DocNode &DocNode::operator=(int Val) {
  *this = getDocument()->getNode(Val);
  return *this;
}
DocNode &DocNode::operator=(unsigned Val) {
  *this = getDocument()->getNode(Val);
  return *this;
}
DocNode &DocNode::operator=(int64_t Val) {
  *this = getDocument()->getNode(Val);
  return *this;
}
DocNode &DocNode::operator=(uint64_t Val) {
  *this = getDocument()->getNode(Val);
  return *this;
}

// A level in the document reading stack.
struct StackLevel {
  StackLevel(DocNode Node, size_t StartIndex, size_t Length,
             DocNode *MapEntry = nullptr)
      : Node(Node), Index(StartIndex), End(StartIndex + Length),
        MapEntry(MapEntry) {}
  DocNode Node;
  size_t Index;
  size_t End;
  // Points to map entry when we have just processed a map key.
  DocNode *MapEntry;
  DocNode MapKey;
};

// Read a document from a binary msgpack blob, merging into anything already in
// the Document.
// The blob data must remain valid for the lifetime of this Document (because a
// string object in the document contains a StringRef into the original blob).
// If Multi, then this sets root to an array and adds top-level objects to it.
// If !Multi, then it only reads a single top-level object, even if there are
// more, and sets root to that.
// Returns false if failed due to illegal format or merge error.

bool Document::readFromBlob(
    StringRef Blob, bool Multi,
    function_ref<int(DocNode *DestNode, DocNode SrcNode, DocNode MapKey)>
        Merger) {
  msgpack::Reader MPReader(Blob);
  SmallVector<StackLevel, 4> Stack;
  if (Multi) {
    // Create the array for multiple top-level objects.
    Root = getArrayNode();
    Stack.push_back(StackLevel(Root, 0, (size_t)-1));
  }
  do {
    // On to next element (or key if doing a map key next).
    // Read the value.
    Object Obj;
    Expected<bool> ReadObj = MPReader.read(Obj);
    if (!ReadObj) {
      // FIXME: Propagate the Error to the caller.
      consumeError(ReadObj.takeError());
      return false;
    }
    if (!ReadObj.get()) {
      if (Multi && Stack.size() == 1) {
        // OK to finish here as we've just done a top-level element with Multi
        break;
      }
      return false; // Finished too early
    }
    // Convert it into a DocNode.
    DocNode Node;
    switch (Obj.Kind) {
    case Type::Nil:
      Node = getNode();
      break;
    case Type::Int:
      Node = getNode(Obj.Int);
      break;
    case Type::UInt:
      Node = getNode(Obj.UInt);
      break;
    case Type::Boolean:
      Node = getNode(Obj.Bool);
      break;
    case Type::Float:
      Node = getNode(Obj.Float);
      break;
    case Type::String:
      Node = getNode(Obj.Raw);
      break;
    case Type::Binary:
      Node = getNode(MemoryBufferRef(Obj.Raw, ""));
      break;
    case Type::Map:
      Node = getMapNode();
      break;
    case Type::Array:
      Node = getArrayNode();
      break;
    default:
      return false; // Raw and Extension not supported
    }

    // Store it.
    DocNode *DestNode = nullptr;
    if (Stack.empty())
      DestNode = &Root;
    else if (Stack.back().Node.getKind() == Type::Array) {
      // Reading an array entry.
      auto &Array = Stack.back().Node.getArray();
      DestNode = &Array[Stack.back().Index++];
    } else {
      auto &Map = Stack.back().Node.getMap();
      if (!Stack.back().MapEntry) {
        // Reading a map key.
        Stack.back().MapKey = Node;
        Stack.back().MapEntry = &Map[Node];
        continue;
      }
      // Reading the value for the map key read in the last iteration.
      DestNode = Stack.back().MapEntry;
      Stack.back().MapEntry = nullptr;
      ++Stack.back().Index;
    }
    int MergeResult = 0;
    if (!DestNode->isEmpty()) {
      // In a merge, there is already a value at this position. Call the
      // callback to attempt to resolve the conflict. The resolution must result
      // in an array or map if Node is an array or map respectively.
      DocNode MapKey = !Stack.empty() && !Stack.back().MapKey.isEmpty()
                           ? Stack.back().MapKey
                           : getNode();
      MergeResult = Merger(DestNode, Node, MapKey);
      if (MergeResult < 0)
        return false; // Merge conflict resolution failed
      assert(!((Node.isMap() && !DestNode->isMap()) ||
               (Node.isArray() && !DestNode->isArray())));
    } else
      *DestNode = Node;

    // See if we're starting a new array or map.
    switch (DestNode->getKind()) {
    case msgpack::Type::Array:
    case msgpack::Type::Map:
      Stack.push_back(StackLevel(*DestNode, MergeResult, Obj.Length, nullptr));
      break;
    default:
      break;
    }

    // Pop finished stack levels.
    while (!Stack.empty()) {
      if (Stack.back().MapEntry)
        break;
      if (Stack.back().Index != Stack.back().End)
        break;
      Stack.pop_back();
    }
  } while (!Stack.empty());
  return true;
}

struct WriterStackLevel {
  DocNode Node;
  DocNode::MapTy::iterator MapIt;
  DocNode::ArrayTy::iterator ArrayIt;
  bool OnKey;
};

/// Write a MsgPack document to a binary MsgPack blob.
void Document::writeToBlob(std::string &Blob) {
  Blob.clear();
  raw_string_ostream OS(Blob);
  msgpack::Writer MPWriter(OS);
  SmallVector<WriterStackLevel, 4> Stack;
  DocNode Node = getRoot();
  for (;;) {
    switch (Node.getKind()) {
    case Type::Array:
      MPWriter.writeArraySize(Node.getArray().size());
      Stack.push_back(
          {Node, DocNode::MapTy::iterator(), Node.getArray().begin(), false});
      break;
    case Type::Map:
      MPWriter.writeMapSize(Node.getMap().size());
      Stack.push_back(
          {Node, Node.getMap().begin(), DocNode::ArrayTy::iterator(), true});
      break;
    case Type::Nil:
      MPWriter.writeNil();
      break;
    case Type::Boolean:
      MPWriter.write(Node.getBool());
      break;
    case Type::Int:
      MPWriter.write(Node.getInt());
      break;
    case Type::UInt:
      MPWriter.write(Node.getUInt());
      break;
    case Type::String:
      MPWriter.write(Node.getString());
      break;
    case Type::Binary:
      MPWriter.write(Node.getBinary());
      break;
    case Type::Empty:
      llvm_unreachable("unhandled empty msgpack node");
    default:
      llvm_unreachable("unhandled msgpack object kind");
    }
    // Pop finished stack levels.
    while (!Stack.empty()) {
      if (Stack.back().Node.getKind() == Type::Map) {
        if (Stack.back().MapIt != Stack.back().Node.getMap().end())
          break;
      } else {
        if (Stack.back().ArrayIt != Stack.back().Node.getArray().end())
          break;
      }
      Stack.pop_back();
    }
    if (Stack.empty())
      break;
    // Get the next value.
    if (Stack.back().Node.getKind() == Type::Map) {
      if (Stack.back().OnKey) {
        // Do the key of a key,value pair in a map.
        Node = Stack.back().MapIt->first;
        Stack.back().OnKey = false;
      } else {
        Node = Stack.back().MapIt->second;
        ++Stack.back().MapIt;
        Stack.back().OnKey = true;
      }
    } else {
      Node = *Stack.back().ArrayIt;
      ++Stack.back().ArrayIt;
    }
  }
}
