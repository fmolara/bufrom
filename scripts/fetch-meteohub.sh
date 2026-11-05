#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# shellcheck source-path=SCRIPTDIR
# Public map API: a single UTC day per download. Called by update.sh.
set -euo pipefail
if [[ $# != 4 ]]; then echo 'Uso: fetch-meteohub.sh CONFIG FROM_UTC TO_UTC OUTPUT.bufr' >&2; exit 2; fi
# shellcheck source=../config/bufrom.conf.example
source "$1"
from=$2 to=$3 target=$4
endpoint=https://meteohub.agenziaitaliameteo.it/api/observations
query="reftime: >=${from},<=${to};product:B12101;license:CCBY_COMPLIANT;timerange:254,0,0;level:103,2000,0,0"
# Retry only idempotent exports, at most twice; never log authentication.
response=$(${CURL:-curl} --silent --show-error --fail --connect-timeout 15 --max-time 90 \
	--retry 2 --retry-delay 3 --retry-max-time 200 --max-filesize 10485760 \
	--get --request POST "$endpoint" --data-urlencode "q=$query" \
	--data-urlencode 'output_format=BUFR' --data-urlencode "networks=$NETWORK" \
	--data-urlencode "latmin=$LAT_MIN" --data-urlencode "latmax=$LAT_MAX" \
	--data-urlencode "lonmin=$LON_MIN" --data-urlencode "lonmax=$LON_MAX" \
	--output "$target" --write-out '%{http_code} %{content_type}')
[[ $response == '200 application/octet-stream'* ]] || { echo "Risposta HTTP inattesa: $response" >&2; exit 1; }
[[ $(head -c 4 "$target") == BUFR ]] || { echo 'Il download non inizia con BUFR' >&2; exit 1; }
# Native full-file decode checks framing, tables and all messages, including tails.
"$PROGRAM" inspect "$target" > /dev/null
hash=$(sha256sum "$target"); hash=${hash%% *}
jq -n --arg endpoint "$endpoint" --arg query "$query" --arg hash "$hash" \
	--arg acquired "$(date -u +%FT%TZ)" --arg network "$NETWORK" \
	--arg latmin "$LAT_MIN" --arg latmax "$LAT_MAX" --arg lonmin "$LON_MIN" --arg lonmax "$LON_MAX" \
	'{endpoint:$endpoint,method:"POST",query:{q:$query,output_format:"BUFR",networks:$network,latmin:$latmin,latmax:$latmax,lonmin:$lonmin,lonmax:$lonmax},sha256:$hash,acquired_utc:$acquired,http_status:200,license:"CC BY 4.0",attribution:"Arpae Emilia-Romagna Idro-Meteo-Clima Service",native_decode_verified:true}' > "$target.json"
