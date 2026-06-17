*This project has been created as part of the 42 curriculum by ahmad.*

## Description
`webserv` is a custom HTTP server written in C++98. It parses an nginx-style configuration file, listens on configured ports, handles multiple clients with non-blocking I/O multiplexing (`poll`), serves static files, supports CGI execution by extension, and returns formatted HTTP responses.

## Instructions
### Build
```bash
make
```

### Run
```bash
./webserv [configuration_file]
```
If no configuration file is provided, it defaults to `config/webserv.conf`.

### Default test config
The provided `config/webserv.conf` includes:
- `listen 8080`
- static root `./www`
- custom `404` and `500` pages
- Python CGI mapping (`.py` -> `/usr/bin/python3`)
- `client_max_body_size`

### Quick manual checks
```bash
curl -i http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/cgi-bin/test_script.py?name=ahmad
curl -i -X POST -H "Content-Type: text/plain" --data "hello" http://127.0.0.1:8080/cgi-bin/test_script.py
curl -i -X DELETE http://127.0.0.1:8080/tmp-file-to-delete.txt
```

## Resources
- RFC 7230 / RFC 7231 HTTP/1.1 semantics and message syntax
- NGINX docs for config inspiration: https://nginx.org/en/docs/
- POSIX docs for `poll`, sockets, `fork`, `execve`, `pipe`

### AI usage disclosure
AI was used for:
- scaffolding and integrating modules (`HttpRequest`, `HttpResponse`, `CgiHandler`)
- implementing server glue and configuration parser boilerplate
- generating evaluation checklist commands and cleanup guidance

All generated code was reviewed, compiled with `-Wall -Wextra -Werror -std=c++98`, and manually tested.
