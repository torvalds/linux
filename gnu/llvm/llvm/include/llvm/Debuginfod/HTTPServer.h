//===-- llvm/Debuginfod/HTTPServer.h - HTTP server library ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the HTTPServer and HTTPServerRequest
/// classes, the HTTPResponse, and StreamingHTTPResponse structs, and the
/// streamFile function.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFOD_HTTPSERVER_H
#define LLVM_DEBUGINFOD_HTTPSERVER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#ifdef LLVM_ENABLE_HTTPLIB
// forward declarations
namespace httplib {
class Request;
class Response;
class Server;
} // namespace httplib
#endif

namespace llvm {

struct HTTPResponse;
struct StreamingHTTPResponse;
class HTTPServer;

class HTTPServerError : public ErrorInfo<HTTPServerError, ECError> {
public:
  static char ID;
  HTTPServerError(const Twine &Msg);
  void log(raw_ostream &OS) const override;

private:
  std::string Msg;
};

class HTTPServerRequest {
  friend HTTPServer;

#ifdef LLVM_ENABLE_HTTPLIB
private:
  HTTPServerRequest(const httplib::Request &HTTPLibRequest,
                    httplib::Response &HTTPLibResponse);
  httplib::Response &HTTPLibResponse;
#endif

public:
  std::string UrlPath;
  /// The elements correspond to match groups in the url path matching regex.
  SmallVector<std::string, 1> UrlPathMatches;

  // TODO bring in HTTP headers

  void setResponse(StreamingHTTPResponse Response);
  void setResponse(HTTPResponse Response);
};

struct HTTPResponse {
  unsigned Code;
  const char *ContentType;
  StringRef Body;
};

typedef std::function<void(HTTPServerRequest &)> HTTPRequestHandler;

/// An HTTPContentProvider is called by the HTTPServer to obtain chunks of the
/// streaming response body. The returned chunk should be located at Offset
/// bytes and have Length bytes.
typedef std::function<StringRef(size_t /*Offset*/, size_t /*Length*/)>
    HTTPContentProvider;

/// Wraps the content provider with HTTP Status code and headers.
struct StreamingHTTPResponse {
  unsigned Code;
  const char *ContentType;
  size_t ContentLength;
  HTTPContentProvider Provider;
  /// Called after the response transfer is complete with the success value of
  /// the transfer.
  std::function<void(bool)> CompletionHandler = [](bool Success) {};
};

/// Sets the response to stream the file at FilePath, if available, and
/// otherwise an HTTP 404 error response.
bool streamFile(HTTPServerRequest &Request, StringRef FilePath);

/// An HTTP server which can listen on a single TCP/IP port for HTTP
/// requests and delgate them to the appropriate registered handler.
class HTTPServer {
#ifdef LLVM_ENABLE_HTTPLIB
  std::unique_ptr<httplib::Server> Server;
  unsigned Port = 0;
#endif
public:
  HTTPServer();
  ~HTTPServer();

  /// Returns true only if LLVM has been compiled with a working HTTPServer.
  static bool isAvailable();

  /// Registers a URL pattern routing rule. When the server is listening, each
  /// request is dispatched to the first registered handler whose UrlPathPattern
  /// matches the UrlPath.
  Error get(StringRef UrlPathPattern, HTTPRequestHandler Handler);

  /// Attempts to assign the requested port and interface, returning an Error
  /// upon failure.
  Error bind(unsigned Port, const char *HostInterface = "0.0.0.0");

  /// Attempts to assign any available port and interface, returning either the
  /// port number or an Error upon failure.
  Expected<unsigned> bind(const char *HostInterface = "0.0.0.0");

  /// Attempts to listen for requests on the bound port. Returns an Error if
  /// called before binding a port.
  Error listen();

  /// If the server is listening, stop and unbind the socket.
  void stop();
};
} // end namespace llvm

#endif // LLVM_DEBUGINFOD_HTTPSERVER_H
