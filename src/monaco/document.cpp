#include "document.hpp"

#include <utility>

namespace monaco {

Document::Document(std::string initial) : text_(std::move(initial)) {}

void Document::on_editor_changed(std::string text, uint64_t version, bool user_edit) {
  std::lock_guard lock(mutex_);
  if (version != 0 && version <= version_)
    return;

  text_ = std::move(text);
  version_ = version;
  if (user_edit)
    changed_ = true;
}

void Document::on_host_write(std::string text) {
  // Only the mirror moves here. The version stays owned by the editor: it will
  // echo the write back with a higher model version, which on_editor_changed
  // then accepts without flagging it as a user edit.
  std::lock_guard lock(mutex_);
  text_ = std::move(text);
}

void Document::reset_version() {
  std::lock_guard lock(mutex_);
  version_ = 0;
}

std::string Document::text() const {
  std::lock_guard lock(mutex_);
  return text_;
}

uint64_t Document::version() const {
  std::lock_guard lock(mutex_);
  return version_;
}

size_t Document::length() const {
  std::lock_guard lock(mutex_);
  return text_.size();
}

bool Document::consume_changed() {
  std::lock_guard lock(mutex_);
  return std::exchange(changed_, false);
}

bool Document::changed() const {
  std::lock_guard lock(mutex_);
  return changed_;
}

} // namespace monaco