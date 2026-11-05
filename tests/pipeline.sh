#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
export WREPORT_EXTRA_TABLES="$repo/tables"
"$repo/build/archive-tests" "$repo/tests/data/meteohub-simnbo.bufr" "$work/current.bufr"
# The only synthetic input is generated inside this temporary offline test.
cat > "$work/curl" <<'CURL'
#!/bin/bash
set -euo pipefail
while [[ $# -gt 0 ]]; do
 if [[ $1 == --output ]]; then target=$2; shift; fi
 shift
done
cp "$BUFROM_TEST_FIXTURE" "$target"
printf '200 application/octet-stream'
CURL
chmod +x "$work/curl"
export BUFROM_TEST_FIXTURE="$work/current.bufr"
# %q preserves arbitrary workspace paths without executing them as shell code.
printf 'PROGRAM=%q\nSTATE_DIR=%q\nPUBLIC_DIR=%q\nCURL=%q\n' \
 "$repo/build/bufrom" "$work/state" "$work/public" "$work/curl" > "$work/config"
cat >> "$work/config" <<'CONF'
REPOSITORY_URL=https://github.com/fmolara/bufrom
NETWORK=simnbo
STATIONS=('simnbo:44.22039,12.04182' 'simnbo:44.15206,12.14217')
LAT_MIN=44.12 LAT_MAX=44.28 LON_MIN=12.00 LON_MAX=12.40
OVERLAP_HOURS=1 DISPLAY_HOURS=48 GNUPLOT=gnuplot
CONF
"$repo/scripts/update.sh" "$work/config"
first=$(sha256sum "$work/public/index.html")
first_release=$(jq -r '.release' "$work/state/last-success.json")
cp "$work/state/last-success.json" "$work/initial-success.json"
# A download failure leaves the complete previous publication and status intact.
printf '\nCURL=/bin/false\n' >> "$work/config"
if "$repo/scripts/update.sh" "$work/config"; then echo 'Download failure accepted'; exit 1; fi
[[ $(sha256sum "$work/public/index.html") == "$first" ]]
cmp "$work/state/last-success.json" "$work/initial-success.json"
# Real decoding and import succeed; gnuplot fails before HTML publication.
printf '\nCURL=%q\nGNUPLOT=/bin/false\n' "$work/curl" >> "$work/config"
if "$repo/scripts/update.sh" "$work/config"; then echo 'Render failure accepted'; exit 1; fi
[[ $(sha256sum "$work/public/index.html") == "$first" ]]
cmp "$work/state/last-success.json" "$work/initial-success.json"
# An existing HTML document keeps its own immutable asset URLs after updates.
printf '\nGNUPLOT=gnuplot\n' >> "$work/config"
"$repo/scripts/update.sh" "$work/config"
[[ $(jq -r '.previous_release' "$work/state/last-success.json") == "$first_release" ]]
[[ -s $work/public/releases/$first_release/temperature-1.svg ]]
"$repo/scripts/rollback.sh" "$work/config"
[[ $(sha256sum "$work/public/index.html") == "$first" ]]
# Every published local asset is versioned and exists, even after rollback.
python3 - "$work/public" <<'PY'
from html.parser import HTMLParser
from pathlib import Path
import csv, sys
root=Path(sys.argv[1])
class Check(HTMLParser):
 def handle_starttag(self,tag,attrs):
  for k,v in attrs:
   if k in ('src','href') and not v.startswith(('https:','#')):
    assert v.startswith('releases/'),v
    assert (root/v).is_file(),v
Check().feed((root/'index.html').read_text())
rows=list(csv.DictReader(next((root/'releases').glob('*/observations.csv')).open()))
assert len(rows)==4
assert {r['station_name'] for r in rows}=={'Bertinoro','Forlì Urbana'}
assert all(r['quality'] for r in rows)
PY
printf '%s\n' 'OK: pipeline offline, errore download/render, release e asset coerenti, ripristino'
