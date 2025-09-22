/*===- count.c - The 'count' testing tool ---------------------------------===*\
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
\*===----------------------------------------------------------------------===*/

#include "llvm/Support/AutoConvert.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
#ifdef __MVS__
  if (enableAutoConversion(fileno(stdin)) == -1)
    fprintf(stderr, "Setting conversion on stdin failed\n");

  if (enableAutoConversion(fileno(stderr)) == -1)
    fprintf(stdout, "Setting conversion on stderr failed\n");
#endif
  size_t Count, NumLines, NumRead;
  char Buffer[4096], *End;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <expected line count>\n", argv[0]);
    return 2;
  }

  Count = strtoul(argv[1], &End, 10);
  if (*End != '\0' && End != argv[1]) {
    fprintf(stderr, "%s: invalid count argument '%s'\n", argv[0], argv[1]);
    return 2;
  }

  NumLines = 0;
  do {
    size_t i;

    NumRead = fread(Buffer, 1, sizeof(Buffer), stdin);

    for (i = 0; i != NumRead; ++i)
      if (Buffer[i] == '\n')
        ++NumLines;
  } while (NumRead == sizeof(Buffer));
    
  if (!feof(stdin)) {
    fprintf(stderr, "%s: error reading stdin\n", argv[0]);
    return 3;
  }

  if (Count != NumLines) {
    fprintf(stderr, "Expected %zu lines, got %zu.\n", Count, NumLines);
    return 1;
  }

  return 0;
}
