#include "cgi/CgiManager.hpp"
#include "cgi/CgiHandler.hpp"
#include "utils/FileUtils.hpp"
#include "http/HttpHandler.hpp"
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <ctime>

namespace CgiManager {

bool shouldUseCgi(const ServerConfig& cfg, const LocationConfig* location, const HttpRequest& request, std::string& ext) {
    ext.clear();
    const std::string& path = request.getPath();
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    ext = path.substr(dot);
    if (location && location->cgiInterpreters.find(ext) != location->cgiInterpreters.end()) return true;
    return cfg.cgiInterpreters.find(ext) != cfg.cgiInterpreters.end();
}

bool setupCgiTask(const ServerConfig& cfg, const LocationConfig* location, const std::string& matchedKey, ClientConnection& client, const std::string& scriptExt) {
    const HttpRequest& request = client.request;

    std::string scriptPath = HttpHandler::resolvePath(cfg, location, matchedKey, request.getPath());
    if (scriptPath.empty()) return false;

    std::string interpreterPath = (location && location->cgiInterpreters.count(scriptExt)) ? location->cgiInterpreters.find(scriptExt)->second : cfg.cgiInterpreters.find(scriptExt)->second;

    HttpRequest requestCopy = request; 
    short cgiError = 0;
    CgiHandler cgi(scriptPath, interpreterPath);
    if (!cgi.execute(requestCopy, cgiError)) return false;

    client.cgiTask.active = true; 
    client.cgiTask.pid = cgi.getPid();
    client.cgiTask.pipe_in_fd = cgi.pipe_in[1]; 
    client.cgiTask.pipe_out_fd = cgi.pipe_out[0];
    client.cgiTask.body_to_write = request.getBody(); 
    client.cgiTask.body_written = 0;
    client.cgiTask.cgi_output = ""; 
    client.cgiTask.start_time_ms = static_cast<long>(time(NULL)) * 1000;
    client.cgiTask.process_exited = false; 
    client.cgiTask.exit_status = 0;
    
    cgi.pipe_in[1] = -1; 
    cgi.pipe_out[0] = -1;
    return true;
}

void applyCgiOutputToResponse(const std::string& cgiOutput, HttpResponse& response) {
    response.setStatusCode(200);
    std::size_t split = cgiOutput.find("\r\n\r\n");
    std::size_t sepLen = 4;
    if (split == std::string::npos) { split = cgiOutput.find("\n\n"); sepLen = 2; }
    if (split == std::string::npos) { response.setContentType("text/plain; charset=utf-8"); response.setBody(cgiOutput); return; }

    const std::string headerBlock = cgiOutput.substr(0, split);
    const std::string body = cgiOutput.substr(split + sepLen);

    std::istringstream lines(headerBlock); std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr(0, colon);
        for (std::size_t i = 0; i < name.size(); ++i) if (name[i] >= 'A' && name[i] <= 'Z') name[i] = static_cast<char>(name[i] - 'A' + 'a');
        
        const std::string value = line.substr(colon + 1);
        std::size_t first = 0;
        while (first < value.size() && (value[first] == ' ' || value[first] == '\t')) ++first;
        const std::string cleanValue = value.substr(first);

        if (name == "status") {
            std::istringstream iss(cleanValue); int code = 200; iss >> code;
            if (code >= 100 && code <= 599) response.setStatusCode(code);
            continue;
        }
        response.setHeader(name, cleanValue);
    }
    if (response.hasHeader("location") && response.getStatusCode() == 200) {
        response.setStatusCode(302);
    }
    if (!response.hasHeader("content-type")) response.setContentType("text/html; charset=utf-8");
    response.setBody(body);
}

} // namespace CgiManager