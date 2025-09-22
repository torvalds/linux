//===-- llvm/Support/HTTPClient.h - HTTP client library ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the HTTPClient library for issuing
/// HTTP requests and handling the responses.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFOD_HTTPCLIENT_H
#define LLVM_DEBUGINFOD_HTTPCLIENT_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

#include <chrono>

namespace llvm {

enum class HTTPMethod { GET };

/// A stateless description of an outbound HTTP request.
struct HTTPRequest {
  SmallString<128> Url;
  SmallVector<std::string, 0> Headers;
  HTTPMethod Method = HTTPMethod::GET;
  bool FollowRedirects = true;
  HTTPRequest(StringRef Url);
};

bool operator==(const HTTPRequest &A, const HTTPRequest &B);

/// A handler for state updates occurring while an HTTPRequest is performed.
/// Can trigger the client to abort the request by returning an Error from any
/// of its methods.
class HTTPResponseHandler {
public:
  /// Processes an additional chunk of bytes of the HTTP response body.
  virtual Error handleBodyChunk(StringRef BodyChunk) = 0;

protected:
  ~HTTPResponseHandler();
};

/// A reusable client that can perform HTTPRequests through a network socket.
class HTTPClient {
#ifdef LLVM_ENABLE_CURL
  void *Curl = nullptr;
#endif

public:
  HTTPClient();
  ~HTTPClient();

  static bool IsInitialized;

  /// Returns true only if LLVM has been compiled with a working HTTPClient.
  static bool isAvailable();

  /// Must be called at the beginning of a program, while it is a single thread.
  static void initialize();

  /// Must be called at the end of a program, while it is a single thread.
  static void cleanup();

  /// Sets the timeout for the entire request, in milliseconds. A zero or
  /// negative value means the request never times out.
  void setTimeout(std::chrono::milliseconds Timeout);

  /// Performs the Request, passing response data to the Handler. Returns all
  /// errors which occur during the request. Aborts if an error is returned by a
  /// Handler method.
  Error perform(const HTTPRequest &Request, HTTPResponseHandler &Handler);

  /// Returns the last received response code or zero if none.
  unsigned responseCode();
};

} // end namespace llvm

#endif // LLVM_DEBUGINFOD_HTTPCLIENT_H
