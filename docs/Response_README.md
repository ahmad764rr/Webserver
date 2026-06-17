# HttpResponse Builder

This README documents the `HttpResponse` class implemented in `HttpResponse.hpp` and `HttpResponse.cpp`. It mirrors the request-side documentation style and explains how the response builder works with non-blocking socket writes.

## Example Fixed-Length Response

```cpp
HttpResponse response("webserv");
response.setStatusCode(200);
response.setContentType("text/html; charset=utf-8");
response.setBody("<h1>Hello</h1>");
response.prepare();

while (!response.isComplete()) {
    std::string out = response.getNextBytes(4096);
    // send(out.data(), out.size(), 0) after poll()/select() says the socket is writable
    response.consumeBytes(out.size());
}
```

## Example Chunked Streaming Response

```cpp
HttpResponse response("webserv");
response.setStatusCode(200);
response.setContentType("text/plain");
response.enableChunkedTransferEncoding();
response.prepare();

response.appendChunk("first piece");
response.appendChunk("second piece");
response.finishChunkedBody();
```

## Response Defaults

- Default HTTP version: `HTTP/1.1`
- Default status: `200 OK`
- Default server name: `webserv`
- Default connection policy: keep-alive
- Default maximum serialized header block: `16 * 1024` bytes (`16 KiB`)

## Subject Alignment

The implementation is guided by the Webserv mandatory requirements that matter on the response side:

- status codes must be accurate
- the server must provide default error pages if no custom ones exist
- the server must support static content
- CGI output may need to be streamed without a known `Content-Length`

Because you are restricted to `HTTP/1.1`, the class supports both:

- fixed-length responses using `Content-Length`
- streaming responses using `Transfer-Encoding: chunked`

This makes it usable for normal static replies, generated pages, and CGI output that arrives incrementally.

## How The Response Builder Works

### Step 1: Build The Metadata

The object starts in `BUILDING`.

In that state, the caller can configure:

- the HTTP version with `setVersion()`
- the status code and reason phrase with `setStatusCode()` and `setReasonPhrase()`
- arbitrary headers with `setHeader()`
- a full fixed body with `setBody()` or `appendToBody()`
- chunked streaming mode with `enableChunkedTransferEncoding()`
- connection behavior with `setKeepAlive()`
- whether this should behave like a `HEAD` response with `setHeadResponse()`

The setters reject invalid header names and values so CRLF injection and malformed response headers are caught early.

### Step 2: Prepare The Wire Format

Calling `prepare()` validates the response and serializes the status line plus headers into the internal outbound buffer.

During `prepare()`, the class:

- requires `HTTP/1.1`
- validates that the status code is in the `100` to `599` range
- fills in a stock reason phrase if you did not set one manually
- adds a `Date` header if one is missing
- adds a `Server` header if one is missing and `_serverName` is set
- sets `Connection: keep-alive` or `Connection: close`
- uses `Content-Length` for fixed responses
- uses `Transfer-Encoding: chunked` for streaming responses
- rejects response bodies for status codes that must not carry one (`1xx`, `204`, `304`)

State transition:

- `BUILDING -> READY`

At that point, the network layer can start pulling bytes with `getNextBytes()`.

### Step 3: Send Without Blocking

The response object never calls `send()` itself. It only exposes the next bytes that are ready.

The non-blocking loop is:

1. wait until `poll()` says the client socket is writable
2. call `getNextBytes(maxBytes)` to obtain a slice of the queued response
3. call `send()` yourself
4. call `consumeBytes(bytesActuallySent)` to advance the internal cursor

While data is queued:

- `READY` means nothing has been acknowledged yet or a chunked response is waiting for the next send opportunity
- `STREAMING` means some bytes were already acknowledged but more are still pending

Once all queued bytes are acknowledged:

- fixed-length responses move to `COMPLETE`
- unfinished chunked responses move back to `READY`, waiting for more chunks

### Step 4: Stream Chunked Bodies

For CGI or any producer that does not know the final body length up front, use chunked mode.

After `enableChunkedTransferEncoding()` and `prepare()`:

- `appendChunk(data)` adds one properly framed chunk:
  - `<hex-size>\r\n`
  - `<data>\r\n`
- `finishChunkedBody()` appends the terminating `0\r\n\r\n`

Chunked state behavior:

- after headers are flushed, the response can temporarily have zero pending bytes while it waits for more CGI output
- once `finishChunkedBody()` is called and every queued byte is acknowledged, the response becomes `COMPLETE`

### Step 5: Default Error Pages

The class includes:

- `defaultReasonPhrase(int statusCode)`
- `buildDefaultErrorPage(int statusCode, const std::string& reasonPhrase)`
- `stockResponse(int statusCode, bool keepAlive, const std::string& serverName)`

`stockResponse()` is a convenience helper for generating responses such as `404 Not Found` or `500 Internal Server Error` with a built-in HTML error page when your configuration does not provide one.

## Public Interface

### Enums

`ResponseState`

- `BUILDING`: the response can still be configured
- `READY`: the response has serialized data ready for the network layer
- `STREAMING`: some bytes were already sent, but more are still pending
- `COMPLETE`: the full response has been acknowledged
- `ERROR`: the response configuration became invalid

`BodyMode`

- `BODY_MODE_NONE`: no response body
- `BODY_MODE_FIXED`: response body uses `Content-Length`
- `BODY_MODE_CHUNKED`: response body uses `Transfer-Encoding: chunked`

### Configuration

- `setVersion(const std::string& version)`: sets the HTTP version token
- `setStatusCode(int statusCode)`: sets the status code and stock reason phrase
- `setReasonPhrase(const std::string& reasonPhrase)`: overrides the reason phrase
- `setHeader(const std::string& name, const std::string& value)`: inserts or replaces a header
- `removeHeader(const std::string& name)`: removes a header before `prepare()`
- `setBody(const std::string& body)`: sets a fixed response body
- `appendToBody(const std::string& data)`: appends to the fixed response body
- `clearBody()`: clears the fixed response body
- `setContentType(const std::string& contentType)`: convenience wrapper for `content-type`
- `setServerName(const std::string& serverName)`: changes the default `Server` header value
- `setKeepAlive(bool keepAlive)`: controls `Connection`
- `setHeadResponse(bool enabled)`: suppresses body serialization while still computing headers like a `HEAD` reply
- `enableChunkedTransferEncoding()`: switches the response to chunked mode

### Chunked Streaming

- `appendChunk(const std::string& data)`: queues one chunk
- `finishChunkedBody()`: queues the zero-length terminating chunk
- `isChunkedBodyFinished()`: reports whether the terminating chunk was already queued

### Build And Send

- `prepare()`: validates and serializes the response
- `getNextBytes(std::size_t maxBytes) const`: returns the next unsent slice
- `consumeBytes(std::size_t bytes)`: acknowledges bytes already sent by the socket layer
- `getPendingBytes() const`: returns how many bytes are still queued
- `getBytesSent() const`: returns the total number of acknowledged bytes
- `getSerializedData() const`: exposes the internal outbound buffer

### Status And Inspection

- `getState() const`: returns the current `ResponseState`
- `isComplete() const`: returns `true` once the response is fully sent
- `hasError() const`: returns `true` if response setup failed
- `getErrorMessage() const`: returns the response-build error
- `getVersion() const`: returns the HTTP version
- `getStatusCode() const`: returns the status code
- `getReasonPhrase() const`: returns the reason phrase
- `hasHeader(const std::string& name) const`: case-insensitive header presence check
- `getHeader(const std::string& name) const`: case-insensitive header lookup
- `getHeaders() const`: returns the normalized header map
- `getBody() const`: returns the fixed body buffer
- `getBodyMode() const`: returns the selected body mode
- `getServerName() const`: returns the configured server name
- `getKeepAlive() const`: returns the configured connection policy
- `isHeadResponse() const`: reports whether body serialization is suppressed for a `HEAD`-style response

### Limits And Helpers

- `setMaxHeaderBytes(std::size_t maxHeaderBytes)`: sets the maximum header block size
- `getMaxHeaderBytes() const`: returns the current header limit
- `stockResponse(...)`: builds a convenient stock response
- `defaultReasonPhrase(...)`: returns the built-in reason phrase for a status code
- `buildDefaultErrorPage(...)`: generates a simple HTML error page

## Internal State

- `_state`: current response builder / send state
- `_bodyMode`: selected body framing mode
- `_version`: response version token
- `_statusCode`: numeric HTTP status code
- `_reasonPhrase`: text part of the status line
- `_headers`: lowercase response header map
- `_body`: fixed-length body buffer
- `_serverName`: default value used for the `Server` header
- `_keepAlive`: selected connection behavior
- `_headResponse`: whether body bytes should be suppressed on the wire
- `_chunkedFinished`: whether the terminating chunk was queued
- `_serialized`: internal outbound buffer waiting to be sent
- `_sendOffset`: current cursor inside `_serialized`
- `_totalBytesSent`: total number of acknowledged bytes
- `_maxHeaderBytes`: maximum serialized header size
- `_errorMessage`: build-time or streaming-time error

## Utility Helpers

- `validateBeforePrepare()`: validates status, version, and body rules
- `buildHeadersBlock()`: writes the status line and headers into `_serialized`
- `appendChunkFrame()`: serializes one chunk into wire format
- `updateStreamingState()`: moves between `READY`, `STREAMING`, and `COMPLETE`
- `compactSerializedBuffer()`: discards already acknowledged bytes to avoid unbounded buffer growth during long streams
- `formatHttpDate()`: formats the HTTP `Date` header in GMT
- `hexSizeToString()`: converts a chunk length to uppercase hexadecimal

## Supported Behavior Summary

- HTTP version: `HTTP/1.1`
- body framing: no body, `Content-Length`, `Transfer-Encoding: chunked`
- default error page generation
- non-blocking partial-write integration
- automatic `Date`, `Server`, `Connection`, and body-framing headers
- body suppression for `1xx`, `204`, and `304`
