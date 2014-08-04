/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * The Makefile in the daemon folder builds and executes 'escape'
 * 'escape' creates configuration_xml.h from configuration.xml and events_xml.h from events-*.xml
 * these genereated xml files are then #included and built as part of the gatord binary
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void print_escaped_path(char *path) {
  if (isdigit(*path)) {
    printf("__");
  }
  for (; *path != '\0'; ++path) {
    printf("%c", isalnum(*path) ? *path : '_');
  }
}

int main(int argc, char *argv[]) {
  int i;
  char *path;
  FILE *in = NULL;
  int ch;
  unsigned int len = 0;

  for (i = 1; i < argc && argv[i][0] == '-'; ++i) ;
  if (i == argc) {
    fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    return EXIT_FAILURE;
  }
  path = argv[i];

  errno = 0;
  if ((in = fopen(path, "r")) == NULL) {
    fprintf(stderr, "Unable to open '%s': %s\n", path, strerror(errno));
    return EXIT_FAILURE;
  }

  printf("static const unsigned char ");
  print_escaped_path(path);
  printf("[] = {");
  for (;;) {
    ch = fgetc(in);
    if (len != 0) {
      printf(",");
    }
    if (len % 12 == 0) {
      printf("\n ");
    }
    // Write out a null character after the contents of the file but do not increment len
    printf(" 0x%.2x", (ch == EOF ? 0 : ch));
    if (ch == EOF) {
      break;
    }
    ++len;
  }
  printf("\n};\nstatic const unsigned int ");
  print_escaped_path(path);
  printf("_len = %i;\n", len);

  fclose(in);

  return EXIT_SUCCESS;
}
