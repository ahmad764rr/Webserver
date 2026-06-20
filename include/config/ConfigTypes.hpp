#ifndef CONFIGTYPES_HPP
#define CONFIGTYPES_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

struct LocationConfig {
    std::vector<std::string> allowedMethods;
    std::pair<int, std::string> redirect;
    std::string root;
    int autoindex;
    std::string index;
    bool uploadEnable;
    std::string uploadDir;
    std::map<std::string, std::string> cgiInterpreters;

    LocationConfig();
};

struct ServerConfig {
    int listenPort;
    std::string root;
    std::size_t clientMaxBodySize;
    bool autoindex;
    std::vector<std::string> serverNames;
    std::map<int, std::string> errorPages;
    std::map<std::string, std::string> cgiInterpreters;
    std::map<std::string, LocationConfig> locations;

    ServerConfig();
};

#endif
