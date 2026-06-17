#ifndef HTTPHANDLER_HPP
#define HTTPHANDLER_HPP

#include "core/ClientConnection.hpp"
#include "config/Config.hpp"
#include <string>
#include <vector>

class WebServer;

namespace HttpHandler {

    void refineServerSelection(ClientConnection& client, const std::vector<ServerConfig>& servers);
    void buildResponseForRequest(ClientConnection& client, const std::vector<ServerConfig>& servers);
    void handleUpload(ClientConnection& client, const std::vector<ServerConfig>& servers, const RouteConfig* route, const std::string& resolvedPath);
    void buildErrorResponse(ClientConnection& client, const std::vector<ServerConfig>& servers, int statusCode, const std::string& message);
    void applyConfiguredErrorPage(const ServerConfig& cfg, int statusCode, HttpResponse& response);
    bool connectionShouldClose(const HttpRequest& request);
    
    std::string resolvePath(const ServerConfig& cfg, const RouteConfig* route, const std::string& matchedKey, const std::string& requestPath);
    const RouteConfig* findRoute(const ServerConfig& cfg, const std::string& requestPath, std::string& matchedKey);

}

#endif
