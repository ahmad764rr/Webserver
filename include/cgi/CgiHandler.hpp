#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include <string>
#include <map>
#include <vector>
#include <unistd.h>

class HttpRequest; // Assuming these classes exist in your project

class CgiHandler {
private:
    std::string         _script_path;
    std::string         _interpreter_path;
    std::map<std::string, std::string> _env_map;
    char** _env_array;
    char** _argv;
    pid_t               _pid;

    // Internal helpers for memory and parsing
    void    _clearArrays();
    void    _setupEnvMap(HttpRequest& request);
    void    _convertEnvToCStyle();
    char* _strdup_safe(const std::string& str);

public:
    int     pipe_in[2];  // Parent writes to CGI stdin
    int     pipe_out[2]; // Parent reads from CGI stdout

    CgiHandler();
    CgiHandler(const std::string& script_path, const std::string& interpreter_path);
    CgiHandler(const CgiHandler& other);
    CgiHandler& operator=(const CgiHandler& other);
    ~CgiHandler();

    // The heart of the CGI execution
    bool    execute(HttpRequest& request, short& error_code);
    
    // Getters for the main loop to monitor FDs
    pid_t   getPid() const { return _pid; }
};

#endif