// Markdown editor: toolbar, character counter, image upload (select / drag&drop /
// paste), and localStorage draft autosave. No client-side Markdown preview and no
// re-sanitizing of server HTML is performed here by design.
import { el } from "./dom-utils.js";
import { api, ApiError } from "./api-client.js";

const DRAFT_DEBOUNCE_MS = 500;
const MAX_CLIENT_SIDE_UPLOAD_BYTES = 5 * 1024 * 1024;
const ALLOWED_UPLOAD_TYPES = new Set(["image/jpeg", "image/png", "image/gif", "image/webp"]);

function wrapSelection(textarea, before, after = before) {
  const { selectionStart, selectionEnd, value } = textarea;
  const selected = value.slice(selectionStart, selectionEnd);
  const next = `${value.slice(0, selectionStart)}${before}${selected}${after}${value.slice(selectionEnd)}`;
  textarea.value = next;
  const cursorStart = selectionStart + before.length;
  const cursorEnd = cursorStart + selected.length;
  textarea.setSelectionRange(cursorStart, cursorEnd);
  textarea.dispatchEvent(new Event("input", { bubbles: true }));
}

function prefixLines(textarea, prefix) {
  const { selectionStart, selectionEnd, value } = textarea;
  const lineStart = value.lastIndexOf("\n", selectionStart - 1) + 1;
  let lineEnd = value.indexOf("\n", selectionEnd);
  if (lineEnd === -1) {
    lineEnd = value.length;
  }
  const block = value.slice(lineStart, lineEnd);
  const prefixed = block
    .split("\n")
    .map((line) => `${prefix}${line}`)
    .join("\n");
  textarea.value = value.slice(0, lineStart) + prefixed + value.slice(lineEnd);
  textarea.setSelectionRange(lineStart, lineStart + prefixed.length);
  textarea.dispatchEvent(new Event("input", { bubbles: true }));
}

function insertAtCursor(textarea, text) {
  const { selectionStart, selectionEnd, value } = textarea;
  textarea.value = value.slice(0, selectionStart) + text + value.slice(selectionEnd);
  const cursor = selectionStart + text.length;
  textarea.setSelectionRange(cursor, cursor);
  textarea.dispatchEvent(new Event("input", { bubbles: true }));
}

export class MarkdownEditor {
  constructor({
    mountPoint,
    maxLength,
    draftKey,
    initialValue = "",
    placeholder = "",
    textareaLabel = "正文",
    textareaId,
  }) {
    this.maxLength = maxLength;
    this.draftKey = draftKey;
    this.draftTimer = null;

    this.textarea = el("textarea", {
      className: "ba-editor-textarea",
      id: textareaId,
      placeholder,
      "aria-label": textareaLabel,
      maxlength: String(maxLength * 2), // soft cap; real limit enforced server-side and via counter
    });

    this.charCount = el("span", { className: "ba-char-count" });
    this.uploadStatus = el("span", { className: "ba-upload-status", role: "status" });
    this.fileInput = el("input", {
      type: "file",
      accept: "image/jpeg,image/png,image/gif,image/webp",
      className: "ba-visually-hidden",
      "aria-label": "选择要插入的图片",
      onChange: (event) => {
        const [file] = event.target.files || [];
        if (file) {
          this.uploadImage(file);
        }
        event.target.value = "";
      },
    });

    const toolbar = el("div", { className: "ba-editor-toolbar", role: "toolbar", "aria-label": "格式工具" }, [
      this.toolbarButton("加粗", "B", () => wrapSelection(this.textarea, "**")),
      this.toolbarButton("斜体", "I", () => wrapSelection(this.textarea, "*")),
      this.toolbarButton("引用", "”", () => prefixLines(this.textarea, "> ")),
      this.toolbarButton("代码", "<>", () => wrapSelection(this.textarea, "`")),
      this.toolbarButton("列表", "•", () => prefixLines(this.textarea, "- ")),
      this.toolbarButton("链接", "链", () => insertAtCursor(this.textarea, "[链接文字](https://)")),
      this.toolbarButton("插入图片", "图", () => this.fileInput.click()),
    ]);

    const footer = el("div", { className: "ba-editor-footer" }, [
      this.charCount,
      this.uploadStatus,
    ]);

    this.root = el("div", { className: "ba-editor ba-editor-dropzone" }, [
      toolbar,
      this.textarea,
      footer,
      this.fileInput,
    ]);

    this.draftNote = el("p", { className: "ba-draft-note" });

    mountPoint.append(this.root, this.draftNote);

    this.textarea.addEventListener("input", () => this.onInput());
    this.textarea.addEventListener("dragover", (event) => {
      event.preventDefault();
      this.root.classList.add("ba-dropzone-active");
    });
    this.textarea.addEventListener("dragleave", () => this.root.classList.remove("ba-dropzone-active"));
    this.textarea.addEventListener("drop", (event) => {
      event.preventDefault();
      this.root.classList.remove("ba-dropzone-active");
      const file = Array.from(event.dataTransfer?.files || []).find((item) =>
        ALLOWED_UPLOAD_TYPES.has(item.type)
      );
      if (file) {
        this.uploadImage(file);
      }
    });
    this.textarea.addEventListener("paste", (event) => {
      const item = Array.from(event.clipboardData?.items || []).find(
        (candidate) => candidate.kind === "file" && ALLOWED_UPLOAD_TYPES.has(candidate.type)
      );
      if (item) {
        event.preventDefault();
        this.uploadImage(item.getAsFile());
      }
    });

    const restored = this.restoreDraft(initialValue);
    this.setValue(restored);
  }

  toolbarButton(label, glyph, action) {
    return el(
      "button",
      { type: "button", title: label, "aria-label": label, onClick: () => action() },
      [glyph]
    );
  }

  onInput() {
    this.updateCharCount();
    this.saveDraftDebounced();
  }

  updateCharCount() {
    const length = [...this.textarea.value].length;
    this.charCount.textContent = `${length} / ${this.maxLength}`;
    this.charCount.classList.toggle("ba-char-count-over", length > this.maxLength);
  }

  saveDraftDebounced() {
    if (!this.draftKey) {
      return;
    }
    window.clearTimeout(this.draftTimer);
    this.draftTimer = window.setTimeout(() => {
      try {
        window.localStorage.setItem(this.draftKey, this.textarea.value);
      } catch (storageError) {
        // Storage can be unavailable (private mode, quota); autosave is best-effort.
      }
    }, DRAFT_DEBOUNCE_MS);
  }

  restoreDraft(initialValue) {
    if (this.draftKey) {
      try {
        const draft = window.localStorage.getItem(this.draftKey);
        // A saved draft always takes priority over the server-provided
        // initialValue: it means the user had unsaved edits (new post or an
        // in-progress edit of existing content) when the page was left.
        if (draft && draft !== initialValue) {
          this.draftNote.textContent = "已恢复本地保存的草稿。";
          return draft;
        }
      } catch (storageError) {
        // Ignore storage access failures.
      }
    }
    return initialValue || "";
  }

  clearDraft() {
    if (!this.draftKey) {
      return;
    }
    try {
      window.localStorage.removeItem(this.draftKey);
    } catch (storageError) {
      // Ignore storage access failures.
    }
  }

  // Re-scopes the draft to a new key (e.g. the compose form's forum changed).
  // Flushes the in-progress text to the old key first so it round-trips if
  // the user switches back, then loads whatever draft exists under the new key.
  setDraftKey(draftKey) {
    if (draftKey === this.draftKey) {
      return;
    }
    window.clearTimeout(this.draftTimer);
    if (this.draftKey) {
      try {
        window.localStorage.setItem(this.draftKey, this.textarea.value);
      } catch (storageError) {
        // Autosave is best-effort; ignore storage failures.
      }
    }
    this.draftKey = draftKey;
    this.setValue(this.restoreDraft(""));
  }

  getValue() {
    return this.textarea.value;
  }

  setValue(value) {
    this.textarea.value = value;
    this.updateCharCount();
  }

  async uploadImage(file) {
    if (!ALLOWED_UPLOAD_TYPES.has(file.type)) {
      this.setUploadStatus("仅支持JPEG、PNG、GIF和WebP格式的图片", true);
      return;
    }
    if (file.size > MAX_CLIENT_SIDE_UPLOAD_BYTES) {
      this.setUploadStatus("图片超过5MB限制,请压缩后重试", true);
      return;
    }
    this.setUploadStatus("正在上传图片...", false);
    const formData = new FormData();
    formData.append("file", file);
    try {
      const result = await api.upload("/api/uploads", formData);
      insertAtCursor(this.textarea, `\n![图片](${result.upload.url})\n`);
      this.setUploadStatus("图片上传成功,已插入正文。", false);
    } catch (error) {
      const message = error instanceof ApiError ? error.message : "图片上传失败";
      this.setUploadStatus(message, true);
    }
  }

  setUploadStatus(message, isError) {
    this.uploadStatus.textContent = message;
    this.uploadStatus.classList.toggle("ba-upload-status-error", Boolean(isError));
  }
}
