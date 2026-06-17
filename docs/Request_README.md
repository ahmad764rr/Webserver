# HttpRequest Parser

This README documents the `HttpRequest` class implemented in `HttpRequest.hpp` and `HttpRequest.cpp`. It collects the walkthrough and inline explanations that were previously stored inside the code.

## Example Request

```http
POST /upload?user=42 HTTP/1.1
Host: example.com
Content-Type: text/plain
Content-Length: 11

hello world
```

## Parser Defaults

- Default maximum body size: `1024 * 1024` bytes (`1 MiB`)
- Default maximum header block size: `16 * 1024` bytes (`16 KiB`)
- Default maximum chunk-size line length: `1024` bytes (`1 KiB`)

## How The Request Parser Works

### Step 1: Receiving Data

The parser works incrementally. Its main entry point is `feed(const std::string& chunk)`, which accepts raw bytes as they arrive from the socket. Incoming data is appended to the internal `_buffer` unless the parser is already in `COMPLETE` or `ERROR`.

The parser starts in the top-level `READING_HEADERS` state.

### Step 2: Parsing The Headers

While the parser is in `READING_HEADERS`, it searches `_buffer` for the sequence `\r\n\r\n`, which marks the end of the HTTP header block.

- If the terminator is not found yet, the parser waits for more data.
- If `_buffer` grows beyond `_maxHeaderBytes` before the terminator appears, the parser enters `ERROR` with HTTP status `431`.
- Once the terminator is found, the parser extracts the header block, removes it from `_buffer`, and splits it into lines using `\r\n`.

The first line is handled by `parseRequestLine()`:

- It expects exactly three space-separated parts: method, request target, and version.
- It stores the method in `_method`, the original target in `_requestTarget`, and the version in `_version`.
- It accepts only `GET`, `POST`, and `DELETE`.
- It accepts only `HTTP/1.1`.
- It requires the request target to start with `/`.
- If a `?` appears in the target, the path is stored in `_path` and the rest is stored in `_queryString`.

The remaining lines are handled by `parseHeaderLine()`:

- Each line must look like `Name: value`.
- Obsolete folded headers are rejected.
- Header names must contain only valid HTTP token characters.
- Header names are normalized to lowercase before being stored in `_headers`.
- Duplicate non-critical headers are merged with `, `.
- Duplicate `Host`, `Content-Length`, or `Transfer-Encoding` headers are rejected.

### Step 3: Finalizing Headers And Choosing The Body Mode

After the header lines are parsed, `finalizeHeaders()` validates the result and decides how the body should be handled.

- A `Host` header is mandatory.
- `Content-Length` and `Transfer-Encoding` cannot both appear in the same request.
- If `Transfer-Encoding: chunked` is present, the parser switches to `BODY_CHUNKED`, resets the chunk sub-state machine, and moves to `READING_BODY`.
- If `Content-Length` is present, the parser converts it to an integer with `parseUnsignedDecimal()`.
- If the declared length exceeds `_clientMaxBodySize`, the parser returns HTTP status `413`.
- If `Content-Length` is `0`, the request is immediately marked complete.
- If neither body-framing header exists, the parser uses `BODY_NONE` and immediately marks the request complete.

### Step 4: Parsing The Body

Once the top-level state becomes `READING_BODY`, `parseBody()` dispatches to the correct body parser based on `_bodyMode`.

#### Fixed-Length Body: `BODY_CONTENT_LENGTH`

`parseContentLengthBody()` copies bytes from `_buffer` into `_body` until `_body.size()` reaches `_contentLength`.

- If no more bytes are available, parsing pauses and `feed()` returns so the caller can provide more data later.
- If the assembled body exceeds `_clientMaxBodySize`, the parser enters `ERROR` with status `413`.
- When the expected number of bytes is reached, the parser calls `markComplete()`.

#### Chunked Body: `BODY_CHUNKED`

`parseChunkedBody()` uses `_chunkState` to decode the message body in stages.

`CHUNK_READING_SIZE`

- Reads the next chunk-size line up to `\r\n`.
- Uses `parseChunkSizeLine()` to extract the hexadecimal size.
- Ignores any chunk extension after `;`.
- If the size is `0`, the parser switches to trailer processing.
- Otherwise, it switches to `CHUNK_READING_DATA`.

`CHUNK_READING_DATA`

- Copies exactly `_currentChunkSize` bytes from `_buffer` into `_body`.
- Tracks progress using `_receivedInCurrentChunk`.
- Once the whole chunk is copied, the parser switches to `CHUNK_EXPECTING_DATA_CRLF`.

`CHUNK_EXPECTING_DATA_CRLF`

- Requires the next two bytes to be `\r\n`.
- If they are missing or invalid, the parser enters `ERROR`.
- If they are valid, the parser returns to `CHUNK_READING_SIZE` for the next chunk.

`CHUNK_READING_TRAILERS`

- Starts after a zero-size chunk.
- Accepts either an immediate `\r\n` for no trailers or a trailer block ending with `\r\n\r\n`.
- Trailer lines are parsed by `parseTrailers()` using rules similar to normal header parsing.

`CHUNK_COMPLETE`

- Marks the end of chunk decoding and lets the parser finalize the request.

### Step 5: Completion And Error Handling

If parsing finishes successfully, `markComplete()` changes `_state` to `COMPLETE`.

- For chunked requests, `_contentLength` is updated to the decoded body size.
- Any leftover bytes stay in `_buffer`, which makes HTTP pipelining possible.

If validation or parsing fails at any point, `setError()` changes `_state` to `ERROR`, stores the HTTP status code in `_errorStatus`, and stores the reason in `_errorMessage`.

## Public Interface

### Enums

`ParseState`

- `READING_HEADERS`: waiting for the complete header block
- `READING_BODY`: headers were accepted and the parser is decoding the body
- `COMPLETE`: the full request has been parsed successfully
- `ERROR`: parsing failed and the error fields are set

`BodyMode`

- `BODY_NONE`: request has no body
- `BODY_CONTENT_LENGTH`: body size is defined by `Content-Length`
- `BODY_CHUNKED`: body uses `Transfer-Encoding: chunked`

### Construction And Control

- `HttpRequest()`: builds a parser with default limits
- `HttpRequest(std::size_t clientMaxBodySize)`: builds a parser with a custom body limit
- `~HttpRequest()`: trivial destructor
- `reset()`: clears all parsed state so the object can be reused for a new request
- `feed(const std::string& chunk)`: appends new bytes and advances the state machine as far as possible

### Status And Error Access

- `getState()`: returns the current `ParseState`
- `isComplete()`: returns `true` if parsing reached `COMPLETE`
- `hasError()`: returns `true` if parsing reached `ERROR`
- `getErrorStatus()`: returns the HTTP status code associated with the failure
- `getErrorMessage()`: returns the human-readable parse error

### Parsed Request Data

- `getMethod()`: returns the parsed HTTP method
- `getRequestTarget()`: returns the original request target from the request line
- `getPath()`: returns the path portion of the request target
- `getQueryString()`: returns the part after `?`
- `getVersion()`: returns the HTTP version token
- `getHeaders()`: returns the full normalized header map
- `hasHeader(const std::string& name)`: case-insensitive header presence check
- `getHeader(const std::string& name)`: case-insensitive header lookup that returns `""` if absent
- `getBody()`: returns the fully assembled request body
- `getContentLength()`: returns the declared body size or the decoded chunked size
- `getBodyMode()`: returns the selected body framing mode
- `isChunked()`: shorthand for checking whether `_bodyMode` is `BODY_CHUNKED`

### Buffer And Limits

- `getUnparsedBuffer()`: returns bytes that remain after the request is complete
- `releaseUnparsedBuffer()`: returns the leftover bytes and clears `_buffer`
- `setClientMaxBodySize()`: changes the maximum accepted payload size
- `getClientMaxBodySize()`: returns the current payload limit
- `setMaxHeaderBytes()`: changes the header and trailer size limit
- `getMaxHeaderBytes()`: returns the current header size limit
- `setMaxChunkLineBytes()`: changes the chunk-size line limit
- `getMaxChunkLineBytes()`: returns the current chunk line limit

## Private Parsing Helpers

### Chunk State

`ChunkState`

- `CHUNK_READING_SIZE`: waiting for the next chunk-size line
- `CHUNK_READING_DATA`: copying the current chunk body
- `CHUNK_EXPECTING_DATA_CRLF`: waiting for the `\r\n` after chunk data
- `CHUNK_READING_TRAILERS`: parsing optional trailer headers after the zero chunk
- `CHUNK_COMPLETE`: chunk parser has finished

### Header Parsing Helpers

- `parseHeadersBlock(const std::string& headerBlock)`: validates and splits the complete header block
- `parseRequestLine(const std::string& line)`: parses `METHOD target HTTP/version`
- `parseHeaderLine(const std::string& line)`: parses one header and merges allowed duplicates
- `finalizeHeaders()`: validates the header set and selects the body mode

### Body Parsing Helpers

- `parseBody()`: dispatches to the correct body parser
- `parseContentLengthBody()`: reads a fixed-size body
- `parseChunkedBody()`: runs the chunked-body state machine
- `parseChunkSizeLine(const std::string& line, std::size_t& outSize) const`: decodes the hexadecimal chunk size
- `parseTrailers(const std::string& trailerBlock)`: parses headers that appear after the zero chunk

### Completion And Error Helpers

- `setError(int status, const std::string& message)`: stores the error result and switches to `ERROR`
- `markComplete()`: switches to `COMPLETE` and finalizes `_contentLength` for chunked requests

## Internal State

- `_state`: current top-level parser state
- `_bodyMode`: selected body framing rule
- `_chunkState`: current chunk sub-state when decoding chunked bodies
- `_buffer`: raw bytes not fully processed yet
- `_method`: parsed method token
- `_requestTarget`: original request target from the request line
- `_path`: path portion of `_requestTarget`
- `_queryString`: query portion of `_requestTarget`
- `_version`: HTTP version token
- `_headers`: lowercase header map for case-insensitive access
- `_body`: assembled body content
- `_contentLength`: expected or final body length
- `_clientMaxBodySize`: configured payload limit
- `_maxHeaderBytes`: maximum allowed size for headers and trailers
- `_maxChunkLineBytes`: maximum allowed size for one chunk-size line
- `_currentChunkSize`: size of the chunk currently being decoded
- `_receivedInCurrentChunk`: number of bytes already copied from the current chunk
- `_errorStatus`: HTTP status code for the parse failure
- `_errorMessage`: human-readable parse failure message

## Utility Helpers

- `isTokenChar(char c)`: checks whether a character is valid inside an HTTP header name
- `isValidHeaderName(const std::string& name)`: validates an entire header name
- `parseUnsignedDecimal(const std::string& value, std::size_t& out)`: safely parses decimal numbers such as `Content-Length`
- `parseUnsignedHex(const std::string& value, std::size_t& out)`: safely parses hexadecimal numbers such as chunk sizes
- `trim(const std::string& value)`: removes leading and trailing spaces or tabs
- `toLower(const std::string& value)`: lowercases a string for case-insensitive matching
- `split(const std::string& value, const std::string& delim)`: splits a string by a delimiter
- `sizeToString(std::size_t value)`: converts a size value to text

## Supported Behavior Summary

- Supported methods: `GET`, `POST`, `DELETE`
- Supported HTTP version: `HTTP/1.1`
- Required header: `Host`
- Supported body framing: no body, `Content-Length`, and `Transfer-Encoding: chunked`
- Duplicate `Host`, `Content-Length`, and `Transfer-Encoding` are rejected
- Chunk trailers are accepted
- Leftover data is preserved for pipelined requests
