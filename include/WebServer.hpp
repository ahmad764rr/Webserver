#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include "Config.hpp"
#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"

#include <poll.h>
#include <map>
#include <string>
#include <vector>

class WebServer {
public:
    explicit WebServer(const std::vector<ServerConfig>& servers);
    ~WebServer();

    bool init(std::string& error);
    void run();

private:
    struct CgiTask {
        bool active;
        pid_t pid;
        int pipe_in_fd;
        int pipe_out_fd;
        std::string body_to_write;
        std::size_t body_written;
        std::string cgi_output;
        long start_time_ms;
        bool process_exited;
        int exit_status;

        CgiTask();
        void cleanup();
    };

    struct ClientConnection {
        int fd;
        std::size_t serverIndex;
        HttpRequest request;
        HttpResponse response;
        bool hasResponse;
        bool closeAfterSend;
        std::string carryBuffer;
        CgiTask cgiTask;

        ClientConnection();
    };

    std::vector<ServerConfig> _servers;
    std::vector<struct pollfd> _pollFds;
    std::map<int, int> _listenSockets;
    std::map<int, ClientConnection> _clients;

    bool createListenSockets(std::string& error);
    void closeAllSockets();

    void rebuildPollFds();
    bool isListenFd(int fd) const;

    void acceptClient(int listenFd);
    void handleClientRead(int clientFd);
    void handleClientWrite(int clientFd);
    void closeClient(int clientFd);

    int findClientForCgiInFd(int fd) const;
    int findClientForCgiOutFd(int fd) const;
    void handleCgiRead(int clientFd);
    void handleCgiWrite(int clientFd);
    void checkCgiTimeouts();

    void refineServerSelection(ClientConnection& client);
    void buildResponseForRequest(ClientConnection& client);
    void handleUpload(ClientConnection& client, const RouteConfig* route, const std::string& resolvedPath);
    void buildErrorResponse(ClientConnection& client, int statusCode, const std::string& message);
    
    bool shouldUseCgi(const ServerConfig& cfg, const RouteConfig* route, const HttpRequest& request, std::string& ext) const;
    bool setupCgiTask(const ServerConfig& cfg, const RouteConfig* route, const std::string& matchedKey, ClientConnection& client) const;
    void applyCgiOutputToResponse(const std::string& cgiOutput, HttpResponse& response) const;

    bool loadFile(const std::string& path, std::string& out) const;
    std::string resolvePath(const ServerConfig& cfg, const RouteConfig* route, const std::string& matchedKey, const std::string& requestPath) const;
    const RouteConfig* findRoute(const ServerConfig& cfg, const std::string& requestPath, std::string& matchedKey) const;
    std::string mimeTypeForPath(const std::string& path) const;
    
    bool isDirectory(const std::string& path) const;
    bool fileExists(const std::string& path) const;
    bool generateAutoIndex(const std::string& path, const std::string& requestPath, std::string& out) const;
    bool connectionShouldClose(const HttpRequest& request) const;

    void applyConfiguredErrorPage(const ServerConfig& cfg,
                                  int statusCode,
                                  HttpResponse& response) const;
};

#endif