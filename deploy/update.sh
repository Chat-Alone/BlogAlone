#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: update.sh <release-directory> [config-path]" >&2
    exit 2
fi

release_root=$1
config_path=${2:-/etc/blogalone/config.json}
app_root=${BLOGALONE_APP_ROOT:-/opt/blogalone}
service_name=${BLOGALONE_SERVICE_NAME:-blogalone.service}
health_url=${BLOGALONE_HEALTH_URL:-http://127.0.0.1:8080/api/healthz}
database_path=${BLOGALONE_DATABASE_PATH:-/var/lib/blogalone/blogalone.db}
uploads_path=${BLOGALONE_UPLOADS_PATH:-/var/lib/blogalone/uploads}
new_binary="$release_root/blogalone"
new_web="$release_root/web"
new_migrations="$release_root/migrations"
current_binary="$app_root/blogalone"
previous_binary="$app_root/blogalone.previous"
current_web="$app_root/web"
previous_web="$app_root/web.previous"
current_migrations="$app_root/migrations"
previous_migrations="$app_root/migrations.previous"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
service_stopped=0
deployment_started=0
new_service_started=0
backup_prefix=

case "$app_root" in
    /*) ;;
    *) echo "BLOGALONE_APP_ROOT must be an absolute path" >&2; exit 2 ;;
esac
case "$database_path" in
    /*) ;;
    *) echo "BLOGALONE_DATABASE_PATH must be an absolute path" >&2; exit 2 ;;
esac
case "$uploads_path" in
    /*) ;;
    *) echo "BLOGALONE_UPLOADS_PATH must be an absolute path" >&2; exit 2 ;;
esac
if [ "$app_root" = "/" ] || [ "$uploads_path" = "/" ]; then
    echo "application and upload roots must not be /" >&2
    exit 2
fi

test -d "$release_root"
test -f "$new_binary"
test -x "$new_binary"
test -d "$new_web"
test -d "$new_migrations"
test -x "$current_binary"
test -d "$current_web"
test -d "$current_migrations"
test -f "$database_path"
test -d "$uploads_path"

"$new_binary" --check-config --config "$config_path"
database_uid=$(stat -c %u "$database_path")
database_gid=$(stat -c %g "$database_path")
database_mode=$(stat -c %a "$database_path")
uploads_uid=$(stat -c %u "$uploads_path")
uploads_gid=$(stat -c %g "$uploads_path")
uploads_mode=$(stat -c %a "$uploads_path")
uploads_name=$(basename "$uploads_path")
uploads_parent=$(dirname "$uploads_path")

restore_backup()
{
    restore_workspace=$(mktemp -d) || return 1
    backup_directory=$(dirname "$backup_prefix")
    backup_name=$(basename "$backup_prefix")

    (
        cd "$backup_directory"
        sha256sum -c "$backup_name.sha256"
    ) || {
        rm -rf "$restore_workspace"
        return 1
    }
    tar -C "$restore_workspace" -xzf "$backup_prefix.uploads.tar.gz" || {
        rm -rf "$restore_workspace"
        return 1
    }
    restored_uploads="$restore_workspace/$uploads_name"
    test -d "$restored_uploads" || {
        rm -rf "$restore_workspace"
        return 1
    }

    install -o "$database_uid" -g "$database_gid" -m "$database_mode" \
        "$backup_prefix.db" "$database_path" || {
        rm -rf "$restore_workspace"
        return 1
    }
    rm -f "$database_path-wal" "$database_path-shm" || {
        rm -rf "$restore_workspace"
        return 1
    }

    failed_uploads="$uploads_parent/.blogalone-uploads-failed-$$"
    test ! -e "$failed_uploads" || {
        rm -rf "$restore_workspace"
        return 1
    }
    mv "$uploads_path" "$failed_uploads" || {
        rm -rf "$restore_workspace"
        return 1
    }
    if ! mv "$restored_uploads" "$uploads_path"; then
        mv "$failed_uploads" "$uploads_path" || true
        rm -rf "$restore_workspace"
        return 1
    fi
    chown -R "$uploads_uid:$uploads_gid" "$uploads_path" || return 1
    chmod "$uploads_mode" "$uploads_path" || return 1
    rm -rf "$failed_uploads" "$restore_workspace" || return 1
}

rollback()
{
    status=$?
    trap - INT TERM HUP EXIT
    if [ "$service_stopped" -eq 1 ]; then
        systemctl stop "$service_name" || true
        if [ "$deployment_started" -eq 1 ]; then
            if [ -f "$previous_binary" ]; then
                install -m 0755 "$previous_binary" "$current_binary" || status=1
            fi
            if [ -d "$previous_web" ]; then
                rm -rf "$current_web" || status=1
                mv "$previous_web" "$current_web" || status=1
            fi
            if [ -d "$previous_migrations" ]; then
                rm -rf "$current_migrations" || status=1
                mv "$previous_migrations" "$current_migrations" || status=1
            fi
        fi
        if [ "$new_service_started" -eq 1 ] && [ -n "$backup_prefix" ]; then
            restore_backup || status=1
        fi
        systemctl start "$service_name" || status=1
    fi
    exit "$status"
}
trap rollback INT TERM HUP EXIT

systemctl stop "$service_name"
service_stopped=1
backup_prefix=$(BLOGALONE_MANAGE_SERVICE=0 "$script_dir/backup.sh")
rm -rf "$previous_web" "$previous_migrations"
install -m 0755 "$current_binary" "$previous_binary"
deployment_started=1
mv "$current_web" "$previous_web"
mv "$current_migrations" "$previous_migrations"
install -m 0755 "$new_binary" "$current_binary"
cp -a "$new_web" "$current_web"
cp -a "$new_migrations" "$current_migrations"
new_service_started=1
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
