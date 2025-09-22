//===-- TraceHTR.h --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_TRACE_HTR_H
#define LLDB_TARGET_TRACE_HTR_H

#include "lldb/Target/Thread.h"
#include "lldb/Target/Trace.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace lldb_private {

/// Metadata associated with an HTR block
/// See lldb/docs/htr.rst for comprehensive HTR documentation
class HTRBlockMetadata {
public:
  /// Constructor for a block's metadata.
  ///
  /// \param[in] first_instruction_load_address
  ///     The load address of the block's first instruction.
  ///
  /// \param[in] num_instructions
  ///     The total number of instructions in the block.
  ///
  /// \param[in] func_calls
  ///     The map of a function name to the number of times it is called from
  ///     the block.
  HTRBlockMetadata(lldb::addr_t first_instruction_load_address,
                   size_t num_instructions,
                   llvm::DenseMap<ConstString, size_t> &&func_calls)
      : m_first_instruction_load_address(first_instruction_load_address),
        m_num_instructions(num_instructions), m_func_calls(func_calls) {}

  /// Merge two \a HTRBlockMetadata in place.
  ///
  /// \param[in][out] merged_metadata
  ///     Metadata that metadata_to_merge will be merged into.
  ///
  /// \param[in] metadata_to_merge
  ///     Metadata to merge into merged_metadata.
  static void MergeMetadata(HTRBlockMetadata &merged_metadata,
                            HTRBlockMetadata const &metadata_to_merge);
  /// Get the number of instructions in the block.
  ///
  /// \return
  ///     The number of instructions in the block.
  size_t GetNumInstructions() const;

  /// Get the name of the most frequently called function from the block.
  ///
  /// \return
  ///     The name of the function that is called the most from this block or
  ///     std::nullopt if no function is called from this block.
  std::optional<llvm::StringRef> GetMostFrequentlyCalledFunction() const;

  /// Get the load address of the first instruction in the block.
  ///
  /// \return
  ///     The load address of the first instruction in the block.
  lldb::addr_t GetFirstInstructionLoadAddress() const;

  /// Get the function calls map for the block.
  /// Function calls are identified in the instruction layer by finding 'call'
  /// instructions and determining the function they are calling. As these
  /// instructions are merged into blocks, we merge these different function
  /// calls into a single map containing the function names to the number of
  /// times it is called from this block.
  ///
  /// \return
  ///     The mapping of function name to the number of times it is called from
  ///     this block.
  llvm::DenseMap<ConstString, size_t> const &GetFunctionCalls() const;

private:
  lldb::addr_t m_first_instruction_load_address;
  size_t m_num_instructions;
  llvm::DenseMap<ConstString, size_t> m_func_calls;
};

/// Block structure representing a sequence of trace "units" (ie instructions).
/// Sequences of blocks are merged to create a new, single block
/// See lldb/docs/htr.rst for comprehensive HTR documentation
class HTRBlock {
public:
  /// Constructor for a block of an HTR layer.
  ///
  /// \param[in] offset
  ///     The offset of the start of this block in the previous layer.
  ///
  /// \param[in] size
  ///     Number of blocks/instructions that make up this block in the previous
  ///     layer.
  ///
  /// \param[in] metadata
  ///     General metadata for this block.
  HTRBlock(size_t offset, size_t size, HTRBlockMetadata metadata)
      : m_offset(offset), m_size(size), m_metadata(metadata) {}

  /// Get the offset of the start of this block in the previous layer.
  ///
  /// \return
  ///     The offset of the block.
  size_t GetOffset() const;

  /// Get the number of blocks/instructions that make up this block in the
  /// previous layer.
  ///
  /// \return
  ///     The size of the block.
  size_t GetSize() const;

  /// Get the metadata for this block.
  ///
  /// \return
  ///     The metadata of the block.
  HTRBlockMetadata const &GetMetadata() const;

private:
  /// Offset in the previous layer
  size_t m_offset;
  /// Number of blocks/instructions that make up this block in the previous
  /// layer
  size_t m_size;
  /// General metadata for this block
  HTRBlockMetadata m_metadata;
};

/// HTR layer interface
/// See lldb/docs/htr.rst for comprehensive HTR documentation
class IHTRLayer {
public:
  /// Construct new HTR layer.
  //
  /// \param[in] id
  ///     The layer's id.
  IHTRLayer(size_t id) : m_layer_id(id) {}

  /// Get the ID of the layer.
  ///
  /// \return
  ///     The layer ID of this layer.
  size_t GetLayerId() const;

  /// Get the metadata of a unit (instruction or block) in the layer.
  ///
  /// \param[in] index
  ///     The position of the unit in the layer.
  ///
  /// \return
  ///     The metadata of the unit in the layer.
  virtual HTRBlockMetadata GetMetadataByIndex(size_t index) const = 0;

  /// Get the total number of units (instruction or block) in this layer.
  ///
  /// \return
  ///     The total number of units in the layer.
  virtual size_t GetNumUnits() const = 0;

  /// Creates a new block from the result of merging a contiguous sequence of
  /// "units" (instructions or blocks depending on layer type) in this layer
  /// This allows the implementation class to decide how to store/generate this
  /// metadata. For example, in the case of the instruction layer we want to
  /// lazily generate this metadata instead of storing it for each instruction.
  ///
  /// \param[in] start_unit_index
  ///     The index of the first unit to be merged.
  ///
  /// \param[in] num_units
  ///     The number of units to be merged. Must be >= 1, since merging 0 blocks
  ///     does not make sense.
  ///
  /// \return
  ///     A new block instance representing the merge of the specified units.
  HTRBlock MergeUnits(size_t start_unit_index, size_t num_units);

  virtual ~IHTRLayer() = default;

protected:
  /// ID of the layer.
  size_t m_layer_id;
};

/// "Base" layer of HTR representing the dynamic instructions of the trace.
/// See lldb/docs/htr.rst for comprehensive HTR documentation
class HTRInstructionLayer : public IHTRLayer {
public:
  /// Construct new instruction layer.
  //
  /// \param[in] id
  ///     The layer's id.
  HTRInstructionLayer(size_t id) : IHTRLayer(id) {}

  size_t GetNumUnits() const override;

  HTRBlockMetadata GetMetadataByIndex(size_t index) const override;

  /// Get the dynamic instruction trace.
  ///
  /// \return
  ///     The dynamic instruction trace.
  llvm::ArrayRef<lldb::addr_t> GetInstructionTrace() const;

  /// Add metadata for a 'call' instruction of the trace.
  ///
  /// \param[in] load_addr
  ///     The load address of the 'call' instruction.
  ///
  /// \param[in] func_name
  ///     The name of the function the 'call' instruction is calling if it can
  ///     be determined, std::nullopt otherwise.
  void AddCallInstructionMetadata(lldb::addr_t load_addr,
                                  std::optional<ConstString> func_name);

  /// Append the load address of an instruction to the dynamic instruction
  /// trace.
  ///
  /// \param[in] load_addr
  ///     The load address of the instruction.
  void AppendInstruction(lldb::addr_t load_addr);

private:
  // Dynamic instructions of trace are stored in chronological order.
  std::vector<lldb::addr_t> m_instruction_trace;
  // Only store metadata for instructions of interest (call instructions)
  // If we stored metadata for each instruction this would be wasteful since
  // most instructions don't contain useful metadata

  // This map contains the load address of all the call instructions.
  // load address maps to the name of the function it calls (std::nullopt if
  // function name can't be determined)
  std::unordered_map<lldb::addr_t, std::optional<ConstString>> m_call_isns;
};

/// HTR layer composed of blocks of the trace.
/// See lldb/docs/htr.rst for comprehensive HTR documentation
class HTRBlockLayer : public IHTRLayer {
public:
  /// Construct new block layer.
  //
  /// \param[in] id
  ///     The layer's id.
  HTRBlockLayer(size_t id) : IHTRLayer(id) {}

  size_t GetNumUnits() const override;

  HTRBlockMetadata GetMetadataByIndex(size_t index) const override;

  /// Get an \a HTRBlock from its block id.
  ///
  /// \param[in] block_id
  ///     The id of the block to retrieve.
  ///
  /// \return
  ///     The \a HTRBlock with the specified id, nullptr if no there is no block
  ///     in the layer with the specified block id.
  HTRBlock const *GetBlockById(size_t block_id) const;

  /// Get the block ID trace for this layer.
  /// This block ID trace stores the block ID of each block that occured in the
  /// trace and the block defs map maps block ID to the corresponding \a
  /// HTRBlock.
  ///
  /// \return
  ///     The block ID trace for this layer.
  llvm::ArrayRef<size_t> GetBlockIdTrace() const;

  /// Appends a new block to the layer.
  ///
  /// \param[in] block_id
  ///     The block id of the new block.
  ///
  /// \param[in] block
  ///     The new \a HTRBlock to be appended to the layer. This block is moved
  ///     into the layer.
  void AppendNewBlock(size_t block_id, HTRBlock &&block);

  /// Appends a repeated block to the layer.
  ///
  /// \param[in] block_id
  ///     The block id of the repeated block.
  void AppendRepeatedBlock(size_t block_id);

private:
  /// Maps a unique Block ID to the corresponding HTRBlock
  std::unordered_map<size_t, HTRBlock> m_block_defs;
  /// Reduce memory footprint by just storing a trace of block IDs and use
  /// m_block_defs to map a block_id to its corresponding HTRBlock
  std::vector<size_t> m_block_id_trace;
};

typedef std::unique_ptr<lldb_private::HTRBlockLayer> HTRBlockLayerUP;
typedef std::unique_ptr<lldb_private::HTRInstructionLayer>
    HTRInstructionLayerUP;

/// Top-level HTR class
/// See lldb/docs/htr.rst for comprehensive HTR documentation
class TraceHTR {

public:
  /// Constructor for a trace's HTR.
  ///
  /// \param[in] thread
  ///     The thread the trace belongs to.
  ///
  /// \param[in] cursor
  ///     The trace cursor that gives access to the trace's contents.
  TraceHTR(Thread &thread, TraceCursor &cursor);

  /// Executes passes on the HTR layers until no further
  /// summarization/compression is achieved
  void ExecutePasses();

  /// Export HTR layers to the specified format and outfile.
  ///
  /// \param[in] outfile
  ///     The file that the exported HTR data will be written to.
  ///
  /// \return
  ///     Success if the export is successful, Error otherwise.
  llvm::Error Export(std::string outfile);

  /// Get the block layers of this HTR.
  ///
  /// \return
  ///     The block layers of this HTR.
  llvm::ArrayRef<HTRBlockLayerUP> GetBlockLayers() const;

  /// Get the instruction layer of this HTR.
  ///
  /// \return
  ///     The instruction layer of this HTR.
  HTRInstructionLayer const &GetInstructionLayer() const;

  /// Add a new block layer to this HTR.
  ///
  /// \param[in]
  ///     The new block layer to be added.
  void AddNewBlockLayer(HTRBlockLayerUP &&block_layer);

private:
  // There is a single instruction layer per HTR
  HTRInstructionLayerUP m_instruction_layer_up;
  // There are one or more block layers per HTR
  std::vector<HTRBlockLayerUP> m_block_layer_ups;
};

// Serialization functions for exporting HTR to Chrome Trace Format
llvm::json::Value toJSON(const TraceHTR &htr);
llvm::json::Value toJSON(const HTRBlock &block);
llvm::json::Value toJSON(const HTRBlockMetadata &metadata);

/// The HTR passes are defined below:

/// Creates a new layer by merging the "basic super blocks" in the current layer
///
/// A "basic super block" is the longest sequence of blocks that always occur in
/// the same order. (The concept is akin to “Basic Block" in compiler theory,
/// but refers to dynamic occurrences rather than CFG nodes)
///
/// Procedure to find all basic super blocks:
//
///   - For each block, compute the number of distinct predecessor and
///   successor blocks.
///       Predecessor - the block that occurs directly before (to the left of)
///       the current block Successor  - the block that occurs directly after
///       (to the right of) the current block
///   - A block with more than one distinct successor is always the start of a
///   super block, the super block will continue until the next block with
///   more than one distinct predecessor or successor.
///
/// The implementation makes use of two terms - 'heads' and 'tails' known as
/// the 'endpoints' of a basic super block:
///   A 'head' is defined to be a block in the trace that doesn't have a
///   unique predecessor
///   A 'tail' is defined to be a block in the trace that doesn't have a
///   unique successor
///
/// A basic super block is defined to be a sequence of blocks between two
/// endpoints
///
/// A head represents the start of the next group, so the current group
/// ends at the block preceding the head and the next group begins with
/// this head block
///
/// A tail represents the end of the current group, so the current group
/// ends with the tail block and the next group begins with the
/// following block.
///
/// See lldb/docs/htr.rst for comprehensive HTR documentation
///
/// \param[in] layer
///     The layer to execute the pass on.
///
/// \return
///     A new layer instance representing the merge of blocks in the
///     previous layer
HTRBlockLayerUP BasicSuperBlockMerge(IHTRLayer &layer);

} // namespace lldb_private

#endif // LLDB_TARGET_TRACE_HTR_H
