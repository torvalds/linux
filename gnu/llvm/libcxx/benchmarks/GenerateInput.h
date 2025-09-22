#ifndef BENCHMARK_GENERATE_INPUT_H
#define BENCHMARK_GENERATE_INPUT_H

#include <algorithm>
#include <climits>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

static const char Letters[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K',
    'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z'};
static const std::size_t LettersSize = sizeof(Letters);

inline std::default_random_engine& getRandomEngine() {
  static std::default_random_engine RandEngine(std::random_device{}());
  return RandEngine;
}

inline char getRandomChar() {
  std::uniform_int_distribution<> LettersDist(0, LettersSize - 1);
  return Letters[LettersDist(getRandomEngine())];
}

template <class IntT>
inline IntT getRandomInteger(IntT Min, IntT Max) {
  std::uniform_int_distribution<unsigned long long> dist(Min, Max);
  return static_cast<IntT>(dist(getRandomEngine()));
}

inline std::string getRandomString(std::size_t Len) {
  std::string str(Len, 0);
  std::generate_n(str.begin(), Len, &getRandomChar);
  return str;
}

template <class IntT>
inline std::vector<IntT> getDuplicateIntegerInputs(size_t N) {
  std::vector<IntT> inputs(N, static_cast<IntT>(-1));
  return inputs;
}

template <class IntT>
inline std::vector<IntT> getSortedIntegerInputs(size_t N) {
  std::vector<IntT> inputs;
  for (size_t i = 0; i < N; i += 1)
    inputs.push_back(i);
  return inputs;
}

template <class IntT>
std::vector<IntT> getSortedLargeIntegerInputs(size_t N) {
  std::vector<IntT> inputs;
  for (size_t i = 0; i < N; ++i) {
    inputs.push_back(i + N);
  }
  return inputs;
}

template <class IntT>
std::vector<IntT> getSortedTopBitsIntegerInputs(size_t N) {
  std::vector<IntT> inputs = getSortedIntegerInputs<IntT>(N);
  for (auto& E : inputs)
    E <<= ((sizeof(IntT) / 2) * CHAR_BIT);
  return inputs;
}

template <class IntT>
inline std::vector<IntT> getReverseSortedIntegerInputs(size_t N) {
  std::vector<IntT> inputs;
  std::size_t i = N;
  while (i > 0) {
    --i;
    inputs.push_back(i);
  }
  return inputs;
}

template <class IntT>
std::vector<IntT> getPipeOrganIntegerInputs(size_t N) {
  std::vector<IntT> v;
  v.reserve(N);
  for (size_t i = 0; i < N / 2; ++i)
    v.push_back(i);
  for (size_t i = N / 2; i < N; ++i)
    v.push_back(N - i);
  return v;
}

template <class IntT>
std::vector<IntT> getRandomIntegerInputs(size_t N) {
  std::vector<IntT> inputs;
  for (size_t i = 0; i < N; ++i) {
    inputs.push_back(getRandomInteger<IntT>(0, std::numeric_limits<IntT>::max()));
  }
  return inputs;
}

inline std::vector<std::string> getDuplicateStringInputs(size_t N) {
  std::vector<std::string> inputs(N, getRandomString(1024));
  return inputs;
}

inline std::vector<std::string> getRandomStringInputs(size_t N) {
  std::vector<std::string> inputs;
  for (size_t i = 0; i < N; ++i) {
    inputs.push_back(getRandomString(1024));
  }
  return inputs;
}

inline std::vector<std::string> getPrefixedRandomStringInputs(size_t N) {
  std::vector<std::string> inputs;
  constexpr int kSuffixLength = 32;
  const std::string prefix    = getRandomString(1024 - kSuffixLength);
  for (size_t i = 0; i < N; ++i) {
    inputs.push_back(prefix + getRandomString(kSuffixLength));
  }
  return inputs;
}

inline std::vector<std::string> getSortedStringInputs(size_t N) {
  std::vector<std::string> inputs = getRandomStringInputs(N);
  std::sort(inputs.begin(), inputs.end());
  return inputs;
}

inline std::vector<std::string> getReverseSortedStringInputs(size_t N) {
  std::vector<std::string> inputs = getSortedStringInputs(N);
  std::reverse(inputs.begin(), inputs.end());
  return inputs;
}

inline std::vector<const char*> getRandomCStringInputs(size_t N) {
  static std::vector<std::string> inputs = getRandomStringInputs(N);
  std::vector<const char*> cinputs;
  for (auto const& str : inputs)
    cinputs.push_back(str.c_str());
  return cinputs;
}

#endif // BENCHMARK_GENERATE_INPUT_H
