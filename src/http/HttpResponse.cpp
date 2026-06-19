#include "http/HttpResponse.hpp"

#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {
const std::size_t kDefaultMaxHeaderBytes = 16 * 1024;
const std::size_t kCompactThreshold = 4096;
}

HttpResponse::HttpResponse()
    : _state(BUILDING),
      _bodyMode(BODY_MODE_NONE),
      _version("HTTP/1.1"),
      _statusCode(200),
      _reasonPhrase(defaultReasonPhrase(200)),
      _keepAlive(true),
      _headResponse(false),
      _chunkedFinished(false),
      _sendOffset(0),
      _totalBytesSent(0),
      _maxHeaderBytes(kDefaultMaxHeaderBytes) {
}


HttpResponse::~HttpResponse() {
}

void HttpResponse::reset() {
    _state = BUILDING;
    _bodyMode = BODY_MODE_NONE;

    _version = "HTTP/1.1";
    _statusCode = 200;
    _reasonPhrase = defaultReasonPhrase(200);
    _headers.clear();
    _body.clear();
    _keepAlive = true;
    _headResponse = false;
    _chunkedFinished = false;

    _serialized.clear();
    _sendOffset = 0;
    _totalBytesSent = 0;
    _errorMessage.clear();
}

void HttpResponse::setVersion(const std::string& version) {
    if (!ensureBuildingState()) {
        return;
    }
    _version = version;
}

const std::string& HttpResponse::getVersion() const {
    return _version;
}

void HttpResponse::setStatusCode(int statusCode) {
    if (!ensureBuildingState()) {
        return;
    }
    _statusCode = statusCode;
    _reasonPhrase = defaultReasonPhrase(statusCode);
}

int HttpResponse::getStatusCode() const {
    return _statusCode;
}

void HttpResponse::setReasonPhrase(const std::string& reasonPhrase) {
    if (!ensureBuildingState()) {
        return;
    }
    _reasonPhrase = reasonPhrase;
}

const std::string& HttpResponse::getReasonPhrase() const {
    return _reasonPhrase;
}

void HttpResponse::setHeader(const std::string& name, const std::string& value) {
    if (!ensureBuildingState()) {
        return;
    }

    const std::string normalizedName = toLower(trim(name));
    const std::string normalizedValue = trim(value);

    if (!isValidHeaderName(normalizedName)) {
        setError("invalid response header name");
        return;
    }
    if (hasInvalidHeaderValue(normalizedValue)) {
        setError("invalid response header value");
        return;
    }

    _headers[normalizedName] = normalizedValue;
}

bool HttpResponse::hasHeader(const std::string& name) const {
    return _headers.find(toLower(name)) != _headers.end();
}

std::string HttpResponse::getHeader(const std::string& name) const {
    std::map<std::string, std::string>::const_iterator it = _headers.find(toLower(name));
    if (it == _headers.end()) {
        return "";
    }
    return it->second;
}

void HttpResponse::removeHeader(const std::string& name) {
    if (!ensureBuildingState()) {
        return;
    }
    _headers.erase(toLower(name));
}

const std::map<std::string, std::string>& HttpResponse::getHeaders() const {
    return _headers;
}

void HttpResponse::setBody(const std::string& body) {
    if (!ensureBuildingState()) {
        return;
    }
    if (_bodyMode == BODY_MODE_CHUNKED) {
        setError("cannot set a fixed body after enabling chunked mode");
        return;
    }

    _body = body;
    _bodyMode = _body.empty() ? BODY_MODE_NONE : BODY_MODE_FIXED;
}

void HttpResponse::appendToBody(const std::string& data) {
    if (!ensureBuildingState()) {
        return;
    }
    if (_bodyMode == BODY_MODE_CHUNKED) {
        setError("cannot append a fixed body after enabling chunked mode");
        return;
    }
    if (data.empty()) {
        return;
    }

    _body.append(data);
    _bodyMode = BODY_MODE_FIXED;
}

void HttpResponse::clearBody() {
    if (!ensureBuildingState()) {
        return;
    }
    if (_bodyMode == BODY_MODE_CHUNKED) {
        setError("cannot clear a chunked body with clearBody()");
        return;
    }

    _body.clear();
    _bodyMode = BODY_MODE_NONE;
}

const std::string& HttpResponse::getBody() const {
    return _body;
}

HttpResponse::BodyMode HttpResponse::getBodyMode() const {
    return _bodyMode;
}

void HttpResponse::setContentType(const std::string& contentType) {
    setHeader("content-type", contentType);
}

void HttpResponse::setKeepAlive(bool keepAlive) {
    if (!ensureBuildingState()) {
        return;
    }
    _keepAlive = keepAlive;
}

bool HttpResponse::getKeepAlive() const {
    return _keepAlive;
}

void HttpResponse::setHeadResponse(bool enabled) {
    if (!ensureBuildingState()) {
        return;
    }
    _headResponse = enabled;
}

bool HttpResponse::isHeadResponse() const {
    return _headResponse;
}

void HttpResponse::enableChunkedTransferEncoding() {
    if (!ensureBuildingState()) {
        return;
    }
    if (!_body.empty()) {
        setError("cannot enable chunked mode after setting a fixed body");
        return;
    }

    _bodyMode = BODY_MODE_CHUNKED;
    _chunkedFinished = false;
}

bool HttpResponse::isChunkedTransferEncoding() const {
    return _bodyMode == BODY_MODE_CHUNKED;
}

bool HttpResponse::appendChunk(const std::string& data) {
    if (_bodyMode != BODY_MODE_CHUNKED) {
        setError("response is not configured for chunked transfer");
        return false;
    }
    if (_chunkedFinished) {
        setError("chunked response is already finished");
        return false;
    }
    if (_headResponse) {
        setError("HEAD-style responses do not support chunk streaming");
        return false;
    }
    if (_state == BUILDING && prepare() == ERROR) {
        return false;
    }
    if (_state == ERROR || _state == COMPLETE) {
        return false;
    }
    if (data.empty()) {
        return true;
    }

    appendChunkFrame(data);
    updateStreamingState();
    return true;
}

bool HttpResponse::finishChunkedBody() {
    if (_bodyMode != BODY_MODE_CHUNKED) {
        setError("response is not configured for chunked transfer");
        return false;
    }
    if (_headResponse) {
        setError("HEAD-style responses do not support chunk streaming");
        return false;
    }
    if (_state == BUILDING && prepare() == ERROR) {
        return false;
    }
    if (_state == ERROR || _state == COMPLETE) {
        return false;
    }
    if (_chunkedFinished) {
        return true;
    }

    _serialized.append("0\r\n\r\n");
    _chunkedFinished = true;
    updateStreamingState();
    return true;
}

bool HttpResponse::isChunkedBodyFinished() const {
    return _chunkedFinished;
}

HttpResponse::ResponseState HttpResponse::prepare() {
    if (_state == ERROR || _state == COMPLETE) {
        return _state;
    }
    if (_state == READY || _state == STREAMING) {
        return _state;
    }
    if (!validateBeforePrepare()) {
        return _state;
    }
    if (!buildHeadersBlock()) {
        return _state;
    }

    // BUILDING -> READY. The caller can now pull bytes with getNextBytes()
    // and acknowledge sent bytes with consumeBytes() after each non-blocking send().
    _state = READY;
    return _state;
}

HttpResponse::ResponseState HttpResponse::getState() const {
    return _state;
}

bool HttpResponse::isComplete() const {
    return _state == COMPLETE;
}

bool HttpResponse::hasError() const {
    return _state == ERROR;
}

const std::string& HttpResponse::getErrorMessage() const {
    return _errorMessage;
}

std::string HttpResponse::getNextBytes(std::size_t maxBytes) const {
    if (_state == BUILDING || _state == ERROR || _state == COMPLETE || maxBytes == 0) {
        return "";
    }

    const std::size_t pending = getPendingBytes();
    if (pending == 0) {
        return "";
    }

    const std::size_t toCopy = pending < maxBytes ? pending : maxBytes;
    return _serialized.substr(_sendOffset, toCopy);
}

void HttpResponse::consumeBytes(std::size_t bytes) {
    if (_state == BUILDING || _state == ERROR || _state == COMPLETE || bytes == 0) {
        return;
    }

    const std::size_t pending = getPendingBytes();
    if (pending == 0) {
        updateStreamingState();
        return;
    }

    if (bytes > pending) {
        bytes = pending;
    }

    _sendOffset += bytes;
    _totalBytesSent += bytes;
    compactSerializedBuffer();
    updateStreamingState();
}

std::size_t HttpResponse::getPendingBytes() const {
    if (_serialized.size() < _sendOffset) {
        return 0;
    }
    return _serialized.size() - _sendOffset;
}

std::size_t HttpResponse::getBytesSent() const {
    return _totalBytesSent;
}

const std::string& HttpResponse::getSerializedData() const {
    return _serialized;
}

void HttpResponse::setMaxHeaderBytes(std::size_t maxHeaderBytes) {
    if (!ensureBuildingState()) {
        return;
    }
    _maxHeaderBytes = maxHeaderBytes;
}

std::size_t HttpResponse::getMaxHeaderBytes() const {
    return _maxHeaderBytes;
}

HttpResponse HttpResponse::stockResponse(int statusCode,
                                         bool keepAlive) {
    HttpResponse response;
    response.setStatusCode(statusCode);
    response.setKeepAlive(keepAlive);

    if (statusCode >= 400 && !isNoBodyStatus(statusCode)) {
        const std::string reasonPhrase = response.getReasonPhrase();
        response.setContentType("text/html; charset=utf-8");
        response.setBody(buildDefaultErrorPage(statusCode, reasonPhrase));
    }

    return response;
}

// Ensure this specific function switch block mapping provides full standard compliance:
std::string HttpResponse::defaultReasonPhrase(int statusCode) {
    switch (statusCode) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default: return "Unknown Status";
    }
}

std::string HttpResponse::buildDefaultErrorPage(int statusCode,
                                                const std::string& reasonPhrase) {
    std::ostringstream html;
    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head>\n"
         << "  <meta charset=\"utf-8\">\n"
         << "  <title>" << statusCode << " " << reasonPhrase << "</title>\n"
         << "</head>\n"
         << "<body>\n"
         << "  <h1>" << statusCode << " " << reasonPhrase << "</h1>\n"
         << "  <p>webserv generated this default error page.</p>\n"
         << "</body>\n"
         << "</html>\n";
    return html.str();
}

bool HttpResponse::validateBeforePrepare() {
    if (_version != "HTTP/1.1" && _version != "HTTP/1.0") {
        setError("only HTTP/1.1 and HTTP/1.0 responses are supported");
        return false;
    }
    if (_statusCode < 100 || _statusCode > 599) {
        setError("invalid HTTP status code");
        return false;
    }
    if (_reasonPhrase.empty()) {
        _reasonPhrase = defaultReasonPhrase(_statusCode);
    }
    if (hasInvalidHeaderValue(_reasonPhrase)) {
        setError("invalid reason phrase");
        return false;
    }
    if (_headResponse && _bodyMode == BODY_MODE_CHUNKED) {
        setError("HEAD-style responses are not supported with chunked transfer");
        return false;
    }
    if (isNoBodyStatus(_statusCode)) {
        if (_bodyMode == BODY_MODE_CHUNKED || !_body.empty()) {
            setError("selected status code must not include a response body");
            return false;
        }
        _bodyMode = BODY_MODE_NONE;
    }

    return true;
}

bool HttpResponse::buildHeadersBlock() {
    _serialized.clear();
    _sendOffset = 0;
    _totalBytesSent = 0;

    _headers["connection"] = _keepAlive ? "keep-alive" : "close";
    _headers["access-control-allow-origin"] = "*";
    _headers["access-control-allow-methods"] = "GET, POST, DELETE, OPTIONS";
    _headers["access-control-allow-headers"] = "*";
    if (!_headers.count("date")) {
        _headers["date"] = formatHttpDate();
    }
    if (!_headers.count("server")) {
        _headers["server"] = "webserv";
    }

    if (_bodyMode == BODY_MODE_CHUNKED) {
        _headers.erase("content-length");
        _headers["transfer-encoding"] = "chunked";
    } else {
        _headers.erase("transfer-encoding");
        if (!isNoBodyStatus(_statusCode)) {
            if (_headers.find("content-length") == _headers.end()) {
                _headers["content-length"] = sizeToString(_body.size());
            }
        } else {
            _headers.erase("content-length");
        }
    }

    std::ostringstream headersStream;
    headersStream << _version << " "
                  << _statusCode << " "
                  << _reasonPhrase << "\r\n";

    std::map<std::string, std::string>::const_iterator it = _headers.begin();
    for (; it != _headers.end(); ++it) {
        headersStream << it->first << ": " << it->second << "\r\n";
    }
    headersStream << "\r\n";

    const std::string headerBlock = headersStream.str();
    if (headerBlock.size() > _maxHeaderBytes) {
        setError("response headers too large");
        return false;
    }

    _serialized = headerBlock;
    if (_bodyMode == BODY_MODE_FIXED && !_headResponse) {
        _serialized.append(_body);
    }

    return true;
}

void HttpResponse::appendChunkFrame(const std::string& data) {
    _serialized.append(hexSizeToString(data.size()));
    _serialized.append("\r\n");
    _serialized.append(data);
    _serialized.append("\r\n");
}

void HttpResponse::setError(const std::string& message) {
    _state = ERROR;
    _errorMessage = message;
}

void HttpResponse::updateStreamingState() {
    if (_state == ERROR || _state == BUILDING) {
        return;
    }

    const std::size_t pending = getPendingBytes();
    if (pending == 0) {
        if (_bodyMode == BODY_MODE_CHUNKED && !_chunkedFinished) {
            _state = READY;
        } else {
            _state = COMPLETE;
        }
        return;
    }

    if (_totalBytesSent == 0) {
        _state = READY;
    } else {
        _state = STREAMING;
    }
}

void HttpResponse::compactSerializedBuffer() {
    if (_sendOffset == 0) {
        return;
    }
    if (_sendOffset == _serialized.size()) {
        _serialized.clear();
        _sendOffset = 0;
        return;
    }
    if (_sendOffset >= kCompactThreshold && _sendOffset >= (_serialized.size() / 2)) {
        _serialized.erase(0, _sendOffset);
        _sendOffset = 0;
    }
}

bool HttpResponse::ensureBuildingState() {
    if (_state != BUILDING) {
        setError("response can no longer be modified after prepare()");
        return false;
    }
    return true;
}

bool HttpResponse::isNoBodyStatus(int statusCode) {
    return (statusCode >= 100 && statusCode < 200) ||
           statusCode == 204 ||
           statusCode == 304;
}

bool HttpResponse::isTokenChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) ||
           c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
           c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
           c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

bool HttpResponse::isValidHeaderName(const std::string& name) {
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

bool HttpResponse::hasInvalidHeaderValue(const std::string& value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c == '\r' || c == '\n' || c == 0 || (c < 32 && c != '\t') || c == 127) {
            return true;
        }
    }
    return false;
}

std::string HttpResponse::toLower(const std::string& value) {
    std::string out(value);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    }
    return out;
}

std::string HttpResponse::trim(const std::string& value) {
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

std::string HttpResponse::sizeToString(std::size_t value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string HttpResponse::hexSizeToString(std::size_t value) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase << value;
    return oss.str();
}

std::string HttpResponse::formatHttpDate() {
    char buffer[64];
    const std::time_t now = std::time(NULL);
    const std::tm* utcTime = std::gmtime(&now);

    if (utcTime == NULL) {
        return "";
    }

    if (std::strftime(buffer, sizeof(buffer),
                      "%a, %d %b %Y %H:%M:%S GMT", utcTime) == 0) {
        return "";
    }

    return std::string(buffer);
}
