//===-- IntervalTree.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements an interval tree.
//
// Further information:
// https://en.wikipedia.org/wiki/Interval_tree
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_INTERVALTREE_H
#define LLVM_ADT_INTERVALTREE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>

// IntervalTree is a light tree data structure to hold intervals. It allows
// finding all intervals that overlap with any given point. At this time,
// it does not support any deletion or rebalancing operations.
//
// The IntervalTree is designed to be set up once, and then queried without
// any further additions.
//
// Synopsis:
//   Closed intervals delimited by PointT objects are mapped to ValueT objects.
//
// Restrictions:
//   PointT must be a fundamental type.
//   ValueT must be a fundamental or pointer type.
//
// template <typename PointT, typename ValueT, typename DataT>
// class IntervalTree {
// public:
//
//   IntervalTree();
//   ~IntervalTree():
//
//   using IntervalReferences = SmallVector<IntervalData *>;
//
//   void create();
//   void insert(PointT Left, PointT Right, ValueT Value);
//
//   IntervalReferences getContaining(PointT Point);
//   static void sortIntervals(IntervalReferences &Intervals, Sorting Sort);
//
//   find_iterator begin(PointType Point) const;
//   find_iterator end() const;
//
//   bool empty() const;
//   void clear();
//
//   void print(raw_ostream &OS, bool HexFormat = true);
// };
//
//===----------------------------------------------------------------------===//
//
// In the below given dataset
//
//   [a, b] <- (x)
//
// 'a' and 'b' describe a range and 'x' the value for that interval.
//
// The following data are purely for illustrative purposes:
//
// [30, 35] <- (3035),    [39, 50] <- (3950),    [55, 61] <- (5561),
// [31, 56] <- (3156),    [12, 21] <- (1221),    [25, 41] <- (2541),
// [49, 65] <- (4965),    [71, 79] <- (7179),    [11, 16] <- (1116),
// [20, 30] <- (2030),    [36, 54] <- (3654),    [60, 70] <- (6070),
// [74, 80] <- (7480),    [15, 40] <- (1540),    [43, 43] <- (4343),
// [50, 75] <- (5075),    [10, 85] <- (1085)
//
// The data represents a set of overlapping intervals:
//
//                    30--35  39------------50  55----61
//                      31------------------------56
//     12--------21 25------------41      49-------------65   71-----79
//   11----16  20-----30    36----------------54    60------70  74---- 80
//       15---------------------40  43--43  50--------------------75
// 10----------------------------------------------------------------------85
//
// The items are stored in a binary tree with each node storing:
//
// MP: A middle point.
// IL: All intervals whose left value are completely to the left of the middle
//     point. They are sorted in ascending order by their beginning point.
// IR: All intervals whose right value are completely to the right of the
//     middle point. They are sorted in descending order by their ending point.
// LS: Left subtree.
// RS: Right subtree.
//
// As IL and IR will contain the same intervals, in order to optimize space,
// instead of storing intervals on each node, we use two vectors that will
// contain the intervals described by IL and IR. Each node will contain an
// index into that vector (global bucket), to indicate the beginning of the
// intervals assigned to the node.
//
// The following is the output from print():
//
// 0: MP:43 IR [10,85] [31,56] [36,54] [39,50] [43,43]
// 0: MP:43 IL [10,85] [31,56] [36,54] [39,50] [43,43]
// 1:   MP:25 IR [25,41] [15,40] [20,30]
// 1:   MP:25 IL [15,40] [20,30] [25,41]
// 2:     MP:15 IR [12,21] [11,16]
// 2:     MP:15 IL [11,16] [12,21]
// 2:     MP:36 IR []
// 2:     MP:36 IL []
// 3:       MP:31 IR [30,35]
// 3:       MP:31 IL [30,35]
// 1:   MP:61 IR [50,75] [60,70] [49,65] [55,61]
// 1:   MP:61 IL [49,65] [50,75] [55,61] [60,70]
// 2:     MP:74 IR [74,80] [71,79]
// 2:     MP:74 IL [71,79] [74,80]
//
// with:
//    0: Root Node.
//   MP: Middle point.
//   IL: Intervals to the left (in ascending order by beginning point).
//   IR: Intervals to the right (in descending order by ending point).
//
//                                    Root
//                                      |
//                                      V
//                       +------------MP:43------------+
//                       |            IL IR            |
//                       |       [10,85] [10,85]       |
//                    LS |       [31,56] [31,56]       | RS
//                       |       [36,54] [36,54]       |
//                       |       [39,50] [39,50]       |
//                       |       [43,43] [43,43]       |
//                       V                             V
//        +------------MP:25------------+            MP:61------------+
//        |            IL IR            |            IL IR            |
//        |       [15,40] [25,41]       |       [49,65] [50,75]       |
//     LS |       [20,30] [15,40]       | RS    [50,75] [60,70]       | RS
//        |       [25,41] [20,30]       |       [55,61] [49,65]       |
//        |                             |       [60,70] [55,61]       |
//        V                             V                             V
//      MP:15                 +-------MP:36                         MP:74
//      IL IR                 |       IL IR                         IL IR
// [11,16] [12,21]         LS |       [] []                    [71,79] [74,80]
// [12,21] [11,16]            |                                [74,80] [71,79]
//                            V
//                          MP:31
//                          IL IR
//                     [30,35] [30,35]
//
// The creation of an interval tree is done in 2 steps:
// 1) Insert the interval items by calling
//    void insert(PointT Left, PointT Right, ValueT Value);
//    Left, Right: the interval left and right limits.
//    Value: the data associated with that specific interval.
//
// 2) Create the interval tree by calling
//    void create();
//
// Once the tree is created, it is switched to query mode.
// Query the tree by using iterators or container.
//
// a) Iterators over intervals overlapping the given point with very weak
//    ordering guarantees.
//    find_iterator begin(PointType Point) const;
//    find_iterator end() const;
//    Point: a target point to be tested for inclusion in any interval.
//
// b) Container:
//    IntervalReferences getContaining(PointT Point);
//    Point: a target point to be tested for inclusion in any interval.
//    Returns vector with all the intervals containing the target point.
//
// The returned intervals are in their natural tree location. They can
// be sorted:
//
// static void sortIntervals(IntervalReferences &Intervals, Sorting Sort);
//
// Ability to print the constructed interval tree:
//   void print(raw_ostream &OS, bool HexFormat = true);
// Display the associated data in hexadecimal format.

namespace llvm {

//===----------------------------------------------------------------------===//
//---                          IntervalData                               ----//
//===----------------------------------------------------------------------===//
/// An interval data composed by a \a Left and \a Right points and an
/// associated \a Value.
/// \a PointT corresponds to the interval endpoints type.
/// \a ValueT corresponds to the interval value type.
template <typename PointT, typename ValueT> class IntervalData {
protected:
  using PointType = PointT;
  using ValueType = ValueT;

private:
  PointType Left;
  PointType Right;
  ValueType Value;

public:
  IntervalData() = delete;
  IntervalData(PointType Left, PointType Right, ValueType Value)
      : Left(Left), Right(Right), Value(Value) {
    assert(Left <= Right && "'Left' must be less or equal to 'Right'");
  }
  virtual ~IntervalData() = default;
  PointType left() const { return Left; }
  PointType right() const { return Right; }
  ValueType value() const { return Value; }

  /// Return true if \a Point is inside the left bound of closed interval \a
  /// [Left;Right]. This is Left <= Point for closed intervals.
  bool left(const PointType &Point) const { return left() <= Point; }

  /// Return true if \a Point is inside the right bound of closed interval \a
  /// [Left;Right]. This is Point <= Right for closed intervals.
  bool right(const PointType &Point) const { return Point <= right(); }

  /// Return true when \a Point is contained in interval \a [Left;Right].
  /// This is Left <= Point <= Right for closed intervals.
  bool contains(const PointType &Point) const {
    return left(Point) && right(Point);
  }
};

//===----------------------------------------------------------------------===//
//---                          IntervalTree                               ----//
//===----------------------------------------------------------------------===//
// Helper class template that is used by the IntervalTree to ensure that one
// does instantiate using only fundamental and/or pointer types.
template <typename T>
using PointTypeIsValid = std::bool_constant<std::is_fundamental<T>::value>;

template <typename T>
using ValueTypeIsValid = std::bool_constant<std::is_fundamental<T>::value ||
                                            std::is_pointer<T>::value>;

template <typename PointT, typename ValueT,
          typename DataT = IntervalData<PointT, ValueT>>
class IntervalTree {
  static_assert(PointTypeIsValid<PointT>::value,
                "PointT must be a fundamental type");
  static_assert(ValueTypeIsValid<ValueT>::value,
                "ValueT must be a fundamental or pointer type");

public:
  using PointType = PointT;
  using ValueType = ValueT;
  using DataType = DataT;
  using Allocator = BumpPtrAllocator;

  enum class Sorting { Ascending, Descending };
  using IntervalReferences = SmallVector<const DataType *, 4>;

private:
  using IntervalVector = SmallVector<DataType, 4>;
  using PointsVector = SmallVector<PointType, 4>;

  class IntervalNode {
    PointType MiddlePoint;             // MP - Middle point.
    IntervalNode *Left = nullptr;      // LS - Left subtree.
    IntervalNode *Right = nullptr;     // RS - Right subtree.
    unsigned BucketIntervalsStart = 0; // Starting index in global bucket.
    unsigned BucketIntervalsSize = 0;  // Size of bucket.

  public:
    PointType middle() const { return MiddlePoint; }
    unsigned start() const { return BucketIntervalsStart; }
    unsigned size() const { return BucketIntervalsSize; }

    IntervalNode(PointType Point, unsigned Start)
        : MiddlePoint(Point), BucketIntervalsStart(Start) {}

    friend IntervalTree;
  };

  Allocator &NodeAllocator;     // Allocator used for creating interval nodes.
  IntervalNode *Root = nullptr; // Interval tree root.
  IntervalVector Intervals; // Storage for each interval and all of the fields
                            // point back into it.
  PointsVector EndPoints; // Sorted left and right points of all the intervals.

  // These vectors provide storage that nodes carve buckets of overlapping
  // intervals out of. All intervals are recorded on each vector.
  // The bucket with the intervals associated to a node, is determined by
  // the fields 'BucketIntervalStart' and 'BucketIntervalSize' in the node.
  // The buckets in the first vector are sorted in ascending order using
  // the left value and the buckets in the second vector are sorted in
  // descending order using the right value. Every interval in a bucket
  // contains the middle point for the node.
  IntervalReferences IntervalsLeft;  // Intervals to the left of middle point.
  IntervalReferences IntervalsRight; // Intervals to the right of middle point.

  // Working vector used during the tree creation to sort the intervals. It is
  // cleared once the tree is created.
  IntervalReferences References;

  /// Recursively delete the constructed tree.
  void deleteTree(IntervalNode *Node) {
    if (Node) {
      deleteTree(Node->Left);
      deleteTree(Node->Right);
      Node->~IntervalNode();
      NodeAllocator.Deallocate(Node);
    }
  }

  /// Print the interval list (left and right) for a given \a Node.
  static void printList(raw_ostream &OS, IntervalReferences &IntervalSet,
                        unsigned Start, unsigned Size, bool HexFormat = true) {
    assert(Start + Size <= IntervalSet.size() &&
           "Start + Size must be in bounds of the IntervalSet");
    const char *Format = HexFormat ? "[0x%08x,0x%08x] " : "[%2d,%2d] ";
    if (Size) {
      for (unsigned Position = Start; Position < Start + Size; ++Position)
        OS << format(Format, IntervalSet[Position]->left(),
                     IntervalSet[Position]->right());
    } else {
      OS << "[]";
    }
    OS << "\n";
  }

  /// Print an interval tree \a Node.
  void printNode(raw_ostream &OS, unsigned Level, IntervalNode *Node,
                 bool HexFormat = true) {
    const char *Format = HexFormat ? "MP:0x%08x " : "MP:%2d ";
    auto PrintNodeData = [&](StringRef Text, IntervalReferences &IntervalSet) {
      OS << format("%5d: ", Level);
      OS.indent(Level * 2);
      OS << format(Format, Node->middle()) << Text << " ";
      printList(OS, IntervalSet, Node->start(), Node->size(), HexFormat);
    };

    PrintNodeData("IR", IntervalsRight);
    PrintNodeData("IL", IntervalsLeft);
  }

  /// Recursively print all the interval nodes.
  void printTree(raw_ostream &OS, unsigned Level, IntervalNode *Node,
                 bool HexFormat = true) {
    if (Node) {
      printNode(OS, Level, Node, HexFormat);
      ++Level;
      printTree(OS, Level, Node->Left, HexFormat);
      printTree(OS, Level, Node->Right, HexFormat);
    }
  }

  /// Recursively construct the interval tree.
  /// IntervalsSize: Number of intervals that have been processed and it will
  /// be used as the start for the intervals bucket for a node.
  /// PointsBeginIndex, PointsEndIndex: Determine the range into the EndPoints
  /// vector of end points to be processed.
  /// ReferencesBeginIndex, ReferencesSize: Determine the range into the
  /// intervals being processed.
  IntervalNode *createTree(unsigned &IntervalsSize, int PointsBeginIndex,
                           int PointsEndIndex, int ReferencesBeginIndex,
                           int ReferencesSize) {
    // We start by taking the entire range of all the intervals and dividing
    // it in half at x_middle (in practice, x_middle should be picked to keep
    // the tree relatively balanced).
    // This gives three sets of intervals, those completely to the left of
    // x_middle which we'll call S_left, those completely to the right of
    // x_middle which we'll call S_right, and those overlapping x_middle
    // which we'll call S_middle.
    // The intervals in S_left and S_right are recursively divided in the
    // same manner until there are no intervals remaining.

    if (PointsBeginIndex > PointsEndIndex ||
        ReferencesBeginIndex >= ReferencesSize)
      return nullptr;

    int MiddleIndex = (PointsBeginIndex + PointsEndIndex) / 2;
    PointType MiddlePoint = EndPoints[MiddleIndex];

    unsigned NewBucketStart = IntervalsSize;
    unsigned NewBucketSize = 0;
    int ReferencesRightIndex = ReferencesSize;

    IntervalNode *Root =
        new (NodeAllocator) IntervalNode(MiddlePoint, NewBucketStart);

    // A quicksort implementation where all the intervals that overlap
    // with the pivot are put into the "bucket", and "References" is the
    // partition space where we recursively sort the remaining intervals.
    for (int Index = ReferencesBeginIndex; Index < ReferencesRightIndex;) {

      // Current interval contains the middle point.
      if (References[Index]->contains(MiddlePoint)) {
        IntervalsLeft[IntervalsSize] = References[Index];
        IntervalsRight[IntervalsSize] = References[Index];
        ++IntervalsSize;
        Root->BucketIntervalsSize = ++NewBucketSize;

        if (Index < --ReferencesRightIndex)
          std::swap(References[Index], References[ReferencesRightIndex]);
        if (ReferencesRightIndex < --ReferencesSize)
          std::swap(References[ReferencesRightIndex],
                    References[ReferencesSize]);
        continue;
      }

      if (References[Index]->left() > MiddlePoint) {
        if (Index < --ReferencesRightIndex)
          std::swap(References[Index], References[ReferencesRightIndex]);
        continue;
      }
      ++Index;
    }

    // Sort intervals on the left and right of the middle point.
    if (NewBucketSize > 1) {
      // Sort the intervals in ascending order by their beginning point.
      std::stable_sort(IntervalsLeft.begin() + NewBucketStart,
                       IntervalsLeft.begin() + NewBucketStart + NewBucketSize,
                       [](const DataType *LHS, const DataType *RHS) {
                         return LHS->left() < RHS->left();
                       });
      // Sort the intervals in descending order by their ending point.
      std::stable_sort(IntervalsRight.begin() + NewBucketStart,
                       IntervalsRight.begin() + NewBucketStart + NewBucketSize,
                       [](const DataType *LHS, const DataType *RHS) {
                         return LHS->right() > RHS->right();
                       });
    }

    if (PointsBeginIndex <= MiddleIndex - 1) {
      Root->Left = createTree(IntervalsSize, PointsBeginIndex, MiddleIndex - 1,
                              ReferencesBeginIndex, ReferencesRightIndex);
    }

    if (MiddleIndex + 1 <= PointsEndIndex) {
      Root->Right = createTree(IntervalsSize, MiddleIndex + 1, PointsEndIndex,
                               ReferencesRightIndex, ReferencesSize);
    }

    return Root;
  }

public:
  class find_iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = DataType;
    using difference_type = DataType;
    using pointer = DataType *;
    using reference = DataType &;

  private:
    const IntervalReferences *AscendingBuckets = nullptr;
    const IntervalReferences *DescendingBuckets = nullptr;

    // Current node and index while traversing the intervals that contain
    // the reference point.
    IntervalNode *Node = nullptr;
    PointType Point = {};
    unsigned Index = 0;

    // For the current node, check if we have intervals that contain the
    // reference point. We return when the node does have intervals that
    // contain such point. Otherwise we keep descending on that branch.
    void initNode() {
      Index = 0;
      while (Node) {
        // Return if the reference point is the same as the middle point or
        // the current node doesn't have any intervals at all.
        if (Point == Node->middle()) {
          if (Node->size() == 0) {
            // No intervals that contain the reference point.
            Node = nullptr;
          }
          return;
        }

        if (Point < Node->middle()) {
          // The reference point can be at the left or right of the middle
          // point. Return if the current node has intervals that contain the
          // reference point; otherwise descend on the respective branch.
          if (Node->size() && (*AscendingBuckets)[Node->start()]->left(Point)) {
            return;
          }
          Node = Node->Left;
        } else {
          if (Node->size() &&
              (*DescendingBuckets)[Node->start()]->right(Point)) {
            return;
          }
          Node = Node->Right;
        }
      }
    }

    // Given the current node (which was initialized by initNode), move to
    // the next interval in the list of intervals that contain the reference
    // point. Otherwise move to the next node, as the intervals contained
    // in that node, can contain the reference point.
    void nextInterval() {
      // If there are available intervals that contain the reference point,
      // traverse them; otherwise move to the left or right node, depending
      // on the middle point value.
      if (++Index < Node->size()) {
        if (Node->middle() == Point)
          return;
        if (Point < Node->middle()) {
          // Reference point is on the left.
          if (!(*AscendingBuckets)[Node->start() + Index]->left(Point)) {
            // The intervals don't contain the reference point. Move to the
            // next node, preserving the descending order.
            Node = Node->Left;
            initNode();
          }
        } else {
          // Reference point is on the right.
          if (!(*DescendingBuckets)[Node->start() + Index]->right(Point)) {
            // The intervals don't contain the reference point. Move to the
            // next node, preserving the ascending order.
            Node = Node->Right;
            initNode();
          }
        }
      } else {
        // We have traversed all the intervals in the current node.
        if (Point == Node->middle()) {
          Node = nullptr;
          Index = 0;
          return;
        }
        // Select a branch based on the middle point.
        Node = Point < Node->middle() ? Node->Left : Node->Right;
        initNode();
      }
    }

    find_iterator() = default;
    explicit find_iterator(const IntervalReferences *Left,
                           const IntervalReferences *Right, IntervalNode *Node,
                           PointType Point)
        : AscendingBuckets(Left), DescendingBuckets(Right), Node(Node),
          Point(Point), Index(0) {
      initNode();
    }

    const DataType *current() const {
      return (Point <= Node->middle())
                 ? (*AscendingBuckets)[Node->start() + Index]
                 : (*DescendingBuckets)[Node->start() + Index];
    }

  public:
    find_iterator &operator++() {
      nextInterval();
      return *this;
    }

    find_iterator operator++(int) {
      find_iterator Iter(*this);
      nextInterval();
      return Iter;
    }

    /// Dereference operators.
    const DataType *operator->() const { return current(); }
    const DataType &operator*() const { return *(current()); }

    /// Comparison operators.
    friend bool operator==(const find_iterator &LHS, const find_iterator &RHS) {
      return (!LHS.Node && !RHS.Node && !LHS.Index && !RHS.Index) ||
             (LHS.Point == RHS.Point && LHS.Node == RHS.Node &&
              LHS.Index == RHS.Index);
    }
    friend bool operator!=(const find_iterator &LHS, const find_iterator &RHS) {
      return !(LHS == RHS);
    }

    friend IntervalTree;
  };

private:
  find_iterator End;

public:
  explicit IntervalTree(Allocator &NodeAllocator)
      : NodeAllocator(NodeAllocator) {}
  ~IntervalTree() { clear(); }

  /// Return true when no intervals are mapped.
  bool empty() const { return Root == nullptr; }

  /// Remove all entries.
  void clear() {
    deleteTree(Root);
    Root = nullptr;
    Intervals.clear();
    IntervalsLeft.clear();
    IntervalsRight.clear();
    EndPoints.clear();
  }

  /// Add a mapping of [Left;Right] to \a Value.
  void insert(PointType Left, PointType Right, ValueType Value) {
    assert(empty() && "Invalid insertion. Interval tree already constructed.");
    Intervals.emplace_back(Left, Right, Value);
  }

  /// Return all the intervals in their natural tree location, that
  /// contain the given point.
  IntervalReferences getContaining(PointType Point) const {
    assert(!empty() && "Interval tree it is not constructed.");
    IntervalReferences IntervalSet;
    for (find_iterator Iter = find(Point), E = find_end(); Iter != E; ++Iter)
      IntervalSet.push_back(const_cast<DataType *>(&(*Iter)));
    return IntervalSet;
  }

  /// Sort the given intervals using the following sort options:
  /// Ascending: return the intervals with the smallest at the front.
  /// Descending: return the intervals with the biggest at the front.
  static void sortIntervals(IntervalReferences &IntervalSet, Sorting Sort) {
    std::stable_sort(IntervalSet.begin(), IntervalSet.end(),
                     [Sort](const DataType *RHS, const DataType *LHS) {
                       return Sort == Sorting::Ascending
                                  ? (LHS->right() - LHS->left()) >
                                        (RHS->right() - RHS->left())
                                  : (LHS->right() - LHS->left()) <
                                        (RHS->right() - RHS->left());
                     });
  }

  /// Print the interval tree.
  /// When \a HexFormat is true, the interval tree interval ranges and
  /// associated values are printed in hexadecimal format.
  void print(raw_ostream &OS, bool HexFormat = true) {
    printTree(OS, 0, Root, HexFormat);
  }

  /// Create the interval tree.
  void create() {
    assert(empty() && "Interval tree already constructed.");
    // Sorted vector of unique end points values of all the intervals.
    // Records references to the collected intervals.
    SmallVector<PointType, 4> Points;
    for (const DataType &Data : Intervals) {
      Points.push_back(Data.left());
      Points.push_back(Data.right());
      References.push_back(std::addressof(Data));
    }
    std::stable_sort(Points.begin(), Points.end());
    auto Last = llvm::unique(Points);
    Points.erase(Last, Points.end());

    EndPoints.assign(Points.begin(), Points.end());

    IntervalsLeft.resize(Intervals.size());
    IntervalsRight.resize(Intervals.size());

    // Given a set of n intervals, construct a data structure so that
    // we can efficiently retrieve all intervals overlapping another
    // interval or point.
    unsigned IntervalsSize = 0;
    Root =
        createTree(IntervalsSize, /*PointsBeginIndex=*/0, EndPoints.size() - 1,
                   /*ReferencesBeginIndex=*/0, References.size());

    // Save to clear this storage, as it used only to sort the intervals.
    References.clear();
  }

  /// Iterator to start a find operation; it returns find_end() if the
  /// tree has not been built.
  /// There is no support to iterate over all the elements of the tree.
  find_iterator find(PointType Point) const {
    return empty()
               ? find_end()
               : find_iterator(&IntervalsLeft, &IntervalsRight, Root, Point);
  }

  /// Iterator to end find operation.
  find_iterator find_end() const { return End; }
};

} // namespace llvm

#endif // LLVM_ADT_INTERVALTREE_H
