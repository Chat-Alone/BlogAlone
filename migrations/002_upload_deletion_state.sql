ALTER TABLE uploads ADD COLUMN pending_delete_at INTEGER;

CREATE INDEX idx_uploads_pending_delete ON uploads(pending_delete_at);
