#!/usr/bin/env python3
import os

print("Content-Type: text/html\r")
print("\r")
print("<html><body>")
print("<h1>CGI Test Success!</h1>")
print("<p>Method: {}</p>".format(os.environ.get('REQUEST_METHOD', 'unknown')))
print("<p>Query: {}</p>".format(os.environ.get('QUERY_STRING', 'none')))
print("<p>Gateway: {}</p>".format(os.environ.get('GATEWAY_INTERFACE', 'none')))
print("</body></html>")
