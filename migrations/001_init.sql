CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    username TEXT NOT NULL UNIQUE COLLATE NOCASE,
    email TEXT UNIQUE COLLATE NOCASE,
    pwd_hash TEXT NOT NULL,
    role TEXT NOT NULL DEFAULT 'user' CHECK (role IN ('user', 'admin')),
    banned_until INTEGER,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    avatar_url TEXT
);

CREATE TABLE sessions (
    token_hash TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    csrf_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,
    revoked_at INTEGER,
    admin_confirmed_at INTEGER,
    ip TEXT NOT NULL,
    user_agent TEXT NOT NULL
);

CREATE TABLE forums (
    id INTEGER PRIMARY KEY,
    slug TEXT NOT NULL UNIQUE COLLATE NOCASE,
    name TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    sort_order INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE threads (
    id INTEGER PRIMARY KEY,
    forum_id INTEGER NOT NULL REFERENCES forums(id) ON DELETE RESTRICT,
    author_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    title TEXT NOT NULL,
    body_md TEXT NOT NULL,
    body_html TEXT NOT NULL,
    is_pinned INTEGER NOT NULL DEFAULT 0 CHECK (is_pinned IN (0, 1)),
    is_featured INTEGER NOT NULL DEFAULT 0 CHECK (is_featured IN (0, 1)),
    is_deleted INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1)),
    deleted_by INTEGER REFERENCES users(id) ON DELETE SET NULL,
    deleted_at INTEGER,
    reply_count INTEGER NOT NULL DEFAULT 0 CHECK (reply_count >= 0),
    last_reply_at INTEGER,
    last_reply_user_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE posts (
    id INTEGER PRIMARY KEY,
    thread_id INTEGER NOT NULL REFERENCES threads(id) ON DELETE CASCADE,
    author_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    floor_no INTEGER NOT NULL CHECK (floor_no > 0),
    body_md TEXT NOT NULL,
    body_html TEXT NOT NULL,
    is_deleted INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1)),
    deleted_by INTEGER REFERENCES users(id) ON DELETE SET NULL,
    deleted_at INTEGER,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    UNIQUE (thread_id, floor_no)
);

CREATE TABLE sub_posts (
    id INTEGER PRIMARY KEY,
    post_id INTEGER NOT NULL REFERENCES posts(id) ON DELETE CASCADE,
    author_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    body_md TEXT NOT NULL,
    body_html TEXT NOT NULL,
    reply_to_user_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
    is_deleted INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1)),
    deleted_by INTEGER REFERENCES users(id) ON DELETE SET NULL,
    deleted_at INTEGER,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE uploads (
    id INTEGER PRIMARY KEY,
    sha256 TEXT NOT NULL UNIQUE,
    path TEXT NOT NULL UNIQUE,
    mime TEXT NOT NULL,
    size INTEGER NOT NULL CHECK (size > 0),
    width INTEGER,
    height INTEGER,
    created_at INTEGER NOT NULL
);

CREATE TABLE upload_refs (
    id INTEGER PRIMARY KEY,
    owner_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    upload_id INTEGER NOT NULL REFERENCES uploads(id) ON DELETE CASCADE,
    created_at INTEGER NOT NULL,
    attached_at INTEGER,
    UNIQUE (owner_id, upload_id)
);

CREATE TABLE audit_log (
    id INTEGER PRIMARY KEY,
    admin_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
    action TEXT NOT NULL,
    target_type TEXT NOT NULL,
    target_id INTEGER NOT NULL,
    detail TEXT NOT NULL,
    created_at INTEGER NOT NULL
);

CREATE TABLE schema_migrations (
    version INTEGER PRIMARY KEY,
    checksum TEXT NOT NULL,
    applied_at INTEGER NOT NULL
);

CREATE INDEX idx_threads_forum_last_reply ON threads(forum_id, is_deleted, is_pinned DESC, last_reply_at DESC);
CREATE INDEX idx_posts_thread_floor ON posts(thread_id, floor_no);
CREATE INDEX idx_sub_posts_post_created ON sub_posts(post_id, created_at);
CREATE INDEX idx_sessions_user ON sessions(user_id);
CREATE INDEX idx_sessions_expires ON sessions(expires_at);
CREATE INDEX idx_upload_refs_owner ON upload_refs(owner_id, created_at);
CREATE INDEX idx_upload_refs_upload ON upload_refs(upload_id);
CREATE INDEX idx_upload_refs_attached ON upload_refs(attached_at);
