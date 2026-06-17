#!/usr/bin/env python3
import sys

# Print headers
print("Content-Type: text/html\r")
print("\r")

# Trigger a python runtime exception
raise Exception("Intentional CGI Runtime Exception")
