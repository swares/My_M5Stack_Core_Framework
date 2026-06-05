#pragma once
// ============================================================
//  HttpSse.h  –  Incremental reader for an HTTP/1.1 streaming
//                (text/event-stream) response.
//
//  A naive "read bytes, split on \n, look for data:" loop gets two
//  HTTP transport details wrong:
//
//    1. RESPONSE HEADERS.  The status line and headers precede the
//       body and must be skipped (up to the blank line).
//    2. Transfer-Encoding: chunked.  Most servers — including the
//       Anthropic Messages API — frame a streamed body as
//          <hex-size> CRLF <data> CRLF <hex-size> CRLF <data> ... 0 CRLF
//       The chunk-size lines are interleaved with the body, and a
//       chunk boundary can fall in the MIDDLE of an SSE "data:" line.
//       A line reader that ignores chunking then either parses a bare
//       hex size as a stray line (harmless) or, worse, splits a
//       data: JSON across two reads so deserializeJson() fails and
//       that token is silently dropped.
//
//  HttpSseReader handles both: feed it the bytes available from a
//  connected client, then pull complete DECODED BODY lines out.  It
//  transparently de-chunks when the response says it should, and
//  passes the body through unchanged otherwise.  The caller does the
//  SSE-specific parsing (data: payloads) on the lines it returns.
//
//  Header-only and transport-agnostic (templated on the client type),
//  so both NetDevice_ClaudeAPI and NetDevice_Router share one copy of
//  the fiddly logic instead of each re-implementing it.
// ============================================================
#include <Arduino.h>
#include <stdlib.h>   // strtol

class HttpSseReader {
 public:
  // Reset for a fresh response.  Call right after sending the request.
  void begin() {
    _raw = "";
    _body = "";
    _headersDone = false;
    _chunked = false;
    _complete = false;
    _overflow = false;
    _chunkRem = -1;   // -1 = a chunk-size line is expected next
  }

  // Drain everything currently available from `client`, decoding
  // headers + chunk framing into the internal body buffer as it goes.
  template <class Client>
  void feed(Client& client) {
    if (_overflow) return;   // stream was abandoned — ignore further bytes
    while (client.available()) {
      int b = client.read();
      if (b < 0) break;
      _raw += static_cast<char>(b);
      // Flush periodically so _raw doesn't balloon when a lot is
      // waiting; _pump() moves decodable bytes into _body.
      if (_raw.length() > kRawFlush) _pump();
    }
    _pump();
    // Hard ceiling: a well-behaved SSE stream keeps both buffers tiny
    // (one event in flight).  If either runs away — a malformed or
    // hostile server that never terminates a chunk/line — abandon the
    // stream rather than grow the heap without bound.  Mark it complete
    // so the caller finishes the turn; keep _body so any already-decoded
    // lines can still be drained, and drop the runaway undecoded _raw.
    if (_raw.length() > kMaxBuffer || _body.length() > kMaxBuffer) {
      _overflow = true;
      _complete = true;
      _raw = "";
    }
  }

  // Fetch the next complete decoded body line (trailing CR removed),
  // or false if no full line is buffered yet.  Loop until it returns
  // false, parsing each line as an SSE event.
  bool nextLine(String& out) {
    int nl = _body.indexOf('\n');
    if (nl < 0) return false;
    out = _body.substring(0, nl);
    if (out.endsWith("\r")) out.remove(out.length() - 1);
    _body.remove(0, nl + 1);
    return true;
  }

  // True once the terminating zero-length chunk has been seen — a clean
  // end-of-stream signal independent of the connection closing.
  bool complete() const { return _complete; }

 private:
  // Soft threshold at which feed() flushes mid-read.  A single SSE
  // chunk is far smaller than this; it only bounds the burst case.
  static constexpr size_t kRawFlush = 1024;

  // Hard ceiling on either internal buffer.  Real SSE events are tiny,
  // so this is pure safety headroom: if a malformed/hostile server never
  // terminates a chunk, a line, or the response headers, the reader
  // abandons the stream instead of growing the heap without bound.
  static constexpr size_t kMaxBuffer = 8192;

  // Move as much of _raw as possible into _body: strip headers once,
  // then either pass the body through or de-chunk it.
  void _pump() {
    if (!_headersDone) {
      int sep = 4;
      int end = _raw.indexOf("\r\n\r\n");
      if (end < 0) { end = _raw.indexOf("\n\n"); sep = 2; }
      if (end < 0) return;                 // headers not fully arrived
      String headers = _raw.substring(0, end);
      headers.toLowerCase();
      _chunked = headers.indexOf("transfer-encoding: chunked") >= 0;
      _raw.remove(0, end + sep);
      _headersDone = true;
    }

    if (!_chunked) {                       // identity encoding — pass through
      if (_raw.length()) { _body += _raw; _raw = ""; }
      return;
    }

    // Chunked: decode every complete chunk currently buffered.
    for (;;) {
      if (_chunkRem < 0) {                 // expecting a size line
        int nl = _raw.indexOf("\r\n");
        if (nl < 0) return;                // size line incomplete
        String sizeLine = _raw.substring(0, nl);
        int semi = sizeLine.indexOf(';');  // drop any chunk extensions
        if (semi >= 0) sizeLine = sizeLine.substring(0, semi);
        sizeLine.trim();
        _chunkRem = strtol(sizeLine.c_str(), nullptr, 16);
        _raw.remove(0, nl + 2);            // consume size line + CRLF
        if (_chunkRem <= 0) {              // 0 = last chunk (ignore trailers)
          _complete = true;
          _chunkRem = -1;
          return;
        }
      }
      // Need the chunk's data plus its trailing CRLF before consuming.
      if (static_cast<long>(_raw.length()) < _chunkRem + 2) return;
      _body += _raw.substring(0, _chunkRem);
      _raw.remove(0, _chunkRem + 2);       // data + CRLF
      _chunkRem = -1;                      // next: another size line
    }
  }

  String _raw;            // undecoded bytes from the socket
  String _body;           // decoded body, ready to hand out as lines
  bool   _headersDone = false;
  bool   _chunked = false;
  bool   _complete = false;
  bool   _overflow = false;  // a buffer exceeded kMaxBuffer → stream abandoned
  long   _chunkRem = -1;  // bytes left in the current chunk (-1 = need size)
};
