#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>
#include <limits>
#include <string>

namespace {

// Worth keeping as the backup: starts the way serializeJson writes a document.
// An empty file, or one whose FAT entry points at another file's data, does not.
bool looksLikeJson(const char* path) {
  auto f = Storage.open(path);
  if (!f) return false;
  const int first = f.read();
  return first == '{' || first == '[';
}

// One attempt at path: false with a log line when the file is there but
// empty or unparseable; false silently when it is not there at all.
bool readDocAt(const char* path, JsonDocument& doc, bool& existed) {
  existed = Storage.exists(path);
  if (!existed) {
    return false;  // Expected on first boot — not an error.
  }
  String json = Storage.readFile(path);
  if (json.isEmpty()) {
    LOG_ERR("PERSIST", "Failed to read %s (empty)", path);
    return false;
  }
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("PERSIST", "JSON parse error in %s: %s", path, error.c_str());
    return false;
  }
  return true;
}

}  // namespace

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc, const bool keepBackup) {
  Storage.mkdir("/.crosspoint");
  String json;
  serializeJson(doc, json);
  // The SD manager's writeFile() removes the old file before it creates the
  // new one, so writing path directly has a window with no file on the card
  // at all. Write beside it and rename into place instead.
  const std::string tmp = std::string(path) + ".tmp";
  if (!Storage.writeFile(tmp.c_str(), json)) {
    LOG_ERR("PERSIST", "Failed to write %s", tmp.c_str());
    Storage.remove(tmp.c_str());
    return false;
  }
  if (Storage.exists(path)) {
    if (keepBackup && looksLikeJson(path)) {
      const std::string bak = std::string(path) + ".bak";
      // FAT rename cannot replace an existing target.
      if (!Storage.remove(bak.c_str()) && Storage.exists(bak.c_str())) {
        LOG_ERR("PERSIST", "Cannot remove %s; the card needs a filesystem check", bak.c_str());
      }
      if (!Storage.rename(path, bak.c_str())) {
        LOG_ERR("PERSIST", "Failed to rotate %s to .bak", path);
        Storage.remove(path);
      }
    } else if (!Storage.remove(path)) {
      // A primary SdFat cannot remove (broken FAT chain) is moved aside, so it neither blocks the rename below
      // nor displaces the good .bak.
      const std::string bad = std::string(path) + ".bad";
      Storage.remove(bad.c_str());
      if (!Storage.rename(path, bad.c_str())) {
        LOG_ERR("PERSIST", "Cannot move %s aside; the card needs a filesystem check", path);
      }
    }
  }
  if (!Storage.rename(tmp.c_str(), path)) {
    LOG_ERR("PERSIST", "Failed to rename %s into place", tmp.c_str());
    return false;
  }
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc, bool* fromBackup) {
  if (fromBackup) *fromBackup = false;
  bool existed = false;
  if (readDocAt(path, doc, existed)) {
    return true;
  }
  const std::string bak = std::string(path) + ".bak";
  bool bakExisted = false;
  doc.clear();
  if (readDocAt(bak.c_str(), doc, bakExisted)) {
    LOG_ERR("PERSIST", "%s %s; loaded %s instead", path, existed ? "unreadable" : "missing", bak.c_str());
    // Drop the bad primary so the caller's rewrite replaces it instead of rotating it over the good .bak.
    if (existed && !Storage.remove(path)) {
      LOG_ERR("PERSIST", "Cannot remove %s; the card needs a filesystem check", path);
    }
    if (fromBackup) *fromBackup = true;
    return true;
  }
  return false;
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool valid = false;
  return extractPassword(doc, needsResave, std::numeric_limits<size_t>::max(), valid);
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave, const size_t maxLength,
                                                  bool& valid) {
  valid = true;
  bool ok = false;
  bool tooLong = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", maxLength, &ok, &tooLong);
  if (tooLong) {
    valid = false;
    return "";
  }
  if (!ok) {
    // Deobfuscation failed — fall back to legacy plaintext password.
    const char* legacyPassword = doc["password"] | "";
    const size_t legacyLength = strlen(legacyPassword);
    if (legacyLength > maxLength) {
      valid = false;
      return "";
    }
    pass.assign(legacyPassword, legacyLength);
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
