#ifndef CGIMANAGER_HPP
#define CGIMANAGER_HPP

#include "core/ClientConnection.hpp"
#include "config/Config.hpp"
#include <string>

namespace CgiManager {

    bool shouldUseCgi(const ServerConfig& cfg, const LocationConfig* location, const HttpRequest& request, std::string& ext);
    bool setupCgiTask(const ServerConfig& cfg, const LocationConfig* location, const std::string& matchedKey, ClientConnection& client, const std::string& scriptExt);
    void applyCgiOutputToResponse(const std::string& cgiOutput, HttpResponse& response);

}

#endif
