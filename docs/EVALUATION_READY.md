# Webserv Evaluation Readiness Checklist

## Build & launch
```bash
make re
./webserv config/webserv.conf
```

## Mandatory smoke tests
```bash
# GET static
curl -i http://127.0.0.1:8080/

# GET CGI
curl -i "http://127.0.0.1:8080/cgi-bin/test_script.py?name=ahmad"

# POST CGI
curl -i -X POST -H "Content-Type: text/plain" --data "hello" http://127.0.0.1:8080/cgi-bin/test_script.py

# DELETE static resource
printf "temp" > tmp-file-to-delete.txt
curl -i -X DELETE http://127.0.0.1:8080/tmp-file-to-delete.txt

# Unknown method behavior (no crash)
curl -i -X FOO http://127.0.0.1:8080/
```

## Config checks
- Multiple ports: add another `server` block with different `listen`.
- Custom error page: test missing path -> verify configured `404` page.
- Body limit: set small `client_max_body_size` and POST larger payload.
- CGI mapping: ensure extension to interpreter mapping works.

## Stability checks
```bash
# Basic pressure test (if siege available)
siege -b -t30s http://127.0.0.1:8080/

# Leak check (example)
valgrind --leak-check=full --show-leak-kinds=all ./webserv config/webserv.conf
```

## Notes
- Keep repository clean before submission: no binaries/object files committed.
- Ensure your final defense branch includes config and sample files required for demo.
