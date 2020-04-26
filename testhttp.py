#!/usr/bin/env python3

import sys
import subprocess
import os
import signal

HTTP_PREF = 'http://'
HTTP_PORT = '80'

HTTPS_PREF = 'https://'
HTTPS_PORT = '443'

LOCALHOST = '127.0.0.1'
LOCAL_PORT = '3333'

TEMPLATE = 'pid =\nforeground = yes\noutput = /dev/stdout\n[service]\nclient = yes\naccept = ' + LOCALHOST + ':' + LOCAL_PORT
FILE_NAME = 'config.txt'

def fatal(message):
    print(message, file=sys.stderr)
    sys.exit(1)

def get_server(prefix):
    address = sys.argv[2][(len(prefix)):]
    try:
        x = address.index('/')
        server = address[:x]
    except:
        fatal('Wrong argument')
    
    return server

def signal_handler(sig, frame):
    try:
        os.remove(FILE_NAME)
    except:
        pass
    try:
        stunnel.kill()
    except:
        pass
    sys.exit(1)

def connect(server, port):
    address = server + ':' + port
    subprocess.call(['./testhttp_raw', address, sys.argv[1], sys.argv[2]])

def make_config_file(server):
    config_file = open(FILE_NAME, 'w')
    print(TEMPLATE, file=config_file)
    print('connect = ' + server + ':' + HTTPS_PORT, file=config_file)
    config_file.close()

signal.signal(signal.SIGINT, signal_handler)

if len(sys.argv) != 3:
    fatal('Wrong number of arguments')

# Sprawdź, czy plik z ciasteczkami istnieje.
if not os.path.isfile(sys.argv[1]):
    fatal('There is no such file')

if sys.argv[2].startswith(HTTPS_PREF):
    server = get_server(HTTPS_PREF)
    make_config_file(server)
    stunnel = subprocess.Popen(['stunnel', FILE_NAME], stdout=subprocess.PIPE, universal_newlines=True)
    while True:
        line = stunnel.stdout.readline()
        if not line:
            break
        if line.find('Configuration successful'):
            break
    connect(LOCALHOST, LOCAL_PORT)
    stunnel.kill()
    os.remove(FILE_NAME)
elif sys.argv[2].startswith(HTTP_PREF):
    server = get_server(HTTP_PREF)
    connect(server, HTTP_PORT)
else:
    fatal('Wrong argument')
