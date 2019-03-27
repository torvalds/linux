/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

#include "fuzz.h"
#include "fuzz_helpers.h"
#include "util.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char const **argv) {
  size_t const kMaxFileSize = (size_t)1 << 27;
  int const kFollowLinks = 1;
  char *fileNamesBuf = NULL;
  char const **files = argv + 1;
  unsigned numFiles = argc - 1;
  uint8_t *buffer = NULL;
  size_t bufferSize = 0;
  unsigned i;
  int ret;

#ifdef UTIL_HAS_CREATEFILELIST
  files = UTIL_createFileList(files, numFiles, &fileNamesBuf, &numFiles,
                              kFollowLinks);
  if (!files)
    numFiles = 0;
#endif
  if (numFiles == 0)
    fprintf(stderr, "WARNING: No files passed to %s\n", argv[0]);
  for (i = 0; i < numFiles; ++i) {
    char const *fileName = files[i];
    size_t const fileSize = UTIL_getFileSize(fileName);
    size_t readSize;
    FILE *file;

    /* Check that it is a regular file, and that the fileSize is valid */
    FUZZ_ASSERT_MSG(UTIL_isRegularFile(fileName), fileName);
    FUZZ_ASSERT_MSG(fileSize <= kMaxFileSize, fileName);
    /* Ensure we have a large enough buffer allocated */
    if (fileSize > bufferSize) {
      free(buffer);
      buffer = (uint8_t *)malloc(fileSize);
      FUZZ_ASSERT_MSG(buffer, fileName);
      bufferSize = fileSize;
    }
    /* Open the file */
    file = fopen(fileName, "rb");
    FUZZ_ASSERT_MSG(file, fileName);
    /* Read the file */
    readSize = fread(buffer, 1, fileSize, file);
    FUZZ_ASSERT_MSG(readSize == fileSize, fileName);
    /* Close the file */
    fclose(file);
    /* Run the fuzz target */
    LLVMFuzzerTestOneInput(buffer, fileSize);
  }

  ret = 0;
  free(buffer);
#ifdef UTIL_HAS_CREATEFILELIST
  UTIL_freeFileList(files, fileNamesBuf);
#endif
  return ret;
}
