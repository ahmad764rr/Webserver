import os

paths = []
for root, dirs, files in os.walk('/home/ahmad/webserv/src'):
    for file in files:
        if file.endswith('.cpp'): paths.append(os.path.join(root, file))
for root, dirs, files in os.walk('/home/ahmad/webserv/include'):
    for file in files:
        if file.endswith('.hpp'): paths.append(os.path.join(root, file))

for p in paths:
    with open(p, 'r') as f:
        content = f.read()
    new_content = content.replace('#include "Config.hpp"', '#include "config/Config.hpp"')
    new_content = new_content.replace('#include "WebServer.hpp"', '#include "core/WebServer.hpp"')
    new_content = new_content.replace('#include "CgiHandler.hpp"', '#include "cgi/CgiHandler.hpp"')
    
    # Also update HttpHandler/CgiManager include paths
    new_content = new_content.replace('#include "HttpHandler.hpp"', '#include "http/HttpHandler.hpp"')
    new_content = new_content.replace('#include "CgiManager.hpp"', '#include "cgi/CgiManager.hpp"')
    new_content = new_content.replace('#include "FileUtils.hpp"', '#include "utils/FileUtils.hpp"')
    
    if new_content != content:
        with open(p, 'w') as f:
            f.write(new_content)
