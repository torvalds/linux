//===-- llvm/ADT/APInt.h - For Arbitrary Precision Integer -----*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a class to represent arbitrary precision
/// integral constant values and operations on them.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_APINT_H
#define LLVM_ADT_APINT_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/float128.h"
#include <cassert>
#include <climits>
#include <cstring>
#include <optional>
#include <utility>

namespace llvm {
class FoldingSetNodeID;
class StringRef;
class hash_code;
class raw_ostream;
struct Align;
class DynamicAPInt;

template <typename T> class SmallVectorImpl;
template <typename T> class ArrayRef;
template <typename T, typename Enable> struct DenseMapInfo;

class APInt;

inline APInt operator-(APInt);

//===----------------------------------------------------------------------===//
//                              APInt Class
//===----------------------------------------------------------------------===//

/// Class for arbitrary precision integers.
///
/// APInt is a functional replacement for common case unsigned integer type like
/// "unsigned", "unsigned long" or "uint64_t", but also allows non-byte-width
/// integer sizes and large integer value types such as 3-bits, 15-bits, or more
/// than 64-bits of precision. APInt provides a variety of arithmetic operators
/// and methods to manipulate integer values of any bit-width. It supports both
/// the typical integer arithmetic and comparison operations as well as bitwise
/// manipulation.
///
/// The class has several invariants worth noting:
///   * All bit, byte, and word positions are zero-based.
///   * Once the bit width is set, it doesn't change except by the Truncate,
///     SignExtend, or ZeroExtend operations.
///   * All binary operators must be on APInt instances of the same bit width.
///     Attempting to use these operators on instances with different bit
///     widths will yield an assertion.
///   * The value is stored canonically as an unsigned value. For operations
///     where it makes a difference, there are both signed and unsigned variants
///     of the operation. For example, sdiv and udiv. However, because the bit
///     widths must be the same, operations such as Mul and Add produce the same
///     results regardless of whether the values are interpreted as signed or
///     not.
///   * In general, the class tries to follow the style of computation that LLVM
///     uses in its IR. This simplifies its use for LLVM.
///   * APInt supports zero-bit-width values, but operations that require bits
///     are not defined on it (e.g. you cannot ask for the sign of a zero-bit
///     integer).  This means that operations like zero extension and logical
///     shifts are defined, but sign extension and ashr is not.  Zero bit values
///     compare and hash equal to themselves, and countLeadingZeros returns 0.
///
class [[nodiscard]] APInt {
public:
  typedef uint64_t WordType;

  /// This enum is used to hold the constants we needed for APInt.
  enum : unsigned {
    /// Byte size of a word.
    APINT_WORD_SIZE = sizeof(WordType),
    /// Bits in a word.
    APINT_BITS_PER_WORD = APINT_WORD_SIZE * CHAR_BIT
  };

  enum class Rounding {
    DOWN,
    TOWARD_ZERO,
    UP,
  };

  static constexpr WordType WORDTYPE_MAX = ~WordType(0);

  /// \name Constructors
  /// @{

  /// Create a new APInt of numBits width, initialized as val.
  ///
  /// If isSigned is true then val is treated as if it were a signed value
  /// (i.e. as an int64_t) and the appropriate sign extension to the bit width
  /// will be done. Otherwise, no sign extension occurs (high order bits beyond
  /// the range of val are zero filled).
  ///
  /// \param numBits the bit width of the constructed APInt
  /// \param val the initial value of the APInt
  /// \param isSigned how to treat signedness of val
  APInt(unsigned numBits, uint64_t val, bool isSigned = false)
      : BitWidth(numBits) {
    if (isSingleWord()) {
      U.VAL = val;
      clearUnusedBits();
    } else {
      initSlowCase(val, isSigned);
    }
  }

  /// Construct an APInt of numBits width, initialized as bigVal[].
  ///
  /// Note that bigVal.size() can be smaller or larger than the corresponding
  /// bit width but any extraneous bits will be dropped.
  ///
  /// \param numBits the bit width of the constructed APInt
  /// \param bigVal a sequence of words to form the initial value of the APInt
  APInt(unsigned numBits, ArrayRef<uint64_t> bigVal);

  /// Equivalent to APInt(numBits, ArrayRef<uint64_t>(bigVal, numWords)), but
  /// deprecated because this constructor is prone to ambiguity with the
  /// APInt(unsigned, uint64_t, bool) constructor.
  ///
  /// If this overload is ever deleted, care should be taken to prevent calls
  /// from being incorrectly captured by the APInt(unsigned, uint64_t, bool)
  /// constructor.
  APInt(unsigned numBits, unsigned numWords, const uint64_t bigVal[]);

  /// Construct an APInt from a string representation.
  ///
  /// This constructor interprets the string \p str in the given radix. The
  /// interpretation stops when the first character that is not suitable for the
  /// radix is encountered, or the end of the string. Acceptable radix values
  /// are 2, 8, 10, 16, and 36. It is an error for the value implied by the
  /// string to require more bits than numBits.
  ///
  /// \param numBits the bit width of the constructed APInt
  /// \param str the string to be interpreted
  /// \param radix the radix to use for the conversion
  APInt(unsigned numBits, StringRef str, uint8_t radix);

  /// Default constructor that creates an APInt with a 1-bit zero value.
  explicit APInt() { U.VAL = 0; }

  /// Copy Constructor.
  APInt(const APInt &that) : BitWidth(that.BitWidth) {
    if (isSingleWord())
      U.VAL = that.U.VAL;
    else
      initSlowCase(that);
  }

  /// Move Constructor.
  APInt(APInt &&that) : BitWidth(that.BitWidth) {
    memcpy(&U, &that.U, sizeof(U));
    that.BitWidth = 0;
  }

  /// Destructor.
  ~APInt() {
    if (needsCleanup())
      delete[] U.pVal;
  }

  /// @}
  /// \name Value Generators
  /// @{

  /// Get the '0' value for the specified bit-width.
  static APInt getZero(unsigned numBits) { return APInt(numBits, 0); }

  /// Return an APInt zero bits wide.
  static APInt getZeroWidth() { return getZero(0); }

  /// Gets maximum unsigned value of APInt for specific bit width.
  static APInt getMaxValue(unsigned numBits) { return getAllOnes(numBits); }

  /// Gets maximum signed value of APInt for a specific bit width.
  static APInt getSignedMaxValue(unsigned numBits) {
    APInt API = getAllOnes(numBits);
    API.clearBit(numBits - 1);
    return API;
  }

  /// Gets minimum unsigned value of APInt for a specific bit width.
  static APInt getMinValue(unsigned numBits) { return APInt(numBits, 0); }

  /// Gets minimum signed value of APInt for a specific bit width.
  static APInt getSignedMinValue(unsigned numBits) {
    APInt API(numBits, 0);
    API.setBit(numBits - 1);
    return API;
  }

  /// Get the SignMask for a specific bit width.
  ///
  /// This is just a wrapper function of getSignedMinValue(), and it helps code
  /// readability when we want to get a SignMask.
  static APInt getSignMask(unsigned BitWidth) {
    return getSignedMinValue(BitWidth);
  }

  /// Return an APInt of a specified width with all bits set.
  static APInt getAllOnes(unsigned numBits) {
    return APInt(numBits, WORDTYPE_MAX, true);
  }

  /// Return an APInt with exactly one bit set in the result.
  static APInt getOneBitSet(unsigned numBits, unsigned BitNo) {
    APInt Res(numBits, 0);
    Res.setBit(BitNo);
    return Res;
  }

  /// Get a value with a block of bits set.
  ///
  /// Constructs an APInt value that has a contiguous range of bits set. The
  /// bits from loBit (inclusive) to hiBit (exclusive) will be set. All other
  /// bits will be zero. For example, with parameters(32, 0, 16) you would get
  /// 0x0000FFFF. Please call getBitsSetWithWrap if \p loBit may be greater than
  /// \p hiBit.
  ///
  /// \param numBits the intended bit width of the result
  /// \param loBit the index of the lowest bit set.
  /// \param hiBit the index of the highest bit set.
  ///
  /// \returns An APInt value with the requested bits set.
  static APInt getBitsSet(unsigned numBits, unsigned loBit, unsigned hiBit) {
    APInt Res(numBits, 0);
    Res.setBits(loBit, hiBit);
    return Res;
  }

  /// Wrap version of getBitsSet.
  /// If \p hiBit is bigger than \p loBit, this is same with getBitsSet.
  /// If \p hiBit is not bigger than \p loBit, the set bits "wrap". For example,
  /// with parameters (32, 28, 4), you would get 0xF000000F.
  /// If \p hiBit is equal to \p loBit, you would get a result with all bits
  /// set.
  static APInt getBitsSetWithWrap(unsigned numBits, unsigned loBit,
                                  unsigned hiBit) {
    APInt Res(numBits, 0);
    Res.setBitsWithWrap(loBit, hiBit);
    return Res;
  }

  /// Constructs an APInt value that has a contiguous range of bits set. The
  /// bits from loBit (inclusive) to numBits (exclusive) will be set. All other
  /// bits will be zero. For example, with parameters(32, 12) you would get
  /// 0xFFFFF000.
  ///
  /// \param numBits the intended bit width of the result
  /// \param loBit the index of the lowest bit to set.
  ///
  /// \returns An APInt value with the requested bits set.
  static APInt getBitsSetFrom(unsigned numBits, unsigned loBit) {
    APInt Res(numBits, 0);
    Res.setBitsFrom(loBit);
    return Res;
  }

  /// Constructs an APInt value that has the top hiBitsSet bits set.
  ///
  /// \param numBits the bitwidth of the result
  /// \param hiBitsSet the number of high-order bits set in the result.
  static APInt getHighBitsSet(unsigned numBits, unsigned hiBitsSet) {
    APInt Res(numBits, 0);
    Res.setHighBits(hiBitsSet);
    return Res;
  }

  /// Constructs an APInt value that has the bottom loBitsSet bits set.
  ///
  /// \param numBits the bitwidth of the result
  /// \param loBitsSet the number of low-order bits set in the result.
  static APInt getLowBitsSet(unsigned numBits, unsigned loBitsSet) {
    APInt Res(numBits, 0);
    Res.setLowBits(loBitsSet);
    return Res;
  }

  /// Return a value containing V broadcasted over NewLen bits.
  static APInt getSplat(unsigned NewLen, const APInt &V);

  /// @}
  /// \name Value Tests
  /// @{

  /// Determine if this APInt just has one word to store value.
  ///
  /// \returns true if the number of bits <= 64, false otherwise.
  bool isSingleWord() const { return BitWidth <= APINT_BITS_PER_WORD; }

  /// Determine sign of this APInt.
  ///
  /// This tests the high bit of this APInt to determine if it is set.
  ///
  /// \returns true if this APInt is negative, false otherwise
  bool isNegative() const { return (*this)[BitWidth - 1]; }

  /// Determine if this APInt Value is non-negative (>= 0)
  ///
  /// This tests the high bit of the APInt to determine if it is unset.
  bool isNonNegative() const { return !isNegative(); }

  /// Determine if sign bit of this APInt is set.
  ///
  /// This tests the high bit of this APInt to determine if it is set.
  ///
  /// \returns true if this APInt has its sign bit set, false otherwise.
  bool isSignBitSet() const { return (*this)[BitWidth - 1]; }

  /// Determine if sign bit of this APInt is clear.
  ///
  /// This tests the high bit of this APInt to determine if it is clear.
  ///
  /// \returns true if this APInt has its sign bit clear, false otherwise.
  bool isSignBitClear() const { return !isSignBitSet(); }

  /// Determine if this APInt Value is positive.
  ///
  /// This tests if the value of this APInt is positive (> 0). Note
  /// that 0 is not a positive value.
  ///
  /// \returns true if this APInt is positive.
  bool isStrictlyPositive() const { return isNonNegative() && !isZero(); }

  /// Determine if this APInt Value is non-positive (<= 0).
  ///
  /// \returns true if this APInt is non-positive.
  bool isNonPositive() const { return !isStrictlyPositive(); }

  /// Determine if this APInt Value only has the specified bit set.
  ///
  /// \returns true if this APInt only has the specified bit set.
  bool isOneBitSet(unsigned BitNo) const {
    return (*this)[BitNo] && popcount() == 1;
  }

  /// Determine if all bits are set.  This is true for zero-width values.
  bool isAllOnes() const {
    if (BitWidth == 0)
      return true;
    if (isSingleWord())
      return U.VAL == WORDTYPE_MAX >> (APINT_BITS_PER_WORD - BitWidth);
    return countTrailingOnesSlowCase() == BitWidth;
  }

  /// Determine if this value is zero, i.e. all bits are clear.
  bool isZero() const {
    if (isSingleWord())
      return U.VAL == 0;
    return countLeadingZerosSlowCase() == BitWidth;
  }

  /// Determine if this is a value of 1.
  ///
  /// This checks to see if the value of this APInt is one.
  bool isOne() const {
    if (isSingleWord())
      return U.VAL == 1;
    return countLeadingZerosSlowCase() == BitWidth - 1;
  }

  /// Determine if this is the largest unsigned value.
  ///
  /// This checks to see if the value of this APInt is the maximum unsigned
  /// value for the APInt's bit width.
  bool isMaxValue() const { return isAllOnes(); }

  /// Determine if this is the largest signed value.
  ///
  /// This checks to see if the value of this APInt is the maximum signed
  /// value for the APInt's bit width.
  bool isMaxSignedValue() const {
    if (isSingleWord()) {
      assert(BitWidth && "zero width values not allowed");
      return U.VAL == ((WordType(1) << (BitWidth - 1)) - 1);
    }
    return !isNegative() && countTrailingOnesSlowCase() == BitWidth - 1;
  }

  /// Determine if this is the smallest unsigned value.
  ///
  /// This checks to see if the value of this APInt is the minimum unsigned
  /// value for the APInt's bit width.
  bool isMinValue() const { return isZero(); }

  /// Determine if this is the smallest signed value.
  ///
  /// This checks to see if the value of this APInt is the minimum signed
  /// value for the APInt's bit width.
  bool isMinSignedValue() const {
    if (isSingleWord()) {
      assert(BitWidth && "zero width values not allowed");
      return U.VAL == (WordType(1) << (BitWidth - 1));
    }
    return isNegative() && countTrailingZerosSlowCase() == BitWidth - 1;
  }

  /// Check if this APInt has an N-bits unsigned integer value.
  bool isIntN(unsigned N) const { return getActiveBits() <= N; }

  /// Check if this APInt has an N-bits signed integer value.
  bool isSignedIntN(unsigned N) const { return getSignificantBits() <= N; }

  /// Check if this APInt's value is a power of two greater than zero.
  ///
  /// \returns true if the argument APInt value is a power of two > 0.
  bool isPowerOf2() const {
    if (isSingleWord()) {
      assert(BitWidth && "zero width values not allowed");
      return isPowerOf2_64(U.VAL);
    }
    return countPopulationSlowCase() == 1;
  }

  /// Check if this APInt's negated value is a power of two greater than zero.
  bool isNegatedPowerOf2() const {
    assert(BitWidth && "zero width values not allowed");
    if (isNonNegative())
      return false;
    // NegatedPowerOf2 - shifted mask in the top bits.
    unsigned LO = countl_one();
    unsigned TZ = countr_zero();
    return (LO + TZ) == BitWidth;
  }

  /// Checks if this APInt -interpreted as an address- is aligned to the
  /// provided value.
  bool isAligned(Align A) const;

  /// Check if the APInt's value is returned by getSignMask.
  ///
  /// \returns true if this is the value returned by getSignMask.
  bool isSignMask() const { return isMinSignedValue(); }

  /// Convert APInt to a boolean value.
  ///
  /// This converts the APInt to a boolean value as a test against zero.
  bool getBoolValue() const { return !isZero(); }

  /// If this value is smaller than the specified limit, return it, otherwise
  /// return the limit value.  This causes the value to saturate to the limit.
  uint64_t getLimitedValue(uint64_t Limit = UINT64_MAX) const {
    return ugt(Limit) ? Limit : getZExtValue();
  }

  /// Check if the APInt consists of a repeated bit pattern.
  ///
  /// e.g. 0x01010101 satisfies isSplat(8).
  /// \param SplatSizeInBits The size of the pattern in bits. Must divide bit
  /// width without remainder.
  bool isSplat(unsigned SplatSizeInBits) const;

  /// \returns true if this APInt value is a sequence of \param numBits ones
  /// starting at the least significant bit with the remainder zero.
  bool isMask(unsigned numBits) const {
    assert(numBits != 0 && "numBits must be non-zero");
    assert(numBits <= BitWidth && "numBits out of range");
    if (isSingleWord())
      return U.VAL == (WORDTYPE_MAX >> (APINT_BITS_PER_WORD - numBits));
    unsigned Ones = countTrailingOnesSlowCase();
    return (numBits == Ones) &&
           ((Ones + countLeadingZerosSlowCase()) == BitWidth);
  }

  /// \returns true if this APInt is a non-empty sequence of ones starting at
  /// the least significant bit with the remainder zero.
  /// Ex. isMask(0x0000FFFFU) == true.
  bool isMask() const {
    if (isSingleWord())
      return isMask_64(U.VAL);
    unsigned Ones = countTrailingOnesSlowCase();
    return (Ones > 0) && ((Ones + countLeadingZerosSlowCase()) == BitWidth);
  }

  /// Return true if this APInt value contains a non-empty sequence of ones with
  /// the remainder zero.
  bool isShiftedMask() const {
    if (isSingleWord())
      return isShiftedMask_64(U.VAL);
    unsigned Ones = countPopulationSlowCase();
    unsigned LeadZ = countLeadingZerosSlowCase();
    return (Ones + LeadZ + countr_zero()) == BitWidth;
  }

  /// Return true if this APInt value contains a non-empty sequence of ones with
  /// the remainder zero. If true, \p MaskIdx will specify the index of the
  /// lowest set bit and \p MaskLen is updated to specify the length of the
  /// mask, else neither are updated.
  bool isShiftedMask(unsigned &MaskIdx, unsigned &MaskLen) const {
    if (isSingleWord())
      return isShiftedMask_64(U.VAL, MaskIdx, MaskLen);
    unsigned Ones = countPopulationSlowCase();
    unsigned LeadZ = countLeadingZerosSlowCase();
    unsigned TrailZ = countTrailingZerosSlowCase();
    if ((Ones + LeadZ + TrailZ) != BitWidth)
      return false;
    MaskLen = Ones;
    MaskIdx = TrailZ;
    return true;
  }

  /// Compute an APInt containing numBits highbits from this APInt.
  ///
  /// Get an APInt with the same BitWidth as this APInt, just zero mask the low
  /// bits and right shift to the least significant bit.
  ///
  /// \returns the high "numBits" bits of this APInt.
  APInt getHiBits(unsigned numBits) const;

  /// Compute an APInt containing numBits lowbits from this APInt.
  ///
  /// Get an APInt with the same BitWidth as this APInt, just zero mask the high
  /// bits.
  ///
  /// \returns the low "numBits" bits of this APInt.
  APInt getLoBits(unsigned numBits) const;

  /// Determine if two APInts have the same value, after zero-extending
  /// one of them (if needed!) to ensure that the bit-widths match.
  static bool isSameValue(const APInt &I1, const APInt &I2) {
    if (I1.getBitWidth() == I2.getBitWidth())
      return I1 == I2;

    if (I1.getBitWidth() > I2.getBitWidth())
      return I1 == I2.zext(I1.getBitWidth());

    return I1.zext(I2.getBitWidth()) == I2;
  }

  /// Overload to compute a hash_code for an APInt value.
  friend hash_code hash_value(const APInt &Arg);

  /// This function returns a pointer to the internal storage of the APInt.
  /// This is useful for writing out the APInt in binary form without any
  /// conversions.
  const uint64_t *getRawData() const {
    if (isSingleWord())
      return &U.VAL;
    return &U.pVal[0];
  }

  /// @}
  /// \name Unary Operators
  /// @{

  /// Postfix increment operator.  Increment *this by 1.
  ///
  /// \returns a new APInt value representing the original value of *this.
  APInt operator++(int) {
    APInt API(*this);
    ++(*this);
    return API;
  }

  /// Prefix increment operator.
  ///
  /// \returns *this incremented by one
  APInt &operator++();

  /// Postfix decrement operator. Decrement *this by 1.
  ///
  /// \returns a new APInt value representing the original value of *this.
  APInt operator--(int) {
    APInt API(*this);
    --(*this);
    return API;
  }

  /// Prefix decrement operator.
  ///
  /// \returns *this decremented by one.
  APInt &operator--();

  /// Logical negation operation on this APInt returns true if zero, like normal
  /// integers.
  bool operator!() const { return isZero(); }

  /// @}
  /// \name Assignment Operators
  /// @{

  /// Copy assignment operator.
  ///
  /// \returns *this after assignment of RHS.
  APInt &operator=(const APInt &RHS) {
    // The common case (both source or dest being inline) doesn't require
    // allocation or deallocation.
    if (isSingleWord() && RHS.isSingleWord()) {
      U.VAL = RHS.U.VAL;
      BitWidth = RHS.BitWidth;
      return *this;
    }

    assignSlowCase(RHS);
    return *this;
  }

  /// Move assignment operator.
  APInt &operator=(APInt &&that) {
#ifdef EXPENSIVE_CHECKS
    // Some std::shuffle implementations still do self-assignment.
    if (this == &that)
      return *this;
#endif
    assert(this != &that && "Self-move not supported");
    if (!isSingleWord())
      delete[] U.pVal;

    // Use memcpy so that type based alias analysis sees both VAL and pVal
    // as modified.
    memcpy(&U, &that.U, sizeof(U));

    BitWidth = that.BitWidth;
    that.BitWidth = 0;
    return *this;
  }

  /// Assignment operator.
  ///
  /// The RHS value is assigned to *this. If the significant bits in RHS exceed
  /// the bit width, the excess bits are truncated. If the bit width is larger
  /// than 64, the value is zero filled in the unspecified high order bits.
  ///
  /// \returns *this after assignment of RHS value.
  APInt &operator=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL = RHS;
      return clearUnusedBits();
    }
    U.pVal[0] = RHS;
    memset(U.pVal + 1, 0, (getNumWords() - 1) * APINT_WORD_SIZE);
    return *this;
  }

  /// Bitwise AND assignment operator.
  ///
  /// Performs a bitwise AND operation on this APInt and RHS. The result is
  /// assigned to *this.
  ///
  /// \returns *this after ANDing with RHS.
  APInt &operator&=(const APInt &RHS) {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      U.VAL &= RHS.U.VAL;
    else
      andAssignSlowCase(RHS);
    return *this;
  }

  /// Bitwise AND assignment operator.
  ///
  /// Performs a bitwise AND operation on this APInt and RHS. RHS is
  /// logically zero-extended or truncated to match the bit-width of
  /// the LHS.
  APInt &operator&=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL &= RHS;
      return *this;
    }
    U.pVal[0] &= RHS;
    memset(U.pVal + 1, 0, (getNumWords() - 1) * APINT_WORD_SIZE);
    return *this;
  }

  /// Bitwise OR assignment operator.
  ///
  /// Performs a bitwise OR operation on this APInt and RHS. The result is
  /// assigned *this;
  ///
  /// \returns *this after ORing with RHS.
  APInt &operator|=(const APInt &RHS) {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      U.VAL |= RHS.U.VAL;
    else
      orAssignSlowCase(RHS);
    return *this;
  }

  /// Bitwise OR assignment operator.
  ///
  /// Performs a bitwise OR operation on this APInt and RHS. RHS is
  /// logically zero-extended or truncated to match the bit-width of
  /// the LHS.
  APInt &operator|=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL |= RHS;
      return clearUnusedBits();
    }
    U.pVal[0] |= RHS;
    return *this;
  }

  /// Bitwise XOR assignment operator.
  ///
  /// Performs a bitwise XOR operation on this APInt and RHS. The result is
  /// assigned to *this.
  ///
  /// \returns *this after XORing with RHS.
  APInt &operator^=(const APInt &RHS) {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      U.VAL ^= RHS.U.VAL;
    else
      xorAssignSlowCase(RHS);
    return *this;
  }

  /// Bitwise XOR assignment operator.
  ///
  /// Performs a bitwise XOR operation on this APInt and RHS. RHS is
  /// logically zero-extended or truncated to match the bit-width of
  /// the LHS.
  APInt &operator^=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL ^= RHS;
      return clearUnusedBits();
    }
    U.pVal[0] ^= RHS;
    return *this;
  }

  /// Multiplication assignment operator.
  ///
  /// Multiplies this APInt by RHS and assigns the result to *this.
  ///
  /// \returns *this
  APInt &operator*=(const APInt &RHS);
  APInt &operator*=(uint64_t RHS);

  /// Addition assignment operator.
  ///
  /// Adds RHS to *this and assigns the result to *this.
  ///
  /// \returns *this
  APInt &operator+=(const APInt &RHS);
  APInt &operator+=(uint64_t RHS);

  /// Subtraction assignment operator.
  ///
  /// Subtracts RHS from *this and assigns the result to *this.
  ///
  /// \returns *this
  APInt &operator-=(const APInt &RHS);
  APInt &operator-=(uint64_t RHS);

  /// Left-shift assignment function.
  ///
  /// Shifts *this left by shiftAmt and assigns the result to *this.
  ///
  /// \returns *this after shifting left by ShiftAmt
  APInt &operator<<=(unsigned ShiftAmt) {
    assert(ShiftAmt <= BitWidth && "Invalid shift amount");
    if (isSingleWord()) {
      if (ShiftAmt == BitWidth)
        U.VAL = 0;
      else
        U.VAL <<= ShiftAmt;
      return clearUnusedBits();
    }
    shlSlowCase(ShiftAmt);
    return *this;
  }

  /// Left-shift assignment function.
  ///
  /// Shifts *this left by shiftAmt and assigns the result to *this.
  ///
  /// \returns *this after shifting left by ShiftAmt
  APInt &operator<<=(const APInt &ShiftAmt);

  /// @}
  /// \name Binary Operators
  /// @{

  /// Multiplication operator.
  ///
  /// Multiplies this APInt by RHS and returns the result.
  APInt operator*(const APInt &RHS) const;

  /// Left logical shift operator.
  ///
  /// Shifts this APInt left by \p Bits and returns the result.
  APInt operator<<(unsigned Bits) const { return shl(Bits); }

  /// Left logical shift operator.
  ///
  /// Shifts this APInt left by \p Bits and returns the result.
  APInt operator<<(const APInt &Bits) const { return shl(Bits); }

  /// Arithmetic right-shift function.
  ///
  /// Arithmetic right-shift this APInt by shiftAmt.
  APInt ashr(unsigned ShiftAmt) const {
    APInt R(*this);
    R.ashrInPlace(ShiftAmt);
    return R;
  }

  /// Arithmetic right-shift this APInt by ShiftAmt in place.
  void ashrInPlace(unsigned ShiftAmt) {
    assert(ShiftAmt <= BitWidth && "Invalid shift amount");
    if (isSingleWord()) {
      int64_t SExtVAL = SignExtend64(U.VAL, BitWidth);
      if (ShiftAmt == BitWidth)
        U.VAL = SExtVAL >> (APINT_BITS_PER_WORD - 1); // Fill with sign bit.
      else
        U.VAL = SExtVAL >> ShiftAmt;
      clearUnusedBits();
      return;
    }
    ashrSlowCase(ShiftAmt);
  }

  /// Logical right-shift function.
  ///
  /// Logical right-shift this APInt by shiftAmt.
  APInt lshr(unsigned shiftAmt) const {
    APInt R(*this);
    R.lshrInPlace(shiftAmt);
    return R;
  }

  /// Logical right-shift this APInt by ShiftAmt in place.
  void lshrInPlace(unsigned ShiftAmt) {
    assert(ShiftAmt <= BitWidth && "Invalid shift amount");
    if (isSingleWord()) {
      if (ShiftAmt == BitWidth)
        U.VAL = 0;
      else
        U.VAL >>= ShiftAmt;
      return;
    }
    lshrSlowCase(ShiftAmt);
  }

  /// Left-shift function.
  ///
  /// Left-shift this APInt by shiftAmt.
  APInt shl(unsigned shiftAmt) const {
    APInt R(*this);
    R <<= shiftAmt;
    return R;
  }

  /// relative logical shift right
  APInt relativeLShr(int RelativeShift) const {
    return RelativeShift > 0 ? lshr(RelativeShift) : shl(-RelativeShift);
  }

  /// relative logical shift left
  APInt relativeLShl(int RelativeShift) const {
    return relativeLShr(-RelativeShift);
  }

  /// relative arithmetic shift right
  APInt relativeAShr(int RelativeShift) const {
    return RelativeShift > 0 ? ashr(RelativeShift) : shl(-RelativeShift);
  }

  /// relative arithmetic shift left
  APInt relativeAShl(int RelativeShift) const {
    return relativeAShr(-RelativeShift);
  }

  /// Rotate left by rotateAmt.
  APInt rotl(unsigned rotateAmt) const;

  /// Rotate right by rotateAmt.
  APInt rotr(unsigned rotateAmt) const;

  /// Arithmetic right-shift function.
  ///
  /// Arithmetic right-shift this APInt by shiftAmt.
  APInt ashr(const APInt &ShiftAmt) const {
    APInt R(*this);
    R.ashrInPlace(ShiftAmt);
    return R;
  }

  /// Arithmetic right-shift this APInt by shiftAmt in place.
  void ashrInPlace(const APInt &shiftAmt);

  /// Logical right-shift function.
  ///
  /// Logical right-shift this APInt by shiftAmt.
  APInt lshr(const APInt &ShiftAmt) const {
    APInt R(*this);
    R.lshrInPlace(ShiftAmt);
    return R;
  }

  /// Logical right-shift this APInt by ShiftAmt in place.
  void lshrInPlace(const APInt &ShiftAmt);

  /// Left-shift function.
  ///
  /// Left-shift this APInt by shiftAmt.
  APInt shl(const APInt &ShiftAmt) const {
    APInt R(*this);
    R <<= ShiftAmt;
    return R;
  }

  /// Rotate left by rotateAmt.
  APInt rotl(const APInt &rotateAmt) const;

  /// Rotate right by rotateAmt.
  APInt rotr(const APInt &rotateAmt) const;

  /// Concatenate the bits from "NewLSB" onto the bottom of *this.  This is
  /// equivalent to:
  ///   (this->zext(NewWidth) << NewLSB.getBitWidth()) | NewLSB.zext(NewWidth)
  APInt concat(const APInt &NewLSB) const {
    /// If the result will be small, then both the merged values are small.
    unsigned NewWidth = getBitWidth() + NewLSB.getBitWidth();
    if (NewWidth <= APINT_BITS_PER_WORD)
      return APInt(NewWidth, (U.VAL << NewLSB.getBitWidth()) | NewLSB.U.VAL);
    return concatSlowCase(NewLSB);
  }

  /// Unsigned division operation.
  ///
  /// Perform an unsigned divide operation on this APInt by RHS. Both this and
  /// RHS are treated as unsigned quantities for purposes of this division.
  ///
  /// \returns a new APInt value containing the division result, rounded towards
  /// zero.
  APInt udiv(const APInt &RHS) const;
  APInt udiv(uint64_t RHS) const;

  /// Signed division function for APInt.
  ///
  /// Signed divide this APInt by APInt RHS.
  ///
  /// The result is rounded towards zero.
  APInt sdiv(const APInt &RHS) const;
  APInt sdiv(int64_t RHS) const;

  /// Unsigned remainder operation.
  ///
  /// Perform an unsigned remainder operation on this APInt with RHS being the
  /// divisor. Both this and RHS are treated as unsigned quantities for purposes
  /// of this operation.
  ///
  /// \returns a new APInt value containing the remainder result
  APInt urem(const APInt &RHS) const;
  uint64_t urem(uint64_t RHS) const;

  /// Function for signed remainder operation.
  ///
  /// Signed remainder operation on APInt.
  ///
  /// Note that this is a true remainder operation and not a modulo operation
  /// because the sign follows the sign of the dividend which is *this.
  APInt srem(const APInt &RHS) const;
  int64_t srem(int64_t RHS) const;

  /// Dual division/remainder interface.
  ///
  /// Sometimes it is convenient to divide two APInt values and obtain both the
  /// quotient and remainder. This function does both operations in the same
  /// computation making it a little more efficient. The pair of input arguments
  /// may overlap with the pair of output arguments. It is safe to call
  /// udivrem(X, Y, X, Y), for example.
  static void udivrem(const APInt &LHS, const APInt &RHS, APInt &Quotient,
                      APInt &Remainder);
  static void udivrem(const APInt &LHS, uint64_t RHS, APInt &Quotient,
                      uint64_t &Remainder);

  static void sdivrem(const APInt &LHS, const APInt &RHS, APInt &Quotient,
                      APInt &Remainder);
  static void sdivrem(const APInt &LHS, int64_t RHS, APInt &Quotient,
                      int64_t &Remainder);

  // Operations that return overflow indicators.
  APInt sadd_ov(const APInt &RHS, bool &Overflow) const;
  APInt uadd_ov(const APInt &RHS, bool &Overflow) const;
  APInt ssub_ov(const APInt &RHS, bool &Overflow) const;
  APInt usub_ov(const APInt &RHS, bool &Overflow) const;
  APInt sdiv_ov(const APInt &RHS, bool &Overflow) const;
  APInt smul_ov(const APInt &RHS, bool &Overflow) const;
  APInt umul_ov(const APInt &RHS, bool &Overflow) const;
  APInt sshl_ov(const APInt &Amt, bool &Overflow) const;
  APInt sshl_ov(unsigned Amt, bool &Overflow) const;
  APInt ushl_ov(const APInt &Amt, bool &Overflow) const;
  APInt ushl_ov(unsigned Amt, bool &Overflow) const;

  /// Signed integer floor division operation.
  ///
  /// Rounds towards negative infinity, i.e. 5 / -2 = -3. Iff minimum value
  /// divided by -1 set Overflow to true.
  APInt sfloordiv_ov(const APInt &RHS, bool &Overflow) const;

  // Operations that saturate
  APInt sadd_sat(const APInt &RHS) const;
  APInt uadd_sat(const APInt &RHS) const;
  APInt ssub_sat(const APInt &RHS) const;
  APInt usub_sat(const APInt &RHS) const;
  APInt smul_sat(const APInt &RHS) const;
  APInt umul_sat(const APInt &RHS) const;
  APInt sshl_sat(const APInt &RHS) const;
  APInt sshl_sat(unsigned RHS) const;
  APInt ushl_sat(const APInt &RHS) const;
  APInt ushl_sat(unsigned RHS) const;

  /// Array-indexing support.
  ///
  /// \returns the bit value at bitPosition
  bool operator[](unsigned bitPosition) const {
    assert(bitPosition < getBitWidth() && "Bit position out of bounds!");
    return (maskBit(bitPosition) & getWord(bitPosition)) != 0;
  }

  /// @}
  /// \name Comparison Operators
  /// @{

  /// Equality operator.
  ///
  /// Compares this APInt with RHS for the validity of the equality
  /// relationship.
  bool operator==(const APInt &RHS) const {
    assert(BitWidth == RHS.BitWidth && "Comparison requires equal bit widths");
    if (isSingleWord())
      return U.VAL == RHS.U.VAL;
    return equalSlowCase(RHS);
  }

  /// Equality operator.
  ///
  /// Compares this APInt with a uint64_t for the validity of the equality
  /// relationship.
  ///
  /// \returns true if *this == Val
  bool operator==(uint64_t Val) const {
    return (isSingleWord() || getActiveBits() <= 64) && getZExtValue() == Val;
  }

  /// Equality comparison.
  ///
  /// Compares this APInt with RHS for the validity of the equality
  /// relationship.
  ///
  /// \returns true if *this == Val
  bool eq(const APInt &RHS) const { return (*this) == RHS; }

  /// Inequality operator.
  ///
  /// Compares this APInt with RHS for the validity of the inequality
  /// relationship.
  ///
  /// \returns true if *this != Val
  bool operator!=(const APInt &RHS) const { return !((*this) == RHS); }

  /// Inequality operator.
  ///
  /// Compares this APInt with a uint64_t for the validity of the inequality
  /// relationship.
  ///
  /// \returns true if *this != Val
  bool operator!=(uint64_t Val) const { return !((*this) == Val); }

  /// Inequality comparison
  ///
  /// Compares this APInt with RHS for the validity of the inequality
  /// relationship.
  ///
  /// \returns true if *this != Val
  bool ne(const APInt &RHS) const { return !((*this) == RHS); }

  /// Unsigned less than comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// the validity of the less-than relationship.
  ///
  /// \returns true if *this < RHS when both are considered unsigned.
  bool ult(const APInt &RHS) const { return compare(RHS) < 0; }

  /// Unsigned less than comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the less-than relationship.
  ///
  /// \returns true if *this < RHS when considered unsigned.
  bool ult(uint64_t RHS) const {
    // Only need to check active bits if not a single word.
    return (isSingleWord() || getActiveBits() <= 64) && getZExtValue() < RHS;
  }

  /// Signed less than comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for
  /// validity of the less-than relationship.
  ///
  /// \returns true if *this < RHS when both are considered signed.
  bool slt(const APInt &RHS) const { return compareSigned(RHS) < 0; }

  /// Signed less than comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for
  /// the validity of the less-than relationship.
  ///
  /// \returns true if *this < RHS when considered signed.
  bool slt(int64_t RHS) const {
    return (!isSingleWord() && getSignificantBits() > 64)
               ? isNegative()
               : getSExtValue() < RHS;
  }

  /// Unsigned less or equal comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// validity of the less-or-equal relationship.
  ///
  /// \returns true if *this <= RHS when both are considered unsigned.
  bool ule(const APInt &RHS) const { return compare(RHS) <= 0; }

  /// Unsigned less or equal comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the less-or-equal relationship.
  ///
  /// \returns true if *this <= RHS when considered unsigned.
  bool ule(uint64_t RHS) const { return !ugt(RHS); }

  /// Signed less or equal comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for
  /// validity of the less-or-equal relationship.
  ///
  /// \returns true if *this <= RHS when both are considered signed.
  bool sle(const APInt &RHS) const { return compareSigned(RHS) <= 0; }

  /// Signed less or equal comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for the
  /// validity of the less-or-equal relationship.
  ///
  /// \returns true if *this <= RHS when considered signed.
  bool sle(uint64_t RHS) const { return !sgt(RHS); }

  /// Unsigned greater than comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// the validity of the greater-than relationship.
  ///
  /// \returns true if *this > RHS when both are considered unsigned.
  bool ugt(const APInt &RHS) const { return !ule(RHS); }

  /// Unsigned greater than comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the greater-than relationship.
  ///
  /// \returns true if *this > RHS when considered unsigned.
  bool ugt(uint64_t RHS) const {
    // Only need to check active bits if not a single word.
    return (!isSingleWord() && getActiveBits() > 64) || getZExtValue() > RHS;
  }

  /// Signed greater than comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for the
  /// validity of the greater-than relationship.
  ///
  /// \returns true if *this > RHS when both are considered signed.
  bool sgt(const APInt &RHS) const { return !sle(RHS); }

  /// Signed greater than comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for
  /// the validity of the greater-than relationship.
  ///
  /// \returns true if *this > RHS when considered signed.
  bool sgt(int64_t RHS) const {
    return (!isSingleWord() && getSignificantBits() > 64)
               ? !isNegative()
               : getSExtValue() > RHS;
  }

  /// Unsigned greater or equal comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// validity of the greater-or-equal relationship.
  ///
  /// \returns true if *this >= RHS when both are considered unsigned.
  bool uge(const APInt &RHS) const { return !ult(RHS); }

  /// Unsigned greater or equal comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the greater-or-equal relationship.
  ///
  /// \returns true if *this >= RHS when considered unsigned.
  bool uge(uint64_t RHS) const { return !ult(RHS); }

  /// Signed greater or equal comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for
  /// validity of the greater-or-equal relationship.
  ///
  /// \returns true if *this >= RHS when both are considered signed.
  bool sge(const APInt &RHS) const { return !slt(RHS); }

  /// Signed greater or equal comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for
  /// the validity of the greater-or-equal relationship.
  ///
  /// \returns true if *this >= RHS when considered signed.
  bool sge(int64_t RHS) const { return !slt(RHS); }

  /// This operation tests if there are any pairs of corresponding bits
  /// between this APInt and RHS that are both set.
  bool intersects(const APInt &RHS) const {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      return (U.VAL & RHS.U.VAL) != 0;
    return intersectsSlowCase(RHS);
  }

  /// This operation checks that all bits set in this APInt are also set in RHS.
  bool isSubsetOf(const APInt &RHS) const {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      return (U.VAL & ~RHS.U.VAL) == 0;
    return isSubsetOfSlowCase(RHS);
  }

  /// @}
  /// \name Resizing Operators
  /// @{

  /// Truncate to new width.
  ///
  /// Truncate the APInt to a specified width. It is an error to specify a width
  /// that is greater than the current width.
  APInt trunc(unsigned width) const;

  /// Truncate to new width with unsigned saturation.
  ///
  /// If the APInt, treated as unsigned integer, can be losslessly truncated to
  /// the new bitwidth, then return truncated APInt. Else, return max value.
  APInt truncUSat(unsigned width) const;

  /// Truncate to new width with signed saturation.
  ///
  /// If this APInt, treated as signed integer, can be losslessly truncated to
  /// the new bitwidth, then return truncated APInt. Else, return either
  /// signed min value if the APInt was negative, or signed max value.
  APInt truncSSat(unsigned width) const;

  /// Sign extend to a new width.
  ///
  /// This operation sign extends the APInt to a new width. If the high order
  /// bit is set, the fill on the left will be done with 1 bits, otherwise zero.
  /// It is an error to specify a width that is less than the
  /// current width.
  APInt sext(unsigned width) const;

  /// Zero extend to a new width.
  ///
  /// This operation zero extends the APInt to a new width. The high order bits
  /// are filled with 0 bits.  It is an error to specify a width that is less
  /// than the current width.
  APInt zext(unsigned width) const;

  /// Sign extend or truncate to width
  ///
  /// Make this APInt have the bit width given by \p width. The value is sign
  /// extended, truncated, or left alone to make it that width.
  APInt sextOrTrunc(unsigned width) const;

  /// Zero extend or truncate to width
  ///
  /// Make this APInt have the bit width given by \p width. The value is zero
  /// extended, truncated, or left alone to make it that width.
  APInt zextOrTrunc(unsigned width) const;

  /// @}
  /// \name Bit Manipulation Operators
  /// @{

  /// Set every bit to 1.
  void setAllBits() {
    if (isSingleWord())
      U.VAL = WORDTYPE_MAX;
    else
      // Set all the bits in all the words.
      memset(U.pVal, -1, getNumWords() * APINT_WORD_SIZE);
    // Clear the unused ones
    clearUnusedBits();
  }

  /// Set the given bit to 1 whose position is given as "bitPosition".
  void setBit(unsigned BitPosition) {
    assert(BitPosition < BitWidth && "BitPosition out of range");
    WordType Mask = maskBit(BitPosition);
    if (isSingleWord())
      U.VAL |= Mask;
    else
      U.pVal[whichWord(BitPosition)] |= Mask;
  }

  /// Set the sign bit to 1.
  void setSignBit() { setBit(BitWidth - 1); }

  /// Set a given bit to a given value.
  void setBitVal(unsigned BitPosition, bool BitValue) {
    if (BitValue)
      setBit(BitPosition);
    else
      clearBit(BitPosition);
  }

  /// Set the bits from loBit (inclusive) to hiBit (exclusive) to 1.
  /// This function handles "wrap" case when \p loBit >= \p hiBit, and calls
  /// setBits when \p loBit < \p hiBit.
  /// For \p loBit == \p hiBit wrap case, set every bit to 1.
  void setBitsWithWrap(unsigned loBit, unsigned hiBit) {
    assert(hiBit <= BitWidth && "hiBit out of range");
    assert(loBit <= BitWidth && "loBit out of range");
    if (loBit < hiBit) {
      setBits(loBit, hiBit);
      return;
    }
    setLowBits(hiBit);
    setHighBits(BitWidth - loBit);
  }

  /// Set the bits from loBit (inclusive) to hiBit (exclusive) to 1.
  /// This function handles case when \p loBit <= \p hiBit.
  void setBits(unsigned loBit, unsigned hiBit) {
    assert(hiBit <= BitWidth && "hiBit out of range");
    assert(loBit <= BitWidth && "loBit out of range");
    assert(loBit <= hiBit && "loBit greater than hiBit");
    if (loBit == hiBit)
      return;
    if (loBit < APINT_BITS_PER_WORD && hiBit <= APINT_BITS_PER_WORD) {
      uint64_t mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - (hiBit - loBit));
      mask <<= loBit;
      if (isSingleWord())
        U.VAL |= mask;
      else
        U.pVal[0] |= mask;
    } else {
      setBitsSlowCase(loBit, hiBit);
    }
  }

  /// Set the top bits starting from loBit.
  void setBitsFrom(unsigned loBit) { return setBits(loBit, BitWidth); }

  /// Set the bottom loBits bits.
  void setLowBits(unsigned loBits) { return setBits(0, loBits); }

  /// Set the top hiBits bits.
  void setHighBits(unsigned hiBits) {
    return setBits(BitWidth - hiBits, BitWidth);
  }

  /// Set every bit to 0.
  void clearAllBits() {
    if (isSingleWord())
      U.VAL = 0;
    else
      memset(U.pVal, 0, getNumWords() * APINT_WORD_SIZE);
  }

  /// Set a given bit to 0.
  ///
  /// Set the given bit to 0 whose position is given as "bitPosition".
  void clearBit(unsigned BitPosition) {
    assert(BitPosition < BitWidth && "BitPosition out of range");
    WordType Mask = ~maskBit(BitPosition);
    if (isSingleWord())
      U.VAL &= Mask;
    else
      U.pVal[whichWord(BitPosition)] &= Mask;
  }

  /// Set bottom loBits bits to 0.
  void clearLowBits(unsigned loBits) {
    assert(loBits <= BitWidth && "More bits than bitwidth");
    APInt Keep = getHighBitsSet(BitWidth, BitWidth - loBits);
    *this &= Keep;
  }

  /// Set top hiBits bits to 0.
  void clearHighBits(unsigned hiBits) {
    assert(hiBits <= BitWidth && "More bits than bitwidth");
    APInt Keep = getLowBitsSet(BitWidth, BitWidth - hiBits);
    *this &= Keep;
  }

  /// Set the sign bit to 0.
  void clearSignBit() { clearBit(BitWidth - 1); }

  /// Toggle every bit to its opposite value.
  void flipAllBits() {
    if (isSingleWord()) {
      U.VAL ^= WORDTYPE_MAX;
      clearUnusedBits();
    } else {
      flipAllBitsSlowCase();
    }
  }

  /// Toggles a given bit to its opposite value.
  ///
  /// Toggle a given bit to its opposite value whose position is given
  /// as "bitPosition".
  void flipBit(unsigned bitPosition);

  /// Negate this APInt in place.
  void negate() {
    flipAllBits();
    ++(*this);
  }

  /// Insert the bits from a smaller APInt starting at bitPosition.
  void insertBits(const APInt &SubBits, unsigned bitPosition);
  void insertBits(uint64_t SubBits, unsigned bitPosition, unsigned numBits);

  /// Return an APInt with the extracted bits [bitPosition,bitPosition+numBits).
  APInt extractBits(unsigned numBits, unsigned bitPosition) const;
  uint64_t extractBitsAsZExtValue(unsigned numBits, unsigned bitPosition) const;

  /// @}
  /// \name Value Characterization Functions
  /// @{

  /// Return the number of bits in the APInt.
  unsigned getBitWidth() const { return BitWidth; }

  /// Get the number of words.
  ///
  /// Here one word's bitwidth equals to that of uint64_t.
  ///
  /// \returns the number of words to hold the integer value of this APInt.
  unsigned getNumWords() const { return getNumWords(BitWidth); }

  /// Get the number of words.
  ///
  /// *NOTE* Here one word's bitwidth equals to that of uint64_t.
  ///
  /// \returns the number of words to hold the integer value with a given bit
  /// width.
  static unsigned getNumWords(unsigned BitWidth) {
    return ((uint64_t)BitWidth + APINT_BITS_PER_WORD - 1) / APINT_BITS_PER_WORD;
  }

  /// Compute the number of active bits in the value
  ///
  /// This function returns the number of active bits which is defined as the
  /// bit width minus the number of leading zeros. This is used in several
  /// computations to see how "wide" the value is.
  unsigned getActiveBits() const { return BitWidth - countl_zero(); }

  /// Compute the number of active words in the value of this APInt.
  ///
  /// This is used in conjunction with getActiveData to extract the raw value of
  /// the APInt.
  unsigned getActiveWords() const {
    unsigned numActiveBits = getActiveBits();
    return numActiveBits ? whichWord(numActiveBits - 1) + 1 : 1;
  }

  /// Get the minimum bit size for this signed APInt
  ///
  /// Computes the minimum bit width for this APInt while considering it to be a
  /// signed (and probably negative) value. If the value is not negative, this
  /// function returns the same value as getActiveBits()+1. Otherwise, it
  /// returns the smallest bit width that will retain the negative value. For
  /// example, -1 can be written as 0b1 or 0xFFFFFFFFFF. 0b1 is shorter and so
  /// for -1, this function will always return 1.
  unsigned getSignificantBits() const {
    return BitWidth - getNumSignBits() + 1;
  }

  /// Get zero extended value
  ///
  /// This method attempts to return the value of this APInt as a zero extended
  /// uint64_t. The bitwidth must be <= 64 or the value must fit within a
  /// uint64_t. Otherwise an assertion will result.
  uint64_t getZExtValue() const {
    if (isSingleWord())
      return U.VAL;
    assert(getActiveBits() <= 64 && "Too many bits for uint64_t");
    return U.pVal[0];
  }

  /// Get zero extended value if possible
  ///
  /// This method attempts to return the value of this APInt as a zero extended
  /// uint64_t. The bitwidth must be <= 64 or the value must fit within a
  /// uint64_t. Otherwise no value is returned.
  std::optional<uint64_t> tryZExtValue() const {
    return (getActiveBits() <= 64) ? std::optional<uint64_t>(getZExtValue())
                                   : std::nullopt;
  };

  /// Get sign extended value
  ///
  /// This method attempts to return the value of this APInt as a sign extended
  /// int64_t. The bit width must be <= 64 or the value must fit within an
  /// int64_t. Otherwise an assertion will result.
  int64_t getSExtValue() const {
    if (isSingleWord())
      return SignExtend64(U.VAL, BitWidth);
    assert(getSignificantBits() <= 64 && "Too many bits for int64_t");
    return int64_t(U.pVal[0]);
  }

  /// Get sign extended value if possible
  ///
  /// This method attempts to return the value of this APInt as a sign extended
  /// int64_t. The bitwidth must be <= 64 or the value must fit within an
  /// int64_t. Otherwise no value is returned.
  std::optional<int64_t> trySExtValue() const {
    return (getSignificantBits() <= 64) ? std::optional<int64_t>(getSExtValue())
                                        : std::nullopt;
  };

  /// Get bits required for string value.
  ///
  /// This method determines how many bits are required to hold the APInt
  /// equivalent of the string given by \p str.
  static unsigned getBitsNeeded(StringRef str, uint8_t radix);

  /// Get the bits that are sufficient to represent the string value. This may
  /// over estimate the amount of bits required, but it does not require
  /// parsing the value in the string.
  static unsigned getSufficientBitsNeeded(StringRef Str, uint8_t Radix);

  /// The APInt version of std::countl_zero.
  ///
  /// It counts the number of zeros from the most significant bit to the first
  /// one bit.
  ///
  /// \returns BitWidth if the value is zero, otherwise returns the number of
  ///   zeros from the most significant bit to the first one bits.
  unsigned countl_zero() const {
    if (isSingleWord()) {
      unsigned unusedBits = APINT_BITS_PER_WORD - BitWidth;
      return llvm::countl_zero(U.VAL) - unusedBits;
    }
    return countLeadingZerosSlowCase();
  }

  unsigned countLeadingZeros() const { return countl_zero(); }

  /// Count the number of leading one bits.
  ///
  /// This function is an APInt version of std::countl_one. It counts the number
  /// of ones from the most significant bit to the first zero bit.
  ///
  /// \returns 0 if the high order bit is not set, otherwise returns the number
  /// of 1 bits from the most significant to the least
  unsigned countl_one() const {
    if (isSingleWord()) {
      if (LLVM_UNLIKELY(BitWidth == 0))
        return 0;
      return llvm::countl_one(U.VAL << (APINT_BITS_PER_WORD - BitWidth));
    }
    return countLeadingOnesSlowCase();
  }

  unsigned countLeadingOnes() const { return countl_one(); }

  /// Computes the number of leading bits of this APInt that are equal to its
  /// sign bit.
  unsigned getNumSignBits() const {
    return isNegative() ? countl_one() : countl_zero();
  }

  /// Count the number of trailing zero bits.
  ///
  /// This function is an APInt version of std::countr_zero. It counts the
  /// number of zeros from the least significant bit to the first set bit.
  ///
  /// \returns BitWidth if the value is zero, otherwise returns the number of
  /// zeros from the least significant bit to the first one bit.
  unsigned countr_zero() const {
    if (isSingleWord()) {
      unsigned TrailingZeros = llvm::countr_zero(U.VAL);
      return (TrailingZeros > BitWidth ? BitWidth : TrailingZeros);
    }
    return countTrailingZerosSlowCase();
  }

  unsigned countTrailingZeros() const { return countr_zero(); }

  /// Count the number of trailing one bits.
  ///
  /// This function is an APInt version of std::countr_one. It counts the number
  /// of ones from the least significant bit to the first zero bit.
  ///
  /// \returns BitWidth if the value is all ones, otherwise returns the number
  /// of ones from the least significant bit to the first zero bit.
  unsigned countr_one() const {
    if (isSingleWord())
      return llvm::countr_one(U.VAL);
    return countTrailingOnesSlowCase();
  }

  unsigned countTrailingOnes() const { return countr_one(); }

  /// Count the number of bits set.
  ///
  /// This function is an APInt version of std::popcount. It counts the number
  /// of 1 bits in the APInt value.
  ///
  /// \returns 0 if the value is zero, otherwise returns the number of set bits.
  unsigned popcount() const {
    if (isSingleWord())
      return llvm::popcount(U.VAL);
    return countPopulationSlowCase();
  }

  /// @}
  /// \name Conversion Functions
  /// @{
  void print(raw_ostream &OS, bool isSigned) const;

  /// Converts an APInt to a string and append it to Str.  Str is commonly a
  /// SmallString. If Radix > 10, UpperCase determine the case of letter
  /// digits.
  void toString(SmallVectorImpl<char> &Str, unsigned Radix, bool Signed,
                bool formatAsCLiteral = false, bool UpperCase = true,
                bool InsertSeparators = false) const;

  /// Considers the APInt to be unsigned and converts it into a string in the
  /// radix given. The radix can be 2, 8, 10 16, or 36.
  void toStringUnsigned(SmallVectorImpl<char> &Str, unsigned Radix = 10) const {
    toString(Str, Radix, false, false);
  }

  /// Considers the APInt to be signed and converts it into a string in the
  /// radix given. The radix can be 2, 8, 10, 16, or 36.
  void toStringSigned(SmallVectorImpl<char> &Str, unsigned Radix = 10) const {
    toString(Str, Radix, true, false);
  }

  /// \returns a byte-swapped representation of this APInt Value.
  APInt byteSwap() const;

  /// \returns the value with the bit representation reversed of this APInt
  /// Value.
  APInt reverseBits() const;

  /// Converts this APInt to a double value.
  double roundToDouble(bool isSigned) const;

  /// Converts this unsigned APInt to a double value.
  double roundToDouble() const { return roundToDouble(false); }

  /// Converts this signed APInt to a double value.
  double signedRoundToDouble() const { return roundToDouble(true); }

  /// Converts APInt bits to a double
  ///
  /// The conversion does not do a translation from integer to double, it just
  /// re-interprets the bits as a double. Note that it is valid to do this on
  /// any bit width. Exactly 64 bits will be translated.
  double bitsToDouble() const { return llvm::bit_cast<double>(getWord(0)); }

#ifdef HAS_IEE754_FLOAT128
  float128 bitsToQuad() const {
    __uint128_t ul = ((__uint128_t)U.pVal[1] << 64) + U.pVal[0];
    return llvm::bit_cast<float128>(ul);
  }
#endif

  /// Converts APInt bits to a float
  ///
  /// The conversion does not do a translation from integer to float, it just
  /// re-interprets the bits as a float. Note that it is valid to do this on
  /// any bit width. Exactly 32 bits will be translated.
  float bitsToFloat() const {
    return llvm::bit_cast<float>(static_cast<uint32_t>(getWord(0)));
  }

  /// Converts a double to APInt bits.
  ///
  /// The conversion does not do a translation from double to integer, it just
  /// re-interprets the bits of the double.
  static APInt doubleToBits(double V) {
    return APInt(sizeof(double) * CHAR_BIT, llvm::bit_cast<uint64_t>(V));
  }

  /// Converts a float to APInt bits.
  ///
  /// The conversion does not do a translation from float to integer, it just
  /// re-interprets the bits of the float.
  static APInt floatToBits(float V) {
    return APInt(sizeof(float) * CHAR_BIT, llvm::bit_cast<uint32_t>(V));
  }

  /// @}
  /// \name Mathematics Operations
  /// @{

  /// \returns the floor log base 2 of this APInt.
  unsigned logBase2() const { return getActiveBits() - 1; }

  /// \returns the ceil log base 2 of this APInt.
  unsigned ceilLogBase2() const {
    APInt temp(*this);
    --temp;
    return temp.getActiveBits();
  }

  /// \returns the nearest log base 2 of this APInt. Ties round up.
  ///
  /// NOTE: When we have a BitWidth of 1, we define:
  ///
  ///   log2(0) = UINT32_MAX
  ///   log2(1) = 0
  ///
  /// to get around any mathematical concerns resulting from
  /// referencing 2 in a space where 2 does no exist.
  unsigned nearestLogBase2() const;

  /// \returns the log base 2 of this APInt if its an exact power of two, -1
  /// otherwise
  int32_t exactLogBase2() const {
    if (!isPowerOf2())
      return -1;
    return logBase2();
  }

  /// Compute the square root.
  APInt sqrt() const;

  /// Get the absolute value.  If *this is < 0 then return -(*this), otherwise
  /// *this.  Note that the "most negative" signed number (e.g. -128 for 8 bit
  /// wide APInt) is unchanged due to how negation works.
  APInt abs() const {
    if (isNegative())
      return -(*this);
    return *this;
  }

  /// \returns the multiplicative inverse of an odd APInt modulo 2^BitWidth.
  APInt multiplicativeInverse() const;

  /// @}
  /// \name Building-block Operations for APInt and APFloat
  /// @{

  // These building block operations operate on a representation of arbitrary
  // precision, two's-complement, bignum integer values. They should be
  // sufficient to implement APInt and APFloat bignum requirements. Inputs are
  // generally a pointer to the base of an array of integer parts, representing
  // an unsigned bignum, and a count of how many parts there are.

  /// Sets the least significant part of a bignum to the input value, and zeroes
  /// out higher parts.
  static void tcSet(WordType *, WordType, unsigned);

  /// Assign one bignum to another.
  static void tcAssign(WordType *, const WordType *, unsigned);

  /// Returns true if a bignum is zero, false otherwise.
  static bool tcIsZero(const WordType *, unsigned);

  /// Extract the given bit of a bignum; returns 0 or 1.  Zero-based.
  static int tcExtractBit(const WordType *, unsigned bit);

  /// Copy the bit vector of width srcBITS from SRC, starting at bit srcLSB, to
  /// DST, of dstCOUNT parts, such that the bit srcLSB becomes the least
  /// significant bit of DST.  All high bits above srcBITS in DST are
  /// zero-filled.
  static void tcExtract(WordType *, unsigned dstCount, const WordType *,
                        unsigned srcBits, unsigned srcLSB);

  /// Set the given bit of a bignum.  Zero-based.
  static void tcSetBit(WordType *, unsigned bit);

  /// Clear the given bit of a bignum.  Zero-based.
  static void tcClearBit(WordType *, unsigned bit);

  /// Returns the bit number of the least or most significant set bit of a
  /// number.  If the input number has no bits set -1U is returned.
  static unsigned tcLSB(const WordType *, unsigned n);
  static unsigned tcMSB(const WordType *parts, unsigned n);

  /// Negate a bignum in-place.
  static void tcNegate(WordType *, unsigned);

  /// DST += RHS + CARRY where CARRY is zero or one.  Returns the carry flag.
  static WordType tcAdd(WordType *, const WordType *, WordType carry, unsigned);
  /// DST += RHS.  Returns the carry flag.
  static WordType tcAddPart(WordType *, WordType, unsigned);

  /// DST -= RHS + CARRY where CARRY is zero or one. Returns the carry flag.
  static WordType tcSubtract(WordType *, const WordType *, WordType carry,
                             unsigned);
  /// DST -= RHS.  Returns the carry flag.
  static WordType tcSubtractPart(WordType *, WordType, unsigned);

  /// DST += SRC * MULTIPLIER + PART   if add is true
  /// DST  = SRC * MULTIPLIER + PART   if add is false
  ///
  /// Requires 0 <= DSTPARTS <= SRCPARTS + 1.  If DST overlaps SRC they must
  /// start at the same point, i.e. DST == SRC.
  ///
  /// If DSTPARTS == SRC_PARTS + 1 no overflow occurs and zero is returned.
  /// Otherwise DST is filled with the least significant DSTPARTS parts of the
  /// result, and if all of the omitted higher parts were zero return zero,
  /// otherwise overflow occurred and return one.
  static int tcMultiplyPart(WordType *dst, const WordType *src,
                            WordType multiplier, WordType carry,
                            unsigned srcParts, unsigned dstParts, bool add);

  /// DST = LHS * RHS, where DST has the same width as the operands and is
  /// filled with the least significant parts of the result.  Returns one if
  /// overflow occurred, otherwise zero.  DST must be disjoint from both
  /// operands.
  static int tcMultiply(WordType *, const WordType *, const WordType *,
                        unsigned);

  /// DST = LHS * RHS, where DST has width the sum of the widths of the
  /// operands. No overflow occurs. DST must be disjoint from both operands.
  static void tcFullMultiply(WordType *, const WordType *, const WordType *,
                             unsigned, unsigned);

  /// If RHS is zero LHS and REMAINDER are left unchanged, return one.
  /// Otherwise set LHS to LHS / RHS with the fractional part discarded, set
  /// REMAINDER to the remainder, return zero.  i.e.
  ///
  ///  OLD_LHS = RHS * LHS + REMAINDER
  ///
  /// SCRATCH is a bignum of the same size as the operands and result for use by
  /// the routine; its contents need not be initialized and are destroyed.  LHS,
  /// REMAINDER and SCRATCH must be distinct.
  static int tcDivide(WordType *lhs, const WordType *rhs, WordType *remainder,
                      WordType *scratch, unsigned parts);

  /// Shift a bignum left Count bits. Shifted in bits are zero. There are no
  /// restrictions on Count.
  static void tcShiftLeft(WordType *, unsigned Words, unsigned Count);

  /// Shift a bignum right Count bits.  Shifted in bits are zero.  There are no
  /// restrictions on Count.
  static void tcShiftRight(WordType *, unsigned Words, unsigned Count);

  /// Comparison (unsigned) of two bignums.
  static int tcCompare(const WordType *, const WordType *, unsigned);

  /// Increment a bignum in-place.  Return the carry flag.
  static WordType tcIncrement(WordType *dst, unsigned parts) {
    return tcAddPart(dst, 1, parts);
  }

  /// Decrement a bignum in-place.  Return the borrow flag.
  static WordType tcDecrement(WordType *dst, unsigned parts) {
    return tcSubtractPart(dst, 1, parts);
  }

  /// Used to insert APInt objects, or objects that contain APInt objects, into
  ///  FoldingSets.
  void Profile(FoldingSetNodeID &id) const;

  /// debug method
  void dump() const;

  /// Returns whether this instance allocated memory.
  bool needsCleanup() const { return !isSingleWord(); }

private:
  /// This union is used to store the integer value. When the
  /// integer bit-width <= 64, it uses VAL, otherwise it uses pVal.
  union {
    uint64_t VAL;   ///< Used to store the <= 64 bits integer value.
    uint64_t *pVal; ///< Used to store the >64 bits integer value.
  } U;

  unsigned BitWidth = 1; ///< The number of bits in this APInt.

  friend struct DenseMapInfo<APInt, void>;
  friend class APSInt;

  // Make DynamicAPInt a friend so it can access BitWidth directly.
  friend DynamicAPInt;

  /// This constructor is used only internally for speed of construction of
  /// temporaries. It is unsafe since it takes ownership of the pointer, so it
  /// is not public.
  APInt(uint64_t *val, unsigned bits) : BitWidth(bits) { U.pVal = val; }

  /// Determine which word a bit is in.
  ///
  /// \returns the word position for the specified bit position.
  static unsigned whichWord(unsigned bitPosition) {
    return bitPosition / APINT_BITS_PER_WORD;
  }

  /// Determine which bit in a word the specified bit position is in.
  static unsigned whichBit(unsigned bitPosition) {
    return bitPosition % APINT_BITS_PER_WORD;
  }

  /// Get a single bit mask.
  ///
  /// \returns a uint64_t with only bit at "whichBit(bitPosition)" set
  /// This method generates and returns a uint64_t (word) mask for a single
  /// bit at a specific bit position. This is used to mask the bit in the
  /// corresponding word.
  static uint64_t maskBit(unsigned bitPosition) {
    return 1ULL << whichBit(bitPosition);
  }

  /// Clear unused high order bits
  ///
  /// This method is used internally to clear the top "N" bits in the high order
  /// word that are not used by the APInt. This is needed after the most
  /// significant word is assigned a value to ensure that those bits are
  /// zero'd out.
  APInt &clearUnusedBits() {
    // Compute how many bits are used in the final word.
    unsigned WordBits = ((BitWidth - 1) % APINT_BITS_PER_WORD) + 1;

    // Mask out the high bits.
    uint64_t mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - WordBits);
    if (LLVM_UNLIKELY(BitWidth == 0))
      mask = 0;

    if (isSingleWord())
      U.VAL &= mask;
    else
      U.pVal[getNumWords() - 1] &= mask;
    return *this;
  }

  /// Get the word corresponding to a bit position
  /// \returns the corresponding word for the specified bit position.
  uint64_t getWord(unsigned bitPosition) const {
    return isSingleWord() ? U.VAL : U.pVal[whichWord(bitPosition)];
  }

  /// Utility method to change the bit width of this APInt to new bit width,
  /// allocating and/or deallocating as necessary. There is no guarantee on the
  /// value of any bits upon return. Caller should populate the bits after.
  void reallocate(unsigned NewBitWidth);

  /// Convert a char array into an APInt
  ///
  /// \param radix 2, 8, 10, 16, or 36
  /// Converts a string into a number.  The string must be non-empty
  /// and well-formed as a number of the given base. The bit-width
  /// must be sufficient to hold the result.
  ///
  /// This is used by the constructors that take string arguments.
  ///
  /// StringRef::getAsInteger is superficially similar but (1) does
  /// not assume that the string is well-formed and (2) grows the
  /// result to hold the input.
  void fromString(unsigned numBits, StringRef str, uint8_t radix);

  /// An internal division function for dividing APInts.
  ///
  /// This is used by the toString method to divide by the radix. It simply
  /// provides a more convenient form of divide for internal use since KnuthDiv
  /// has specific constraints on its inputs. If those constraints are not met
  /// then it provides a simpler form of divide.
  static void divide(const WordType *LHS, unsigned lhsWords,
                     const WordType *RHS, unsigned rhsWords, WordType *Quotient,
                     WordType *Remainder);

  /// out-of-line slow case for inline constructor
  void initSlowCase(uint64_t val, bool isSigned);

  /// shared code between two array constructors
  void initFromArray(ArrayRef<uint64_t> array);

  /// out-of-line slow case for inline copy constructor
  void initSlowCase(const APInt &that);

  /// out-of-line slow case for shl
  void shlSlowCase(unsigned ShiftAmt);

  /// out-of-line slow case for lshr.
  void lshrSlowCase(unsigned ShiftAmt);

  /// out-of-line slow case for ashr.
  void ashrSlowCase(unsigned ShiftAmt);

  /// out-of-line slow case for operator=
  void assignSlowCase(const APInt &RHS);

  /// out-of-line slow case for operator==
  bool equalSlowCase(const APInt &RHS) const LLVM_READONLY;

  /// out-of-line slow case for countLeadingZeros
  unsigned countLeadingZerosSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countLeadingOnes.
  unsigned countLeadingOnesSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countTrailingZeros.
  unsigned countTrailingZerosSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countTrailingOnes
  unsigned countTrailingOnesSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countPopulation
  unsigned countPopulationSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for intersects.
  bool intersectsSlowCase(const APInt &RHS) const LLVM_READONLY;

  /// out-of-line slow case for isSubsetOf.
  bool isSubsetOfSlowCase(const APInt &RHS) const LLVM_READONLY;

  /// out-of-line slow case for setBits.
  void setBitsSlowCase(unsigned loBit, unsigned hiBit);

  /// out-of-line slow case for flipAllBits.
  void flipAllBitsSlowCase();

  /// out-of-line slow case for concat.
  APInt concatSlowCase(const APInt &NewLSB) const;

  /// out-of-line slow case for operator&=.
  void andAssignSlowCase(const APInt &RHS);

  /// out-of-line slow case for operator|=.
  void orAssignSlowCase(const APInt &RHS);

  /// out-of-line slow case for operator^=.
  void xorAssignSlowCase(const APInt &RHS);

  /// Unsigned comparison. Returns -1, 0, or 1 if this APInt is less than, equal
  /// to, or greater than RHS.
  int compare(const APInt &RHS) const LLVM_READONLY;

  /// Signed comparison. Returns -1, 0, or 1 if this APInt is less than, equal
  /// to, or greater than RHS.
  int compareSigned(const APInt &RHS) const LLVM_READONLY;

  /// @}
};

inline bool operator==(uint64_t V1, const APInt &V2) { return V2 == V1; }

inline bool operator!=(uint64_t V1, const APInt &V2) { return V2 != V1; }

/// Unary bitwise complement operator.
///
/// \returns an APInt that is the bitwise complement of \p v.
inline APInt operator~(APInt v) {
  v.flipAllBits();
  return v;
}

inline APInt operator&(APInt a, const APInt &b) {
  a &= b;
  return a;
}

inline APInt operator&(const APInt &a, APInt &&b) {
  b &= a;
  return std::move(b);
}

inline APInt operator&(APInt a, uint64_t RHS) {
  a &= RHS;
  return a;
}

inline APInt operator&(uint64_t LHS, APInt b) {
  b &= LHS;
  return b;
}

inline APInt operator|(APInt a, const APInt &b) {
  a |= b;
  return a;
}

inline APInt operator|(const APInt &a, APInt &&b) {
  b |= a;
  return std::move(b);
}

inline APInt operator|(APInt a, uint64_t RHS) {
  a |= RHS;
  return a;
}

inline APInt operator|(uint64_t LHS, APInt b) {
  b |= LHS;
  return b;
}

inline APInt operator^(APInt a, const APInt &b) {
  a ^= b;
  return a;
}

inline APInt operator^(const APInt &a, APInt &&b) {
  b ^= a;
  return std::move(b);
}

inline APInt operator^(APInt a, uint64_t RHS) {
  a ^= RHS;
  return a;
}

inline APInt operator^(uint64_t LHS, APInt b) {
  b ^= LHS;
  return b;
}

inline raw_ostream &operator<<(raw_ostream &OS, const APInt &I) {
  I.print(OS, true);
  return OS;
}

inline APInt operator-(APInt v) {
  v.negate();
  return v;
}

inline APInt operator+(APInt a, const APInt &b) {
  a += b;
  return a;
}

inline APInt operator+(const APInt &a, APInt &&b) {
  b += a;
  return std::move(b);
}

inline APInt operator+(APInt a, uint64_t RHS) {
  a += RHS;
  return a;
}

inline APInt operator+(uint64_t LHS, APInt b) {
  b += LHS;
  return b;
}

inline APInt operator-(APInt a, const APInt &b) {
  a -= b;
  return a;
}

inline APInt operator-(const APInt &a, APInt &&b) {
  b.negate();
  b += a;
  return std::move(b);
}

inline APInt operator-(APInt a, uint64_t RHS) {
  a -= RHS;
  return a;
}

inline APInt operator-(uint64_t LHS, APInt b) {
  b.negate();
  b += LHS;
  return b;
}

inline APInt operator*(APInt a, uint64_t RHS) {
  a *= RHS;
  return a;
}

inline APInt operator*(uint64_t LHS, APInt b) {
  b *= LHS;
  return b;
}

namespace APIntOps {

/// Determine the smaller of two APInts considered to be signed.
inline const APInt &smin(const APInt &A, const APInt &B) {
  return A.slt(B) ? A : B;
}

/// Determine the larger of two APInts considered to be signed.
inline const APInt &smax(const APInt &A, const APInt &B) {
  return A.sgt(B) ? A : B;
}

/// Determine the smaller of two APInts considered to be unsigned.
inline const APInt &umin(const APInt &A, const APInt &B) {
  return A.ult(B) ? A : B;
}

/// Determine the larger of two APInts considered to be unsigned.
inline const APInt &umax(const APInt &A, const APInt &B) {
  return A.ugt(B) ? A : B;
}

/// Determine the absolute difference of two APInts considered to be signed.
inline const APInt abds(const APInt &A, const APInt &B) {
  return A.sge(B) ? (A - B) : (B - A);
}

/// Determine the absolute difference of two APInts considered to be unsigned.
inline const APInt abdu(const APInt &A, const APInt &B) {
  return A.uge(B) ? (A - B) : (B - A);
}

/// Compute the floor of the signed average of C1 and C2
APInt avgFloorS(const APInt &C1, const APInt &C2);

/// Compute the floor of the unsigned average of C1 and C2
APInt avgFloorU(const APInt &C1, const APInt &C2);

/// Compute the ceil of the signed average of C1 and C2
APInt avgCeilS(const APInt &C1, const APInt &C2);

/// Compute the ceil of the unsigned average of C1 and C2
APInt avgCeilU(const APInt &C1, const APInt &C2);

/// Performs (2*N)-bit multiplication on sign-extended operands.
/// Returns the high N bits of the multiplication result.
APInt mulhs(const APInt &C1, const APInt &C2);

/// Performs (2*N)-bit multiplication on zero-extended operands.
/// Returns the high N bits of the multiplication result.
APInt mulhu(const APInt &C1, const APInt &C2);

/// Compute GCD of two unsigned APInt values.
///
/// This function returns the greatest common divisor of the two APInt values
/// using Stein's algorithm.
///
/// \returns the greatest common divisor of A and B.
APInt GreatestCommonDivisor(APInt A, APInt B);

/// Converts the given APInt to a double value.
///
/// Treats the APInt as an unsigned value for conversion purposes.
inline double RoundAPIntToDouble(const APInt &APIVal) {
  return APIVal.roundToDouble();
}

/// Converts the given APInt to a double value.
///
/// Treats the APInt as a signed value for conversion purposes.
inline double RoundSignedAPIntToDouble(const APInt &APIVal) {
  return APIVal.signedRoundToDouble();
}

/// Converts the given APInt to a float value.
inline float RoundAPIntToFloat(const APInt &APIVal) {
  return float(RoundAPIntToDouble(APIVal));
}

/// Converts the given APInt to a float value.
///
/// Treats the APInt as a signed value for conversion purposes.
inline float RoundSignedAPIntToFloat(const APInt &APIVal) {
  return float(APIVal.signedRoundToDouble());
}

/// Converts the given double value into a APInt.
///
/// This function convert a double value to an APInt value.
APInt RoundDoubleToAPInt(double Double, unsigned width);

/// Converts a float value into a APInt.
///
/// Converts a float value into an APInt value.
inline APInt RoundFloatToAPInt(float Float, unsigned width) {
  return RoundDoubleToAPInt(double(Float), width);
}

/// Return A unsign-divided by B, rounded by the given rounding mode.
APInt RoundingUDiv(const APInt &A, const APInt &B, APInt::Rounding RM);

/// Return A sign-divided by B, rounded by the given rounding mode.
APInt RoundingSDiv(const APInt &A, const APInt &B, APInt::Rounding RM);

/// Let q(n) = An^2 + Bn + C, and BW = bit width of the value range
/// (e.g. 32 for i32).
/// This function finds the smallest number n, such that
/// (a) n >= 0 and q(n) = 0, or
/// (b) n >= 1 and q(n-1) and q(n), when evaluated in the set of all
///     integers, belong to two different intervals [Rk, Rk+R),
///     where R = 2^BW, and k is an integer.
/// The idea here is to find when q(n) "overflows" 2^BW, while at the
/// same time "allowing" subtraction. In unsigned modulo arithmetic a
/// subtraction (treated as addition of negated numbers) would always
/// count as an overflow, but here we want to allow values to decrease
/// and increase as long as they are within the same interval.
/// Specifically, adding of two negative numbers should not cause an
/// overflow (as long as the magnitude does not exceed the bit width).
/// On the other hand, given a positive number, adding a negative
/// number to it can give a negative result, which would cause the
/// value to go from [-2^BW, 0) to [0, 2^BW). In that sense, zero is
/// treated as a special case of an overflow.
///
/// This function returns std::nullopt if after finding k that minimizes the
/// positive solution to q(n) = kR, both solutions are contained between
/// two consecutive integers.
///
/// There are cases where q(n) > T, and q(n+1) < T (assuming evaluation
/// in arithmetic modulo 2^BW, and treating the values as signed) by the
/// virtue of *signed* overflow. This function will *not* find such an n,
/// however it may find a value of n satisfying the inequalities due to
/// an *unsigned* overflow (if the values are treated as unsigned).
/// To find a solution for a signed overflow, treat it as a problem of
/// finding an unsigned overflow with a range with of BW-1.
///
/// The returned value may have a different bit width from the input
/// coefficients.
std::optional<APInt> SolveQuadraticEquationWrap(APInt A, APInt B, APInt C,
                                                unsigned RangeWidth);

/// Compare two values, and if they are different, return the position of the
/// most significant bit that is different in the values.
std::optional<unsigned> GetMostSignificantDifferentBit(const APInt &A,
                                                       const APInt &B);

/// Splat/Merge neighboring bits to widen/narrow the bitmask represented
/// by \param A to \param NewBitWidth bits.
///
/// MatchAnyBits: (Default)
/// e.g. ScaleBitMask(0b0101, 8) -> 0b00110011
/// e.g. ScaleBitMask(0b00011011, 4) -> 0b0111
///
/// MatchAllBits:
/// e.g. ScaleBitMask(0b0101, 8) -> 0b00110011
/// e.g. ScaleBitMask(0b00011011, 4) -> 0b0001
/// A.getBitwidth() or NewBitWidth must be a whole multiples of the other.
APInt ScaleBitMask(const APInt &A, unsigned NewBitWidth,
                   bool MatchAllBits = false);
} // namespace APIntOps

// See friend declaration above. This additional declaration is required in
// order to compile LLVM with IBM xlC compiler.
hash_code hash_value(const APInt &Arg);

/// StoreIntToMemory - Fills the StoreBytes bytes of memory starting from Dst
/// with the integer held in IntVal.
void StoreIntToMemory(const APInt &IntVal, uint8_t *Dst, unsigned StoreBytes);

/// LoadIntFromMemory - Loads the integer stored in the LoadBytes bytes starting
/// from Src into IntVal, which is assumed to be wide enough and to hold zero.
void LoadIntFromMemory(APInt &IntVal, const uint8_t *Src, unsigned LoadBytes);

/// Provide DenseMapInfo for APInt.
template <> struct DenseMapInfo<APInt, void> {
  static inline APInt getEmptyKey() {
    APInt V(nullptr, 0);
    V.U.VAL = ~0ULL;
    return V;
  }

  static inline APInt getTombstoneKey() {
    APInt V(nullptr, 0);
    V.U.VAL = ~1ULL;
    return V;
  }

  static unsigned getHashValue(const APInt &Key);

  static bool isEqual(const APInt &LHS, const APInt &RHS) {
    return LHS.getBitWidth() == RHS.getBitWidth() && LHS == RHS;
  }
};

} // namespace llvm

#endif
