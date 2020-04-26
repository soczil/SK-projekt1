#!/usr/bin/env python3

import sys
import subprocess
import os.path

HTTP_PREF = 'http://'
HTTP_PORT = '80'

HTTPS_PREF = 'https://'
HTTPS_PORT = '443'

def fatal(message):
    print(message, file=sys.stderr)
    sys.exit(1)

def split_arg(prefix):
    address = sys.argv[2][(len(prefix)):]

    try:
        x = address.index('/')
        server = address[:x]
        target = address[x:]
    except:
        print('Wrong argument', file=sys.stderr)
        sys.exit(1)
    
    l = [server, target]
    return l

if len(sys.argv) != 3:
    print('Wrong number of arguments', file=sys.stderr)
    sys.exit(1)

# Sprawdź, czy plik z ciasteczkami istnieje.
if not os.path.isfile(sys.argv[1]):
    fatal('There is no such file')

if sys.argv[2].startswith(HTTPS_PREF):
    print('https')
elif sys.argv[2].startswith(HTTP_PREF):
    l = split_arg(HTTP_PREF)
    first_arg = l[0] + ':' + HTTP_PORT
    print(first_arg)
    subprocess.call(['./testhttp_raw', l[0] + ':' + HTTP_PORT, sys.argv[1], sys.argv[2]])
else:
    print('Wrong argument', file=sys.stderr)
    sys.exit(1)
