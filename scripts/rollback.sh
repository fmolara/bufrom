#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck source-path=SCRIPTDIR
set -euo pipefail
umask 077
[[ $# == 1 ]] || { echo 'Uso: rollback.sh CONFIG' >&2; exit 2; }
# shellcheck source=../config/bufrom.conf.example
source "$1"
cd -- "$STATE_DIR"
exec 9> "$STATE_DIR/update.lock"
flock -n 9 || exit 75
previous=$(jq -r '.previous_release' "$STATE_DIR/last-success.json")
[[ $previous =~ ^[0-9]{8}T[0-9]{6}Z-[0-9]+$ ]] || { echo 'Nessuna release precedente'; exit 1; }
install -m 644 "$PUBLIC_DIR/releases/$previous/index.html" "$PUBLIC_DIR/.rollback.tmp"
mv -Tf "$PUBLIC_DIR/.rollback.tmp" "$PUBLIC_DIR/index.html"
echo "$(date -u +%FT%TZ) Ripristinata release $previous" >> "$STATE_DIR/update.log"
