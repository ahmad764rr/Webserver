#ifndef FILEUTILS_HPP
#define FILEUTILS_HPP

#include <string>

namespace FileUtils {

    bool loadFile(const std::string& path, std::string& out);
    bool isDirectory(const std::string& path);
    bool fileExists(const std::string& path);
    bool generateAutoIndex(const std::string& path, const std::string& requestPath, std::string& out);
    std::string mimeTypeForPath(const std::string& path);
    
}

#endif
