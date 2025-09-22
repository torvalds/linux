/*===-- helpers.c - tool for testing libLLVM and llvm-c API ---------------===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* Helper functions                                                           *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include <stdio.h>
#include <string.h>

#define MAX_TOKENS 512
#define MAX_LINE_LEN 1024

void llvm_tokenize_stdin(void (*cb)(char **tokens, int ntokens)) {
  char line[MAX_LINE_LEN];
  char *tokbuf[MAX_TOKENS];

  while (fgets(line, sizeof(line), stdin)) {
    int c = 0;

    if (line[0] == ';' || line[0] == '\n')
      continue;

    while (c < MAX_TOKENS) {
      tokbuf[c] = strtok(c ? NULL : line, " \n");
      if (!tokbuf[c])
        break;
      c++;
    }
    if (c)
      cb(tokbuf, c);
  }
}
