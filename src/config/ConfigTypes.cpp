#include "config/ConfigTypes.hpp"

RouteConfig::RouteConfig()
    : redirect(0, ""), autoindex(false), uploadEnable(false) {
}

ServerConfig::ServerConfig()
    : listenPort(8080),
      root("."),
      serverName("webserv"),
      clientMaxBodySize(1024 * 1024),
      autoindex(false) {
}
