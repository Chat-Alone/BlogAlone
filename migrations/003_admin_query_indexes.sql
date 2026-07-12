CREATE INDEX idx_sessions_user_created ON sessions(user_id, created_at DESC);
CREATE INDEX idx_threads_deleted_at ON threads(is_deleted, deleted_at DESC);
CREATE INDEX idx_posts_deleted_at ON posts(is_deleted, deleted_at DESC);
CREATE INDEX idx_sub_posts_deleted_at ON sub_posts(is_deleted, deleted_at DESC);
