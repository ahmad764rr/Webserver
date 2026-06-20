#include "http/HttpRequest.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {
const std::size_t kDefaultClientMaxBodySize = 1024 * 1024;
const std::size_t kDefaultMaxHeaderBytes = 16 * 1024;
const std::size_t kDefaultMaxChunkLineBytes = 1024;
}

HttpRequest::HttpRequest()
    : _state(READING_HEADERS),
      _bodyMode(BODY_NONE),
      _chunkState(CHUNK_READING_SIZE),
      _contentLength(0),
      _clientMaxBodySize(kDefaultClientMaxBodySize),
      _maxHeaderBytes(kDefaultMaxHeaderBytes),
      _maxChunkLineBytes(kDefaultMaxChunkLineBytes),
      _currentChunkSize(0),
      _receivedInCurrentChunk(0),
      _errorStatus(0) {
}

HttpRequest::HttpRequest(std::size_t clientMaxBodySize)
    : _state(READING_HEADERS),
      _bodyMode(BODY_NONE),
      _chunkState(CHUNK_READING_SIZE),
      _contentLength(0),
      _clientMaxBodySize(clientMaxBodySize),
      _maxHeaderBytes(kDefaultMaxHeaderBytes),
      _maxChunkLineBytes(kDefaultMaxChunkLineBytes),
      _currentChunkSize(0),
      _receivedInCurrentChunk(0),
      _errorStatus(0) {
}

HttpRequest::~HttpRequest() {
}

void HttpRequest::reset() {
    _state = READING_HEADERS;
    _bodyMode = BODY_NONE;
    _chunkState = CHUNK_READING_SIZE;

    _buffer.clear();
    _method.clear();
    _requestTarget.clear();
    _path.clear();
    _queryString.clear();
    _version.clear();
    _headers.clear();
    _body.clear();

    _contentLength = 0;
    _currentChunkSize = 0;
    _receivedInCurrentChunk = 0;

    _errorStatus = 0;
    _errorMessage.clear();
}

HttpRequest::ParseState HttpRequest::feed(const std::string& chunk) {
    return feed(chunk.data(), chunk.size());
}

HttpRequest::ParseState HttpRequest::feed(const char* data, std::size_t len) {
    if (_state == COMPLETE || _state == ERROR) {
        if (len > 0) {
            _buffer.append(data, len);
        }
        return _state;
    }

    if (len > 0) {
        _buffer.append(data, len);
    }

    while (_state == READING_HEADERS || _state == READING_BODY) {
        if (_state == READING_HEADERS) {
            const std::size_t headerEnd = _buffer.find("\r\n\r\n");
            if (headerEnd == std::string::npos) {
                if (_buffer.size() > _maxHeaderBytes) {
                    setError(431, "request headers too large");
                }
                break;
            }

            const std::string headerBlock = _buffer.substr(0, headerEnd);
            _buffer.erase(0, headerEnd + 4);

            if (!parseHeadersBlock(headerBlock)) {
                break;
            }

            continue;
        }

        if (_state == READING_BODY) {
            const bool progressed = parseBody();
            if (!progressed) {
                break;
            }
            continue;
        }
    }

    return _state;
}

bool HttpRequest::parseHeadersBlock(const std::string& headerBlock) {
    if (headerBlock.empty()) {
        setError(400, "empty request");
        return false;
    }

    if (headerBlock.size() > _maxHeaderBytes) {
        setError(431, "request headers too large");
        return false;
    }

    const std::vector<std::string> lines = split(headerBlock, "\r\n");
    if (lines.empty()) {
        setError(400, "malformed request headers");
        return false;
    }

    if (!parseRequestLine(lines[0])) {
        return false;
    }

    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].empty()) {
            continue;
        }
        if (!parseHeaderLine(lines[i])) {
            return false;
        }
    }

    return finalizeHeaders();
}

bool HttpRequest::parseRequestLine(const std::string& line) {
    const std::vector<std::string> parts = split(line, " ");
    if (parts.size() != 3 || parts[0].empty() || parts[1].empty() || parts[2].empty()) {
        setError(400, "invalid request line");
        return false;
    }

    _method = parts[0];
    _requestTarget = parts[1];
    _version = parts[2];

    if (_version.length() < 5 || _version.substr(0, 5) != "HTTP/") {
        setError(400, "malformed HTTP version");
        return false;
    }

    if (_version != "HTTP/1.1" && _version != "HTTP/1.0") {
        setError(505, "unsupported HTTP version");
        return false;
    }

    if (_method != "GET" && _method != "POST" && _method != "DELETE") {
        setError(501, "unsupported method");
        return false;
    }

    if (_requestTarget.empty() || _requestTarget[0] != '/') {
        setError(400, "invalid request target");
        return false;
    }

    const std::size_t queryPos = _requestTarget.find('?');
    if (queryPos == std::string::npos) {
        _path = _requestTarget;
        _queryString.clear();
    } else {
        _path = _requestTarget.substr(0, queryPos);
        _queryString = _requestTarget.substr(queryPos + 1);
    }

    return true;
}

bool HttpRequest::parseHeaderLine(const std::string& line) {
    if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
        setError(400, "obsolete header folding is not supported");
        return false;
    }

    const std::size_t colonPos = line.find(':');
    if (colonPos == std::string::npos || colonPos == 0) {
        setError(400, "invalid header line");
        return false;
    }

    const std::string name = trim(line.substr(0, colonPos));
    const std::string value = trim(line.substr(colonPos + 1));
    if (!isValidHeaderName(name)) {
        setError(400, "invalid header name");
        return false;
    }

    const std::string normalizedName = toLower(name);
    std::map<std::string, std::string>::iterator it = _headers.find(normalizedName);
    if (it != _headers.end()) {
        if (normalizedName == "host" || normalizedName == "content-length" || normalizedName == "transfer-encoding") {
            setError(400, "duplicate critical header");
            return false;
        }
        if (!value.empty()) {
            if (!it->second.empty()) {
                it->second.append(", ");
            }
            it->second.append(value);
        }
    } else {
        _headers.insert(std::make_pair(normalizedName, value));
    }

    return true;
}

bool HttpRequest::finalizeHeaders() {
    if (_version == "HTTP/1.1") {
        const std::map<std::string, std::string>::const_iterator hostIt = _headers.find("host");
        if (hostIt == _headers.end() || trim(hostIt->second).empty()) {
            setError(400, "missing required Host header");
            return false;
        }
    }

    const std::map<std::string, std::string>::const_iterator teIt = _headers.find("transfer-encoding");
    const std::map<std::string, std::string>::const_iterator clIt = _headers.find("content-length");
    const bool hasTransferEncoding = teIt != _headers.end();
    const bool hasContentLength = clIt != _headers.end();

    if (hasTransferEncoding && hasContentLength) {
        setError(400, "cannot use both Content-Length and Transfer-Encoding");
        return false;
    }

    if (hasTransferEncoding) {
        const std::vector<std::string> encodings = split(toLower(teIt->second), ",");
        if (encodings.size() != 1 || trim(encodings[0]) != "chunked") {
            setError(501, "unsupported Transfer-Encoding");
            return false;
        }

        _bodyMode = BODY_CHUNKED;
        _chunkState = CHUNK_READING_SIZE;
        _state = READING_BODY;
        return true;
    }

    if (hasContentLength) {
        std::size_t parsedLength = 0;
        if (!parseUnsignedDecimal(trim(clIt->second), parsedLength)) {
            setError(400, "invalid Content-Length");
            return false;
        }

        _contentLength = parsedLength;
        if (_contentLength > _clientMaxBodySize) {
            setError(413, "payload too large");
            return false;
        }

        if (_contentLength == 0) {
            markComplete();
            return true;
        }

        try {
            _body.reserve(_contentLength);
        } catch (...) {
            setError(500, "out of memory for body allocation");
            return false;
        }

        _bodyMode = BODY_CONTENT_LENGTH;
        _state = READING_BODY;
        return true;
    }

    _bodyMode = BODY_NONE;
    _contentLength = 0;
    markComplete();
    return true;
}

bool HttpRequest::parseBody() {
    if (_bodyMode == BODY_CONTENT_LENGTH) {
        return parseContentLengthBody();
    }
    if (_bodyMode == BODY_CHUNKED) {
        return parseChunkedBody();
    }

    markComplete();
    return true;
}

bool HttpRequest::parseContentLengthBody() {
    if (_body.size() >= _contentLength) {
        markComplete();
        return true;
    }

    if (_buffer.empty()) {
        return false;
    }

    const std::size_t remaining = _contentLength - _body.size();
    const std::size_t toCopy = std::min(remaining, _buffer.size());
    _body.append(_buffer, 0, toCopy);
    _buffer.erase(0, toCopy);

    if (_body.size() > _clientMaxBodySize) {
        setError(413, "payload too large");
        return true;
    }

    if (_body.size() == _contentLength) {
        markComplete();
    }

    return true;
}

bool HttpRequest::parseChunkedBody() {
    bool progressed = false;

    while (_state == READING_BODY && _bodyMode == BODY_CHUNKED) {
        if (_chunkState == CHUNK_READING_SIZE) {
            const std::size_t lineEnd = _buffer.find("\r\n");
            if (lineEnd == std::string::npos) {
                if (_buffer.size() > _maxChunkLineBytes) {
                    setError(400, "chunk size line too large");
                    return true;
                }
                return progressed;
            }

            const std::string chunkSizeLine = _buffer.substr(0, lineEnd);
            _buffer.erase(0, lineEnd + 2);
            progressed = true;

            std::size_t parsedSize = 0;
            if (!parseChunkSizeLine(chunkSizeLine, parsedSize)) {
                setError(400, "invalid chunk size");
                return true;
            }

            _currentChunkSize = parsedSize;
            _receivedInCurrentChunk = 0;

            if (_currentChunkSize == 0) {
                _chunkState = CHUNK_READING_TRAILERS;
            } else {
                if (_body.size() > _clientMaxBodySize || _currentChunkSize > (_clientMaxBodySize - _body.size())) {
                    setError(413, "payload too large");
                    return true;
                }
                _chunkState = CHUNK_READING_DATA;
            }
            continue;
        }

        if (_chunkState == CHUNK_READING_DATA) {
            if (_buffer.empty()) {
                return progressed;
            }

            const std::size_t remaining = _currentChunkSize - _receivedInCurrentChunk;
            const std::size_t toCopy = std::min(remaining, _buffer.size());
            _body.append(_buffer, 0, toCopy);
            _buffer.erase(0, toCopy);
            _receivedInCurrentChunk += toCopy;
            progressed = true;

            if (_body.size() > _clientMaxBodySize) {
                setError(413, "payload too large");
                return true;
            }

            if (_receivedInCurrentChunk == _currentChunkSize) {
                _chunkState = CHUNK_EXPECTING_DATA_CRLF;
            }

            continue;
        }

        if (_chunkState == CHUNK_EXPECTING_DATA_CRLF) {
            if (_buffer.size() < 2) {
                return progressed;
            }

            if (_buffer[0] != '\r' || _buffer[1] != '\n') {
                setError(400, "missing CRLF after chunk data");
                return true;
            }

            _buffer.erase(0, 2);
            _chunkState = CHUNK_READING_SIZE;
            progressed = true;
            continue;
        }

        if (_chunkState == CHUNK_READING_TRAILERS) {
            if (_buffer.size() >= 2 && _buffer[0] == '\r' && _buffer[1] == '\n') {
                _buffer.erase(0, 2);
                _chunkState = CHUNK_COMPLETE;
                progressed = true;
                markComplete();
                return true;
            }

            const std::size_t trailerEnd = _buffer.find("\r\n\r\n");
            if (trailerEnd == std::string::npos) {
                if (_buffer.size() > _maxHeaderBytes) {
                    setError(431, "chunk trailers too large");
                    return true;
                }
                return progressed;
            }

            const std::string trailerBlock = _buffer.substr(0, trailerEnd);
            _buffer.erase(0, trailerEnd + 4);
            progressed = true;

            if (!parseTrailers(trailerBlock)) {
                return true;
            }

            _chunkState = CHUNK_COMPLETE;
            markComplete();
            return true;
        }

        if (_chunkState == CHUNK_COMPLETE) {
            markComplete();
            return true;
        }
    }

    return progressed;
}

bool HttpRequest::parseChunkSizeLine(const std::string& line, std::size_t& outSize) const {
    const std::size_t semicolonPos = line.find(';');
    const std::string sizeToken = trim(line.substr(0, semicolonPos));
    if (sizeToken.empty()) {
        return false;
    }
    return parseUnsignedHex(sizeToken, outSize);
}

bool HttpRequest::parseTrailers(const std::string& trailerBlock) {
    if (trailerBlock.empty()) {
        return true;
    }

    const std::vector<std::string> lines = split(trailerBlock, "\r\n");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty()) {
            continue;
        }

        const std::size_t colonPos = line.find(':');
        if (colonPos == std::string::npos || colonPos == 0) {
            setError(400, "invalid chunk trailer");
            return false;
        }

        const std::string name = trim(line.substr(0, colonPos));
        const std::string value = trim(line.substr(colonPos + 1));
        if (!isValidHeaderName(name)) {
            setError(400, "invalid chunk trailer name");
            return false;
        }

        const std::string normalizedName = toLower(name);
        std::map<std::string, std::string>::iterator it = _headers.find(normalizedName);
        if (it == _headers.end()) {
            _headers.insert(std::make_pair(normalizedName, value));
        } else if (!value.empty()) {
            if (!it->second.empty()) {
                it->second.append(", ");
            }
            it->second.append(value);
        }
    }

    return true;
}

void HttpRequest::setError(int status, const std::string& message) {
    _state = ERROR;
    _errorStatus = status;
    _errorMessage = message;
}

void HttpRequest::markComplete() {
    _state = COMPLETE;
    if (_bodyMode == BODY_CHUNKED) {
        _contentLength = _body.size();
    }
}

HttpRequest::ParseState HttpRequest::getState() const {
    return _state;
}

bool HttpRequest::isComplete() const {
    return _state == COMPLETE;
}

bool HttpRequest::hasError() const {
    return _state == ERROR;
}

int HttpRequest::getErrorStatus() const {
    return _errorStatus;
}

const std::string& HttpRequest::getErrorMessage() const {
    return _errorMessage;
}

const std::string& HttpRequest::getMethod() const {
    return _method;
}

const std::string& HttpRequest::getRequestTarget() const {
    return _requestTarget;
}

const std::string& HttpRequest::getPath() const {
    return _path;
}

const std::string& HttpRequest::getQueryString() const {
    return _queryString;
}

const std::string& HttpRequest::getVersion() const {
    return _version;
}

const std::map<std::string, std::string>& HttpRequest::getHeaders() const {
    return _headers;
}

bool HttpRequest::hasHeader(const std::string& name) const {
    return _headers.find(toLower(name)) != _headers.end();
}

std::string HttpRequest::getHeader(const std::string& name) const {
    std::map<std::string, std::string>::const_iterator it = _headers.find(toLower(name));
    if (it == _headers.end()) {
        return "";
    }
    return it->second;
}

const std::string& HttpRequest::getBody() const {
    return _body;
}

std::size_t HttpRequest::getContentLength() const {
    return _contentLength;
}

HttpRequest::BodyMode HttpRequest::getBodyMode() const {
    return _bodyMode;
}

bool HttpRequest::isChunked() const {
    return _bodyMode == BODY_CHUNKED;
}

const std::string& HttpRequest::getUnparsedBuffer() const {
    return _buffer;
}

std::string HttpRequest::releaseUnparsedBuffer() {
    std::string out = _buffer;
    _buffer.clear();
    return out;
}

void HttpRequest::setClientMaxBodySize(std::size_t maxBodySize) {
    _clientMaxBodySize = maxBodySize;
}

std::size_t HttpRequest::getClientMaxBodySize() const {
    return _clientMaxBodySize;
}

void HttpRequest::setMaxHeaderBytes(std::size_t maxHeaderBytes) {
    _maxHeaderBytes = maxHeaderBytes;
}

std::size_t HttpRequest::getMaxHeaderBytes() const {
    return _maxHeaderBytes;
}

void HttpRequest::setMaxChunkLineBytes(std::size_t maxChunkLineBytes) {
    _maxChunkLineBytes = maxChunkLineBytes;
}

std::size_t HttpRequest::getMaxChunkLineBytes() const {
    return _maxChunkLineBytes;
}

bool HttpRequest::isTokenChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) ||
           c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
           c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
           c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

bool HttpRequest::isValidHeaderName(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (!isTokenChar(name[i])) {
            return false;
        }
    }
    return true;
}

bool HttpRequest::parseUnsignedDecimal(const std::string& value, std::size_t& out) {
    if (value.empty()) {
        return false;
    }

    const std::size_t maxValue = static_cast<std::size_t>(-1);
    std::size_t result = 0;

    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c < '0' || c > '9') {
            return false;
        }
        const std::size_t digit = static_cast<std::size_t>(c - '0');
        if (result > (maxValue - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }

    out = result;
    return true;
}

bool HttpRequest::parseUnsignedHex(const std::string& value, std::size_t& out) {
    if (value.empty()) {
        return false;
    }

    const std::size_t maxValue = static_cast<std::size_t>(-1);
    std::size_t result = 0;

    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        std::size_t digit = 0;

        if (c >= '0' && c <= '9') {
            digit = static_cast<std::size_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::size_t>(10 + (c - 'a'));
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<std::size_t>(10 + (c - 'A'));
        } else {
            return false;
        }

        if (result > (maxValue - digit) / 16) {
            return false;
        }
        result = result * 16 + digit;
    }

    out = result;
    return true;
}

std::string HttpRequest::trim(const std::string& value) {
    if (value.empty()) {
        return value;
    }

    std::size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        ++start;
    }

    if (start == value.size()) {
        return "";
    }

    std::size_t end = value.size() - 1;
    while (end > start && (value[end] == ' ' || value[end] == '\t')) {
        --end;
    }

    return value.substr(start, end - start + 1);
}

std::string HttpRequest::toLower(const std::string& value) {
    std::string out(value);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    }
    return out;
}

std::vector<std::string> HttpRequest::split(const std::string& value, const std::string& delim) {
    std::vector<std::string> parts;
    if (delim.empty()) {
        parts.push_back(value);
        return parts;
    }

    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t pos = value.find(delim, start);
        if (pos == std::string::npos) {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, pos - start));
        start = pos + delim.size();
    }
    return parts;
}

std::string HttpRequest::sizeToString(std::size_t value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}
