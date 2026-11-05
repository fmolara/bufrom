#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Run on the development machine, never on the production server.
set -euo pipefail
repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
[[ $# == 1 ]] || { echo 'Uso: package-runtime.sh DIRECTORY_OUTPUT' >&2; exit 2; }
[[ -z $(git status --porcelain) ]] || { echo 'Prima salvare tutte le modifiche in un commit' >&2; exit 1; }
source /etc/os-release
[[ $ID == debian && $VERSION_ID == 13 ]] || { echo 'Questo pacchetto richiede un ambiente Debian 13' >&2; exit 1; }
arch=$(dpkg --print-architecture)
make -j2 check
version=$(./build/bufrom --version); version=${version#bufrom }
commit=$(git rev-parse HEAD)
short_commit=$(git rev-parse --short HEAD)
[[ $version =~ ^[0-9][0-9A-Za-z.+-]*$ ]] || exit 1
output=$(realpath -m "$1")
[[ $output != "$repo" && $output != "$repo/"* ]] || { echo 'Output fuori dal repository' >&2; exit 2; }
mkdir -p "$output"
stem="bufrom-$version-debian13-$arch"
source_name="bufrom-$version-source.tar.gz"
for name in "$stem.tar.gz" "$source_name" SHA256SUMS; do
	[[ ! -e $output/$name ]] || { echo "File già presente: $output/$name" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
name="bufrom-$version-$short_commit"
package="$work/$name"
install -d "$package/build" "$package/scripts"
install -m 755 build/bufrom "$package/build/bufrom"
strip --strip-unneeded "$package/build/bufrom"
install -m 755 scripts/update.sh scripts/fetch-meteohub.sh scripts/rollback.sh "$package/scripts/"
cp -R tables config systemd apache "$package/"
install -m 644 README.md OPERATIONS.md COPYING VALIDATION-debian.txt "$package/"
{
	printf 'BufRom %s\nCommit %s\nTarget Debian 13 (%s)\n' "$version" "$commit" "$arch"
	printf 'Build UTC: %s\n\n' "$(date -u +%FT%TZ)"
	g++ --version | head -n 1
	dpkg-query -W -f='${binary:Package} ${Version}\n' libc6 libstdc++6 libwreport3t64 wreport-common libsqlite3-0 libssl3t64
	printf '\nSource archive: %s\n' "$source_name"
} > "$package/BUILD-INFO.txt"
find "$package" -type d -exec chmod 755 {} +
find "$package" -type f ! -path "$package/build/bufrom" ! -path "$package/scripts/*" -exec chmod 644 {} +
# Check that the installed layout resolves its own local BUFR tables.
env -u WREPORT_EXTRA_TABLES "$package/build/bufrom" inspect tests/data/meteohub-simnbo.bufr > "$work/inspect.txt"
[[ $(head -n 1 <("$package/build/bufrom" --version)) == "bufrom $version" ]]
git archive --format=tar.gz --prefix="bufrom-$version/" --output="$output/$source_name" HEAD
tar --owner=0 --group=0 --numeric-owner -czf "$output/$stem.tar.gz" -C "$work" "$name"
(cd "$output" && sha256sum "$stem.tar.gz" "$source_name" > SHA256SUMS)
printf 'Creati:\n%s\n%s\n%s\n' "$output/$stem.tar.gz" "$output/$source_name" "$output/SHA256SUMS"
