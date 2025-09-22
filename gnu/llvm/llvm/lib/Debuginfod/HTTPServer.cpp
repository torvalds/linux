//===-- llvm/Debuginfod/HTTPServer.cpp - HTTP server library -----*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// This file defines the methods of the HTTPServer class and the streamFile
/// function.
///
//===----------------------------------------------------------------------===//

#include "llvm/Debuginfod/HTTPServer.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Regex.h"

#ifdef LLVM_ENABLE_HTTPLIB
#include "httplib.h"
#endif

using namespace llvm;

char HTTPServerError::ID = 0;

HTTPServerError::HTTPServerError(const Twine &Msg) : Msg(Msg.str()) {}

void HTTPServerError::log(raw_ostream &OS) const { OS << Msg; }

bool llvm::streamFile(HTTPServerRequest &Request, StringRef FilePath) {
  Expected<sys::fs::file_t> FDOrErr = sys::fs::openNativeFileForRead(FilePath);
  if (Error Err = FDOrErr.takeError()) {
    consumeError(std::move(Err));
    Request.setResponse({404u, "text/plain", "Could not open file to read.\n"});
    return false;
  }
  ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
      MemoryBuffer::getOpenFile(*FDOrErr, FilePath,
                                /*FileSize=*/-1,
                                /*RequiresNullTerminator=*/false);
  sys::fs::closeFile(*FDOrErr);
  if (Error Err = errorCodeToError(MBOrErr.getError())) {
    consumeError(std::move(Err));
    Request.setResponse({404u, "text/plain", "Could not memory-map file.\n"});
    return false;
  }
  // Lambdas are copied on conversion to std::function, preventing use of
  // smart pointers.
  MemoryBuffer *MB = MBOrErr->release();
  Request.setResponse({200u, "application/octet-stream", MB->getBufferSize(),
                       [=](size_t Offset, size_t Length) -> StringRef {
                         return MB->getBuffer().substr(Offset, Length);
                       },
                       [=](bool Success) { delete MB; }});
  return true;
}

#ifdef LLVM_ENABLE_HTTPLIB

bool HTTPServer::isAvailable() { return true; }

HTTPServer::HTTPServer() { Server = std::make_unique<httplib::Server>(); }

HTTPServer::~HTTPServer() { stop(); }

static void expandUrlPathMatches(const std::smatch &Matches,
                                 HTTPServerRequest &Request) {
  bool UrlPathSet = false;
  for (const auto &it : Matches) {
    if (UrlPathSet)
      Request.UrlPathMatches.push_back(it);
    else {
      Request.UrlPath = it;
      UrlPathSet = true;
    }
  }
}

HTTPServerRequest::HTTPServerRequest(const httplib::Request &HTTPLibRequest,
                                     httplib::Response &HTTPLibResponse)
    : HTTPLibResponse(HTTPLibResponse) {
  expandUrlPathMatches(HTTPLibRequest.matches, *this);
}

void HTTPServerRequest::setResponse(HTTPResponse Response) {
  HTTPLibResponse.set_content(Response.Body.begin(), Response.Body.size(),
                              Response.ContentType);
  HTTPLibResponse.status = Response.Code;
}

void HTTPServerRequest::setResponse(StreamingHTTPResponse Response) {
  HTTPLibResponse.set_content_provider(
      Response.ContentLength, Response.ContentType,
      [=](size_t Offset, size_t Length, httplib::DataSink &Sink) {
        if (Offset < Response.ContentLength) {
          StringRef Chunk = Response.Provider(Offset, Length);
          Sink.write(Chunk.begin(), Chunk.size());
        }
        return true;
      },
      [=](bool Success) { Response.CompletionHandler(Success); });

  HTTPLibResponse.status = Response.Code;
}

Error HTTPServer::get(StringRef UrlPathPattern, HTTPRequestHandler Handler) {
  std::string ErrorMessage;
  if (!Regex(UrlPathPattern).isValid(ErrorMessage))
    return createStringError(errc::argument_out_of_domain, ErrorMessage);
  Server->Get(std::string(UrlPathPattern),
              [Handler](const httplib::Request &HTTPLibRequest,
                        httplib::Response &HTTPLibResponse) {
                HTTPServerRequest Request(HTTPLibRequest, HTTPLibResponse);
                Handler(Request);
              });
  return Error::success();
}

Error HTTPServer::bind(unsigned ListenPort, const char *HostInterface) {
  if (!Server->bind_to_port(HostInterface, ListenPort))
    return createStringError(errc::io_error,
                             "Could not assign requested address.");
  Port = ListenPort;
  return Error::success();
}

Expected<unsigned> HTTPServer::bind(const char *HostInterface) {
  int ListenPort = Server->bind_to_any_port(HostInterface);
  if (ListenPort < 0)
    return createStringError(errc::io_error,
                             "Could not assign any port on requested address.");
  return Port = ListenPort;
}

Error HTTPServer::listen() {
  if (!Port)
    return createStringError(errc::io_error,
                             "Cannot listen without first binding to a port.");
  if (!Server->listen_after_bind())
    return createStringError(
        errc::io_error,
        "An unknown error occurred when cpp-httplib attempted to listen.");
  return Error::success();
}

void HTTPServer::stop() {
  Server->stop();
  Port = 0;
}

#else

// TODO: Implement barebones standalone HTTP server implementation.
bool HTTPServer::isAvailable() { return false; }

HTTPServer::HTTPServer() = default;

HTTPServer::~HTTPServer() = default;

void HTTPServerRequest::setResponse(HTTPResponse Response) {
  llvm_unreachable("no httplib");
}

void HTTPServerRequest::setResponse(StreamingHTTPResponse Response) {
  llvm_unreachable("no httplib");
}

Error HTTPServer::get(StringRef UrlPathPattern, HTTPRequestHandler Handler) {
  // TODO(https://github.com/llvm/llvm-project/issues/63873) We would ideally
  // return an error as well but that's going to require refactoring of error
  // handling in DebuginfodServer.
  return Error::success();
}

Error HTTPServer::bind(unsigned ListenPort, const char *HostInterface) {
  return make_error<HTTPServerError>("no httplib");
}

Expected<unsigned> HTTPServer::bind(const char *HostInterface) {
  return make_error<HTTPServerError>("no httplib");
}

Error HTTPServer::listen() {
  return make_error<HTTPServerError>("no httplib");
}

void HTTPServer::stop() {
  llvm_unreachable("no httplib");
}

#endif // LLVM_ENABLE_HTTPLIB
