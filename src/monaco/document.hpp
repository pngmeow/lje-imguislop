#pragma once
#include <cstdint>
#include <mutex>
#include <string>

namespace monaco {

// C++ side mirror of a Monaco text model.
//
// The real model lives in the renderer process; Monaco pushes its content back
// over the message router whenever it changes (debounced), and host writes are
// applied optimistically so a set_text() immediately followed by text() reads
// back what the caller just wrote instead of the stale editor content.
//
// Every method is safe to call from any thread: editor updates arrive on the
// CEF UI thread while Lua reads happen on the script thread.
class Document {
public:
  explicit Document(std::string initial = {});

  // Called when the editor reports new content. |version| is Monaco's model
  // version id, which only ever increases for a given model, so stale updates
  // (they can overtake each other across the process boundary) are dropped.
  // |user_edit| is false when the change is just the editor echoing back a
  // host write, which must not raise the changed flag.
  void on_editor_changed(std::string text, uint64_t version, bool user_edit);

  // Called when the host writes the document, before the write reaches the
  // editor.
  void on_host_write(std::string text);

  // Called when a new model is installed and version ids restart.
  void reset_version();

  std::string text() const;
  uint64_t version() const;
  size_t length() const;

  // True if the user edited the document since the last call. Consumes the
  // flag, so a script can poll it once per frame to react to edits.
  bool consume_changed();
  bool changed() const;

private:
  mutable std::mutex mutex_;
  std::string text_;
  uint64_t version_ = 0;
  bool changed_ = false;
};

} // namespace monaco