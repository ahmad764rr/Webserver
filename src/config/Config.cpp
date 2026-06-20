#include "config/Config.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

ConfigParser::ConfigParser() {
}

ConfigParser::~ConfigParser() {
}

bool ConfigParser::parseFile(const std::string& path,
                             std::vector<ServerConfig>& outServers,
                             std::string& error) const {
    outServers.clear();

    std::ifstream in(path.c_str());
    if (!in.is_open()) {
        error = "cannot open config file: " + path;
        return false;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string content = buffer.str();

    std::vector<std::string> tokens;
    if (!tokenize(content, tokens, error)) {
        return false;
    }

    if (!parseServers(tokens, outServers, error)) {
        return false;
    }

    if (outServers.empty()) {
        error = "config must contain at least one server block";
        return false;
    }

    return true;
}

bool ConfigParser::tokenize(const std::string& content,
                            std::vector<std::string>& tokens,
                            std::string& error) const {
    tokens.clear();
    std::string current;

    for (std::size_t i = 0; i < content.size(); ++i) {
        const char c = content[i];

        if (c == '#') {
            while (i < content.size() && content[i] != '\n') {
                ++i;
            }
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        if (c == '{' || c == '}' || c == ';') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            tokens.push_back(std::string(1, c));
            continue;
        }

        current.push_back(c);
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    if (tokens.empty()) {
        error = "config file is empty";
        return false;
    }

    return true;
}

bool ConfigParser::parseServers(const std::vector<std::string>& tokens,
                                std::vector<ServerConfig>& outServers,
                                std::string& error) const {
    std::size_t index = 0;
    while (index < tokens.size()) {
        if (tokens[index] != "server") {
            error = "expected 'server' at token: " + tokens[index];
            return false;
        }
        ++index;

        if (index >= tokens.size() || tokens[index] != "{") {
            error = "expected '{' after server";
            return false;
        }
        ++index;

        ServerConfig cfg;
        if (!parseServerBlock(tokens, index, cfg, error)) {
            return false;
        }
        outServers.push_back(cfg);
    }

    return true;
}

bool ConfigParser::parseServerBlock(const std::vector<std::string>& tokens,
                                    std::size_t& index,
                                    ServerConfig& cfg,
                                    std::string& error) const {
    while (index < tokens.size()) {
        if (tokens[index] == "}") {
            ++index;
            if (!isValidPort(cfg.listenPort)) {
                error = "invalid listen port";
                return false;
            }
            return true;
        }

        const std::string directive = tokens[index++];

        if (directive == "listen") {
            if (index >= tokens.size()) {
                error = "listen requires a port";
                return false;
            }
            cfg.listenPort = std::atoi(tokens[index++].c_str());
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "listen directive must end with ';'";
                return false;
            }
            ++index;
            continue;
        }

        if (directive == "root") {
            if (index >= tokens.size()) {
                error = "root requires a path";
                return false;
            }
            cfg.root = tokens[index++];
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "root directive must end with ';'";
                return false;
            }
            ++index;
            continue;
        }

        if (directive == "server_name") {
            while (index < tokens.size() && tokens[index] != ";") {
                cfg.serverNames.push_back(tokens[index++]);
            }
            if (index < tokens.size() && tokens[index] == ";") {
                ++index;
            } else {
                error = "server_name directive must end with ';'";
                return false;
            }
            continue;
        }

        if (directive == "client_max_body_size") {
            if (index >= tokens.size()) {
                error = "client_max_body_size requires a value";
                return false;
            }
            cfg.clientMaxBodySize = static_cast<std::size_t>(std::atoi(tokens[index++].c_str()));
            if (cfg.clientMaxBodySize == 0) {
                error = "client_max_body_size must be > 0";
                return false;
            }
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "client_max_body_size directive must end with ';'";
                return false;
            }
            ++index;
            continue;
        }

        if (directive == "error_page") {
            if (index + 2 >= tokens.size()) {
                error = "error_page requires: status path ;";
                return false;
            }

            const int status = std::atoi(tokens[index++].c_str());
            const std::string page = tokens[index++];
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "error_page directive must end with ';'";
                return false;
            }
            ++index;

            if (status < 400 || status > 599) {
                error = "error_page status must be between 400 and 599";
                return false;
            }
            cfg.errorPages[status] = page;
            continue;
        }

        if (directive == "autoindex") {
            if (index >= tokens.size()) {
                error = "autoindex requires a value (on/off)";
                return false;
            }
            std::string val = tokens[index++];
            cfg.autoindex = (val == "on");
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "autoindex directive must end with ';'";
                return false;
            }
            ++index;
            continue;
        }

        if (directive == "location") {
            if (index >= tokens.size()) {
                error = "location requires a path";
                return false;
            }
            std::string path = tokens[index++];
            if (index >= tokens.size() || tokens[index] != "{") {
                error = "expected '{' after location path";
                return false;
            }
            ++index;
            
            LocationConfig locCfg;
            if (!parseLocationBlock(tokens, index, locCfg, error)) {
                return false;
            }
            cfg.locations[path] = locCfg;
            continue;
        }

        if (directive == "cgi_extension" || directive == "cgi") {
            if (index + 2 >= tokens.size()) {
                error = "cgi_extension requires: extension interpreter ;";
                return false;
            }
            std::string ext = tokens[index++];
            std::string interpreter = tokens[index++];

            if (index >= tokens.size() || tokens[index] != ";") {
                error = "cgi_extension directive must end with ';'";
                return false;
            }
            ++index;

            if (ext.empty() || ext[0] != '.') {
                error = "cgi extension must start with '.'";
                return false;
            }
            cfg.cgiInterpreters[ext] = interpreter;
            continue;
        }

        error = "unknown directive: " + directive;
        return false;
    }

    error = "server block not closed with '}'";
    return false;
}

bool ConfigParser::parseLocationBlock(const std::vector<std::string>& tokens,
                                      std::size_t& index,
                                      LocationConfig& cfg,
                                      std::string& error) const {
    while (index < tokens.size()) {
        if (tokens[index] == "}") {
            ++index;
            return true;
        }

        const std::string directive = tokens[index++];

        if (directive == "methods") {
            while (index < tokens.size() && tokens[index] != ";") {
                cfg.allowedMethods.push_back(tokens[index++]);
            }
            if (index < tokens.size() && tokens[index] == ";") {
                ++index;
            } else {
                error = "methods directive must end with ';'";
                return false;
            }
            continue;
        }

        if (directive == "redirect") {
            if (index + 2 >= tokens.size() && tokens[index + 1] != ";") {
                error = "redirect requires: status url ;";
                return false;
            }
            cfg.redirect.first = std::atoi(tokens[index++].c_str());
            cfg.redirect.second = tokens[index++];
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "redirect directive must end with ';'";
                return false;
            }
            ++index;
            continue;
        }

        if (directive == "root") {
            if (index >= tokens.size()) {
                error = "root requires a path";
                return false;
            }
            cfg.root = tokens[index++];
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "root directive must end with ';'";
                return false;
            }
            ++index;
            continue;
        }

        if (directive == "autoindex") {
            if (index >= tokens.size()) {
                error = "autoindex requires on/off";
                return false;
            }
            cfg.autoindex = (tokens[index++] == "on");
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "autoindex directive must end with ';'";
                return false;
            }
            ++index;
            continue;
        }

        if (directive == "index") {
            if (index >= tokens.size()) {
                error = "index requires a file name";
                return false;
            }
            cfg.index = tokens[index++];
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "index directive must end with ';'";
                return false;
            }
            ++index;
            continue;
        }

        if (directive == "upload_enable") {
            if (index >= tokens.size()) {
                error = "upload_enable requires on/off";
                return false;
            }
            cfg.uploadEnable = (tokens[index++] == "on");
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "upload_enable directive must end with ';'";
                return false;
            }
            ++index;
            continue;
        }

        if (directive == "upload_dir") {
            if (index >= tokens.size()) {
                error = "upload_dir requires a path";
                return false;
            }
            cfg.uploadDir = tokens[index++];
            if (index >= tokens.size() || tokens[index] != ";") {
                error = "upload_dir directive must end with ';'";
                return false;
            }
            ++index;
            continue;
        }

        if (directive == "cgi_extension" || directive == "cgi") {
            if (index + 2 >= tokens.size()) {
                error = "cgi_extension requires: extension interpreter ;";
                return false;
            }
            std::string ext = tokens[index++];
            std::string interpreter = tokens[index++];

            if (index >= tokens.size() || tokens[index] != ";") {
                error = "cgi_extension directive must end with ';'";
                return false;
            }
            ++index;

            if (ext.empty() || ext[0] != '.') {
                error = "cgi extension must start with '.'";
                return false;
            }
            cfg.cgiInterpreters[ext] = interpreter;
            continue;
        }

        error = "unknown location directive: " + directive;
        return false;
    }

    error = "location block not closed with '}'";
    return false;
}

bool ConfigParser::isValidPort(int port) {
    return port > 0 && port <= 65535;
}

std::string ConfigParser::trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}
