/*
 * bench.c -- simple scanner/parser benchmarking tool
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(HAVE_GETOPT)
# include "getopt.h"
#else
# include <unistd.h>
#endif

#include "zone.h"
#include "config.h"
#include "isadetection.h"
#include "attributes.h"
#include "diagnostic.h"

#if _MSC_VER
#define strcasecmp(s1, s2) _stricmp(s1, s2)
#define strncasecmp(s1, s2, n) _strnicmp(s1, s2, n)
#else
#include <strings.h>
#endif

typedef zone_parser_t parser_t;

#if HAVE_HASWELL
extern int32_t zone_bench_haswell_lex(zone_parser_t *, size_t *);
extern int32_t zone_haswell_parse(zone_parser_t *);
#endif

#if HAVE_WESTMERE
extern int32_t zone_bench_westmere_lex(zone_parser_t *, size_t *);
extern int32_t zone_westmere_parse(zone_parser_t *);
#endif

extern int32_t zone_bench_fallback_lex(zone_parser_t *, size_t *);
extern int32_t zone_fallback_parse(zone_parser_t *);

typedef struct kernel kernel_t;
struct kernel {
  const char *name;
  uint32_t instruction_set;
  int32_t (*bench_lex)(zone_parser_t *, size_t *);
  int32_t (*parse)(zone_parser_t *);
};

static const kernel_t kernels[] = {
#if HAVE_HASWELL
  { "haswell", AVX2, &zone_bench_haswell_lex, &zone_haswell_parse },
#endif
#if HAVE_WESTMERE
  { "westmere", SSE42|PCLMULQDQ, &zone_bench_westmere_lex, &zone_westmere_parse },
#endif
  { "fallback", DEFAULT, &zone_bench_fallback_lex, &zone_fallback_parse }
};

extern int32_t zone_open(
  zone_parser_t *,
  const zone_options_t *,
  zone_buffers_t *,
  const char *,
  void *user_data);

extern void zone_close(
  zone_parser_t *);

static int32_t bench_lex(zone_parser_t *parser, const kernel_t *kernel)
{
  size_t tokens = 0;
  int32_t result;

  if ((result = kernel->bench_lex(parser, &tokens)) < 0)
    return result;

  printf("Lexed %zu tokens\n", tokens);
  return 0;
}

static int32_t bench_accept(
  parser_t *parser,
  const zone_name_t *owner,
  uint16_t type,
  uint16_t class,
  uint32_t ttl,
  uint16_t rdlength,
  const uint8_t *rdata,
  void *user_data)
{
  (void)parser;
  (void)owner;
  (void)type;
  (void)class;
  (void)ttl;
  (void)rdlength;
  (void)rdata;
  (*(size_t *)user_data)++;
  return ZONE_SUCCESS;
}

static int32_t bench_parse(zone_parser_t *parser, const kernel_t *kernel)
{
  size_t records = 0;
  int32_t result;

  parser->user_data = &records;
  result = kernel->parse(parser);

  printf("Parsed %zu records\n", records);
  return result;
}

diagnostic_push()
msvc_diagnostic_ignored(4996)

static const kernel_t *select_kernel(const char *name)
{
  const size_t n = sizeof(kernels)/sizeof(kernels[0]);
  const uint32_t supported = detect_supported_architectures();
  const kernel_t *kernel = NULL;

  if ((!name || !*name) && !(name = getenv("ZONE_KERNEL"))) {
    for (size_t i=0; !kernel && i < n; i++) {
      if ((kernels[i].instruction_set & supported) == kernels[i].instruction_set)
        kernel = &kernels[i];
    }
    assert(kernel != NULL);
  } else {
    for (size_t i=0; !kernel && i < n; i++) {
      if (strcasecmp(name, kernels[i].name) == 0)
        kernel = &kernels[i];
    }

    if (!kernel || (kernel->instruction_set && !(kernel->instruction_set & supported))) {
      fprintf(stderr, "Target %s is unavailable\n", name);
      return NULL;
    }
  }

  printf("Selected target %s\n", kernel->name);
  return kernel;
}

diagnostic_pop()

static void help(const char *program)
{
  const char *format =
    "Usage: %s [OPTION] <lex or parse> <zone file>\n"
    "\n"
    "Options:\n"
    "  -h         Display available options.\n"
    "  -t target  Select target (default:%s)\n"
    "\n"
    "Kernels:\n";

  printf(format, program, kernels[0].name);

  for (size_t i=0, n=sizeof(kernels)/sizeof(kernels[0]); i < n; i++)
    printf("  %s\n", kernels[i].name);
}

static void usage(const char *program)
{
  fprintf(stderr, "Usage: %s [OPTION] <lex or parse> <zone file>\n", program);
  exit(EXIT_FAILURE);
}

static uint8_t root[] = { 0 };

int main(int argc, char *argv[])
{
  const char *name = NULL, *program = argv[0];

  for (const char *slash = argv[0]; *slash; slash++)
    if (*slash == '/' || *slash == '\\')
      program = slash + 1;

  for (int option; (option = getopt(argc, argv, "ht:")) != -1;) {
    switch (option) {
      case 'h':
        help(program);
        exit(EXIT_SUCCESS);
      case 't':
        name = optarg;
        break;
      default:
        usage(program);
    }
  }

  if (optind > argc || argc - optind < 2)
    usage(program);

  int32_t (*bench)(zone_parser_t *, const kernel_t *) = 0;
  if (strcasecmp(argv[optind], "lex") == 0)
    bench = &bench_lex;
  else if (strcasecmp(argv[optind], "parse") == 0)
    bench = &bench_parse;
  else
    usage(program);

  const kernel_t *kernel;
  if (!(kernel = select_kernel(name)))
    exit(EXIT_FAILURE);

  zone_parser_t parser;
  memset(&parser, 0, sizeof(parser));
  zone_options_t options;
  memset(&options, 0, sizeof(options));
  options.pretty_ttls = true;
  options.origin.octets = root;
  options.origin.length = 1;
  options.accept.callback = &bench_accept;
  options.default_ttl = 3600;
  options.default_class = 1;

  zone_name_buffer_t owner;
  zone_rdata_buffer_t rdata;
  zone_buffers_t buffers = { 1, &owner, &rdata };

  if (zone_open(&parser, &options, &buffers, argv[argc-1], NULL) < 0)
    exit(EXIT_FAILURE);
  if (bench(&parser, kernel) < 0)
    exit(EXIT_FAILURE);

  zone_close(&parser);
  return EXIT_SUCCESS;
}
