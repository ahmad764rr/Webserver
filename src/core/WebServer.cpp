#include "core/WebServer.hpp"
#include "http/HttpHandler.hpp"
#include "cgi/CgiManager.hpp"

#include <cerrno>
#include <csignal>
#include <ctime>
#include <iostream>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <set>

namespace {
const int kBacklog = 128;
const int kBufferSize = 8192;

bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
}

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
        if (it->second.hasResponse || it->second.sendFileFd != -1 || !it->second.sendFileBuf.empty()) p.events |= POLLOUT;
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
        _clients[clientFd] = conn;
    }
}

void WebServer::run() {
    std::cout << "Webserver engine active. Multiplexing operations via poll()." << std::endl;
    while (true) {
        rebuildPollFds();
        if (_pollFds.empty()) break;

        const int ready = poll(&_pollFds[0], _pollFds.size(), 100);
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

    if (client.cgiTask.body_written >= client.cgiTask.body_to_write.size()) {
        if (client.cgiTask.pipe_in_fd != -1) { close(client.cgiTask.pipe_in_fd); client.cgiTask.pipe_in_fd = -1; }
        return;
    }

    const std::string& body = client.cgiTask.body_to_write;
    std::size_t remaining = body.size() - client.cgiTask.body_written;
    if (remaining > 8192) remaining = 8192;
    const ssize_t wrote = write(client.cgiTask.pipe_in_fd, body.data() + client.cgiTask.body_written, remaining);
    if (wrote > 0) {
        client.cgiTask.body_written += static_cast<std::size_t>(wrote);
    } else if (wrote < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        close(client.cgiTask.pipe_in_fd);
        client.cgiTask.pipe_in_fd = -1;
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
    if (n > 0) {
        client.cgiTask.cgi_output.append(buffer, static_cast<std::size_t>(n));
    } else if (n == 0) {
        if (client.cgiTask.pipe_out_fd != -1) { close(client.cgiTask.pipe_out_fd); client.cgiTask.pipe_out_fd = -1; }
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
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
                    waitpid(client.cgiTask.pid, NULL, WNOHANG);
                    client.cgiTask.process_exited = true;
                    client.cgiTask.exit_status = 1;
                }
            }
            if (client.cgiTask.process_exited && client.cgiTask.pipe_out_fd == -1) {
                int statusCode = 200;
                if (client.cgiTask.exit_status != 0) statusCode = 502;
                client.cgiTask.cleanup();
                if (statusCode != 200) HttpHandler::buildErrorResponse(client, _servers, statusCode, "CGI script error");
                else {
                    CgiManager::applyCgiOutputToResponse(client.cgiTask.cgi_output, client.response);
                    if (client.response.prepare() == HttpResponse::ERROR) HttpHandler::buildErrorResponse(client, _servers, 500, client.response.getErrorMessage());
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
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        closeClient(clientFd);
        return;
    }
    if (n == 0) {
        closeClient(clientFd);
        return;
    }

    client.request.feed(std::string(buffer, static_cast<std::size_t>(n)));

    if (client.request.hasError()) {
        int errCode = client.request.getErrorStatus() ? client.request.getErrorStatus() : 400;
        if (errCode == 501) errCode = 405; 
        HttpHandler::buildErrorResponse(client, _servers, errCode, client.request.getErrorMessage());
        client.closeAfterSend = true;
        client.hasResponse = true;
        return;
    }

    if (client.request.isComplete()) {
        client.carryBuffer = client.request.releaseUnparsedBuffer();
        HttpHandler::buildResponseForRequest(client, _servers);
        client.hasResponse = true;
    }
}

void WebServer::handleClientWrite(int clientFd) {
    std::map<int, ClientConnection>::iterator it = _clients.find(clientFd);
    if (it == _clients.end()) return;
    ClientConnection& client = it->second;

    // Phase 1: Send response headers (and any in-memory body)
    if (client.hasResponse && !client.response.isComplete()) {
        const std::string chunk = client.response.getNextBytes(8192);
        if (!chunk.empty()) {
            const ssize_t n = send(clientFd, chunk.data(), chunk.size(), 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                closeClient(clientFd);
                return;
            }
            if (n == 0) {
                closeClient(clientFd);
                return;
            }
            client.response.consumeBytes(static_cast<std::size_t>(n));
        }
        return;
    }

    // Phase 2: Stream file body in small chunks (non-blocking)
    if (client.sendFileFd != -1 || !client.sendFileBuf.empty()) {
        // First drain any buffered data from a previous partial send
        if (!client.sendFileBuf.empty()) {
            const ssize_t n = send(clientFd, client.sendFileBuf.data(), client.sendFileBuf.size(), 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                closeClient(clientFd); return;
            }
            if (n == 0) { closeClient(clientFd); return; }
            if (static_cast<std::size_t>(n) < client.sendFileBuf.size())
                client.sendFileBuf.erase(0, static_cast<std::size_t>(n));
            else
                client.sendFileBuf.clear();
            return;
        }
        // Read next chunk from file and send
        char buf[8192];
        const ssize_t r = read(client.sendFileFd, buf, sizeof(buf));
        if (r > 0) {
            const ssize_t n = send(clientFd, buf, static_cast<std::size_t>(r), 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    client.sendFileBuf.assign(buf, static_cast<std::size_t>(r));
                    return;
                }
                closeClient(clientFd); return;
            }
            if (n == 0) { closeClient(clientFd); return; }
            if (n < r)
                client.sendFileBuf.assign(buf + n, static_cast<std::size_t>(r - n));
            return;
        }
        // EOF or read error — done streaming
        close(client.sendFileFd);
        client.sendFileFd = -1;
        // Fall through to connection cleanup
    }

    if (!client.response.isComplete()) return;

    // Phase 3: Response fully sent — handle keep-alive or close
    client.hasResponse = false;
    if (client.closeAfterSend) { closeClient(clientFd); return; }

    client.request.reset();
    client.request.setClientMaxBodySize(_servers[client.serverIndex].clientMaxBodySize);
    client.response.reset();

    if (!client.carryBuffer.empty()) {
        const std::string carry = client.carryBuffer;
        client.carryBuffer.clear();
        client.request.feed(carry);

        if (client.request.hasError()) {
            int errCode = client.request.getErrorStatus() ? client.request.getErrorStatus() : 400;
            if (errCode == 501) errCode = 405;
            HttpHandler::buildErrorResponse(client, _servers, errCode, client.request.getErrorMessage());
            client.closeAfterSend = true;
            client.hasResponse = true;
            return;
        }
        if (client.request.isComplete()) {
            client.carryBuffer = client.request.releaseUnparsedBuffer();
            HttpHandler::buildResponseForRequest(client, _servers);
            client.hasResponse = true;
        }
    }
}

void WebServer::closeClient(int clientFd) {
    std::map<int, ClientConnection>::iterator it = _clients.find(clientFd);
    if (it != _clients.end()) {
        if (it->second.sendFileFd != -1) {
            close(it->second.sendFileFd);
            it->second.sendFileFd = -1;
        }
        it->second.cgiTask.cleanup();
        close(it->first);
        _clients.erase(it);
    }
}