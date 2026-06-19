#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "config/ConfigTypes.hpp"
class ConfigParser {
public:
    ConfigParser();
    ~ConfigParser();

    bool parseFile(const std::string& path,
                   std::vector<ServerConfig>& outServers,
                   std::string& error) const;

private:
    bool tokenize(const std::string& content,
                  std::vector<std::string>& tokens,
                  std::string& error) const;
    bool parseServers(const std::vector<std::string>& tokens,
                      std::vector<ServerConfig>& outServers,
                      std::string& error) const;
    bool parseServerBlock(const std::vector<std::string>& tokens,
                          std::size_t& index,
                          ServerConfig& cfg,
                          std::string& error) const;
    bool parseLocationBlock(const std::vector<std::string>& tokens,
                            std::size_t& index,
                            LocationConfig& cfg,
                            std::string& error) const;

    static bool isValidPort(int port);
    static std::string trim(const std::string& value);
};

#endif