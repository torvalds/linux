/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#pragma once

#include "ErrorHolder.h"
#include "Logging.h"
#include "Options.h"
#include "utils/Buffer.h"
#include "utils/Range.h"
#include "utils/ResourcePool.h"
#include "utils/ThreadPool.h"
#include "utils/WorkQueue.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#undef ZSTD_STATIC_LINKING_ONLY

#include <cstddef>
#include <cstdint>
#include <memory>

namespace pzstd {
/**
 * Runs pzstd with `options` and returns the number of bytes written.
 * An error occurred if `errorHandler.hasError()`.
 *
 * @param options      The pzstd options to use for (de)compression
 * @returns            0 upon success and non-zero on failure.
 */
int pzstdMain(const Options& options);

class SharedState {
 public:
  SharedState(const Options& options) : log(options.verbosity) {
    if (!options.decompress) {
      auto parameters = options.determineParameters();
      cStreamPool.reset(new ResourcePool<ZSTD_CStream>{
          [this, parameters]() -> ZSTD_CStream* {
            this->log(VERBOSE, "%s\n", "Creating new ZSTD_CStream");
            auto zcs = ZSTD_createCStream();
            if (zcs) {
              auto err = ZSTD_initCStream_advanced(
                  zcs, nullptr, 0, parameters, 0);
              if (ZSTD_isError(err)) {
                ZSTD_freeCStream(zcs);
                return nullptr;
              }
            }
            return zcs;
          },
          [](ZSTD_CStream *zcs) {
            ZSTD_freeCStream(zcs);
          }});
    } else {
      dStreamPool.reset(new ResourcePool<ZSTD_DStream>{
          [this]() -> ZSTD_DStream* {
            this->log(VERBOSE, "%s\n", "Creating new ZSTD_DStream");
            auto zds = ZSTD_createDStream();
            if (zds) {
              auto err = ZSTD_initDStream(zds);
              if (ZSTD_isError(err)) {
                ZSTD_freeDStream(zds);
                return nullptr;
              }
            }
            return zds;
          },
          [](ZSTD_DStream *zds) {
            ZSTD_freeDStream(zds);
          }});
    }
  }

  ~SharedState() {
    // The resource pools have references to this, so destroy them first.
    cStreamPool.reset();
    dStreamPool.reset();
  }

  Logger log;
  ErrorHolder errorHolder;
  std::unique_ptr<ResourcePool<ZSTD_CStream>> cStreamPool;
  std::unique_ptr<ResourcePool<ZSTD_DStream>> dStreamPool;
};

/**
 * Streams input from `fd`, breaks input up into chunks, and compresses each
 * chunk independently.  Output of each chunk gets streamed to a queue, and
 * the output queues get put into `chunks` in order.
 *
 * @param state        The shared state
 * @param chunks       Each compression jobs output queue gets `pushed()` here
 *                      as soon as it is available
 * @param executor     The thread pool to run compression jobs in
 * @param fd           The input file descriptor
 * @param size         The size of the input file if known, 0 otherwise
 * @param numThreads   The number of threads in the thread pool
 * @param parameters   The zstd parameters to use for compression
 * @returns            The number of bytes read from the file
 */
std::uint64_t asyncCompressChunks(
    SharedState& state,
    WorkQueue<std::shared_ptr<BufferWorkQueue>>& chunks,
    ThreadPool& executor,
    FILE* fd,
    std::uintmax_t size,
    std::size_t numThreads,
    ZSTD_parameters parameters);

/**
 * Streams input from `fd`.  If pzstd headers are available it breaks the input
 * up into independent frames.  It sends each frame to an independent
 * decompression job.  Output of each frame gets streamed to a queue, and
 * the output queues get put into `frames` in order.
 *
 * @param state        The shared state
 * @param frames       Each decompression jobs output queue gets `pushed()` here
 *                      as soon as it is available
 * @param executor     The thread pool to run compression jobs in
 * @param fd           The input file descriptor
 * @returns            The number of bytes read from the file
 */
std::uint64_t asyncDecompressFrames(
    SharedState& state,
    WorkQueue<std::shared_ptr<BufferWorkQueue>>& frames,
    ThreadPool& executor,
    FILE* fd);

/**
 * Streams input in from each queue in `outs` in order, and writes the data to
 * `outputFd`.
 *
 * @param state        The shared state
 * @param outs         A queue of output queues, one for each
 *                      (de)compression job.
 * @param outputFd     The file descriptor to write to
 * @param decompress   Are we decompressing?
 * @returns            The number of bytes written
 */
std::uint64_t writeFile(
    SharedState& state,
    WorkQueue<std::shared_ptr<BufferWorkQueue>>& outs,
    FILE* outputFd,
    bool decompress);
}
