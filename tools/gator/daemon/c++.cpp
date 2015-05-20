/**
 * Minimal set of C++ functions so that libstdc++ is not required
 *
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <stdlib.h>

void operator delete(void *ptr) {
  if (ptr != NULL) {
    free(ptr);
  }
}

void operator delete[](void *ptr) {
  operator delete(ptr);
}

void *operator new(size_t size) {
  void *ptr = malloc(size == 0 ? 1 : size);
  if (ptr == NULL) {
    abort();
  }
  return ptr;
}

void *operator new[](size_t size) {
  return operator new(size);
}

extern "C"
void __cxa_pure_virtual() {
  printf("pure virtual method called\n");
  abort();
}
