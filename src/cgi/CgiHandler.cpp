#include "cgi/CgiHandler.hpp"
#include "http/HttpRequest.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>

/* ========================================================================
 * CANONICAL FORM (Orthodox)
 * ======================================================================== */

CgiHandler::CgiHandler() : _env_array(NULL), _argv(NULL), _pid(-1) {
    pipe_in[0] = pipe_in[1] = pipe_out[0] = pipe_out[1] = -1;
}

CgiHandler::CgiHandler(const std::string& script, const std::string& bin) 
    : _script_path(script), _interpreter_path(bin), _env_array(NULL), _argv(NULL), _pid(-1) {
    pipe_in[0] = pipe_in[1] = pipe_out[0] = pipe_out[1] = -1;
}

CgiHandler::~CgiHandler() {
    _clearArrays();
    // Close any open pipes
    if (pipe_in[0] != -1) close(pipe_in[0]);
    if (pipe_in[1] != -1) close(pipe_in[1]);
    if (pipe_out[0] != -1) close(pipe_out[0]);
    if (pipe_out[1] != -1) close(pipe_out[1]);
}

CgiHandler::CgiHandler(const CgiHandler& other) 
    : _env_array(NULL), _argv(NULL), _pid(-1) {
    pipe_in[0] = pipe_in[1] = pipe_out[0] = pipe_out[1] = -1;
    *this = other;
}

CgiHandler& CgiHandler::operator=(const CgiHandler& other) {
    if (this != &other) {
        _clearArrays();
        _script_path = other._script_path;
        _interpreter_path = other._interpreter_path;
        _pid = other._pid;
        _env_map = other._env_map;
        // Note: Don't copy pipes - they're process-specific
    }
    return *this;
}

/* ========================================================================
 * PRIVATE HELPERS
 * ======================================================================== */

void CgiHandler::_clearArrays() {
    if (_env_array) {
        for (int i = 0; _env_array[i]; i++) free(_env_array[i]);
        free(_env_array);
        _env_array = NULL;
    }
    if (_argv) {
        for (int i = 0; _argv[i]; i++) free(_argv[i]);
        free(_argv);
        _argv = NULL;
    }
}

char* CgiHandler::_strdup_safe(const std::string& str) {
    char* res = (char*)malloc(str.length() + 1);
    if (!res) return NULL;
    std::strcpy(res, str.c_str());
    return res;
}

/**
 * Sets up mandatory RFC 3875 variables 
 */
void CgiHandler::_setupEnvMap(HttpRequest& request) {
    _env_map["GATEWAY_INTERFACE"] = "CGI/1.1";
    _env_map["SERVER_PROTOCOL"] = "HTTP/1.1";
    _env_map["SERVER_SOFTWARE"] = "Webserv/1.0";
    _env_map["REQUEST_METHOD"] = request.getMethod();
    _env_map["QUERY_STRING"] = request.getQueryString();
    _env_map["SCRIPT_NAME"] = _script_path;
    _env_map["PATH_INFO"] = _script_path;
    
    // For POST data 
    if (request.getMethod() == "POST") {
        _env_map["CONTENT_LENGTH"] = request.getHeader("content-length");
        _env_map["CONTENT_TYPE"] = request.getHeader("content-type");
    }
    
    // Forward HTTP request headers with HTTP_ prefix
    const std::map<std::string, std::string>& headers = request.getHeaders();
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
        std::string key = it->first;
        if (key == "content-type" || key == "content-length") continue;
        for (std::size_t i = 0; i < key.size(); ++i) {
            if (key[i] == '-') key[i] = '_';
            else if (key[i] >= 'a' && key[i] <= 'z') key[i] = static_cast<char>(key[i] - 'a' + 'A');
        }
        _env_map["HTTP_" + key] = it->second;
    }
    
    // This variable is often required by PHP-CGI to run [cite: 174]
    _env_map["REDIRECT_STATUS"] = "200"; 
}

void CgiHandler::_convertEnvToCStyle() {
    _env_array = (char**)malloc(sizeof(char*) * (_env_map.size() + 1));
    if (!_env_array) return;
    
    int i = 0;
    for (std::map<std::string, std::string>::iterator it = _env_map.begin(); it != _env_map.end(); ++it) {
        std::string entry = it->first + "=" + it->second;
        _env_array[i] = _strdup_safe(entry);
        if (!_env_array[i]) {
            while (--i >= 0) free(_env_array[i]);
            free(_env_array);
            _env_array = NULL;
            return;
        }
        i++;
    }
    _env_array[i] = NULL;

    _argv = (char**)malloc(sizeof(char*) * 3);
    if (!_argv) return;
    
    _argv[0] = _strdup_safe(_interpreter_path);
    std::string script_name = _script_path;
    std::size_t slash = script_name.find_last_of('/');
    if (slash != std::string::npos) {
        script_name = script_name.substr(slash + 1);
    }
    _argv[1] = _strdup_safe(script_name);
    if (!_argv[0] || !_argv[1]) {
        if (_argv[0]) free(_argv[0]);
        if (_argv[1]) free(_argv[1]);
        free(_argv);
        _argv = NULL;
        return;
    }
    _argv[2] = NULL;
}

/* ========================================================================
 * MAIN EXECUTION
 * ======================================================================== */

bool CgiHandler::execute(HttpRequest& request, short& error_code) {
    _setupEnvMap(request);
    _convertEnvToCStyle();
    
    if (!_env_array || !_argv) {
        error_code = 500;
        return false;
    }

    if (pipe(pipe_in) < 0) {
        error_code = 500;
        return false;
    }
    
    if (pipe(pipe_out) < 0) {
        close(pipe_in[0]);
        close(pipe_in[1]);
        error_code = 500;
        return false;
    }

    fcntl(pipe_in[1], F_SETFL, O_NONBLOCK);
    fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);

    _pid = fork();
    if (_pid < 0) {
        close(pipe_in[0]);
        close(pipe_in[1]);
        close(pipe_out[0]);
        close(pipe_out[1]);
        error_code = 500;
        return false;
    }

    if (_pid == 0) {
        /* CHILD PROCESS */
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_out[1], STDERR_FILENO);

        close(pipe_in[1]);
        close(pipe_out[0]);
        close(pipe_in[0]);
        close(pipe_out[1]);

        size_t last_slash = _script_path.find_last_of('/');
        if (last_slash != std::string::npos)
            chdir(_script_path.substr(0, last_slash).c_str());

        execve(_argv[0], _argv, _env_array);
        _exit(1);
    }

    /* PARENT PROCESS */
    close(pipe_in[0]);
    pipe_in[0] = -1;
    close(pipe_out[1]);
    pipe_out[1] = -1;
    
    return true;
}