"""Dev/test-only helper: promotes a user to admin directly in the SQLite
dev database. Used by the Playwright E2E setup because BlogAlone does not
yet ship a CLI admin-creation command (roadmap phase 10). Never use against
a production database.
"""
import sqlite3
import sys

if len(sys.argv) != 3:
    print("usage: promote_admin.py <db_path> <username>", file=sys.stderr)
    sys.exit(1)

db_path, username = sys.argv[1], sys.argv[2]
conn = sqlite3.connect(db_path)
try:
    cur = conn.cursor()
    cur.execute("UPDATE users SET role = 'admin' WHERE username = ? COLLATE NOCASE", (username,))
    if cur.rowcount != 1:
        print(f"expected to update 1 row, updated {cur.rowcount}", file=sys.stderr)
        sys.exit(1)
    conn.commit()
finally:
    conn.close()
