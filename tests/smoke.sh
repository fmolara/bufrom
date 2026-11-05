#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
program="$task_root/build/diario-meteo"
fixture="$task_root/tests/data/synop3new.bufr"
gnuplot_program=${GNUPLOT:-gnuplot}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM

"$program" --help > "$work/help.txt"
"$program" verify "$task_root"/tests/data/*.bufr > "$work/verify.txt" 2> "$work/verify.log"
"$program" export --output "$work/raw.csv" "$fixture" 2> "$work/export.log"
test -s "$work/raw.csv"

"$program" build --output "$work/site" --gnuplot "$gnuplot_program" \
	--station 16148 --station 16149 "$fixture" > "$work/build.txt" 2> "$work/build.log"
test -s "$work/site/index.html"
test -s "$work/site/temperature-1.svg"
test -s "$work/site/temperature-2.svg"
test "$(wc -l < "$work/site/observations.csv")" -eq 3
test "$(find "$work/site" -name '*.svg' | wc -l)" -eq 2

if "$program" build --output "$work/site" "$fixture" > "$work/repeat.txt" 2>&1; then
	printf '%s\n' 'FAIL: build ha accettato una directory esistente' >&2
	exit 1
fi

if "$program" build --output "$work/failed-site" --gnuplot "$work/missing-gnuplot" \
	--station 16148 "$fixture" > "$work/failure.txt" 2>&1; then
	printf '%s\n' 'FAIL: build non ha rilevato il fallimento di gnuplot' >&2
	exit 1
fi
test ! -e "$work/failed-site"
test "$(find "$work" -maxdepth 1 -name '.failed-site.tmp-*' | wc -l)" -eq 0

if "$program" build --output "$work/no-station" --station 99999 "$fixture" \
	> "$work/no-station.txt" 2>&1; then
	printf '%s\n' 'FAIL: build ha accettato una selezione priva di osservazioni' >&2
	exit 1
fi
test ! -e "$work/no-station"

printf '%s\n' 'OK: CLI, roundtrip, CSV, gnuplot, selezione stazioni, protezione output e cleanup'
