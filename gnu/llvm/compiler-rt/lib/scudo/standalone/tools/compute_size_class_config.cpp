//===-- compute_size_class_config.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

struct Alloc {
  size_t size, count;
};

size_t measureWastage(const std::vector<Alloc> &allocs,
                      const std::vector<size_t> &classes, size_t pageSize,
                      size_t headerSize) {
  size_t totalWastage = 0;
  for (auto &a : allocs) {
    size_t sizePlusHeader = a.size + headerSize;
    size_t wastage = -1ull;
    for (auto c : classes)
      if (c >= sizePlusHeader && c - sizePlusHeader < wastage)
        wastage = c - sizePlusHeader;
    if (wastage == -1ull)
      continue;
    if (wastage > 2 * pageSize)
      wastage = 2 * pageSize;
    totalWastage += wastage * a.count;
  }
  return totalWastage;
}

void readAllocs(std::vector<Alloc> &allocs, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "compute_size_class_config: could not open %s: %s\n", path,
            strerror(errno));
    exit(1);
  }

  const char header[] = "<malloc version=\"scudo-1\">\n";
  char buf[sizeof(header) - 1];
  if (fread(buf, 1, sizeof(header) - 1, f) != sizeof(header) - 1 ||
      memcmp(buf, header, sizeof(header) - 1) != 0) {
    fprintf(stderr, "compute_size_class_config: invalid input format\n");
    exit(1);
  }

  Alloc a;
  while (fscanf(f, "<alloc size=\"%zu\" count=\"%zu\"/>\n", &a.size,
                &a.count) == 2)
    allocs.push_back(a);
  fclose(f);
}

size_t log2Floor(size_t x) { return sizeof(long) * 8 - 1 - __builtin_clzl(x); }

void usage() {
  fprintf(stderr,
          "usage: compute_size_class_config [-p pageSize] [-c largestClass] "
          "[-h headerSize] [-n numClasses] [-b numBits] profile...\n");
  exit(1);
}

int main(int argc, char **argv) {
  size_t pageSize = 4096;
  size_t largestClass = 65552;
  size_t headerSize = 16;
  size_t numClasses = 32;
  size_t numBits = 5;

  std::vector<Alloc> allocs;
  for (size_t i = 1; i != argc;) {
    auto matchArg = [&](size_t &arg, const char *name) {
      if (strcmp(argv[i], name) == 0) {
        if (i + 1 != argc) {
          arg = atoi(argv[i + 1]);
          i += 2;
        } else {
          usage();
        }
        return true;
      }
      return false;
    };
    if (matchArg(pageSize, "-p") || matchArg(largestClass, "-c") ||
        matchArg(headerSize, "-h") || matchArg(numClasses, "-n") ||
        matchArg(numBits, "-b"))
      continue;
    readAllocs(allocs, argv[i]);
    ++i;
  }

  if (allocs.empty())
    usage();

  std::vector<size_t> classes;
  classes.push_back(largestClass);
  for (size_t i = 1; i != numClasses; ++i) {
    size_t minWastage = -1ull;
    size_t minWastageClass;
    for (size_t newClass = 16; newClass != largestClass; newClass += 16) {
      // Skip classes with more than numBits bits, ignoring leading or trailing
      // zero bits.
      if (__builtin_ctzl(newClass - headerSize) +
              __builtin_clzl(newClass - headerSize) <
          sizeof(long) * 8 - numBits)
        continue;

      classes.push_back(newClass);
      size_t newWastage = measureWastage(allocs, classes, pageSize, headerSize);
      classes.pop_back();
      if (newWastage < minWastage) {
        minWastage = newWastage;
        minWastageClass = newClass;
      }
    }
    classes.push_back(minWastageClass);
  }

  std::sort(classes.begin(), classes.end());
  size_t minSizeLog = log2Floor(headerSize);
  size_t midSizeIndex = 0;
  while (classes[midSizeIndex + 1] - classes[midSizeIndex] == (1 << minSizeLog))
    midSizeIndex++;
  size_t midSizeLog = log2Floor(classes[midSizeIndex] - headerSize);
  size_t maxSizeLog = log2Floor(classes.back() - headerSize - 1) + 1;

  printf(R"(// wastage = %zu

struct MySizeClassConfig {
  static const uptr NumBits = %zu;
  static const uptr MinSizeLog = %zu;
  static const uptr MidSizeLog = %zu;
  static const uptr MaxSizeLog = %zu;
  static const u16 MaxNumCachedHint = 14;
  static const uptr MaxBytesCachedLog = 14;

  static constexpr u32 Classes[] = {)",
         measureWastage(allocs, classes, pageSize, headerSize), numBits,
         minSizeLog, midSizeLog, maxSizeLog);
  for (size_t i = 0; i != classes.size(); ++i) {
    if ((i % 8) == 0)
      printf("\n      ");
    else
      printf(" ");
    printf("0x%05zx,", classes[i]);
  }
  printf(R"(
  };
  static const uptr SizeDelta = %zu;
};
)",
         headerSize);
}
