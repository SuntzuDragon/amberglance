#!/usr/bin/env bash
# List serial ports Windows can see. See tools/win_env.sh for why this is not
# just `ls /dev/ttyACM*`.
set -euo pipefail

cd "$(dirname "$0")/.."
source tools/win_env.sh

cp tools/ports.py "$WIN_STAGE_UNIX/"
win_py "$WIN_STAGE_DOS\\ports.py"
