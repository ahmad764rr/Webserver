#include "utils/FileUtils.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>

namespace FileUtils {

bool loadFile(const std::string& path, std::string& out) {
    out.clear(); 
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream content; 
    content << in.rdbuf(); 
    out = content.str(); 
    return true;
}

bool isDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool fileExists(const std::string& path) {
    struct stat st; 
    return (stat(path.c_str(), &st) == 0);
}

bool generateAutoIndex(const std::string& path, const std::string& requestPath, std::string& out) {
    DIR* dir = opendir(path.c_str());
    if (!dir) return false;

    std::ostringstream html;
    html << "<!DOCTYPE html>\n<html>\n<head>\n<title>Index of " << requestPath << "</title>\n"
         << "<style>body{font-family:monospace; background:#0f172a; color:#f8fafc; padding:20px;} a{color:#38bdf8; text-decoration:none;}</style>\n"
         << "</head>\n<body>\n<h1>Index of " << requestPath << "</h1>\n<hr><pre>\n";

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        if (name == ".") continue;
        std::string relLink = requestPath;
        if (relLink.empty() || relLink[relLink.size() - 1] != '/') relLink += "/";
        relLink += name;
        html << "<a href=\"" << relLink << "\">" << name << "</a>\n";
    }
    closedir(dir);
    html << "</pre><hr>\n</body>\n</html>\n";
    out = html.str();
    return true;
}

std::string mimeTypeForPath(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot);
    for (std::size_t i = 0; i < ext.size(); ++i) {
        if (ext[i] >= 'A' && ext[i] <= 'Z') 
            ext[i] = static_cast<char>(ext[i] - 'A' + 'a');
    }
    
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    return "application/octet-stream";
}

} // namespace FileUtils
