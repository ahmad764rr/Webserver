#include "config/ConfigTypes.hpp"

LocationConfig::LocationConfig()
    : redirect(0, ""), autoindex(false), uploadEnable(false) {
}

ServerConfig::ServerConfig()
    : listenPort(8080),
      root("."),
      clientMaxBodySize(1024 * 1024),
      autoindex(false) {
}
