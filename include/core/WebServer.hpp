#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include "config/Config.hpp"
#include "core/ClientConnection.hpp"

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
};

#endif
