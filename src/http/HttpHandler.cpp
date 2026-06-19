#include "http/HttpHandler.hpp"
#include "utils/FileUtils.hpp"
#include "cgi/CgiManager.hpp"
#include "core/WebServer.hpp"

#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <sys/stat.h>


namespace HttpHandler {

const LocationConfig* findLocation(const ServerConfig& cfg, const std::string& requestPath, std::string& matchedKey) {
    const LocationConfig* bestMatch = NULL;
    std::size_t longestMatch = 0;
    matchedKey.clear();
    for (std::map<std::string, LocationConfig>::const_iterator it = cfg.locations.begin(); it != cfg.locations.end(); ++it) {
        const std::string& locationPath = it->first;
        if (requestPath.find(locationPath) == 0 && locationPath.length() > longestMatch) {
            longestMatch = locationPath.length(); bestMatch = &it->second; matchedKey = locationPath;
        }
    }
    return bestMatch;
}

std::string resolvePath(const ServerConfig& cfg, const LocationConfig* location, const std::string& matchedKey, const std::string& requestPath) {
    if (requestPath.find("..") != std::string::npos) return "";
    std::string full = (location && !location->root.empty()) ? location->root : cfg.root;
    if (full.empty()) full = ".";

    if (location && !location->root.empty() && !matchedKey.empty()) {
        std::string remainingPath = requestPath.substr(matchedKey.length());
        if (full[full.size() - 1] == '/' && !remainingPath.empty() && remainingPath[0] == '/') full.erase(full.size() - 1);
        else if (full[full.size() - 1] != '/' && !remainingPath.empty() && remainingPath[0] != '/') full += "/";
        full += remainingPath;
    } else {
        if (full[full.size() - 1] == '/' && !requestPath.empty() && requestPath[0] == '/') full.erase(full.size() - 1);
        else if (full[full.size() - 1] != '/' && !requestPath.empty() && requestPath[0] != '/') full += "/";
        full += requestPath;
    }
    return full;
}



void applyConfiguredErrorPage(const ServerConfig& cfg, int statusCode, HttpResponse& response) {
    std::map<int, std::string>::const_iterator it = cfg.errorPages.find(statusCode);
    if (it == cfg.errorPages.end()) return;
    std::string pagePath = it->second;
    if (!pagePath.empty() && pagePath[0] == '/') pagePath = resolvePath(cfg, NULL, "", pagePath);
    std::string body;
    if (!pagePath.empty() && FileUtils::loadFile(pagePath, body)) { response.setContentType("text/html; charset=utf-8"); response.setBody(body); }
}

void buildErrorResponse(ClientConnection& client, const std::vector<ServerConfig>& servers, int statusCode, const std::string& message) {
    const ServerConfig& cfg = servers[client.serverIndex];
    client.response = HttpResponse::stockResponse(statusCode, !client.closeAfterSend);
    applyConfiguredErrorPage(cfg, statusCode, client.response);
    if (!message.empty()) client.response.setHeader("x-webserv-error", message);
    if (client.response.prepare() == HttpResponse::ERROR) {
        HttpResponse fallback = HttpResponse::stockResponse(500, false); fallback.prepare();
        client.response = fallback; client.closeAfterSend = true;
    }
}

bool connectionShouldClose(const HttpRequest& request) {
    std::string conn = request.getHeader("connection");
    for (std::size_t i = 0; i < conn.size(); ++i) if (conn[i] >= 'A' && conn[i] <= 'Z') conn[i] = static_cast<char>(conn[i] - 'A' + 'a');
    if (request.getVersion() == "HTTP/1.0") {
        return conn != "keep-alive";
    }
    return conn == "close";
}

void handleUpload(ClientConnection& client, const std::vector<ServerConfig>& servers, const LocationConfig* location, const std::string& resolvedPath) {
    std::string uploadTargetDir = location->uploadDir;
    if (uploadTargetDir.empty()) uploadTargetDir = "./www/upload";

    struct stat st;
    if (stat(uploadTargetDir.c_str(), &st) != 0) mkdir(uploadTargetDir.c_str(), 0755);

    std::string targetFile;
    const std::string& reqPath = client.request.getPath();
    if (reqPath.empty() || reqPath[reqPath.size() - 1] == '/') {
        std::ostringstream filenameOss;
        filenameOss << uploadTargetDir;
        if (uploadTargetDir.empty() || uploadTargetDir[uploadTargetDir.size() - 1] != '/') filenameOss << "/";
        filenameOss << "upload_" << time(NULL) << "_" << rand() % 1000 << ".bin";
        targetFile = filenameOss.str();
    } else {
        targetFile = resolvedPath;
    }

    std::ofstream out(targetFile.c_str(), std::ios::out | std::ios::binary);
    if (!out.is_open()) { buildErrorResponse(client, servers, 500, "Failed to instantiate file handle"); return; }
    out.write(client.request.getBody().data(), client.request.getBody().size());
    out.close();

    client.response.setStatusCode(201);
    client.response.setContentType("text/html; charset=utf-8");
    client.response.setBody("<html><body><h1>File uploaded successfully</h1></body></html>");
    client.response.prepare();
}

void buildResponseForRequest(ClientConnection& client, const std::vector<ServerConfig>& servers) {
    const ServerConfig& cfg = servers[client.serverIndex];
    const HttpRequest& request = client.request;

    client.response.reset();
    client.response.setVersion(request.getVersion());
    client.response.setKeepAlive(!connectionShouldClose(request));
    client.closeAfterSend = connectionShouldClose(request);

    if (request.getBody().size() > cfg.clientMaxBodySize) {
        buildErrorResponse(client, servers, 413, "Payload exceeds body storage restrictions");
        return;
    }

    std::string method = request.getMethod();
    if (method != "GET" && method != "POST" && method != "DELETE") {
        buildErrorResponse(client, servers, 405, "Method Not Allowed");
        return;
    }

    std::string locationMatchedPath;
    const LocationConfig* location = findLocation(cfg, request.getPath(), locationMatchedPath);

    if (location && !location->allowedMethods.empty()) {
        bool authorized = false;
        for (std::size_t i = 0; i < location->allowedMethods.size(); ++i) {
            if (location->allowedMethods[i] == method) { authorized = true; break; }
        }
        if (!authorized) { buildErrorResponse(client, servers, 405, "Method not allowed on explicit location mapping"); return; }
    }

    if (location && location->redirect.first >= 300 && location->redirect.first <= 399) {
        client.response.setStatusCode(location->redirect.first);
        client.response.setHeader("location", location->redirect.second);
        client.response.prepare();
        return;
    }

    std::string resolvedPath = resolvePath(cfg, location, locationMatchedPath, request.getPath());
    if (resolvedPath.empty()) { buildErrorResponse(client, servers, 403, "Forbidden path access detected"); return; }

    std::string ext;
    if (CgiManager::shouldUseCgi(cfg, location, request, ext)) {
        if (!CgiManager::setupCgiTask(cfg, location, locationMatchedPath, client, ext)) {
            buildErrorResponse(client, servers, 502, "Failed to start CGI engine");
            client.closeAfterSend = true; client.hasResponse = true;
        } else { client.hasResponse = false; }
        return;
    }

    if (method == "POST" && location && location->uploadEnable) {
        handleUpload(client, servers, location, resolvedPath);
        return;
    }

    if (method == "DELETE") {
        if (!FileUtils::fileExists(resolvedPath)) buildErrorResponse(client, servers, 404, "Target resource not found");
        else if (FileUtils::isDirectory(resolvedPath)) buildErrorResponse(client, servers, 403, "Cannot delete directories");
        else if (std::remove(resolvedPath.c_str()) == 0) { client.response.setStatusCode(204); client.response.prepare(); }
        else buildErrorResponse(client, servers, 403, "Delete operations blocked");
        return;
    }

    if (method != "GET") { buildErrorResponse(client, servers, 405, "Method unauthorized"); return; }

    if (FileUtils::isDirectory(resolvedPath)) {
        std::string indexFile = (location && !location->index.empty()) ? location->index : "index.html";
        std::string pathWithIndex = resolvedPath;
        if (pathWithIndex[pathWithIndex.size() - 1] != '/') pathWithIndex += "/";
        pathWithIndex += indexFile;

        if (FileUtils::fileExists(pathWithIndex)) resolvedPath = pathWithIndex;
        else {
            bool autoindexActive = location ? location->autoindex : cfg.autoindex;
            if (autoindexActive) {
                std::string listingBody;
                if (FileUtils::generateAutoIndex(resolvedPath, request.getPath(), listingBody)) {
                    client.response.setStatusCode(200); client.response.setContentType("text/html; charset=utf-8");
                    client.response.setBody(listingBody); client.response.prepare();
                    return;
                }
                buildErrorResponse(client, servers, 500, "Directory index construction error"); return;
            } else { buildErrorResponse(client, servers, 403, "Directory structure exploration forbidden"); return; }
        }
    }

    std::string body;
    if (!FileUtils::loadFile(resolvedPath, body)) { buildErrorResponse(client, servers, 404, "Target missing"); return; }

    client.response.setStatusCode(200);
    client.response.setContentType(FileUtils::mimeTypeForPath(resolvedPath));
    client.response.setBody(body);
    if (client.response.prepare() == HttpResponse::ERROR) { buildErrorResponse(client, servers, 500, client.response.getErrorMessage()); client.closeAfterSend = true; }
}

} // namespace HttpHandler