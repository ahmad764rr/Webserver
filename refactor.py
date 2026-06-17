import os
import re

old_cpp = open("src/WebServer.cpp", "r").read()

# We need to extract the functions. It is easier to just write the new files completely in python
# since we know exactly what they should contain. 
# But wait, we need to preserve the complex logic of handleCgiRead, handleUpload, etc.

# Let's extract the bodies using a simple brace-matching parser.
def extract_function_body(source, signature_regex):
    match = re.search(signature_regex, source)
    if not match: return ""
    start_idx = match.end()
    brace_count = 0
    in_func = False
    for i in range(start_idx, len(source)):
        if source[i] == '{':
            if not in_func: in_func = True
            brace_count += 1
        elif source[i] == '}':
            brace_count -= 1
            if in_func and brace_count == 0:
                return source[match.start():i+1]
    return ""

def replace_in_body(body, old, new):
    return body.replace(old, new)

# This approach might be fragile. Let's just write a sed script or use python to do surgical replacements.
