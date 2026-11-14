#!/bin/bash
# Double-clickable launcher (macOS): Meta Quest 3 viewing mode.
# Extra scrcpy options can be appended, for example:
#     ./scrcpy-quest.command -f --quest-preset=A
cd "$(dirname "$0")" || exit 1
export ADB="$PWD/adb"
exec ./scrcpy --quest --no-audio "$@"
