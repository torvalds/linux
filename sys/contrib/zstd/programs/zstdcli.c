/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/*-************************************
*  Tuning parameters
**************************************/
#ifndef ZSTDCLI_CLEVEL_DEFAULT
#  define ZSTDCLI_CLEVEL_DEFAULT 3
#endif

#ifndef ZSTDCLI_CLEVEL_MAX
#  define ZSTDCLI_CLEVEL_MAX 19   /* without using --ultra */
#endif



/*-************************************
*  Dependencies
**************************************/
#include "platform.h" /* IS_CONSOLE, PLATFORM_POSIX_VERSION */
#include "util.h"     /* UTIL_HAS_CREATEFILELIST, UTIL_createFileList */
#include <stdio.h>    /* fprintf(), stdin, stdout, stderr */
#include <stdlib.h>   /* getenv */
#include <string.h>   /* strcmp, strlen */
#include <errno.h>    /* errno */
#include "fileio.h"   /* stdinmark, stdoutmark, ZSTD_EXTENSION */
#ifndef ZSTD_NOBENCH
#  include "benchzstd.h"  /* BMK_benchFiles */
#endif
#ifndef ZSTD_NODICT
#  include "dibio.h"  /* ZDICT_cover_params_t, DiB_trainFromFiles() */
#endif
#define ZSTD_STATIC_LINKING_ONLY   /* ZSTD_minCLevel */
#include "zstd.h"     /* ZSTD_VERSION_STRING, ZSTD_maxCLevel */


/*-************************************
*  Constants
**************************************/
#define COMPRESSOR_NAME "zstd command line interface"
#ifndef ZSTD_VERSION
#  define ZSTD_VERSION "v" ZSTD_VERSION_STRING
#endif
#define AUTHOR "Yann Collet"
#define WELCOME_MESSAGE "*** %s %i-bits %s, by %s ***\n", COMPRESSOR_NAME, (int)(sizeof(size_t)*8), ZSTD_VERSION, AUTHOR

#define ZSTD_ZSTDMT "zstdmt"
#define ZSTD_UNZSTD "unzstd"
#define ZSTD_CAT "zstdcat"
#define ZSTD_ZCAT "zcat"
#define ZSTD_GZ "gzip"
#define ZSTD_GUNZIP "gunzip"
#define ZSTD_GZCAT "gzcat"
#define ZSTD_LZMA "lzma"
#define ZSTD_UNLZMA "unlzma"
#define ZSTD_XZ "xz"
#define ZSTD_UNXZ "unxz"
#define ZSTD_LZ4 "lz4"
#define ZSTD_UNLZ4 "unlz4"

#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define DISPLAY_LEVEL_DEFAULT 2

static const char*    g_defaultDictName = "dictionary";
static const unsigned g_defaultMaxDictSize = 110 KB;
static const int      g_defaultDictCLevel = 3;
static const unsigned g_defaultSelectivityLevel = 9;
static const unsigned g_defaultMaxWindowLog = 27;
#define OVERLAP_LOG_DEFAULT 9999
#define LDM_PARAM_DEFAULT 9999  /* Default for parameters where 0 is valid */
static U32 g_overlapLog = OVERLAP_LOG_DEFAULT;
static U32 g_ldmHashLog = 0;
static U32 g_ldmMinMatch = 0;
static U32 g_ldmHashRateLog = LDM_PARAM_DEFAULT;
static U32 g_ldmBucketSizeLog = LDM_PARAM_DEFAULT;


#define DEFAULT_ACCEL 1

typedef enum { cover, fastCover, legacy } dictType;

/*-************************************
*  Display Macros
**************************************/
#define DISPLAY(...)         fprintf(g_displayOut, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...) { if (g_displayLevel>=l) { DISPLAY(__VA_ARGS__); } }
static int g_displayLevel = DISPLAY_LEVEL_DEFAULT;   /* 0 : no display,  1: errors,  2 : + result + interaction + warnings,  3 : + progression,  4 : + information */
static FILE* g_displayOut;


/*-************************************
*  Command Line
**************************************/
static int usage(const char* programName)
{
    DISPLAY( "Usage : \n");
    DISPLAY( "      %s [args] [FILE(s)] [-o file] \n", programName);
    DISPLAY( "\n");
    DISPLAY( "FILE    : a filename \n");
    DISPLAY( "          with no FILE, or when FILE is - , read standard input\n");
    DISPLAY( "Arguments : \n");
#ifndef ZSTD_NOCOMPRESS
    DISPLAY( " -#     : # compression level (1-%d, default: %d) \n", ZSTDCLI_CLEVEL_MAX, ZSTDCLI_CLEVEL_DEFAULT);
#endif
#ifndef ZSTD_NODECOMPRESS
    DISPLAY( " -d     : decompression \n");
#endif
    DISPLAY( " -D file: use `file` as Dictionary \n");
    DISPLAY( " -o file: result stored into `file` (only if 1 input file) \n");
    DISPLAY( " -f     : overwrite output without prompting and (de)compress links \n");
    DISPLAY( "--rm    : remove source file(s) after successful de/compression \n");
    DISPLAY( " -k     : preserve source file(s) (default) \n");
    DISPLAY( " -h/-H  : display help/long help and exit \n");
    return 0;
}

static int usage_advanced(const char* programName)
{
    DISPLAY(WELCOME_MESSAGE);
    usage(programName);
    DISPLAY( "\n");
    DISPLAY( "Advanced arguments : \n");
    DISPLAY( " -V     : display Version number and exit \n");
    DISPLAY( " -v     : verbose mode; specify multiple times to increase verbosity\n");
    DISPLAY( " -q     : suppress warnings; specify twice to suppress errors too\n");
    DISPLAY( " -c     : force write to standard output, even if it is the console\n");
    DISPLAY( " -l     : print information about zstd compressed files \n");
#ifndef ZSTD_NOCOMPRESS
    DISPLAY( "--ultra : enable levels beyond %i, up to %i (requires more memory)\n", ZSTDCLI_CLEVEL_MAX, ZSTD_maxCLevel());
    DISPLAY( "--long[=#]: enable long distance matching with given window log (default: %u)\n", g_defaultMaxWindowLog);
    DISPLAY( "--fast[=#]: switch to ultra fast compression level (default: %u)\n", 1);
    DISPLAY( "--adapt : dynamically adapt compression level to I/O conditions \n");
#ifdef ZSTD_MULTITHREAD
    DISPLAY( " -T#    : spawns # compression threads (default: 1, 0==# cores) \n");
    DISPLAY( " -B#    : select size of each job (default: 0==automatic) \n");
    DISPLAY( " --rsyncable : compress using a rsync-friendly method (-B sets block size) \n");
#endif
    DISPLAY( "--no-dictID : don't write dictID into header (dictionary compression)\n");
    DISPLAY( "--[no-]check : integrity check (default: enabled) \n");
#endif
#ifdef UTIL_HAS_CREATEFILELIST
    DISPLAY( " -r     : operate recursively on directories \n");
#endif
    DISPLAY( "--format=zstd : compress files to the .zst format (default) \n");
#ifdef ZSTD_GZCOMPRESS
    DISPLAY( "--format=gzip : compress files to the .gz format \n");
#endif
#ifdef ZSTD_LZMACOMPRESS
    DISPLAY( "--format=xz : compress files to the .xz format \n");
    DISPLAY( "--format=lzma : compress files to the .lzma format \n");
#endif
#ifdef ZSTD_LZ4COMPRESS
    DISPLAY( "--format=lz4 : compress files to the .lz4 format \n");
#endif
#ifndef ZSTD_NODECOMPRESS
    DISPLAY( "--test  : test compressed file integrity \n");
#if ZSTD_SPARSE_DEFAULT
    DISPLAY( "--[no-]sparse : sparse mode (default: enabled on file, disabled on stdout)\n");
#else
    DISPLAY( "--[no-]sparse : sparse mode (default: disabled)\n");
#endif
#endif
    DISPLAY( " -M#    : Set a memory usage limit for decompression \n");
    DISPLAY( "--no-progress : do not display the progress bar \n");
    DISPLAY( "--      : All arguments after \"--\" are treated as files \n");
#ifndef ZSTD_NODICT
    DISPLAY( "\n");
    DISPLAY( "Dictionary builder : \n");
    DISPLAY( "--train ## : create a dictionary from a training set of files \n");
    DISPLAY( "--train-cover[=k=#,d=#,steps=#,split=#] : use the cover algorithm with optional args\n");
    DISPLAY( "--train-fastcover[=k=#,d=#,f=#,steps=#,split=#,accel=#] : use the fast cover algorithm with optional args\n");
    DISPLAY( "--train-legacy[=s=#] : use the legacy algorithm with selectivity (default: %u)\n", g_defaultSelectivityLevel);
    DISPLAY( " -o file : `file` is dictionary name (default: %s) \n", g_defaultDictName);
    DISPLAY( "--maxdict=# : limit dictionary to specified size (default: %u) \n", g_defaultMaxDictSize);
    DISPLAY( "--dictID=# : force dictionary ID to specified value (default: random)\n");
#endif
#ifndef ZSTD_NOBENCH
    DISPLAY( "\n");
    DISPLAY( "Benchmark arguments : \n");
    DISPLAY( " -b#    : benchmark file(s), using # compression level (default: %d) \n", ZSTDCLI_CLEVEL_DEFAULT);
    DISPLAY( " -e#    : test all compression levels from -bX to # (default: 1)\n");
    DISPLAY( " -i#    : minimum evaluation time in seconds (default: 3s) \n");
    DISPLAY( " -B#    : cut file into independent blocks of size # (default: no block)\n");
    DISPLAY( "--priority=rt : set process priority to real-time \n");
#endif
    return 0;
}

static int badusage(const char* programName)
{
    DISPLAYLEVEL(1, "Incorrect parameters\n");
    if (g_displayLevel >= 2) usage(programName);
    return 1;
}

static void waitEnter(void)
{
    int unused;
    DISPLAY("Press enter to continue...\n");
    unused = getchar();
    (void)unused;
}

static const char* lastNameFromPath(const char* path)
{
    const char* name = path;
    if (strrchr(name, '/')) name = strrchr(name, '/') + 1;
    if (strrchr(name, '\\')) name = strrchr(name, '\\') + 1; /* windows */
    return name;
}

/*! exeNameMatch() :
    @return : a non-zero value if exeName matches test, excluding the extension
   */
static int exeNameMatch(const char* exeName, const char* test)
{
    return !strncmp(exeName, test, strlen(test)) &&
        (exeName[strlen(test)] == '\0' || exeName[strlen(test)] == '.');
}

static void errorOut(const char* msg)
{
    DISPLAY("%s \n", msg); exit(1);
}

/*! readU32FromCharChecked() :
 * @return 0 if success, and store the result in *value.
 *  allows and interprets K, KB, KiB, M, MB and MiB suffix.
 *  Will also modify `*stringPtr`, advancing it to position where it stopped reading.
 * @return 1 if an overflow error occurs */
static int readU32FromCharChecked(const char** stringPtr, unsigned* value)
{
    static unsigned const max = (((unsigned)(-1)) / 10) - 1;
    unsigned result = 0;
    while ((**stringPtr >='0') && (**stringPtr <='9')) {
        if (result > max) return 1; // overflow error
        result *= 10, result += **stringPtr - '0', (*stringPtr)++ ;
    }
    if ((**stringPtr=='K') || (**stringPtr=='M')) {
        unsigned const maxK = ((unsigned)(-1)) >> 10;
        if (result > maxK) return 1; // overflow error
        result <<= 10;
        if (**stringPtr=='M') {
            if (result > maxK) return 1; // overflow error
            result <<= 10;
        }
        (*stringPtr)++;  /* skip `K` or `M` */
        if (**stringPtr=='i') (*stringPtr)++;
        if (**stringPtr=='B') (*stringPtr)++;
    }
    *value = result;
    return 0;
}

/*! readU32FromChar() :
 * @return : unsigned integer value read from input in `char` format.
 *  allows and interprets K, KB, KiB, M, MB and MiB suffix.
 *  Will also modify `*stringPtr`, advancing it to position where it stopped reading.
 *  Note : function will exit() program if digit sequence overflows */
static unsigned readU32FromChar(const char** stringPtr) {
    static const char errorMsg[] = "error: numeric value too large";
    unsigned result;
    if (readU32FromCharChecked(stringPtr, &result)) { errorOut(errorMsg); }
    return result;
}

/** longCommandWArg() :
 *  check if *stringPtr is the same as longCommand.
 *  If yes, @return 1 and advances *stringPtr to the position which immediately follows longCommand.
 * @return 0 and doesn't modify *stringPtr otherwise.
 */
static unsigned longCommandWArg(const char** stringPtr, const char* longCommand)
{
    size_t const comSize = strlen(longCommand);
    int const result = !strncmp(*stringPtr, longCommand, comSize);
    if (result) *stringPtr += comSize;
    return result;
}


#ifndef ZSTD_NODICT
/**
 * parseCoverParameters() :
 * reads cover parameters from *stringPtr (e.g. "--train-cover=k=48,d=8,steps=32") into *params
 * @return 1 means that cover parameters were correct
 * @return 0 in case of malformed parameters
 */
static unsigned parseCoverParameters(const char* stringPtr, ZDICT_cover_params_t* params)
{
    memset(params, 0, sizeof(*params));
    for (; ;) {
        if (longCommandWArg(&stringPtr, "k=")) { params->k = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "d=")) { params->d = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "steps=")) { params->steps = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "split=")) {
          unsigned splitPercentage = readU32FromChar(&stringPtr);
          params->splitPoint = (double)splitPercentage / 100.0;
          if (stringPtr[0]==',') { stringPtr++; continue; } else break;
        }
        return 0;
    }
    if (stringPtr[0] != 0) return 0;
    DISPLAYLEVEL(4, "cover: k=%u\nd=%u\nsteps=%u\nsplit=%u\n", params->k, params->d, params->steps, (unsigned)(params->splitPoint * 100));
    return 1;
}

/**
 * parseFastCoverParameters() :
 * reads fastcover parameters from *stringPtr (e.g. "--train-fastcover=k=48,d=8,f=20,steps=32,accel=2") into *params
 * @return 1 means that fastcover parameters were correct
 * @return 0 in case of malformed parameters
 */
static unsigned parseFastCoverParameters(const char* stringPtr, ZDICT_fastCover_params_t* params)
{
    memset(params, 0, sizeof(*params));
    for (; ;) {
        if (longCommandWArg(&stringPtr, "k=")) { params->k = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "d=")) { params->d = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "f=")) { params->f = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "steps=")) { params->steps = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "accel=")) { params->accel = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "split=")) {
          unsigned splitPercentage = readU32FromChar(&stringPtr);
          params->splitPoint = (double)splitPercentage / 100.0;
          if (stringPtr[0]==',') { stringPtr++; continue; } else break;
        }
        return 0;
    }
    if (stringPtr[0] != 0) return 0;
    DISPLAYLEVEL(4, "cover: k=%u\nd=%u\nf=%u\nsteps=%u\nsplit=%u\naccel=%u\n", params->k, params->d, params->f, params->steps, (unsigned)(params->splitPoint * 100), params->accel);
    return 1;
}

/**
 * parseLegacyParameters() :
 * reads legacy dictioanry builter parameters from *stringPtr (e.g. "--train-legacy=selectivity=8") into *selectivity
 * @return 1 means that legacy dictionary builder parameters were correct
 * @return 0 in case of malformed parameters
 */
static unsigned parseLegacyParameters(const char* stringPtr, unsigned* selectivity)
{
    if (!longCommandWArg(&stringPtr, "s=") && !longCommandWArg(&stringPtr, "selectivity=")) { return 0; }
    *selectivity = readU32FromChar(&stringPtr);
    if (stringPtr[0] != 0) return 0;
    DISPLAYLEVEL(4, "legacy: selectivity=%u\n", *selectivity);
    return 1;
}

static ZDICT_cover_params_t defaultCoverParams(void)
{
    ZDICT_cover_params_t params;
    memset(&params, 0, sizeof(params));
    params.d = 8;
    params.steps = 4;
    params.splitPoint = 1.0;
    return params;
}

static ZDICT_fastCover_params_t defaultFastCoverParams(void)
{
    ZDICT_fastCover_params_t params;
    memset(&params, 0, sizeof(params));
    params.d = 8;
    params.f = 20;
    params.steps = 4;
    params.splitPoint = 0.75; /* different from default splitPoint of cover */
    params.accel = DEFAULT_ACCEL;
    return params;
}
#endif


/** parseAdaptParameters() :
 *  reads adapt parameters from *stringPtr (e.g. "--zstd=min=1,max=19) and store them into adaptMinPtr and adaptMaxPtr.
 *  Both adaptMinPtr and adaptMaxPtr must be already allocated and correctly initialized.
 *  There is no guarantee that any of these values will be updated.
 *  @return 1 means that parsing was successful,
 *  @return 0 in case of malformed parameters
 */
static unsigned parseAdaptParameters(const char* stringPtr, int* adaptMinPtr, int* adaptMaxPtr)
{
    for ( ; ;) {
        if (longCommandWArg(&stringPtr, "min=")) { *adaptMinPtr = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "max=")) { *adaptMaxPtr = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        DISPLAYLEVEL(4, "invalid compression parameter \n");
        return 0;
    }
    if (stringPtr[0] != 0) return 0; /* check the end of string */
    if (*adaptMinPtr > *adaptMaxPtr) {
        DISPLAYLEVEL(4, "incoherent adaptation limits \n");
        return 0;
    }
    return 1;
}


/** parseCompressionParameters() :
 *  reads compression parameters from *stringPtr (e.g. "--zstd=wlog=23,clog=23,hlog=22,slog=6,mml=3,tlen=48,strat=6") into *params
 *  @return 1 means that compression parameters were correct
 *  @return 0 in case of malformed parameters
 */
static unsigned parseCompressionParameters(const char* stringPtr, ZSTD_compressionParameters* params)
{
    for ( ; ;) {
        if (longCommandWArg(&stringPtr, "windowLog=") || longCommandWArg(&stringPtr, "wlog=")) { params->windowLog = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "chainLog=") || longCommandWArg(&stringPtr, "clog=")) { params->chainLog = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "hashLog=") || longCommandWArg(&stringPtr, "hlog=")) { params->hashLog = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "searchLog=") || longCommandWArg(&stringPtr, "slog=")) { params->searchLog = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "minMatch=") || longCommandWArg(&stringPtr, "mml=")) { params->minMatch = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "targetLength=") || longCommandWArg(&stringPtr, "tlen=")) { params->targetLength = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "strategy=") || longCommandWArg(&stringPtr, "strat=")) { params->strategy = (ZSTD_strategy)(readU32FromChar(&stringPtr)); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "overlapLog=") || longCommandWArg(&stringPtr, "ovlog=")) { g_overlapLog = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "ldmHashLog=") || longCommandWArg(&stringPtr, "lhlog=")) { g_ldmHashLog = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "ldmMinMatch=") || longCommandWArg(&stringPtr, "lmml=")) { g_ldmMinMatch = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "ldmBucketSizeLog=") || longCommandWArg(&stringPtr, "lblog=")) { g_ldmBucketSizeLog = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        if (longCommandWArg(&stringPtr, "ldmHashRateLog=") || longCommandWArg(&stringPtr, "lhrlog=")) { g_ldmHashRateLog = readU32FromChar(&stringPtr); if (stringPtr[0]==',') { stringPtr++; continue; } else break; }
        DISPLAYLEVEL(4, "invalid compression parameter \n");
        return 0;
    }

    DISPLAYLEVEL(4, "windowLog=%d, chainLog=%d, hashLog=%d, searchLog=%d \n", params->windowLog, params->chainLog, params->hashLog, params->searchLog);
    DISPLAYLEVEL(4, "minMatch=%d, targetLength=%d, strategy=%d \n", params->minMatch, params->targetLength, params->strategy);
    if (stringPtr[0] != 0) return 0; /* check the end of string */
    return 1;
}

static void printVersion(void)
{
    DISPLAY(WELCOME_MESSAGE);
    /* format support */
    DISPLAYLEVEL(3, "*** supports: zstd");
#if defined(ZSTD_LEGACY_SUPPORT) && (ZSTD_LEGACY_SUPPORT>0) && (ZSTD_LEGACY_SUPPORT<8)
    DISPLAYLEVEL(3, ", zstd legacy v0.%d+", ZSTD_LEGACY_SUPPORT);
#endif
#ifdef ZSTD_GZCOMPRESS
    DISPLAYLEVEL(3, ", gzip");
#endif
#ifdef ZSTD_LZ4COMPRESS
    DISPLAYLEVEL(3, ", lz4");
#endif
#ifdef ZSTD_LZMACOMPRESS
    DISPLAYLEVEL(3, ", lzma, xz ");
#endif
    DISPLAYLEVEL(3, "\n");
    /* posix support */
#ifdef _POSIX_C_SOURCE
    DISPLAYLEVEL(4, "_POSIX_C_SOURCE defined: %ldL\n", (long) _POSIX_C_SOURCE);
#endif
#ifdef _POSIX_VERSION
    DISPLAYLEVEL(4, "_POSIX_VERSION defined: %ldL \n", (long) _POSIX_VERSION);
#endif
#ifdef PLATFORM_POSIX_VERSION
    DISPLAYLEVEL(4, "PLATFORM_POSIX_VERSION defined: %ldL\n", (long) PLATFORM_POSIX_VERSION);
#endif
}

/* Environment variables for parameter setting */
#define ENV_CLEVEL "ZSTD_CLEVEL"

/* functions that pick up environment variables */
static int init_cLevel(void) {
    const char* const env = getenv(ENV_CLEVEL);
    if (env) {
        const char *ptr = env;
        int sign = 1;
        if (*ptr == '-') {
            sign = -1;
            ptr++;
        } else if (*ptr == '+') {
            ptr++;
        }

        if ((*ptr>='0') && (*ptr<='9')) {
            unsigned absLevel;
            if (readU32FromCharChecked(&ptr, &absLevel)) { 
                DISPLAYLEVEL(2, "Ignore environment variable setting %s=%s: numeric value too large\n", ENV_CLEVEL, env);
                return ZSTDCLI_CLEVEL_DEFAULT;
            } else if (*ptr == 0) {
                return sign * absLevel;
            }
        }

        DISPLAYLEVEL(2, "Ignore environment variable setting %s=%s: not a valid integer value\n", ENV_CLEVEL, env);
    }

    return ZSTDCLI_CLEVEL_DEFAULT;
}

typedef enum { zom_compress, zom_decompress, zom_test, zom_bench, zom_train, zom_list } zstd_operation_mode;

#define CLEAN_RETURN(i) { operationResult = (i); goto _end; }

#ifdef ZSTD_NOCOMPRESS
/* symbols from compression library are not defined and should not be invoked */
# define MINCLEVEL  -50
# define MAXCLEVEL   22
#else
# define MINCLEVEL  ZSTD_minCLevel()
# define MAXCLEVEL  ZSTD_maxCLevel()
#endif

int main(int argCount, const char* argv[])
{
    int argNb,
        followLinks = 0,
        forceStdout = 0,
        lastCommand = 0,
        ldmFlag = 0,
        main_pause = 0,
        nbWorkers = 0,
        adapt = 0,
        adaptMin = MINCLEVEL,
        adaptMax = MAXCLEVEL,
        rsyncable = 0,
        nextArgumentIsOutFileName = 0,
        nextArgumentIsMaxDict = 0,
        nextArgumentIsDictID = 0,
        nextArgumentsAreFiles = 0,
        nextEntryIsDictionary = 0,
        operationResult = 0,
        separateFiles = 0,
        setRealTimePrio = 0,
        singleThread = 0,
        ultra=0;
    double compressibility = 0.5;
    unsigned bench_nbSeconds = 3;   /* would be better if this value was synchronized from bench */
    size_t blockSize = 0;
    zstd_operation_mode operation = zom_compress;
    ZSTD_compressionParameters compressionParams;
    int cLevel;
    int cLevelLast = -1000000000;
    unsigned recursive = 0;
    unsigned memLimit = 0;
    const char** filenameTable = (const char**)malloc(argCount * sizeof(const char*));   /* argCount >= 1 */
    unsigned filenameIdx = 0;
    const char* programName = argv[0];
    const char* outFileName = NULL;
    const char* dictFileName = NULL;
    const char* suffix = ZSTD_EXTENSION;
    unsigned maxDictSize = g_defaultMaxDictSize;
    unsigned dictID = 0;
    int dictCLevel = g_defaultDictCLevel;
    unsigned dictSelect = g_defaultSelectivityLevel;
#ifdef UTIL_HAS_CREATEFILELIST
    const char** extendedFileList = NULL;
    char* fileNamesBuf = NULL;
    unsigned fileNamesNb;
#endif
#ifndef ZSTD_NODICT
    ZDICT_cover_params_t coverParams = defaultCoverParams();
    ZDICT_fastCover_params_t fastCoverParams = defaultFastCoverParams();
    dictType dict = fastCover;
#endif
#ifndef ZSTD_NOBENCH
    BMK_advancedParams_t benchParams = BMK_initAdvancedParams();
#endif


    /* init */
    (void)recursive; (void)cLevelLast;    /* not used when ZSTD_NOBENCH set */
    (void)memLimit;   /* not used when ZSTD_NODECOMPRESS set */
    if (filenameTable==NULL) { DISPLAY("zstd: %s \n", strerror(errno)); exit(1); }
    filenameTable[0] = stdinmark;
    g_displayOut = stderr;
    cLevel = init_cLevel();
    programName = lastNameFromPath(programName);
#ifdef ZSTD_MULTITHREAD
    nbWorkers = 1;
#endif

    /* preset behaviors */
    if (exeNameMatch(programName, ZSTD_ZSTDMT)) nbWorkers=0, singleThread=0;
    if (exeNameMatch(programName, ZSTD_UNZSTD)) operation=zom_decompress;
    if (exeNameMatch(programName, ZSTD_CAT)) { operation=zom_decompress; forceStdout=1; FIO_overwriteMode(); outFileName=stdoutmark; g_displayLevel=1; }   /* supports multiple formats */
    if (exeNameMatch(programName, ZSTD_ZCAT)) { operation=zom_decompress; forceStdout=1; FIO_overwriteMode(); outFileName=stdoutmark; g_displayLevel=1; }  /* behave like zcat, also supports multiple formats */
    if (exeNameMatch(programName, ZSTD_GZ)) { suffix = GZ_EXTENSION; FIO_setCompressionType(FIO_gzipCompression); FIO_setRemoveSrcFile(1); }               /* behave like gzip */
    if (exeNameMatch(programName, ZSTD_GUNZIP)) { operation=zom_decompress; FIO_setRemoveSrcFile(1); }                                                     /* behave like gunzip, also supports multiple formats */
    if (exeNameMatch(programName, ZSTD_GZCAT)) { operation=zom_decompress; forceStdout=1; FIO_overwriteMode(); outFileName=stdoutmark; g_displayLevel=1; } /* behave like gzcat, also supports multiple formats */
    if (exeNameMatch(programName, ZSTD_LZMA)) { suffix = LZMA_EXTENSION; FIO_setCompressionType(FIO_lzmaCompression); FIO_setRemoveSrcFile(1); }           /* behave like lzma */
    if (exeNameMatch(programName, ZSTD_UNLZMA)) { operation=zom_decompress; FIO_setCompressionType(FIO_lzmaCompression); FIO_setRemoveSrcFile(1); }        /* behave like unlzma, also supports multiple formats */
    if (exeNameMatch(programName, ZSTD_XZ)) { suffix = XZ_EXTENSION; FIO_setCompressionType(FIO_xzCompression); FIO_setRemoveSrcFile(1); }                 /* behave like xz */
    if (exeNameMatch(programName, ZSTD_UNXZ)) { operation=zom_decompress; FIO_setCompressionType(FIO_xzCompression); FIO_setRemoveSrcFile(1); }            /* behave like unxz, also supports multiple formats */
    if (exeNameMatch(programName, ZSTD_LZ4)) { suffix = LZ4_EXTENSION; FIO_setCompressionType(FIO_lz4Compression); }                                       /* behave like lz4 */
    if (exeNameMatch(programName, ZSTD_UNLZ4)) { operation=zom_decompress; FIO_setCompressionType(FIO_lz4Compression); }                                   /* behave like unlz4, also supports multiple formats */
    memset(&compressionParams, 0, sizeof(compressionParams));

    /* init crash handler */
    FIO_addAbortHandler();

    /* command switches */
    for (argNb=1; argNb<argCount; argNb++) {
        const char* argument = argv[argNb];
        if(!argument) continue;   /* Protection if argument empty */

        if (nextArgumentsAreFiles==0) {
            /* "-" means stdin/stdout */
            if (!strcmp(argument, "-")){
                if (!filenameIdx) {
                    filenameIdx=1, filenameTable[0]=stdinmark;
                    outFileName=stdoutmark;
                    g_displayLevel-=(g_displayLevel==2);
                    continue;
            }   }

            /* Decode commands (note : aggregated commands are allowed) */
            if (argument[0]=='-') {

                if (argument[1]=='-') {
                    /* long commands (--long-word) */
                    if (!strcmp(argument, "--")) { nextArgumentsAreFiles=1; continue; }   /* only file names allowed from now on */
                    if (!strcmp(argument, "--list")) { operation=zom_list; continue; }
                    if (!strcmp(argument, "--compress")) { operation=zom_compress; continue; }
                    if (!strcmp(argument, "--decompress")) { operation=zom_decompress; continue; }
                    if (!strcmp(argument, "--uncompress")) { operation=zom_decompress; continue; }
                    if (!strcmp(argument, "--force")) { FIO_overwriteMode(); forceStdout=1; followLinks=1; continue; }
                    if (!strcmp(argument, "--version")) { g_displayOut=stdout; DISPLAY(WELCOME_MESSAGE); CLEAN_RETURN(0); }
                    if (!strcmp(argument, "--help")) { g_displayOut=stdout; CLEAN_RETURN(usage_advanced(programName)); }
                    if (!strcmp(argument, "--verbose")) { g_displayLevel++; continue; }
                    if (!strcmp(argument, "--quiet")) { g_displayLevel--; continue; }
                    if (!strcmp(argument, "--stdout")) { forceStdout=1; outFileName=stdoutmark; g_displayLevel-=(g_displayLevel==2); continue; }
                    if (!strcmp(argument, "--ultra")) { ultra=1; continue; }
                    if (!strcmp(argument, "--check")) { FIO_setChecksumFlag(2); continue; }
                    if (!strcmp(argument, "--no-check")) { FIO_setChecksumFlag(0); continue; }
                    if (!strcmp(argument, "--sparse")) { FIO_setSparseWrite(2); continue; }
                    if (!strcmp(argument, "--no-sparse")) { FIO_setSparseWrite(0); continue; }
                    if (!strcmp(argument, "--test")) { operation=zom_test; continue; }
                    if (!strcmp(argument, "--train")) { operation=zom_train; if (outFileName==NULL) outFileName=g_defaultDictName; continue; }
                    if (!strcmp(argument, "--maxdict")) { nextArgumentIsMaxDict=1; lastCommand=1; continue; }  /* kept available for compatibility with old syntax ; will be removed one day */
                    if (!strcmp(argument, "--dictID")) { nextArgumentIsDictID=1; lastCommand=1; continue; }  /* kept available for compatibility with old syntax ; will be removed one day */
                    if (!strcmp(argument, "--no-dictID")) { FIO_setDictIDFlag(0); continue; }
                    if (!strcmp(argument, "--keep")) { FIO_setRemoveSrcFile(0); continue; }
                    if (!strcmp(argument, "--rm")) { FIO_setRemoveSrcFile(1); continue; }
                    if (!strcmp(argument, "--priority=rt")) { setRealTimePrio = 1; continue; }
                    if (!strcmp(argument, "--adapt")) { adapt = 1; continue; }
                    if (longCommandWArg(&argument, "--adapt=")) { adapt = 1; if (!parseAdaptParameters(argument, &adaptMin, &adaptMax)) CLEAN_RETURN(badusage(programName)); continue; }
                    if (!strcmp(argument, "--single-thread")) { nbWorkers = 0; singleThread = 1; continue; }
                    if (!strcmp(argument, "--format=zstd")) { suffix = ZSTD_EXTENSION; FIO_setCompressionType(FIO_zstdCompression); continue; }
#ifdef ZSTD_GZCOMPRESS
                    if (!strcmp(argument, "--format=gzip")) { suffix = GZ_EXTENSION; FIO_setCompressionType(FIO_gzipCompression); continue; }
#endif
#ifdef ZSTD_LZMACOMPRESS
                    if (!strcmp(argument, "--format=lzma")) { suffix = LZMA_EXTENSION; FIO_setCompressionType(FIO_lzmaCompression);  continue; }
                    if (!strcmp(argument, "--format=xz")) { suffix = XZ_EXTENSION; FIO_setCompressionType(FIO_xzCompression);  continue; }
#endif
#ifdef ZSTD_LZ4COMPRESS
                    if (!strcmp(argument, "--format=lz4")) { suffix = LZ4_EXTENSION; FIO_setCompressionType(FIO_lz4Compression);  continue; }
#endif
                    if (!strcmp(argument, "--rsyncable")) { rsyncable = 1; continue; }
                    if (!strcmp(argument, "--no-progress")) { FIO_setNoProgress(1); continue; }

                    /* long commands with arguments */
#ifndef ZSTD_NODICT
                    if (longCommandWArg(&argument, "--train-cover")) {
                      operation = zom_train;
                      if (outFileName == NULL)
                          outFileName = g_defaultDictName;
                      dict = cover;
                      /* Allow optional arguments following an = */
                      if (*argument == 0) { memset(&coverParams, 0, sizeof(coverParams)); }
                      else if (*argument++ != '=') { CLEAN_RETURN(badusage(programName)); }
                      else if (!parseCoverParameters(argument, &coverParams)) { CLEAN_RETURN(badusage(programName)); }
                      continue;
                    }
                    if (longCommandWArg(&argument, "--train-fastcover")) {
                      operation = zom_train;
                      if (outFileName == NULL)
                          outFileName = g_defaultDictName;
                      dict = fastCover;
                      /* Allow optional arguments following an = */
                      if (*argument == 0) { memset(&fastCoverParams, 0, sizeof(fastCoverParams)); }
                      else if (*argument++ != '=') { CLEAN_RETURN(badusage(programName)); }
                      else if (!parseFastCoverParameters(argument, &fastCoverParams)) { CLEAN_RETURN(badusage(programName)); }
                      continue;
                    }
                    if (longCommandWArg(&argument, "--train-legacy")) {
                      operation = zom_train;
                      if (outFileName == NULL)
                          outFileName = g_defaultDictName;
                      dict = legacy;
                      /* Allow optional arguments following an = */
                      if (*argument == 0) { continue; }
                      else if (*argument++ != '=') { CLEAN_RETURN(badusage(programName)); }
                      else if (!parseLegacyParameters(argument, &dictSelect)) { CLEAN_RETURN(badusage(programName)); }
                      continue;
                    }
#endif
                    if (longCommandWArg(&argument, "--threads=")) { nbWorkers = readU32FromChar(&argument); continue; }
                    if (longCommandWArg(&argument, "--memlimit=")) { memLimit = readU32FromChar(&argument); continue; }
                    if (longCommandWArg(&argument, "--memory=")) { memLimit = readU32FromChar(&argument); continue; }
                    if (longCommandWArg(&argument, "--memlimit-decompress=")) { memLimit = readU32FromChar(&argument); continue; }
                    if (longCommandWArg(&argument, "--block-size=")) { blockSize = readU32FromChar(&argument); continue; }
                    if (longCommandWArg(&argument, "--maxdict=")) { maxDictSize = readU32FromChar(&argument); continue; }
                    if (longCommandWArg(&argument, "--dictID=")) { dictID = readU32FromChar(&argument); continue; }
                    if (longCommandWArg(&argument, "--zstd=")) { if (!parseCompressionParameters(argument, &compressionParams)) CLEAN_RETURN(badusage(programName)); continue; }
                    if (longCommandWArg(&argument, "--long")) {
                        unsigned ldmWindowLog = 0;
                        ldmFlag = 1;
                        /* Parse optional window log */
                        if (*argument == '=') {
                            ++argument;
                            ldmWindowLog = readU32FromChar(&argument);
                        } else if (*argument != 0) {
                            /* Invalid character following --long */
                            CLEAN_RETURN(badusage(programName));
                        }
                        /* Only set windowLog if not already set by --zstd */
                        if (compressionParams.windowLog == 0)
                            compressionParams.windowLog = ldmWindowLog;
                        continue;
                    }
#ifndef ZSTD_NOCOMPRESS   /* linking ZSTD_minCLevel() requires compression support */
                    if (longCommandWArg(&argument, "--fast")) {
                        /* Parse optional acceleration factor */
                        if (*argument == '=') {
                            U32 const maxFast = (U32)-ZSTD_minCLevel();
                            U32 fastLevel;
                            ++argument;
                            fastLevel = readU32FromChar(&argument);
                            if (fastLevel > maxFast) fastLevel = maxFast;
                            if (fastLevel) {
                              dictCLevel = cLevel = -(int)fastLevel;
                            } else {
                              CLEAN_RETURN(badusage(programName));
                            }
                        } else if (*argument != 0) {
                            /* Invalid character following --fast */
                            CLEAN_RETURN(badusage(programName));
                        } else {
                            cLevel = -1;  /* default for --fast */
                        }
                        continue;
                    }
#endif
                    /* fall-through, will trigger bad_usage() later on */
                }

                argument++;
                while (argument[0]!=0) {
                    if (lastCommand) {
                        DISPLAY("error : command must be followed by argument \n");
                        CLEAN_RETURN(1);
                    }
#ifndef ZSTD_NOCOMPRESS
                    /* compression Level */
                    if ((*argument>='0') && (*argument<='9')) {
                        dictCLevel = cLevel = readU32FromChar(&argument);
                        continue;
                    }
#endif

                    switch(argument[0])
                    {
                        /* Display help */
                    case 'V': g_displayOut=stdout; printVersion(); CLEAN_RETURN(0);   /* Version Only */
                    case 'H':
                    case 'h': g_displayOut=stdout; CLEAN_RETURN(usage_advanced(programName));

                         /* Compress */
                    case 'z': operation=zom_compress; argument++; break;

                         /* Decoding */
                    case 'd':
#ifndef ZSTD_NOBENCH
                            benchParams.mode = BMK_decodeOnly;
                            if (operation==zom_bench) { argument++; break; }  /* benchmark decode (hidden option) */
#endif
                            operation=zom_decompress; argument++; break;

                        /* Force stdout, even if stdout==console */
                    case 'c': forceStdout=1; outFileName=stdoutmark; argument++; break;

                        /* Use file content as dictionary */
                    case 'D': nextEntryIsDictionary = 1; lastCommand = 1; argument++; break;

                        /* Overwrite */
                    case 'f': FIO_overwriteMode(); forceStdout=1; followLinks=1; argument++; break;

                        /* Verbose mode */
                    case 'v': g_displayLevel++; argument++; break;

                        /* Quiet mode */
                    case 'q': g_displayLevel--; argument++; break;

                        /* keep source file (default) */
                    case 'k': FIO_setRemoveSrcFile(0); argument++; break;

                        /* Checksum */
                    case 'C': FIO_setChecksumFlag(2); argument++; break;

                        /* test compressed file */
                    case 't': operation=zom_test; argument++; break;

                        /* destination file name */
                    case 'o': nextArgumentIsOutFileName=1; lastCommand=1; argument++; break;

                        /* limit decompression memory */
                    case 'M':
                        argument++;
                        memLimit = readU32FromChar(&argument);
                        break;
                    case 'l': operation=zom_list; argument++; break;
#ifdef UTIL_HAS_CREATEFILELIST
                        /* recursive */
                    case 'r': recursive=1; argument++; break;
#endif

#ifndef ZSTD_NOBENCH
                        /* Benchmark */
                    case 'b':
                        operation=zom_bench;
                        argument++;
                        break;

                        /* range bench (benchmark only) */
                    case 'e':
                        /* compression Level */
                        argument++;
                        cLevelLast = readU32FromChar(&argument);
                        break;

                        /* Modify Nb Iterations (benchmark only) */
                    case 'i':
                        argument++;
                        bench_nbSeconds = readU32FromChar(&argument);
                        break;

                        /* cut input into blocks (benchmark only) */
                    case 'B':
                        argument++;
                        blockSize = readU32FromChar(&argument);
                        break;

                        /* benchmark files separately (hidden option) */
                    case 'S':
                        argument++;
                        separateFiles = 1;
                        break;

#endif   /* ZSTD_NOBENCH */

                        /* nb of threads (hidden option) */
                    case 'T':
                        argument++;
                        nbWorkers = readU32FromChar(&argument);
                        break;

                        /* Dictionary Selection level */
                    case 's':
                        argument++;
                        dictSelect = readU32FromChar(&argument);
                        break;

                        /* Pause at the end (-p) or set an additional param (-p#) (hidden option) */
                    case 'p': argument++;
#ifndef ZSTD_NOBENCH
                        if ((*argument>='0') && (*argument<='9')) {
                            benchParams.additionalParam = (int)readU32FromChar(&argument);
                        } else
#endif
                            main_pause=1;
                        break;

                        /* Select compressibility of synthetic sample */
                    case 'P':
                    {   argument++;
                        compressibility = (double)readU32FromChar(&argument) / 100;
                    }
                    break;

                        /* unknown command */
                    default : CLEAN_RETURN(badusage(programName));
                    }
                }
                continue;
            }   /* if (argument[0]=='-') */

            if (nextArgumentIsMaxDict) {  /* kept available for compatibility with old syntax ; will be removed one day */
                nextArgumentIsMaxDict = 0;
                lastCommand = 0;
                maxDictSize = readU32FromChar(&argument);
                continue;
            }

            if (nextArgumentIsDictID) {  /* kept available for compatibility with old syntax ; will be removed one day */
                nextArgumentIsDictID = 0;
                lastCommand = 0;
                dictID = readU32FromChar(&argument);
                continue;
            }

        }   /* if (nextArgumentIsAFile==0) */

        if (nextEntryIsDictionary) {
            nextEntryIsDictionary = 0;
            lastCommand = 0;
            dictFileName = argument;
            continue;
        }

        if (nextArgumentIsOutFileName) {
            nextArgumentIsOutFileName = 0;
            lastCommand = 0;
            outFileName = argument;
            if (!strcmp(outFileName, "-")) outFileName = stdoutmark;
            continue;
        }

        /* add filename to list */
        filenameTable[filenameIdx++] = argument;
    }

    if (lastCommand) { /* forgotten argument */
        DISPLAY("error : command must be followed by argument \n");
        CLEAN_RETURN(1);
    }

    /* Welcome message (if verbose) */
    DISPLAYLEVEL(3, WELCOME_MESSAGE);

#ifdef ZSTD_MULTITHREAD
    if ((nbWorkers==0) && (!singleThread)) {
        /* automatically set # workers based on # of reported cpus */
        nbWorkers = UTIL_countPhysicalCores();
        DISPLAYLEVEL(3, "Note: %d physical core(s) detected \n", nbWorkers);
    }
#else
    (void)singleThread; (void)nbWorkers;
#endif

#ifdef UTIL_HAS_CREATEFILELIST
    g_utilDisplayLevel = g_displayLevel;
    if (!followLinks) {
        unsigned u;
        for (u=0, fileNamesNb=0; u<filenameIdx; u++) {
            if (UTIL_isLink(filenameTable[u])) {
                DISPLAYLEVEL(2, "Warning : %s is a symbolic link, ignoring\n", filenameTable[u]);
            } else {
                filenameTable[fileNamesNb++] = filenameTable[u];
            }
        }
        filenameIdx = fileNamesNb;
    }
    if (recursive) {  /* at this stage, filenameTable is a list of paths, which can contain both files and directories */
        extendedFileList = UTIL_createFileList(filenameTable, filenameIdx, &fileNamesBuf, &fileNamesNb, followLinks);
        if (extendedFileList) {
            unsigned u;
            for (u=0; u<fileNamesNb; u++) DISPLAYLEVEL(4, "%u %s\n", u, extendedFileList[u]);
            free((void*)filenameTable);
            filenameTable = extendedFileList;
            filenameIdx = fileNamesNb;
        }
    }
#else
    (void)followLinks;
#endif

    if (operation == zom_list) {
#ifndef ZSTD_NODECOMPRESS
        int const ret = FIO_listMultipleFiles(filenameIdx, filenameTable, g_displayLevel);
        CLEAN_RETURN(ret);
#else
        DISPLAY("file information is not supported \n");
        CLEAN_RETURN(1);
#endif
    }

    /* Check if benchmark is selected */
    if (operation==zom_bench) {
#ifndef ZSTD_NOBENCH
        benchParams.blockSize = blockSize;
        benchParams.nbWorkers = nbWorkers;
        benchParams.realTime = setRealTimePrio;
        benchParams.nbSeconds = bench_nbSeconds;
        benchParams.ldmFlag = ldmFlag;
        benchParams.ldmMinMatch = g_ldmMinMatch;
        benchParams.ldmHashLog = g_ldmHashLog;
        if (g_ldmBucketSizeLog != LDM_PARAM_DEFAULT) {
            benchParams.ldmBucketSizeLog = g_ldmBucketSizeLog;
        }
        if (g_ldmHashRateLog != LDM_PARAM_DEFAULT) {
            benchParams.ldmHashRateLog = g_ldmHashRateLog;
        }

        if (cLevel > ZSTD_maxCLevel()) cLevel = ZSTD_maxCLevel();
        if (cLevelLast > ZSTD_maxCLevel()) cLevelLast = ZSTD_maxCLevel();
        if (cLevelLast < cLevel) cLevelLast = cLevel;
        if (cLevelLast > cLevel)
            DISPLAYLEVEL(3, "Benchmarking levels from %d to %d\n", cLevel, cLevelLast);
        if(filenameIdx) {
            if(separateFiles) {
                unsigned i;
                for(i = 0; i < filenameIdx; i++) {
                    int c;
                    DISPLAYLEVEL(3, "Benchmarking %s \n", filenameTable[i]);
                    for(c = cLevel; c <= cLevelLast; c++) {
                        BMK_benchFilesAdvanced(&filenameTable[i], 1, dictFileName, c, &compressionParams, g_displayLevel, &benchParams);
                    }
                }
            } else {
                for(; cLevel <= cLevelLast; cLevel++) {
                    BMK_benchFilesAdvanced(filenameTable, filenameIdx, dictFileName, cLevel, &compressionParams, g_displayLevel, &benchParams);
                }
            }
        } else {
            for(; cLevel <= cLevelLast; cLevel++) {
                BMK_syntheticTest(cLevel, compressibility, &compressionParams, g_displayLevel, &benchParams);
            }
        }

#else
        (void)bench_nbSeconds; (void)blockSize; (void)setRealTimePrio; (void)separateFiles; (void)compressibility;
#endif
        goto _end;
    }

    /* Check if dictionary builder is selected */
    if (operation==zom_train) {
#ifndef ZSTD_NODICT
        ZDICT_params_t zParams;
        zParams.compressionLevel = dictCLevel;
        zParams.notificationLevel = g_displayLevel;
        zParams.dictID = dictID;
        if (dict == cover) {
            int const optimize = !coverParams.k || !coverParams.d;
            coverParams.nbThreads = nbWorkers;
            coverParams.zParams = zParams;
            operationResult = DiB_trainFromFiles(outFileName, maxDictSize, filenameTable, filenameIdx, blockSize, NULL, &coverParams, NULL, optimize);
        } else if (dict == fastCover) {
            int const optimize = !fastCoverParams.k || !fastCoverParams.d;
            fastCoverParams.nbThreads = nbWorkers;
            fastCoverParams.zParams = zParams;
            operationResult = DiB_trainFromFiles(outFileName, maxDictSize, filenameTable, filenameIdx, blockSize, NULL, NULL, &fastCoverParams, optimize);
        } else {
            ZDICT_legacy_params_t dictParams;
            memset(&dictParams, 0, sizeof(dictParams));
            dictParams.selectivityLevel = dictSelect;
            dictParams.zParams = zParams;
            operationResult = DiB_trainFromFiles(outFileName, maxDictSize, filenameTable, filenameIdx, blockSize, &dictParams, NULL, NULL, 0);
        }
#else
        (void)dictCLevel; (void)dictSelect; (void)dictID;  (void)maxDictSize; /* not used when ZSTD_NODICT set */
        DISPLAYLEVEL(1, "training mode not available \n");
        operationResult = 1;
#endif
        goto _end;
    }

#ifndef ZSTD_NODECOMPRESS
    if (operation==zom_test) { outFileName=nulmark; FIO_setRemoveSrcFile(0); }  /* test mode */
#endif

    /* No input filename ==> use stdin and stdout */
    filenameIdx += !filenameIdx;   /* filenameTable[0] is stdin by default */
    if (!strcmp(filenameTable[0], stdinmark) && !outFileName)
        outFileName = stdoutmark;  /* when input is stdin, default output is stdout */

    /* Check if input/output defined as console; trigger an error in this case */
    if (!strcmp(filenameTable[0], stdinmark) && IS_CONSOLE(stdin) )
        CLEAN_RETURN(badusage(programName));
    if ( outFileName && !strcmp(outFileName, stdoutmark)
      && IS_CONSOLE(stdout)
      && !strcmp(filenameTable[0], stdinmark)
      && !forceStdout
      && operation!=zom_decompress )
        CLEAN_RETURN(badusage(programName));

#ifndef ZSTD_NOCOMPRESS
    /* check compression level limits */
    {   int const maxCLevel = ultra ? ZSTD_maxCLevel() : ZSTDCLI_CLEVEL_MAX;
        if (cLevel > maxCLevel) {
            DISPLAYLEVEL(2, "Warning : compression level higher than max, reduced to %i \n", maxCLevel);
            cLevel = maxCLevel;
    }   }
#endif

    /* No status message in pipe mode (stdin - stdout) or multi-files mode */
    if (!strcmp(filenameTable[0], stdinmark) && outFileName && !strcmp(outFileName,stdoutmark) && (g_displayLevel==2)) g_displayLevel=1;
    if ((filenameIdx>1) & (g_displayLevel==2)) g_displayLevel=1;

    /* IO Stream/File */
    FIO_setNotificationLevel(g_displayLevel);
    if (operation==zom_compress) {
#ifndef ZSTD_NOCOMPRESS
        FIO_setNbWorkers(nbWorkers);
        FIO_setBlockSize((U32)blockSize);
        if (g_overlapLog!=OVERLAP_LOG_DEFAULT) FIO_setOverlapLog(g_overlapLog);
        FIO_setLdmFlag(ldmFlag);
        FIO_setLdmHashLog(g_ldmHashLog);
        FIO_setLdmMinMatch(g_ldmMinMatch);
        if (g_ldmBucketSizeLog != LDM_PARAM_DEFAULT) FIO_setLdmBucketSizeLog(g_ldmBucketSizeLog);
        if (g_ldmHashRateLog != LDM_PARAM_DEFAULT) FIO_setLdmHashRateLog(g_ldmHashRateLog);
        FIO_setAdaptiveMode(adapt);
        FIO_setAdaptMin(adaptMin);
        FIO_setAdaptMax(adaptMax);
        FIO_setRsyncable(rsyncable);
        if (adaptMin > cLevel) cLevel = adaptMin;
        if (adaptMax < cLevel) cLevel = adaptMax;

        if ((filenameIdx==1) && outFileName)
          operationResult = FIO_compressFilename(outFileName, filenameTable[0], dictFileName, cLevel, compressionParams);
        else
          operationResult = FIO_compressMultipleFilenames(filenameTable, filenameIdx, outFileName, suffix, dictFileName, cLevel, compressionParams);
#else
        (void)suffix; (void)adapt; (void)rsyncable; (void)ultra; (void)cLevel; (void)ldmFlag; /* not used when ZSTD_NOCOMPRESS set */
        DISPLAY("Compression not supported \n");
#endif
    } else {  /* decompression or test */
#ifndef ZSTD_NODECOMPRESS
        if (memLimit == 0) {
            if (compressionParams.windowLog == 0)
                memLimit = (U32)1 << g_defaultMaxWindowLog;
            else {
                memLimit = (U32)1 << (compressionParams.windowLog & 31);
            }
        }
        FIO_setMemLimit(memLimit);
        if (filenameIdx==1 && outFileName)
            operationResult = FIO_decompressFilename(outFileName, filenameTable[0], dictFileName);
        else
            operationResult = FIO_decompressMultipleFilenames(filenameTable, filenameIdx, outFileName, dictFileName);
#else
        DISPLAY("Decompression not supported \n");
#endif
    }

_end:
    if (main_pause) waitEnter();
#ifdef UTIL_HAS_CREATEFILELIST
    if (extendedFileList)
        UTIL_freeFileList(extendedFileList, fileNamesBuf);
    else
#endif
        free((void*)filenameTable);
    return operationResult;
}
