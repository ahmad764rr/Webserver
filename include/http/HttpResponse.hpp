#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include <cstddef>
#include <map>
#include <string>

class HttpResponse {
public:
    enum ResponseState {
        BUILDING,
        READY,
        STREAMING,
        COMPLETE,
        ERROR
    };

    enum BodyMode {
        BODY_MODE_NONE,
        BODY_MODE_FIXED,
        BODY_MODE_CHUNKED
    };

    HttpResponse();
    ~HttpResponse();

    void reset();

    void setVersion(const std::string& version);
    const std::string& getVersion() const;

    void setStatusCode(int statusCode);
    int getStatusCode() const;
    void setReasonPhrase(const std::string& reasonPhrase);
    const std::string& getReasonPhrase() const;

    void setHeader(const std::string& name, const std::string& value);
    bool hasHeader(const std::string& name) const;
    std::string getHeader(const std::string& name) const;
    void removeHeader(const std::string& name);
    const std::map<std::string, std::string>& getHeaders() const;

    void setBody(const std::string& body);
    void appendToBody(const std::string& data);
    void clearBody();
    const std::string& getBody() const;
    BodyMode getBodyMode() const;

    void setContentType(const std::string& contentType);
    void setKeepAlive(bool keepAlive);
    bool getKeepAlive() const;
    void setHeadResponse(bool enabled);
    bool isHeadResponse() const;

    void enableChunkedTransferEncoding();
    bool isChunkedTransferEncoding() const;
    bool appendChunk(const std::string& data);
    bool finishChunkedBody();
    bool isChunkedBodyFinished() const;

    ResponseState prepare();
    ResponseState getState() const;
    bool isComplete() const;
    bool hasError() const;
    const std::string& getErrorMessage() const;

    std::string getNextBytes(std::size_t maxBytes) const;
    void consumeBytes(std::size_t bytes);
    std::size_t getPendingBytes() const;
    std::size_t getBytesSent() const;
    const std::string& getSerializedData() const;

    void setMaxHeaderBytes(std::size_t maxHeaderBytes);
    std::size_t getMaxHeaderBytes() const;

    static HttpResponse stockResponse(int statusCode,
                                      bool keepAlive);
    static std::string defaultReasonPhrase(int statusCode);
    static std::string buildDefaultErrorPage(int statusCode,
                                             const std::string& reasonPhrase);

private:
    ResponseState _state;
    BodyMode _bodyMode;

    std::string _version;
    int _statusCode;
    std::string _reasonPhrase;
    std::map<std::string, std::string> _headers;
    std::string _body;
    bool _keepAlive;
    bool _headResponse;
    bool _chunkedFinished;

    std::string _serialized;
    std::size_t _sendOffset;
    std::size_t _totalBytesSent;
    std::size_t _maxHeaderBytes;
    std::string _errorMessage;

    bool validateBeforePrepare();
    bool buildHeadersBlock();
    void appendChunkFrame(const std::string& data);
    void setError(const std::string& message);
    void updateStreamingState();
    void compactSerializedBuffer();
    bool ensureBuildingState();

    static bool isNoBodyStatus(int statusCode);
    static bool isTokenChar(char c);
    static bool isValidHeaderName(const std::string& name);
    static bool hasInvalidHeaderValue(const std::string& value);
    static std::string toLower(const std::string& value);
    static std::string trim(const std::string& value);
    static std::string sizeToString(std::size_t value);
    static std::string hexSizeToString(std::size_t value);
    static std::string formatHttpDate();
};

#endif
