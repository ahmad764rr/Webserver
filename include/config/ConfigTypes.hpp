#ifndef CONFIGTYPES_HPP
#define CONFIGTYPES_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

struct RouteConfig {
    std::vector<std::string> allowedMethods;
    std::pair<int, std::string> redirect;
    std::string root;
    bool autoindex;
    std::string index;
    bool uploadEnable;
    std::string uploadDir;
    std::map<std::string, std::string> cgiInterpreters;

    RouteConfig();
};

struct ServerConfig {
    int listenPort;
    std::string root;
    std::string serverName;
    std::size_t clientMaxBodySize;
    bool autoindex;
    std::map<int, std::string> errorPages;
    std::map<std::string, std::string> cgiInterpreters;
    std::map<std::string, RouteConfig> routes;

    ServerConfig();
};

#endif
