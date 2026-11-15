#!/usr/bin/env bash
# QuestView web mode: the headset itself serves a web page with its picture.
#
# This script (run on a computer, once after every reboot of the headset) pushes the modified scrcpy-server to the headset and starts it
# detached from adb: the cable and the computer are not needed anymore. Then, on the TV (or any device on the same Wi-Fi), open:
#
#     http://<headset IP>:8090/
#
#   ./questweb.sh start     push and start (prints the address to open)
#   ./questweb.sh stop      stop
#   ./questweb.sh status    is it running? who is connected?
#   ./questweb.sh log       show the log
#   ./questweb.sh wifi      switch adb to Wi-Fi (adb tcpip), so that the next "start" does not need the cable until the next reboot
#
# Settings (environment variables):
#   PORT=8090         TCP port of the web page
#   EYE=left          left | right | both
#   PRESET=B          view: A (wide), B, C (tall), D (full picture)
#   BOX=1920x1080     maximum size of the picture (most TVs cannot decode more in a browser); 3840x2160 for a 4K TV
#   PROCESS=device    device: the headset straightens the picture (any browser can show it);
#                     page: the raw buffer is sent, the page straightens it with WebGL (lighter for the headset, heavier for the TV)
#   BITRATE=12000000  video bit rate (bits/s)
#   FPS=60            maximum frame rate
#   MAX_SIZE=2560     with PROCESS=page only: maximum width of the raw buffer (0 = full 4128x2208)
#   ADB=adb           adb executable; SERIAL=<serial> if several devices are connected
#   SERVER=<path>     scrcpy-server built from the quest-web branch (default: next to this script)
set -e

DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ADB=${ADB:-adb}
if [[ -x "$DIR/adb" && "$ADB" == adb ]]
then
    ADB="$DIR/adb"
fi
if [[ -n "$SERIAL" ]]
then
    ADB="$ADB -s $SERIAL"
fi

PORT=${PORT:-8090}
EYE=${EYE:-left}
PRESET=${PRESET:-B}
BOX=${BOX:-1920x1080}
PROCESS=${PROCESS:-device}
BITRATE=${BITRATE:-12000000}
FPS=${FPS:-60}
MAX_SIZE=${MAX_SIZE:-2560}
SERVER=${SERVER:-$DIR/scrcpy-server-questweb}
VERSION=3.3.4

REMOTE_DIR=/data/local/tmp
REMOTE_JAR=$REMOTE_DIR/questweb.jar
REMOTE_RUN=$REMOTE_DIR/questweb-run.sh
REMOTE_LOG=$REMOTE_DIR/questweb.log
# Patterns for pgrep/pkill -f (the brackets prevent them from matching the shell executing the command)
PAT_SERVER='scrcpy[.]Server .*web_port='
PAT_RUNNER='questweb-run[.]sh'

device_ip() {
    $ADB shell "ip -4 -o addr show wlan0 2>/dev/null" | sed -n 's/.* inet \([0-9.]*\).*/\1/p' | head -n 1
}

stop() {
    # The runner loop first (so that it does not restart the server), then the server itself
    $ADB shell "pkill -f '$PAT_RUNNER'; pkill -f '$PAT_SERVER'; true"
}

case "${1:-start}" in
start)
    if [[ ! -f "$SERVER" ]]
    then
        echo "Server file not found: $SERVER" >&2
        exit 1
    fi
    stop
    $ADB push "$SERVER" $REMOTE_JAR >/dev/null

    # The runner restarts the server if it exits (encoder error...). It is executed by "setsid nohup", so that it survives the end of the
    # adb connection: adbd only sends SIGHUP to the shell it started.
    cat > "${TMPDIR:-/tmp}/questweb-run.$$" <<EOF
#!/system/bin/sh
while true
do
    CLASSPATH=$REMOTE_JAR app_process / com.genymobile.scrcpy.Server $VERSION \\
        web_port=$PORT log_level=info cleanup=false audio=false control=false \\
        video_bit_rate=$BITRATE max_fps=$FPS max_size=$MAX_SIZE \\
        quest_process=$PROCESS quest_eye=$EYE quest_preset=$PRESET quest_box=$BOX
    echo "[runner] server exited (\$?), restarting in 3 seconds"
    sleep 3
done
EOF
    $ADB push "${TMPDIR:-/tmp}/questweb-run.$$" $REMOTE_RUN >/dev/null
    rm -f "${TMPDIR:-/tmp}/questweb-run.$$"
    $ADB shell "chmod 755 $REMOTE_RUN; setsid nohup sh $REMOTE_RUN > $REMOTE_LOG 2>&1 < /dev/null & sleep 2"
    sleep 1
    if ! $ADB shell "pgrep -f '$PAT_SERVER'" >/dev/null
    then
        echo "The server did not start, log:" >&2
        $ADB shell "cat $REMOTE_LOG" >&2
        stop
        exit 1
    fi
    IP=$(device_ip)
    echo "QuestView web mode is running on the headset."
    echo "Open on the TV (same Wi-Fi):  http://${IP:-<headset IP>}:$PORT/"
    echo "The cable can be unplugged. It runs until the headset is rebooted (or: $0 stop)."
    ;;
stop)
    stop
    echo "Stopped."
    ;;
status)
    if $ADB shell "pgrep -f '$PAT_SERVER'" >/dev/null
    then
        echo "Running: http://$(device_ip):$PORT/"
        $ADB shell "printf 'GET /status HTTP/1.0\r\n\r\n' | toybox nc 127.0.0.1 $PORT 2>/dev/null | tail -n 1" || true
    else
        echo "Not running."
    fi
    ;;
log)
    $ADB shell "cat $REMOTE_LOG"
    ;;
wifi)
    IP=$(device_ip)
    if [[ -z "$IP" ]]
    then
        echo "The headset is not connected to Wi-Fi" >&2
        exit 1
    fi
    # adbd is restarted by this command, and everything started from adb is killed with it: do it BEFORE "start"
    stop
    $ADB tcpip 5555
    sleep 3
    ${ADB%% -s *} connect "$IP:5555"
    echo "Now unplug the cable and run:  SERIAL=$IP:5555 $0 start"
    ;;
*)
    sed -n '2,25p' "${BASH_SOURCE[0]}"
    exit 1
    ;;
esac
