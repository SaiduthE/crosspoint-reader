#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Logging.h>

#include <mutex>
#include <string>

/**
 * @brief Non-template core of PersistableStore.
 *
 * All ArduinoJson parse/serialize machinery is instantiated once here (in
 * PersistableStore.cpp) instead of in every store's translation unit. GCC
 * emits the JSON serializer/parser templates as local .isra clones per TU
 * (~0.5KB each), so keeping serializeJson/deserializeJson out of the stores
 * is what makes the abstraction flash-neutral.
 */
class PersistableStoreBase {
 protected:
  PersistableStoreBase() = default;
  ~PersistableStoreBase() = default;

  // Serializes saveToFile/loadFromFile against each other across FreeRTOS
  // tasks, so the JSON snapshot cannot tear mid-serialize and two concurrent
  // saves cannot write their documents out of order. Concurrent saves are
  // reachable: the web server task saves settings while the main task can too.
  //
  // It is deliberately held across the SD write. That is safe only because the
  // read path does NOT take it — derived stores build their snapshots (e.g.
  // CrossPointSettings::statusBarSpec) unlocked. If you ever lock this mutex on
  // a read path, you put it on the render path and stall rendering behind SD
  // I/O, and you create a storeMutex/storageMutex ordering hazard. Don't.
  mutable std::mutex storeMutex;

  // fromJson() implementations call this (instead of saveToFile()) when the
  // on-disk JSON used a legacy shape that was upgraded in memory.
  // loadFromFile() performs the save after releasing storeMutex; calling
  // saveToFile() from inside fromJson() would deadlock on storeMutex.
  void requestResave() { resaveRequested = true; }

  bool resaveRequested = false;

 public:
  // Public so non-store JSON files (e.g. per-book bookmarks) can reuse them
  // instead of instantiating serializeJson/deserializeJson in their own TU —
  // that per-TU duplication is exactly what this class exists to prevent.

  // Serializes doc and writes it to path (ensures /.crosspoint exists). Logs
  // on failure. The bytes go to a sibling .tmp that is renamed into place, so
  // a reset mid-write leaves the previous file, never a truncated one. With
  // keepBackup the previous file is first rotated to .bak (only when it holds
  // something: an empty current file must not evict a good backup) so
  // readDocFromFile() has a fallback. The stores use it; per-book JSON does
  // not need the second file.
  static bool writeDocToFile(const char* path, const JsonDocument& doc, bool keepBackup = false);

  // Reads path and parses it into doc. Returns false silently when the file
  // does not exist (expected on first boot); logs on read/parse failure. When
  // path is missing, empty or unparseable and a .bak exists, loads that
  // instead and sets *fromBackup, so the caller can rewrite the primary.
  static bool readDocFromFile(const char* path, JsonDocument& doc, bool* fromBackup = nullptr);

 protected:
  /**
   * Helper function for extracting an obfuscated password from a JSON value.
   * Accepts JsonVariantConst so callers can pass either a whole JsonDocument
   * or a JsonObject element (e.g. inside an array iteration).
   * If the decoded password requires a resave (e.g. from plaintext fallback), `needsResave` is set to true.
   */
  static std::string extractPassword(JsonVariantConst doc, bool& needsResave);
  static std::string extractPassword(JsonVariantConst doc, bool& needsResave, size_t maxLength, bool& valid);
};

/**
 * @brief Base class for persistable singletons using CRTP.
 *
 * Derived classes must provide:
 * - A private default constructor
 * - friend class PersistableStore<Derived>;
 * - static const char* getFilePath();
 * - void toJson(JsonDocument& doc) const;
 * - bool fromJson(JsonVariantConst doc);
 *
 * Note for implementers: read string values as `const char*` (e.g.
 * `obj["name"] | ""`), never as `| std::string("")` — ArduinoJson's
 * std::string converter drags a per-TU copy of the whole JSON serializer
 * into flash via its serializeJson fallback.
 *
 * Concurrency: saveToFile/loadFromFile lock storeMutex, so toJson/fromJson
 * always run under it. fromJson must signal legacy-shape upgrades with
 * requestResave(), never by calling saveToFile() directly (deadlock).
 */
template <typename T>
class PersistableStore : public PersistableStoreBase {
 protected:
  PersistableStore() = default;
  ~PersistableStore() = default;

 public:
  // Delete copy constructor and assignment
  PersistableStore(const PersistableStore&) = delete;
  PersistableStore& operator=(const PersistableStore&) = delete;

  static T& getInstance() {
    static T instance;
    return instance;
  }

  bool saveToFile() const {
    std::lock_guard<std::mutex> lock(storeMutex);
    JsonDocument doc;
    static_cast<const T*>(this)->toJson(doc);
    return writeDocToFile(T::getFilePath(), doc, /*keepBackup=*/true);
  }

  // A store that fails to load runs on defaults, and the next save writes
  // those defaults over whatever was on the card -- which is how a panic on
  // 2026-09-20 cost every setting (theme back to Lyra, the lot). Hence the
  // .bak: a primary that is missing, empty or unparseable falls back to the
  // last good save, and the primary is rewritten from it.
  bool loadFromFile() {
    bool ok;
    bool doResave;
    {
      std::lock_guard<std::mutex> lock(storeMutex);
      resaveRequested = false;
      JsonDocument doc;
      bool fromBackup = false;
      if (!readDocFromFile(T::getFilePath(), doc, &fromBackup)) {
        return false;
      }
      ok = static_cast<T*>(this)->fromJson(doc.as<JsonVariantConst>());
      // Read the flag under the lock that guards the fromJson() that set it.
      doResave = resaveRequested || fromBackup;
      resaveRequested = false;
    }
    // Deliberately outside the lock: saveToFile() takes storeMutex itself.
    if (ok && doResave && !saveToFile()) {
      LOG_ERR("PERSIST", "Failed to resave %s after format update or backup restore", T::getFilePath());
    }
    return ok;
  }
};
