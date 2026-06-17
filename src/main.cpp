#include "Config.hpp"
#include "WebServer.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string configPath = "config/webserv.conf";
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [config_file]" << std::endl;
        return 1;
    }
    if (argc == 2) {
        configPath = argv[1];
    }

    ConfigParser parser;
    std::vector<ServerConfig> servers;
    std::string error;

    if (!parser.parseFile(configPath, servers, error)) {
        std::cerr << "Config error: " << error << std::endl;
        return 1;
    }

    WebServer server(servers);
    if (!server.init(error)) {
        std::cerr << "Server init error: " << error << std::endl;
        return 1;
    }

    server.run();
    return 0;
}
