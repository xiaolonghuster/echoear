#!/bin/bash
# Activate the existing local ESP-IDF runtime, then select a board explicitly.
set -eo pipefail
ECHOEAR_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ECHOEAR_IDF="${XIAOZHI_IDF_DIR:-${IDF_PATH:-$ECHOEAR_ROOT/../toolchains/esp-idf-v6.0.1}}"
for ECHOEAR_PYTHON_BIN in "${IDF_TOOLS_PATH:-$HOME/.espressif}"/python_env/idf6.*_env/bin; do
    if [[ -x "$ECHOEAR_PYTHON_BIN/python" ]]; then
        export PATH="$ECHOEAR_PYTHON_BIN:$PATH"
        break
    fi
done
if [[ "${1:-}" == "build" ]]; then
    if [[ ! -f "$ECHOEAR_IDF/export.sh" ]]; then
        echo "Set XIAOZHI_IDF_DIR to an installed ESP-IDF 6.0.1 or newer." >&2
        exit 1
    fi
    source "$ECHOEAR_IDF/export.sh" >/dev/null
fi
exec python "$ECHOEAR_ROOT/scripts/echoear.py" "$@"
