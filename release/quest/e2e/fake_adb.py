#!/usr/bin/env python3
# Fake adb for an end-to-end smoke test of the scrcpy client without device:
# it answers the adb commands used by scrcpy, and plays the role of
# scrcpy-server by sending a recorded video stream (test.h264 or test.h265,
# with access unit delimiters) in a loop.
#
#   ADB=release/quest/e2e/fake_adb.py SCRCPY_SERVER_PATH=<any file> \
#       scrcpy --quest --no-audio --no-control --quest-perf --time-limit=8
import os
import socket
import struct
import sys
import time

D = os.path.dirname(os.path.abspath(__file__))
PORT_FILE = os.environ.get('FAKE_ADB_PORT_FILE', '/tmp/scrcpy_fake_adb_port')
a = sys.argv[1:]
if a and a[0] == '-s':
    a = a[2:]
if not a or a[0] in ('start-server', 'push', 'kill-server'):
    sys.exit(0)
if a[0] == 'devices':
    print('List of devices attached')
    print('FAKEQUEST3             device usb:1-1 product:eureka '
          'model:Quest_3 device:eureka transport_id:1')
    sys.exit(0)
if a[0] == 'reverse':
    if a[1] != '--remove':
        with open(PORT_FILE, 'w') as f:
            f.write(a[2].split(':')[1])
    sys.exit(0)
if a[0] == 'shell' and 'app_process' in ' '.join(a):
    args = ' '.join(a)
    h265 = 'video_codec=h265' in args
    with open(PORT_FILE) as f:
        port = int(f.read())
    with open(os.path.join(D, 'test.h265' if h265 else 'test.h264'), 'rb') as f:
        data = f.read()
    # split into access units
    aud = b'\x00\x00\x00\x01\x46\x01' if h265 else b'\x00\x00\x00\x01\x09'
    key = b'\x00\x00\x00\x01\x40\x01' if h265 else b'\x00\x00\x00\x01\x67'
    pos = []
    i = data.find(aud)
    while i != -1:
        pos.append(i)
        i = data.find(aud, i + 5)
    pos.append(len(data))
    aus = [data[pos[i]:pos[i + 1]] for i in range(len(pos) - 1)]
    fps = float(os.environ.get('FAKE_FPS', '30'))
    s = socket.create_connection(('127.0.0.1', port))
    ctrl = None
    if 'control=false' not in args:
        ctrl = socket.create_connection(('127.0.0.1', port))
    s.sendall(b'Quest 3 (fake)'.ljust(64, b'\0'))
    s.sendall(struct.pack('>III', 0x68323635 if h265 else 0x68323634,
                          2064, 1104))
    start = time.time()
    n = 0
    try:
        while True:
            for au in aus:
                flags = (1 << 62) if key in au[:64] else 0
                pts = int(n * 1e6 / fps)
                s.sendall(struct.pack('>QI', pts | flags, len(au)) + au)
                n += 1
                delay = start + n / fps - time.time()
                if delay > 0:
                    time.sleep(delay)
    except (BrokenPipeError, ConnectionResetError, OSError):
        pass
sys.exit(0)
