#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 3 ]; then
    echo "usage: restore-drill.sh <backup-prefix> [binary] [config-path]" >&2
    exit 2
fi

backup_prefix=$1
binary=${2:-/opt/blogalone/blogalone}
base_config=${3:-/etc/blogalone/config.json}
port=${BLOGALONE_RESTORE_PORT:-18081}
workspace=$(mktemp -d)
database_path="$workspace/blogalone.db"
uploads_path="$workspace/uploads"
drill_config="$workspace/config.json"
server_pid=

cleanup()
{
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$workspace"
}
trap cleanup INT TERM HUP EXIT

(
    cd "$(dirname "$backup_prefix")"
    sha256sum -c "$(basename "$backup_prefix.sha256")"
)
cp "$backup_prefix.db" "$database_path"
tar -C "$workspace" -xzf "$backup_prefix.uploads.tar.gz"
test -d "$uploads_path"

integrity=$(sqlite3 "$database_path" "PRAGMA integrity_check;")
if [ "$integrity" != "ok" ]; then
    echo "restored database integrity check failed: $integrity" >&2
    exit 1
fi

app_root=$(CDPATH= cd -- "$(dirname -- "$binary")" && pwd)
python3 - "$base_config" "$drill_config" "$database_path" "$uploads_path" "$app_root" "$port" <<'PY'
import json
import pathlib
import sys

source, target, database, uploads, app_root, port = sys.argv[1:]
with open(source, encoding="utf-8") as stream:
    config = json.load(stream)
config["listeners"] = [{"address": "127.0.0.1", "port": int(port), "https": False}]
config["db_clients"][0]["filename"] = database
config["custom_config"]["uploads_root"] = uploads
config["custom_config"]["web_root"] = str(pathlib.Path(app_root) / "web")
for plugin in config["plugins"]:
    if plugin["name"] == "blogalone::plugins::DatabaseMigrationPlugin":
        plugin["config"]["database_path"] = database
        plugin["config"]["migrations_dir"] = str(pathlib.Path(app_root) / "migrations")
with open(target, "w", encoding="utf-8") as stream:
    json.dump(config, stream, ensure_ascii=False, indent=2)
PY

"$binary" --check-config --config "$drill_config"
"$binary" --config "$drill_config" >"$workspace/server.log" 2>&1 &
server_pid=$!

base_url="http://127.0.0.1:$port"
attempt=0
while [ "$attempt" -lt 20 ]; do
    if curl --fail --silent --show-error --max-time 2 "$base_url/api/healthz" >/dev/null; then
        break
    fi
    attempt=$((attempt + 1))
    sleep 1
done
if [ "$attempt" -eq 20 ]; then
    cat "$workspace/server.log" >&2
    exit 1
fi

forums_json=$(curl --fail --silent --show-error "$base_url/api/forums")
forum_slug=$(printf '%s' "$forums_json" | python3 -c 'import json,sys; items=json.load(sys.stdin).get("items",[]); print(items[0]["slug"] if items else "")')
if [ -n "$forum_slug" ]; then
    threads_json=$(curl --fail --silent --show-error "$base_url/api/forums/$forum_slug/threads?page=1&page_size=1")
    thread_id=$(printf '%s' "$threads_json" | python3 -c 'import json,sys; items=json.load(sys.stdin).get("items",[]); print(items[0]["id"] if items else "")')
    if [ -n "$thread_id" ]; then
        curl --fail --silent --show-error "$base_url/api/threads/$thread_id?page=1&page_size=1" >/dev/null
    fi
fi

upload_url=$(sqlite3 "$database_path" "SELECT '/uploads/' || path FROM uploads ORDER BY id LIMIT 1;")
if [ -n "$upload_url" ]; then
    curl --fail --silent --show-error "$base_url$upload_url" >/dev/null
fi

echo "restore drill completed"
