#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

class HttpRequest {
public:
    enum ParseState {
        READING_HEADERS,
        READING_BODY,
        COMPLETE,
        ERROR
    };

    enum BodyMode {
        BODY_NONE,
        BODY_CONTENT_LENGTH,
        BODY_CHUNKED
    };

    HttpRequest();
    explicit HttpRequest(std::size_t clientMaxBodySize);
    ~HttpRequest();

    void reset();
    ParseState feed(const std::string& chunk);

    ParseState getState() const;
    bool isComplete() const;
    bool hasError() const;
    int getErrorStatus() const;
    const std::string& getErrorMessage() const;

    const std::string& getMethod() const;
    const std::string& getRequestTarget() const;
    const std::string& getPath() const;
    const std::string& getQueryString() const;
    const std::string& getVersion() const;
    const std::map<std::string, std::string>& getHeaders() const;
    bool hasHeader(const std::string& name) const;
    std::string getHeader(const std::string& name) const;

    const std::string& getBody() const;
    std::size_t getContentLength() const;
    BodyMode getBodyMode() const;
    bool isChunked() const;

    const std::string& getUnparsedBuffer() const;
    std::string releaseUnparsedBuffer();

    void setClientMaxBodySize(std::size_t maxBodySize);
    std::size_t getClientMaxBodySize() const;
    void setMaxHeaderBytes(std::size_t maxHeaderBytes);
    std::size_t getMaxHeaderBytes() const;
    void setMaxChunkLineBytes(std::size_t maxChunkLineBytes);
    std::size_t getMaxChunkLineBytes() const;

private:
    enum ChunkState {
        CHUNK_READING_SIZE,
        CHUNK_READING_DATA,
        CHUNK_EXPECTING_DATA_CRLF,
        CHUNK_READING_TRAILERS,
        CHUNK_COMPLETE
    };

    ParseState _state;
    BodyMode _bodyMode;
    ChunkState _chunkState;

    std::string _buffer;
    std::string _method;
    std::string _requestTarget;
    std::string _path;
    std::string _queryString;
    std::string _version;
    std::map<std::string, std::string> _headers;
    std::string _body;

    std::size_t _contentLength;
    std::size_t _clientMaxBodySize;
    std::size_t _maxHeaderBytes;
    std::size_t _maxChunkLineBytes;

    std::size_t _currentChunkSize;
    std::size_t _receivedInCurrentChunk;

    int _errorStatus;
    std::string _errorMessage;

    bool parseHeadersBlock(const std::string& headerBlock);
    bool parseRequestLine(const std::string& line);
    bool parseHeaderLine(const std::string& line);
    bool finalizeHeaders();

    bool parseBody();
    bool parseContentLengthBody();
    bool parseChunkedBody();
    bool parseChunkSizeLine(const std::string& line, std::size_t& outSize) const;
    bool parseTrailers(const std::string& trailerBlock);

    void setError(int status, const std::string& message);
    void markComplete();

    static bool isTokenChar(char c);
    static bool isValidHeaderName(const std::string& name);
    static bool parseUnsignedDecimal(const std::string& value, std::size_t& out);
    static bool parseUnsignedHex(const std::string& value, std::size_t& out);

    static std::string trim(const std::string& value);
    static std::string toLower(const std::string& value);
    static std::vector<std::string> split(const std::string& value, const std::string& delim);
    static std::string sizeToString(std::size_t value);
};

#endif
