#!/usr/bin/env sh
set -eu

umask 077

database_path=${BLOGALONE_DATABASE_PATH:-/var/lib/blogalone/blogalone.db}
uploads_path=${BLOGALONE_UPLOADS_PATH:-/var/lib/blogalone/uploads}
backup_root=${BLOGALONE_BACKUP_ROOT:-/backup/blogalone}
timestamp=$(date -u +%Y%m%d-%H%M%S)
daily_prefix="$backup_root/daily-$timestamp"
lock_file="$backup_root/.backup.lock"

mkdir -p "$backup_root"
exec 9>"$lock_file"
flock -n 9

test -f "$database_path"
test -d "$uploads_path"

sqlite3 -cmd "PRAGMA busy_timeout=5000;" "$database_path" ".backup '$daily_prefix.db'"
integrity=$(sqlite3 "$daily_prefix.db" "PRAGMA integrity_check;")
if [ "$integrity" != "ok" ]; then
    rm -f "$daily_prefix.db"
    echo "backup integrity check failed: $integrity" >&2
    exit 1
fi

tar -C "$(dirname "$uploads_path")" -czf "$daily_prefix.uploads.tar.gz" "$(basename "$uploads_path")"
(
    cd "$backup_root"
    sha256sum "$(basename "$daily_prefix.db")" "$(basename "$daily_prefix.uploads.tar.gz")"
) >"$daily_prefix.sha256"

if [ "$(date -u +%u)" = "7" ]; then
    weekly_prefix="$backup_root/weekly-$timestamp"
    cp "$daily_prefix.db" "$weekly_prefix.db"
    cp "$daily_prefix.uploads.tar.gz" "$weekly_prefix.uploads.tar.gz"
    (
        cd "$backup_root"
        sha256sum "$(basename "$weekly_prefix.db")" "$(basename "$weekly_prefix.uploads.tar.gz")"
    ) >"$weekly_prefix.sha256"
fi

find "$backup_root" -type f -name 'daily-*' -mtime +7 -delete
find "$backup_root" -type f -name 'weekly-*' -mtime +28 -delete

echo "$daily_prefix"
