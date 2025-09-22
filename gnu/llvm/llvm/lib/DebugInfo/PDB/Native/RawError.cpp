#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;
using namespace llvm::pdb;

namespace {
// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class RawErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "llvm.pdb.raw"; }
  std::string message(int Condition) const override {
    switch (static_cast<raw_error_code>(Condition)) {
    case raw_error_code::unspecified:
      return "An unknown error has occurred.";
    case raw_error_code::feature_unsupported:
      return "The feature is unsupported by the implementation.";
    case raw_error_code::invalid_format:
      return "The record is in an unexpected format.";
    case raw_error_code::corrupt_file:
      return "The PDB file is corrupt.";
    case raw_error_code::insufficient_buffer:
      return "The buffer is not large enough to read the requested number of "
             "bytes.";
    case raw_error_code::no_stream:
      return "The specified stream could not be loaded.";
    case raw_error_code::index_out_of_bounds:
      return "The specified item does not exist in the array.";
    case raw_error_code::invalid_block_address:
      return "The specified block address is not valid.";
    case raw_error_code::duplicate_entry:
      return "The entry already exists.";
    case raw_error_code::no_entry:
      return "The entry does not exist.";
    case raw_error_code::not_writable:
      return "The PDB does not support writing.";
    case raw_error_code::stream_too_long:
      return "The stream was longer than expected.";
    case raw_error_code::invalid_tpi_hash:
      return "The Type record has an invalid hash value.";
    }
    llvm_unreachable("Unrecognized raw_error_code");
  }
};
} // namespace

const std::error_category &llvm::pdb::RawErrCategory() {
  static RawErrorCategory RawCategory;
  return RawCategory;
}

char RawError::ID;
