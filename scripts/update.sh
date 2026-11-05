#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck source-path=SCRIPTDIR
set -euo pipefail
umask 077
if [[ ${BUFROM_TIMEOUT_ACTIVE:-0} != 1 ]]; then
	exec timeout --kill-after=10s 15m env BUFROM_TIMEOUT_ACTIVE=1 "$0" "$@"
fi
[[ $# == 1 ]] || { echo 'Uso: update.sh /percorso/privato/bufrom.conf' >&2; exit 2; }
config=$(realpath "$1")
# shellcheck source=../config/bufrom.conf.example
source "$config"
script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
OVERLAP_HOURS=${OVERLAP_HOURS:-30}
DISPLAY_HOURS=${DISPLAY_HOURS:-48}
: "${PROGRAM:?}" "${STATE_DIR:?}" "${PUBLIC_DIR:?}" "${REPOSITORY_URL:?}" "${NETWORK:?}"
[[ $NETWORK == simnbo ]] || { echo 'Questo rilascio operativo supporta solo la rete simnbo' >&2; exit 2; }
[[ ${OVERLAP_HOURS:-30} =~ ^[0-9]+$ && $OVERLAP_HOURS -ge 1 && $OVERLAP_HOURS -le 216 ]] || exit 2
[[ ${DISPLAY_HOURS:-48} =~ ^[0-9]+$ && $DISPLAY_HOURS -ge 1 && $DISPLAY_HOURS -le 8760 ]] || exit 2
state=$(realpath -m "$STATE_DIR") public=$(realpath -m "$PUBLIC_DIR")
repo=$(realpath "$script_dir/..")
for private_path in "$state" "$config"; do
	[[ $private_path != "$public" && $private_path != "$public/"* ]] || { echo 'Dati/configurazione dentro la document root' >&2; exit 2; }
done
[[ $public != "$state" && $public != "$state/"* && $state != "$repo" && $state != "$repo/"* ]] || { echo 'Separare repository, archivio e directory pubblica' >&2; exit 2; }
mkdir -p "$state/raw" "$state/receipts"
chmod 700 "$state" "$state/raw" "$state/receipts"
exec 9> "$state/update.lock"
flock -n 9 || { echo 'Aggiornamento già in corso' >&2; exit 75; }
exec >> "$state/update.log" 2>&1
stage=initialization
attempt=$(date -u +%FT%TZ)
work=$(mktemp -d "$state/.update-XXXXXX")
finish() {
	result=$?
	jq -n --arg attempted "$attempt" --arg ended "$(date -u +%FT%TZ)" --arg stage "$stage" --argjson status "$result" \
		'{attempt_utc:$attempted,ended_utc:$ended,stage:$stage,exit_status:$status}' > "$state/.last-attempt.tmp"
	mv -f "$state/.last-attempt.tmp" "$state/last-attempt.json"
	rm -rf -- "$work"
	if [[ $result != 0 ]]; then echo "Errore: fase $stage, stato $result; pubblicazione precedente conservata"; fi
}
trap finish EXIT
trap 'exit 143' TERM
jq -n --arg attempted "$attempt" '{attempt_utc:$attempted,stage:"running"}' > "$state/last-attempt.json"
echo "Avvio $attempt"
if [[ ! -d $public ]]; then mkdir -p "$public"; chmod 755 "$public"; fi
if [[ ! -f $public/.bufrom-site ]]; then
	[[ -z $(find "$public" -mindepth 1 -maxdepth 1 -print -quit) ]] || { echo 'Directory pubblica non vuota e non inizializzata da BufRom'; exit 1; }
	: > "$public/.bufrom-site"
fi
mkdir -p "$public/releases"
chmod 755 "$public/releases"
station_args=()
for station in "${STATIONS[@]}"; do station_args+=(--station "$station"); done
stage=acquisition
now_epoch=$(date -u +%s)
start_epoch=$((now_epoch - OVERLAP_HOURS*3600))
cursor=$start_epoch
files=()
while [[ $cursor -le $now_epoch ]]; do
	day=$(date -u -d "@$cursor" +%F)
	end_day=$(date -u -d "$day 23:59:59 UTC" +%s)
	end=$end_day
	if [[ $end -gt $now_epoch ]]; then end=$now_epoch; fi
	from=$(date -u -d "@$cursor" '+%F %H:%M') to=$(date -u -d "@$end" '+%F %H:%M')
	file="$work/$day.bufr"
	"$script_dir/fetch-meteohub.sh" "$config" "$from" "$to" "$file"
	files+=("$file")
	cursor=$((end_day+1))
done
stage=import
acquired=$(date -u +%FT%TZ)
"$PROGRAM" import --database "$state/observations.sqlite" --raw-dir "$state/raw" \
	--origin https://meteohub.agenziaitaliameteo.it/api/observations --dataset simnbo \
	--acquired-at "$acquired" "${station_args[@]}" "${files[@]}" > "$work/counts.json"
cat "$work/counts.json"
for file in "${files[@]}"; do
	hash=$(sha256sum "$file"); hash=${hash%% *}
	cp "$file.json" "$state/receipts/$hash-${acquired//:/}.json"
done
stage=render
release_id="$(date -u +%Y%m%dT%H%M%SZ)-$BASHPID"
release="$public/releases/$release_id"
"$PROGRAM" site --database "$state/observations.sqlite" --output "$release" \
	--repository "$REPOSITORY_URL" --hours "$DISPLAY_HOURS" --gnuplot "${GNUPLOT:-gnuplot}" \
	--asset-prefix "releases/$release_id/" "${station_args[@]}"
# Prepare both status and new entry point before changing the public HTML.
previous=''
if [[ -f $state/last-success.json ]]; then previous=$(jq -r '.release' "$state/last-success.json"); fi
jq -n --arg succeeded "$(date -u +%FT%TZ)" --arg release "$release_id" --arg previous "$previous" \
	--slurpfile counts "$work/counts.json" '{success_utc:$succeeded,release:$release,previous_release:$previous,counts:$counts[0]}' > "$state/.last-success.tmp"
stage=publication
install -m 644 "$release/index.html" "$public/.index-$release_id.tmp"
mv -Tf "$public/.index-$release_id.tmp" "$public/index.html"
mv -f "$state/.last-success.tmp" "$state/last-success.json"
stage=success
echo "Pubblicata release $release_id"
