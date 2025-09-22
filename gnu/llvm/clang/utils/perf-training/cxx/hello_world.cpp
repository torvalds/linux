// RUN: %clang_cpp -c %s
// RUN: %clang_cpp_skip_driver -Wall -pedantic -c %s
#include <iostream>

int main(int, char**) {
  std::cout << "Hello, World!";
  return 0;
}
