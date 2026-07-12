#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: update.sh <new-binary> [config-path]" >&2
    exit 2
fi

new_binary=$1
config_path=${2:-/etc/blogalone/config.json}
app_root=${BLOGALONE_APP_ROOT:-/opt/blogalone}
service_name=${BLOGALONE_SERVICE_NAME:-blogalone.service}
health_url=${BLOGALONE_HEALTH_URL:-http://127.0.0.1:8080/api/healthz}
current_binary="$app_root/blogalone"
previous_binary="$app_root/blogalone.previous"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
service_stopped=0

test -f "$new_binary"
test -x "$new_binary"
test -x "$current_binary"

"$new_binary" --check-config --config "$config_path"
"$script_dir/backup.sh"

rollback()
{
    status=$?
    if [ "$service_stopped" -eq 1 ] && [ -f "$previous_binary" ]; then
        systemctl stop "$service_name" || true
        install -m 0755 "$previous_binary" "$current_binary"
        systemctl start "$service_name" || true
    fi
    exit "$status"
}
trap rollback INT TERM HUP EXIT

systemctl stop "$service_name"
service_stopped=1
install -m 0755 "$current_binary" "$previous_binary"
install -m 0755 "$new_binary" "$current_binary"
systemctl start "$service_name"

attempt=0
while [ "$attempt" -lt 20 ]; do
    if curl --fail --silent --show-error --max-time 2 "$health_url" >/dev/null; then
        service_stopped=0
        trap - INT TERM HUP EXIT
        echo "update completed"
        exit 0
    fi
    attempt=$((attempt + 1))
    sleep 1
done

echo "health check failed; rolling back" >&2
exit 1
