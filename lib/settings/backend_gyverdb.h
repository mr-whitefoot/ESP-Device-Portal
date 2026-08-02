#pragma once
#include <GyverDBFile.h>
#include <LittleFS.h>

#include "settings_copy.h"
#include "settings_key.h"

// Реализация слоя настроек на GyverDB.
//
// GyverDB хранит пары ключ-значение с хэш-ключами по 8 байт на ячейку, а не
// структуру. Именно поэтому добавление параметра не может сдвинуть уже
// сохранённые: init() создаёт ячейку, только если её нет.
//
// Экземпляр базы живёт здесь и наружу не торчит -- в этом весь смысл слоя.

namespace settings {
namespace detail {

// Конструктор GyverDBFile только запоминает указатели, к файловой системе
// не обращается, поэтому статическая инициализация тут безопасна.
static GyverDBFile _db(&LittleFS, "/data.db");

inline GyverDBFile& db() {
  return _db;
}

}  // namespace detail

inline bool begin() {
  LittleFS.begin();
  return detail::db().begin();
}

inline void tick() {
  detail::db().tick();
}

inline void commit() {
  detail::db().update();
}

inline void clear() {
  detail::db().clear();
  detail::db().update();
}

inline bool exists(const Key& key) {
  return detail::db().has(key.id);
}

// bool кладётся как int32 (0/1): так тип ячейки не зависит от того, как
// конкретный бэкенд представляет булево, и keepTypes не устроит сюрприз
// при последующей записи.
inline void setBool(const Key& key, bool value) {
  detail::db().set(key.id, (int32_t)(value ? 1 : 0));
}

inline void setInt(const Key& key, int32_t value) {
  detail::db().set(key.id, value);
}

inline void setFloat(const Key& key, float value) {
  detail::db().set(key.id, value);
}

inline void setString(const Key& key, const char* value) {
  detail::db().set(key.id, (value != nullptr) ? value : "");
}

inline void defineBool(const Key& key, bool value) {
  detail::db().init(key.id, (int32_t)(value ? 1 : 0));
}

inline void defineInt(const Key& key, int32_t value) {
  detail::db().init(key.id, value);
}

inline void defineFloat(const Key& key, float value) {
  detail::db().init(key.id, value);
}

inline void defineString(const Key& key, const char* value) {
  detail::db().init(key.id, (value != nullptr) ? value : "");
}

inline int32_t getInt(const Key& key) {
  return detail::db().get(key.id).toInt32();
}

inline bool getBool(const Key& key) {
  return getInt(key) != 0;
}

inline float getFloat(const Key& key) {
  return detail::db().get(key.id).toFloat();
}

inline size_t getString(const Key& key, char* buf, size_t size) {
  gdb::Entry entry = detail::db().get(key.id);
  if (!entry.valid()) return detail::copyBounded(buf, size, nullptr, 0);

  // Строка лежит в ячейке без нуля на конце, копируем ровно size() байт.
  if (entry.type() == gdb::Type::String)
    return detail::copyBounded(buf, size, (const char*)entry.buffer(), entry.size());

  // Число, запрошенное строкой -- редкий путь, тут допустима аллокация.
  String text = entry.toString();
  return detail::copyBounded(buf, size, text.c_str(), text.length());
}

}  // namespace settings
