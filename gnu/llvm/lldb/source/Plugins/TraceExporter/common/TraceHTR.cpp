//===-- TraceHTR.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TraceHTR.h"

#include "lldb/Symbol/Function.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "llvm/Support/JSON.h"
#include <optional>
#include <sstream>
#include <string>

using namespace lldb_private;
using namespace lldb;

size_t HTRBlockMetadata::GetNumInstructions() const {
  return m_num_instructions;
}

std::optional<llvm::StringRef>
HTRBlockMetadata::GetMostFrequentlyCalledFunction() const {
  size_t max_ncalls = 0;
  std::optional<llvm::StringRef> max_name;
  for (const auto &it : m_func_calls) {
    ConstString name = it.first;
    size_t ncalls = it.second;
    if (ncalls > max_ncalls) {
      max_ncalls = ncalls;
      max_name = name.GetStringRef();
    }
  }
  return max_name;
}

llvm::DenseMap<ConstString, size_t> const &
HTRBlockMetadata::GetFunctionCalls() const {
  return m_func_calls;
}

lldb::addr_t HTRBlockMetadata::GetFirstInstructionLoadAddress() const {
  return m_first_instruction_load_address;
}

size_t HTRBlock::GetOffset() const { return m_offset; }

size_t HTRBlock::GetSize() const { return m_size; }

HTRBlockMetadata const &HTRBlock::GetMetadata() const { return m_metadata; }

llvm::ArrayRef<HTRBlockLayerUP> TraceHTR::GetBlockLayers() const {
  return m_block_layer_ups;
}

HTRInstructionLayer const &TraceHTR::GetInstructionLayer() const {
  return *m_instruction_layer_up;
}

void TraceHTR::AddNewBlockLayer(HTRBlockLayerUP &&block_layer) {
  m_block_layer_ups.emplace_back(std::move(block_layer));
}

size_t IHTRLayer::GetLayerId() const { return m_layer_id; }

void HTRBlockLayer::AppendNewBlock(size_t block_id, HTRBlock &&block) {
  m_block_id_trace.emplace_back(block_id);
  m_block_defs.emplace(block_id, std::move(block));
}

void HTRBlockLayer::AppendRepeatedBlock(size_t block_id) {
  m_block_id_trace.emplace_back(block_id);
}

llvm::ArrayRef<lldb::addr_t> HTRInstructionLayer::GetInstructionTrace() const {
  return m_instruction_trace;
}

void HTRInstructionLayer::AddCallInstructionMetadata(
    lldb::addr_t load_addr, std::optional<ConstString> func_name) {
  m_call_isns.emplace(load_addr, func_name);
}

void HTRInstructionLayer::AppendInstruction(lldb::addr_t load_addr) {
  m_instruction_trace.emplace_back(load_addr);
}

HTRBlock const *HTRBlockLayer::GetBlockById(size_t block_id) const {
  auto block_it = m_block_defs.find(block_id);
  if (block_it == m_block_defs.end())
    return nullptr;
  else
    return &block_it->second;
}

llvm::ArrayRef<size_t> HTRBlockLayer::GetBlockIdTrace() const {
  return m_block_id_trace;
}

size_t HTRBlockLayer::GetNumUnits() const { return m_block_id_trace.size(); }

HTRBlockMetadata HTRInstructionLayer::GetMetadataByIndex(size_t index) const {
  lldb::addr_t instruction_load_address = m_instruction_trace[index];
  llvm::DenseMap<ConstString, size_t> func_calls;

  auto func_name_it = m_call_isns.find(instruction_load_address);
  if (func_name_it != m_call_isns.end()) {
    if (std::optional<ConstString> func_name = func_name_it->second) {
      func_calls[*func_name] = 1;
    }
  }
  return {instruction_load_address, 1, std::move(func_calls)};
}

size_t HTRInstructionLayer::GetNumUnits() const {
  return m_instruction_trace.size();
}

HTRBlockMetadata HTRBlockLayer::GetMetadataByIndex(size_t index) const {
  size_t block_id = m_block_id_trace[index];
  HTRBlock block = m_block_defs.find(block_id)->second;
  return block.GetMetadata();
}

TraceHTR::TraceHTR(Thread &thread, TraceCursor &cursor)
    : m_instruction_layer_up(std::make_unique<HTRInstructionLayer>(0)) {

  // Move cursor to the first instruction in the trace
  cursor.SetForwards(true);
  cursor.Seek(0, lldb::eTraceCursorSeekTypeBeginning);

  // TODO: fix after persona0220's patch on a new way to access instruction
  // kinds
  /*
  Target &target = thread.GetProcess()->GetTarget();
  auto function_name_from_load_address =
      [&](lldb::addr_t load_address) -> std::optional<ConstString> {
    lldb_private::Address pc_addr;
    SymbolContext sc;
    if (target.ResolveLoadAddress(load_address, pc_addr) &&
        pc_addr.CalculateSymbolContext(&sc))
      return sc.GetFunctionName()
                 ? std::optional<ConstString>(sc.GetFunctionName())
                 : std::nullopt;
    else
      return std::nullopt;
  };

  while (cursor.HasValue()) { if (cursor.IsError()) {
      // Append a load address of 0 for all instructions that an error occured
      // while decoding.
      // TODO: Make distinction between errors by storing the error messages.
      // Currently, all errors are treated the same.
      m_instruction_layer_up->AppendInstruction(0);
      cursor.Next();
    } else if (cursor.IsEvent()) {
      cursor.Next();
    } else {
      lldb::addr_t current_instruction_load_address = cursor.GetLoadAddress();
      lldb::InstructionControlFlowKind current_instruction_type =
          cursor.GetInstructionControlFlowKind();

      m_instruction_layer_up->AppendInstruction(
          current_instruction_load_address);
      cursor.Next();
      bool more_data_in_trace = cursor.HasValue();
      if (current_instruction_type &
          lldb::eInstructionControlFlowKindCall) {
        if (more_data_in_trace && !cursor.IsError()) {
          m_instruction_layer_up->AddCallInstructionMetadata(
              current_instruction_load_address,
              function_name_from_load_address(cursor.GetLoadAddress()));
        } else {
          // Next instruction is not known - pass None to indicate the name
          // of the function being called is not known
          m_instruction_layer_up->AddCallInstructionMetadata(
              current_instruction_load_address, std::nullopt);
        }
      }
    }
  }
  */
}

void HTRBlockMetadata::MergeMetadata(
    HTRBlockMetadata &merged_metadata,
    HTRBlockMetadata const &metadata_to_merge) {
  merged_metadata.m_num_instructions += metadata_to_merge.m_num_instructions;
  for (const auto &it : metadata_to_merge.m_func_calls) {
    ConstString name = it.first;
    size_t num_calls = it.second;
    merged_metadata.m_func_calls[name] += num_calls;
  }
}

HTRBlock IHTRLayer::MergeUnits(size_t start_unit_index, size_t num_units) {
  // TODO: make this function take `end_unit_index` as a parameter instead of
  // unit and merge the range [start_unit_indx, end_unit_index] inclusive.
  HTRBlockMetadata merged_metadata = GetMetadataByIndex(start_unit_index);
  for (size_t i = start_unit_index + 1; i < start_unit_index + num_units; i++) {
    // merge the new metadata into merged_metadata
    HTRBlockMetadata::MergeMetadata(merged_metadata, GetMetadataByIndex(i));
  }
  return {start_unit_index, num_units, merged_metadata};
}

void TraceHTR::ExecutePasses() {
  auto are_passes_done = [](IHTRLayer &l1, IHTRLayer &l2) {
    return l1.GetNumUnits() == l2.GetNumUnits();
  };
  HTRBlockLayerUP current_block_layer_up =
      BasicSuperBlockMerge(*m_instruction_layer_up);
  HTRBlockLayer &current_block_layer = *current_block_layer_up;
  if (are_passes_done(*m_instruction_layer_up, *current_block_layer_up))
    return;

  AddNewBlockLayer(std::move(current_block_layer_up));
  while (true) {
    HTRBlockLayerUP new_block_layer_up =
        BasicSuperBlockMerge(current_block_layer);
    if (are_passes_done(current_block_layer, *new_block_layer_up))
      return;

    current_block_layer = *new_block_layer_up;
    AddNewBlockLayer(std::move(new_block_layer_up));
  }
}

llvm::Error TraceHTR::Export(std::string outfile) {
  std::error_code ec;
  llvm::raw_fd_ostream os(outfile, ec, llvm::sys::fs::OF_Text);
  if (ec) {
    return llvm::make_error<llvm::StringError>(
        "unable to open destination file: " + outfile, os.error());
  } else {
    os << toJSON(*this);
    os.close();
    if (os.has_error()) {
      return llvm::make_error<llvm::StringError>(
          "unable to write to destination file: " + outfile, os.error());
    }
  }
  return llvm::Error::success();
}

HTRBlockLayerUP lldb_private::BasicSuperBlockMerge(IHTRLayer &layer) {
  std::unique_ptr<HTRBlockLayer> new_block_layer =
      std::make_unique<HTRBlockLayer>(layer.GetLayerId() + 1);

  if (layer.GetNumUnits()) {
    // Future Improvement: split this into two functions - one for finding heads
    // and tails, one for merging/creating the next layer A 'head' is defined to
    // be a block whose occurrences in the trace do not have a unique preceding
    // block.
    std::unordered_set<size_t> heads;

    // The load address of the first instruction of a block is the unique ID for
    // that block (i.e. blocks with the same first instruction load address are
    // the same block)

    // Future Improvement: no need to store all its preceding block ids, all we
    // care about is that there is more than one preceding block id, so an enum
    // could be used
    std::unordered_map<lldb::addr_t, std::unordered_set<lldb::addr_t>> head_map;
    lldb::addr_t prev_id =
        layer.GetMetadataByIndex(0).GetFirstInstructionLoadAddress();
    size_t num_units = layer.GetNumUnits();
    // This excludes the first unit since it has no previous unit
    for (size_t i = 1; i < num_units; i++) {
      lldb::addr_t current_id =
          layer.GetMetadataByIndex(i).GetFirstInstructionLoadAddress();
      head_map[current_id].insert(prev_id);
      prev_id = current_id;
    }
    for (const auto &it : head_map) {
      // ID of 0 represents an error - errors can't be heads or tails
      lldb::addr_t id = it.first;
      const std::unordered_set<lldb::addr_t> predecessor_set = it.second;
      if (id && predecessor_set.size() > 1)
        heads.insert(id);
    }

    // Future Improvement: identify heads and tails in the same loop
    // A 'tail' is defined to be a block whose occurrences in the trace do
    // not have a unique succeeding block.
    std::unordered_set<lldb::addr_t> tails;
    std::unordered_map<lldb::addr_t, std::unordered_set<lldb::addr_t>> tail_map;

    // This excludes the last unit since it has no next unit
    for (size_t i = 0; i < num_units - 1; i++) {
      lldb::addr_t current_id =
          layer.GetMetadataByIndex(i).GetFirstInstructionLoadAddress();
      lldb::addr_t next_id =
          layer.GetMetadataByIndex(i + 1).GetFirstInstructionLoadAddress();
      tail_map[current_id].insert(next_id);
    }

    // Mark last block as tail so the algorithm stops gracefully
    lldb::addr_t last_id = layer.GetMetadataByIndex(num_units - 1)
                               .GetFirstInstructionLoadAddress();
    tails.insert(last_id);
    for (const auto &it : tail_map) {
      lldb::addr_t id = it.first;
      const std::unordered_set<lldb::addr_t> successor_set = it.second;
      // ID of 0 represents an error - errors can't be heads or tails
      if (id && successor_set.size() > 1)
        tails.insert(id);
    }

    // Need to keep track of size of string since things we push are variable
    // length
    size_t superblock_size = 0;
    // Each super block always has the same first unit (we call this the
    // super block head) This gurantee allows us to use the super block head as
    // the unique key mapping to the super block it begins
    std::optional<size_t> superblock_head;
    auto construct_next_layer = [&](size_t merge_start, size_t n) -> void {
      if (!superblock_head)
        return;
      if (new_block_layer->GetBlockById(*superblock_head)) {
        new_block_layer->AppendRepeatedBlock(*superblock_head);
      } else {
        HTRBlock new_block = layer.MergeUnits(merge_start, n);
        new_block_layer->AppendNewBlock(*superblock_head, std::move(new_block));
      }
    };

    for (size_t i = 0; i < num_units; i++) {
      lldb::addr_t unit_id =
          layer.GetMetadataByIndex(i).GetFirstInstructionLoadAddress();
      auto isHead = heads.count(unit_id) > 0;
      auto isTail = tails.count(unit_id) > 0;

      if (isHead && isTail) {
        // Head logic
        if (superblock_size) { // this handles (tail, head) adjacency -
                               // otherwise an empty
                               // block is created
          // End previous super block
          construct_next_layer(i - superblock_size, superblock_size);
        }
        // Current id is first in next super block since it's a head
        superblock_head = unit_id;
        superblock_size = 1;

        // Tail logic
        construct_next_layer(i - superblock_size + 1, superblock_size);
        // Reset the block_head since the prev super block has come to and end
        superblock_head = std::nullopt;
        superblock_size = 0;
      } else if (isHead) {
        if (superblock_size) { // this handles (tail, head) adjacency -
                               // otherwise an empty
                               // block is created
          // End previous super block
          construct_next_layer(i - superblock_size, superblock_size);
        }
        // Current id is first in next super block since it's a head
        superblock_head = unit_id;
        superblock_size = 1;
      } else if (isTail) {
        if (!superblock_head)
          superblock_head = unit_id;
        superblock_size++;

        // End previous super block
        construct_next_layer(i - superblock_size + 1, superblock_size);
        // Reset the block_head since the prev super block has come to and end
        superblock_head = std::nullopt;
        superblock_size = 0;
      } else {
        if (!superblock_head)
          superblock_head = unit_id;
        superblock_size++;
      }
    }
  }
  return new_block_layer;
}

llvm::json::Value lldb_private::toJSON(const TraceHTR &htr) {
  std::vector<llvm::json::Value> layers_as_json;
  for (size_t i = 0; i < htr.GetInstructionLayer().GetInstructionTrace().size();
       i++) {
    size_t layer_id = htr.GetInstructionLayer().GetLayerId();
    HTRBlockMetadata metadata = htr.GetInstructionLayer().GetMetadataByIndex(i);
    lldb::addr_t load_address = metadata.GetFirstInstructionLoadAddress();

    std::string display_name;

    std::stringstream stream;
    stream << "0x" << std::hex << load_address;
    std::string load_address_hex_string(stream.str());
    display_name.assign(load_address_hex_string);

    // name: load address of the first instruction of the block and the name
    // of the most frequently called function from the block (if applicable)

    // ph: the event type - 'X' for Complete events (see link to documentation
    // below)

    // Since trace timestamps aren't yet supported in HTR, the ts (timestamp) is
    // based on the instruction's offset in the trace and the dur (duration) is
    // 1 since this layer contains single instructions. Using the instruction
    // offset and a duration of 1 oversimplifies the true timing information of
    // the trace, nonetheless, these approximate timestamps/durations provide an
    // clear visualization of the trace.

    // ts: offset from the beginning of the trace for the first instruction in
    // the block

    // dur: 1 since this layer contains single instructions.

    // pid: the ID of the HTR layer the blocks belong to

    // See
    // https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview#heading=h.j75x71ritcoy
    // for documentation on the Trace Event Format
    layers_as_json.emplace_back(llvm::json::Object{
        {"name", display_name},
        {"ph", "X"},
        {"ts", (int64_t)i},
        {"dur", 1},
        {"pid", (int64_t)layer_id},
    });
  }

  for (const auto &layer : htr.GetBlockLayers()) {
    size_t start_ts = 0;
    std::vector<size_t> block_id_trace = layer->GetBlockIdTrace();
    for (size_t i = 0; i < block_id_trace.size(); i++) {
      size_t id = block_id_trace[i];
      // Guranteed that this ID is valid, so safe to dereference here.
      HTRBlock block = *layer->GetBlockById(id);
      llvm::json::Value block_json = toJSON(block);
      size_t layer_id = layer->GetLayerId();

      HTRBlockMetadata metadata = block.GetMetadata();

      std::optional<llvm::StringRef> most_freq_func =
          metadata.GetMostFrequentlyCalledFunction();
      std::stringstream stream;
      stream << "0x" << std::hex << metadata.GetFirstInstructionLoadAddress();
      std::string offset_hex_string(stream.str());
      std::string display_name =
          most_freq_func ? offset_hex_string + ": " + most_freq_func->str()
                         : offset_hex_string;

      // Since trace timestamps aren't yet supported in HTR, the ts (timestamp)
      // and dur (duration) are based on the block's offset in the trace and
      // number of instructions in the block, respectively. Using the block
      // offset and the number of instructions oversimplifies the true timing
      // information of the trace, nonetheless, these approximate
      // timestamps/durations provide an understandable visualization of the
      // trace.
      auto duration = metadata.GetNumInstructions();
      layers_as_json.emplace_back(llvm::json::Object{
          {"name", display_name},
          {"ph", "X"},
          {"ts", (int64_t)start_ts},
          {"dur", (int64_t)duration},
          {"pid", (int64_t)layer_id},
          {"args", block_json},
      });
      start_ts += duration;
    }
  }
  return layers_as_json;
}

llvm::json::Value lldb_private::toJSON(const HTRBlock &block) {
  return llvm::json::Value(
      llvm::json::Object{{"Metadata", block.GetMetadata()}});
}

llvm::json::Value lldb_private::toJSON(const HTRBlockMetadata &metadata) {
  std::vector<llvm::json::Value> function_calls;
  for (const auto &it : metadata.GetFunctionCalls()) {
    ConstString name = it.first;
    size_t n_calls = it.second;
    function_calls.emplace_back(llvm::formatv("({0}: {1})", name, n_calls));
  }

  return llvm::json::Value(llvm::json::Object{
      {"Number of Instructions", (ssize_t)metadata.GetNumInstructions()},
      {"Functions", function_calls}});
}
