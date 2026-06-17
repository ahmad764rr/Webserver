#include "WebServer.hpp"
#include "CgiHandler.hpp"

#include <cerrno>
#include <csignal>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <set>

namespace {
const int kBacklog = 128;
const int kBufferSize = 8192;

bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

std::string toLowerCopy(const std::string& value) {
    std::string out = value;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] >= 'A' && out[i] <= 'Z') out[i] = static_cast<char>(out[i] - 'A' + 'a');
    }
    return out;
}
}

WebServer::CgiTask::CgiTask() : active(false), pid(-1), pipe_in_fd(-1), pipe_out_fd(-1), body_to_write(""), body_written(0), cgi_output(""), start_time_ms(0), process_exited(false), exit_status(0) {}

void WebServer::CgiTask::cleanup() {
    if (active) {
        if (pipe_in_fd != -1) { close(pipe_in_fd); pipe_in_fd = -1; }
        if (pipe_out_fd != -1) { close(pipe_out_fd); pipe_out_fd = -1; }
        if (pid > 0) {
            if (!process_exited) { kill(pid, SIGKILL); waitpid(pid, NULL, WNOHANG); }
            pid = -1;
        }
        active = false;
    }
}

WebServer::ClientConnection::ClientConnection() : fd(-1), serverIndex(0), request(), response(), hasResponse(false), closeAfterSend(false), carryBuffer(""), cgiTask() {}

WebServer::WebServer(const std::vector<ServerConfig>& servers) : _servers(servers) {}

WebServer::~WebServer() { closeAllSockets(); }

bool WebServer::init(std::string& error) {
    if (_servers.empty()) { error = "no server blocks loaded"; return false; }
    if (!createListenSockets(error)) { closeAllSockets(); return false; }
    std::signal(SIGPIPE, SIG_IGN);
    rebuildPollFds();
    return true;
}

bool WebServer::createListenSockets(std::string& error) {
    std::set<int> boundPorts;
    for (std::size_t i = 0; i < _servers.size(); ++i) {
        int port = _servers[i].listenPort;
        if (boundPorts.count(port)) continue;

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { error = "socket() failed"; return false; }

        int yes = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) { close(fd); error = "setsockopt() failed"; return false; }
        if (!setNonBlocking(fd)) { close(fd); error = "failed to set non-blocking"; return false; }

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<unsigned short>(port));

        if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd); error = "bind() failed"; return false;
        }
        if (listen(fd, kBacklog) < 0) { close(fd); error = "listen() failed"; return false; }

        _listenSockets[fd] = port;
        boundPorts.insert(port);
        std::cout << "Listening interface initialized on port: " << port << std::endl;
    }
    return true;
}

void WebServer::closeAllSockets() {
    for (std::map<int, ClientConnection>::iterator it = _clients.begin(); it != _clients.end(); ++it) close(it->first);
    _clients.clear();
    for (std::map<int, int>::iterator it = _listenSockets.begin(); it != _listenSockets.end(); ++it) close(it->first);
    _listenSockets.clear();
    _pollFds.clear();
}

void WebServer::rebuildPollFds() {
    _pollFds.clear();
    for (std::map<int, int>::const_iterator it = _listenSockets.begin(); it != _listenSockets.end(); ++it) {
        struct pollfd p; p.fd = it->first; p.events = POLLIN; p.revents = 0; _pollFds.push_back(p);
    }
    for (std::map<int, ClientConnection>::const_iterator it = _clients.begin(); it != _clients.end(); ++it) {
        struct pollfd p; p.fd = it->first; p.events = POLLIN;
        if (it->second.hasResponse) p.events |= POLLOUT;
        p.revents = 0; _pollFds.push_back(p);

        if (it->second.cgiTask.active) {
            if (it->second.cgiTask.pipe_in_fd != -1) { struct pollfd pi; pi.fd = it->second.cgiTask.pipe_in_fd; pi.events = POLLOUT; pi.revents = 0; _pollFds.push_back(pi); }
            if (it->second.cgiTask.pipe_out_fd != -1) { struct pollfd po; po.fd = it->second.cgiTask.pipe_out_fd; po.events = POLLIN; po.revents = 0; _pollFds.push_back(po); }
        }
    }
}

bool WebServer::isListenFd(int fd) const { return _listenSockets.find(fd) != _listenSockets.end(); }

void WebServer::acceptClient(int listenFd) {
    while (true) {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        const int clientFd = accept(listenFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen);
        if (clientFd < 0) return;
        if (!setNonBlocking(clientFd)) { close(clientFd); continue; }

        int port = _listenSockets[listenFd];
        std::size_t defaultIndex = 0;
        for (std::size_t i = 0; i < _servers.size(); ++i) {
            if (_servers[i].listenPort == port) { defaultIndex = i; break; }
        }

        ClientConnection conn;
        conn.fd = clientFd;
        conn.serverIndex = defaultIndex;
        conn.request.setClientMaxBodySize(_servers[defaultIndex].clientMaxBodySize);
        conn.response.setServerName(_servers[defaultIndex].serverName);
        _clients[clientFd] = conn;
    }
}

void WebServer::run() {
    std::cout << "Webserver engine active. Multiplexing operations via poll()." << std::endl;
    while (true) {
        rebuildPollFds();
        if (_pollFds.empty()) break;

        const int ready = poll(&_pollFds[0], _pollFds.size(), -1);
        if (ready < 0) { if (errno == EINTR) continue; break; }

        for (std::size_t i = 0; i < _pollFds.size(); ++i) {
            const struct pollfd p = _pollFds[i];
            if (p.revents == 0) continue;

            if (isListenFd(p.fd)) {
                if (p.revents & POLLIN) acceptClient(p.fd);
                continue;
            }

            int cgiClientFd = findClientForCgiInFd(p.fd);
            if (cgiClientFd != -1) {
                if (p.revents & POLLOUT) handleCgiWrite(cgiClientFd);
                if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    std::map<int, ClientConnection>::iterator it = _clients.find(cgiClientFd);
                    if (it != _clients.end() && it->second.cgiTask.pipe_in_fd != -1) { close(it->second.cgiTask.pipe_in_fd); it->second.cgiTask.pipe_in_fd = -1; }
                }
                continue;
            }

            cgiClientFd = findClientForCgiOutFd(p.fd);
            if (cgiClientFd != -1) {
                if (p.revents & POLLIN) handleCgiRead(cgiClientFd);
                if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) handleCgiRead(cgiClientFd);
                continue;
            }

            if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { closeClient(p.fd); continue; }
            if (p.revents & POLLIN) handleClientRead(p.fd);
            if (_clients.find(p.fd) != _clients.end() && (p.revents & POLLOUT)) handleClientWrite(p.fd);
        }
        checkCgiTimeouts();
    }
}

int WebServer::findClientForCgiInFd(int fd) const {
    for (std::map<int, ClientConnection>::const_iterator it = _clients.begin(); it != _clients.end(); ++it)
        if (it->second.cgiTask.active && it->second.cgiTask.pipe_in_fd == fd) return it->first;
    return -1;
}

int WebServer::findClientForCgiOutFd(int fd) const {
    for (std::map<int, ClientConnection>::const_iterator it = _clients.begin(); it != _clients.end(); ++it)
        if (it->second.cgiTask.active && it->second.cgiTask.pipe_out_fd == fd) return it->first;
    return -1;
}

void WebServer::handleCgiWrite(int clientFd) {
    std::map<int, ClientConnection>::iterator it = _clients.find(clientFd);
    if (it == _clients.end()) return;
    ClientConnection& client = it->second;

    if (client.cgiTask.body_written < client.cgiTask.body_to_write.size()) {
        const std::string& body = client.cgiTask.body_to_write;
        std::size_t remaining = body.size() - client.cgiTask.body_written;
        const ssize_t wrote = write(client.cgiTask.pipe_in_fd, body.data() + client.cgiTask.body_written, remaining);
        if (wrote > 0) client.cgiTask.body_written += static_cast<std::size_t>(wrote);
        else if (wrote < 0) { close(client.cgiTask.pipe_in_fd); client.cgiTask.pipe_in_fd = -1; }
    }
    if (client.cgiTask.body_written >= client.cgiTask.body_to_write.size()) {
        if (client.cgiTask.pipe_in_fd != -1) { close(client.cgiTask.pipe_in_fd); client.cgiTask.pipe_in_fd = -1; }
    }
}

void WebServer::handleCgiRead(int clientFd) {
    std::map<int, ClientConnection>::iterator it = _clients.find(clientFd);
    if (it == _clients.end()) return;
    ClientConnection& client = it->second;

    char buffer[4096];
    const ssize_t n = read(client.cgiTask.pipe_out_fd, buffer, sizeof(buffer));
    if (n > 0) client.cgiTask.cgi_output.append(buffer, static_cast<std::size_t>(n));
    else if (n <= 0) {
        if (client.cgiTask.pipe_out_fd != -1) { close(client.cgiTask.pipe_out_fd); client.cgiTask.pipe_out_fd = -1; }
    }
}

void WebServer::checkCgiTimeouts() {
    const long nowMs = static_cast<long>(time(NULL)) * 1000;
    for (std::map<int, ClientConnection>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (it->second.cgiTask.active) {
            ClientConnection& client = it->second;
            int wstatus = 0;
            if (!client.cgiTask.process_exited) {
                const pid_t w = waitpid(client.cgiTask.pid, &wstatus, WNOHANG);
                if (w == client.cgiTask.pid) {
                    client.cgiTask.process_exited = true;
                    client.cgiTask.exit_status = WEXITSTATUS(wstatus);
                } else if (w == 0 && (nowMs - client.cgiTask.start_time_ms >= 10000)) {
                    kill(client.cgiTask.pid, SIGKILL);
                    waitpid(client.cgiTask.pid, NULL, 0);
                    client.cgiTask.process_exited = true;
                    client.cgiTask.exit_status = 1;
                }
            }
            if (client.cgiTask.process_exited && client.cgiTask.pipe_out_fd == -1) {
                int statusCode = 200;
                if (client.cgiTask.exit_status != 0) statusCode = 502;
                client.cgiTask.cleanup();
                if (statusCode != 200) buildErrorResponse(client, statusCode, "CGI script error");
                else {
                    applyCgiOutputToResponse(client.cgiTask.cgi_output, client.response);
                    if (client.response.prepare() == HttpResponse::ERROR) buildErrorResponse(client, 500, client.response.getErrorMessage());
                }
                client.hasResponse = true;
            }
        }
    }
}

void WebServer::handleClientRead(int clientFd) {
    std::map<int, ClientConnection>::iterator it = _clients.find(clientFd);
    if (it == _clients.end()) return;
    ClientConnection& client = it->second;

    char buffer[kBufferSize];
    const ssize_t n = recv(clientFd, buffer, sizeof(buffer), 0);
    if (n <= 0) { closeClient(clientFd); return; }

    client.request.feed(std::string(buffer, static_cast<std::size_t>(n)));

    if (client.request.hasError()) {
        int errCode = client.request.getErrorStatus() ? client.request.getErrorStatus() : 400;
        if (errCode == 501) errCode = 405; 
        buildErrorResponse(client, errCode, client.request.getErrorMessage());
        client.closeAfterSend = true;
        client.hasResponse = true;
        return;
    }

    if (client.request.isComplete()) {
        client.carryBuffer = client.request.releaseUnparsedBuffer();
        refineServerSelection(client);
        buildResponseForRequest(client);
        client.hasResponse = true;
    }
}

void WebServer::refineServerSelection(ClientConnection& client) {
    int activePort = _servers[client.serverIndex].listenPort;
    
    // Hardened Host Extraction: Checks multiple cases to bypass parser flaws
    std::string hostHeader = client.request.getHeader("host");
    if (hostHeader.empty()) hostHeader = client.request.getHeader("Host");
    if (hostHeader.empty()) hostHeader = client.request.getHeader("HOST");
    
    // Aggressive whitespace & trailing carriage return trimming
    if (!hostHeader.empty()) {
        std::size_t start = hostHeader.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) hostHeader = hostHeader.substr(start);
        else hostHeader.clear();
    }
    if (!hostHeader.empty()) {
        std::size_t end = hostHeader.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) hostHeader = hostHeader.substr(0, end + 1);
    }
    
    hostHeader = toLowerCopy(hostHeader);
    
    // Strip trailing port attachments (e.g. localhost:8080 -> localhost)
    std::size_t colon = hostHeader.find(':');
    if (colon != std::string::npos) hostHeader = hostHeader.substr(0, colon);

    for (std::size_t i = 0; i < _servers.size(); ++i) {
        if (_servers[i].listenPort == activePort && _servers[i].serverName == hostHeader) {
            client.serverIndex = i;
            client.request.setClientMaxBodySize(_servers[i].clientMaxBodySize);
            break;
        }
    }
}

void WebServer::handleClientWrite(int clientFd) {
    std::map<int, ClientConnection>::iterator it = _clients.find(clientFd);
    if (it == _clients.end()) return;
    ClientConnection& client = it->second;

    if (client.hasResponse && !client.response.isComplete()) {
        const std::string chunk = client.response.getNextBytes(8192);
        if (!chunk.empty()) {
            const ssize_t n = send(clientFd, chunk.data(), chunk.size(), 0);
            if (n <= 0) { closeClient(clientFd); return; }
            client.response.consumeBytes(static_cast<std::size_t>(n));
        }
    }

    if (!client.response.isComplete()) return;

    client.hasResponse = false;
    if (client.closeAfterSend) { closeClient(clientFd); return; }

    client.request.reset();
    client.request.setClientMaxBodySize(_servers[client.serverIndex].clientMaxBodySize);
    client.response.reset();
    client.response.setServerName(_servers[client.serverIndex].serverName);

    if (!client.carryBuffer.empty()) {
        const std::string carry = client.carryBuffer;
        client.carryBuffer.clear();
        client.request.feed(carry);

        if (client.request.hasError()) {
            int errCode = client.request.getErrorStatus() ? client.request.getErrorStatus() : 400;
            if (errCode == 501) errCode = 405;
            buildErrorResponse(client, errCode, client.request.getErrorMessage());
            client.closeAfterSend = true;
            client.hasResponse = true;
            return;
        }
        if (client.request.isComplete()) {
            client.carryBuffer = client.request.releaseUnparsedBuffer();
            refineServerSelection(client);
            buildResponseForRequest(client);
            client.hasResponse = true;
        }
    }
}

void WebServer::closeClient(int clientFd) {
    std::map<int, ClientConnection>::iterator it = _clients.find(clientFd);
    if (it != _clients.end()) {
        it->second.cgiTask.cleanup();
        close(it->first);
        _clients.erase(it);
    }
}

void WebServer::buildResponseForRequest(ClientConnection& client) {
    const ServerConfig& cfg = _servers[client.serverIndex];
    const HttpRequest& request = client.request;

    client.response.reset();
    client.response.setServerName(cfg.serverName);
    client.response.setKeepAlive(!connectionShouldClose(request));
    client.closeAfterSend = connectionShouldClose(request);

    // Guaranteed Body Limit Verification mapping accurately to active Virtual Host
    if (request.getBody().size() > cfg.clientMaxBodySize) {
        buildErrorResponse(client, 413, "Payload exceeds body storage restrictions");
        return;
    }

    std::string method = request.getMethod();
    if (method != "GET" && method != "POST" && method != "DELETE") {
        buildErrorResponse(client, 405, "Method Not Allowed");
        return;
    }

    std::string routeMatchedPath;
    const RouteConfig* route = findRoute(cfg, request.getPath(), routeMatchedPath);

    if (route && !route->allowedMethods.empty()) {
        bool authorized = false;
        for (std::size_t i = 0; i < route->allowedMethods.size(); ++i) {
            if (route->allowedMethods[i] == method) { authorized = true; break; }
        }
        if (!authorized) { buildErrorResponse(client, 405, "Method not allowed on explicit route mapping"); return; }
    }

    if (route && route->redirect.first >= 300 && route->redirect.first <= 399) {
        client.response.setStatusCode(route->redirect.first);
        client.response.setHeader("location", route->redirect.second);
        client.response.prepare();
        return;
    }

    std::string resolvedPath = resolvePath(cfg, route, routeMatchedPath, request.getPath());
    if (resolvedPath.empty()) { buildErrorResponse(client, 403, "Forbidden path access detected"); return; }

    std::string ext;
    if (shouldUseCgi(cfg, route, request, ext)) {
        if (!setupCgiTask(cfg, route, routeMatchedPath, client)) {
            buildErrorResponse(client, 502, "Failed to start CGI engine");
            client.closeAfterSend = true; client.hasResponse = true;
        } else { client.hasResponse = false; }
        return;
    }

    if (method == "POST" && route && route->uploadEnable) {
        handleUpload(client, route, resolvedPath);
        return;
    }

    if (method == "DELETE") {
        if (!fileExists(resolvedPath)) buildErrorResponse(client, 404, "Target resource not found");
        else if (isDirectory(resolvedPath)) buildErrorResponse(client, 403, "Cannot delete directories");
        else if (std::remove(resolvedPath.c_str()) == 0) { client.response.setStatusCode(204); client.response.prepare(); }
        else buildErrorResponse(client, 403, "Delete operations blocked");
        return;
    }

    if (method != "GET") { buildErrorResponse(client, 405, "Method unauthorized"); return; }

    if (isDirectory(resolvedPath)) {
        std::string indexFile = (route && !route->index.empty()) ? route->index : "index.html";
        std::string pathWithIndex = resolvedPath;
        if (pathWithIndex[pathWithIndex.size() - 1] != '/') pathWithIndex += "/";
        pathWithIndex += indexFile;

        if (fileExists(pathWithIndex)) resolvedPath = pathWithIndex;
        else {
            bool autoindexActive = route ? route->autoindex : cfg.autoindex;
            if (autoindexActive) {
                std::string listingBody;
                if (generateAutoIndex(resolvedPath, request.getPath(), listingBody)) {
                    client.response.setStatusCode(200); client.response.setContentType("text/html; charset=utf-8");
                    client.response.setBody(listingBody); client.response.prepare();
                    return;
                }
                buildErrorResponse(client, 500, "Directory index construction error"); return;
            } else { buildErrorResponse(client, 403, "Directory structure exploration forbidden"); return; }
        }
    }

    std::string body;
    if (!loadFile(resolvedPath, body)) { buildErrorResponse(client, 404, "Target missing"); return; }

    client.response.setStatusCode(200);
    client.response.setContentType(mimeTypeForPath(resolvedPath));
    client.response.setBody(body);
    if (client.response.prepare() == HttpResponse::ERROR) { buildErrorResponse(client, 500, client.response.getErrorMessage()); client.closeAfterSend = true; }
}

void WebServer::handleUpload(ClientConnection& client, const RouteConfig* route, const std::string& resolvedPath) {
    std::string uploadTargetDir = route->uploadDir;
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
    if (!out.is_open()) { buildErrorResponse(client, 500, "Failed to instantiate file handle"); return; }
    out.write(client.request.getBody().data(), client.request.getBody().size());
    out.close();

    client.response.setStatusCode(201);
    client.response.setContentType("text/html; charset=utf-8");
    client.response.setBody("<html><body><h1>File uploaded successfully</h1></body></html>");
    client.response.prepare();
}

bool WebServer::generateAutoIndex(const std::string& path, const std::string& requestPath, std::string& out) const {
    DIR* dir = opendir(path.c_str());
    if (!dir) return false;

    std::ostringstream html;
    html << "<!DOCTYPE html>\n<html>\n<head>\n<title>Index of " << requestPath << "</title>\n"
         << "<style>body{font-family:monospace; background:#0f172a; color:#f8fafc; padding:20px;} a{color:#38bdf8; text-decoration:none;}</style>\n"
         << "</head>\n<body>\n<h1>Index of " << requestPath << "</h1>\n<hr><pre>\n";

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        if (name == ".") continue;
        std::string relLink = requestPath;
        if (relLink.empty() || relLink[relLink.size() - 1] != '/') relLink += "/";
        relLink += name;
        html << "<a href=\"" << relLink << "\">" << name << "</a>\n";
    }
    closedir(dir);
    html << "</pre><hr>\n</body>\n</html>\n";
    out = html.str();
    return true;
}

bool WebServer::isDirectory(const std::string& path) const {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool WebServer::fileExists(const std::string& path) const {
    struct stat st; return (stat(path.c_str(), &st) == 0);
}

const RouteConfig* WebServer::findRoute(const ServerConfig& cfg, const std::string& requestPath, std::string& matchedKey) const {
    const RouteConfig* bestMatch = NULL;
    std::size_t longestMatch = 0;
    matchedKey.clear();
    for (std::map<std::string, RouteConfig>::const_iterator it = cfg.routes.begin(); it != cfg.routes.end(); ++it) {
        const std::string& routePath = it->first;
        if (requestPath.find(routePath) == 0 && routePath.length() > longestMatch) {
            longestMatch = routePath.length(); bestMatch = &it->second; matchedKey = routePath;
        }
    }
    return bestMatch;
}

std::string WebServer::resolvePath(const ServerConfig& cfg, const RouteConfig* route, const std::string& matchedKey, const std::string& requestPath) const {
    if (requestPath.find("..") != std::string::npos) return "";
    std::string full = (route && !route->root.empty()) ? route->root : cfg.root;
    if (full.empty()) full = ".";

    if (route && !route->root.empty() && !matchedKey.empty()) {
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

bool WebServer::shouldUseCgi(const ServerConfig& cfg, const RouteConfig* route, const HttpRequest& request, std::string& ext) const {
    ext.clear();
    const std::string& path = request.getPath();
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    ext = path.substr(dot);
    if (route && route->cgiInterpreters.find(ext) != route->cgiInterpreters.end()) return true;
    return cfg.cgiInterpreters.find(ext) != cfg.cgiInterpreters.end();
}

bool WebServer::setupCgiTask(const ServerConfig& cfg, const RouteConfig* route, const std::string& matchedKey, ClientConnection& client) const {
    const HttpRequest& request = client.request;
    std::string ext;
    if (!shouldUseCgi(cfg, route, request, ext)) return false;

    std::string scriptPath = resolvePath(cfg, route, matchedKey, request.getPath());
    if (scriptPath.empty()) return false;

    std::string interpreterPath = (route && route->cgiInterpreters.count(ext)) ? route->cgiInterpreters.find(ext)->second : cfg.cgiInterpreters.find(ext)->second;

    HttpRequest requestCopy = request; short cgiError = 0;
    CgiHandler cgi(scriptPath, interpreterPath);
    if (!cgi.execute(requestCopy, cgiError)) return false;

    client.cgiTask.active = true; client.cgiTask.pid = cgi.getPid();
    client.cgiTask.pipe_in_fd = cgi.pipe_in[1]; client.cgiTask.pipe_out_fd = cgi.pipe_out[0];
    client.cgiTask.body_to_write = request.getBody(); client.cgiTask.body_written = 0;
    client.cgiTask.cgi_output = ""; client.cgiTask.start_time_ms = static_cast<long>(time(NULL)) * 1000;
    client.cgiTask.process_exited = false; client.cgiTask.exit_status = 0;
    
    cgi.pipe_in[1] = -1; cgi.pipe_out[0] = -1;
    return true;
}

void WebServer::buildErrorResponse(ClientConnection& client, int statusCode, const std::string& message) {
    const ServerConfig& cfg = _servers[client.serverIndex];
    client.response = HttpResponse::stockResponse(statusCode, !client.closeAfterSend, cfg.serverName);
    applyConfiguredErrorPage(cfg, statusCode, client.response);
    if (!message.empty()) client.response.setHeader("x-webserv-error", message);
    if (client.response.prepare() == HttpResponse::ERROR) {
        HttpResponse fallback = HttpResponse::stockResponse(500, false, cfg.serverName); fallback.prepare();
        client.response = fallback; client.closeAfterSend = true;
    }
}

void WebServer::applyCgiOutputToResponse(const std::string& cgiOutput, HttpResponse& response) const {
    response.setStatusCode(200);
    std::size_t split = cgiOutput.find("\r\n\r\n");
    std::size_t sepLen = 4;
    if (split == std::string::npos) { split = cgiOutput.find("\n\n"); sepLen = 2; }
    if (split == std::string::npos) { response.setContentType("text/plain; charset=utf-8"); response.setBody(cgiOutput); return; }

    const std::string headerBlock = cgiOutput.substr(0, split);
    const std::string body = cgiOutput.substr(split + sepLen);

    std::istringstream lines(headerBlock); std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr(0, colon);
        for (std::size_t i = 0; i < name.size(); ++i) if (name[i] >= 'A' && name[i] <= 'Z') name[i] = static_cast<char>(name[i] - 'A' + 'a');
        
        const std::string value = line.substr(colon + 1);
        std::size_t first = 0;
        while (first < value.size() && (value[first] == ' ' || value[first] == '\t')) ++first;
        const std::string cleanValue = value.substr(first);

        if (name == "status") {
            std::istringstream iss(cleanValue); int code = 200; iss >> code;
            if (code >= 100 && code <= 599) response.setStatusCode(code);
            continue;
        }
        response.setHeader(name, cleanValue);
    }
    if (response.hasHeader("location") && response.getStatusCode() == 200) {
        response.setStatusCode(302);
    }
    if (!response.hasHeader("content-type")) response.setContentType("text/html; charset=utf-8");
    response.setBody(body);
}

bool WebServer::loadFile(const std::string& path, std::string& out) const {
    out.clear(); std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream content; content << in.rdbuf(); out = content.str(); return true;
}

std::string WebServer::mimeTypeForPath(const std::string& path) const {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot);
    for (std::size_t i = 0; i < ext.size(); ++i) if (ext[i] >= 'A' && ext[i] <= 'Z') ext[i] = static_cast<char>(ext[i] - 'A' + 'a');
    
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    return "application/octet-stream";
}

bool WebServer::connectionShouldClose(const HttpRequest& request) const {
    std::string conn = request.getHeader("connection");
    for (std::size_t i = 0; i < conn.size(); ++i) if (conn[i] >= 'A' && conn[i] <= 'Z') conn[i] = static_cast<char>(conn[i] - 'A' + 'a');
    return conn == "close";
}

void WebServer::applyConfiguredErrorPage(const ServerConfig& cfg, int statusCode, HttpResponse& response) const {
    std::map<int, std::string>::const_iterator it = cfg.errorPages.find(statusCode);
    if (it == cfg.errorPages.end()) return;
    std::string pagePath = it->second;
    if (!pagePath.empty() && pagePath[0] == '/') pagePath = resolvePath(cfg, NULL, "", pagePath);
    std::string body;
    if (!pagePath.empty() && loadFile(pagePath, body)) { response.setContentType("text/html; charset=utf-8"); response.setBody(body); }
}