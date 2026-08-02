#pragma once
#include <stdint.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include "settings_copy.h"
#include "settings_key.h"

// Реализация слоя настроек в оперативной памяти.
//
// Нужна для native-тестов: с ней модули устройств тестируются на хосте, без
// GyverDBFile и LittleFS. Ради этого слой и заводился в первую очередь --
// подменяемость хранилища оказалась вторым по важности свойством.
//
// На контроллере не используется: std::vector и std::string тут уместны
// именно потому, что этот бэкенд собирается только под хост.

namespace settings {
namespace detail {

enum class ValueType { None, Bool, Int, Float, String };

struct Entry {
  uint32_t id = 0;
  ValueType type = ValueType::None;
  int32_t number = 0;
  float real = 0.0f;
  std::string text;
};

inline std::vector<Entry>& store() {
  static std::vector<Entry> entries;
  return entries;
}

inline Entry* find(uint32_t id) {
  for (Entry& e : store())
    if (e.id == id) return &e;
  return nullptr;
}

inline Entry& obtain(uint32_t id) {
  if (Entry* e = find(id)) return *e;
  store().push_back(Entry{id, ValueType::None, 0, 0.0f, std::string()});
  return store().back();
}

// Счётчик записей на «носитель»: тесты проверяют, что setX не пишет сразу,
// а обновление откладывается до tick()/commit().
inline unsigned& commitCount() {
  static unsigned count = 0;
  return count;
}

inline bool& dirty() {
  static bool flag = false;
  return flag;
}

}  // namespace detail

// Только для тестов: вернуть слой в исходное состояние.
inline void resetForTest() {
  detail::store().clear();
  detail::commitCount() = 0;
  detail::dirty() = false;
}

inline bool begin() {
  return true;
}

inline void commit() {
  if (detail::dirty()) {
    detail::commitCount()++;
    detail::dirty() = false;
  }
}

inline void tick() {
  commit();
}

inline void clear() {
  detail::store().clear();
  detail::dirty() = true;
}

inline bool exists(const Key& key) {
  return detail::find(key.id) != nullptr;
}

inline void setBool(const Key& key, bool value) {
  detail::Entry& e = detail::obtain(key.id);
  e.type = detail::ValueType::Bool;
  e.number = value ? 1 : 0;
  detail::dirty() = true;
}

inline void setInt(const Key& key, int32_t value) {
  detail::Entry& e = detail::obtain(key.id);
  e.type = detail::ValueType::Int;
  e.number = value;
  detail::dirty() = true;
}

inline void setFloat(const Key& key, float value) {
  detail::Entry& e = detail::obtain(key.id);
  e.type = detail::ValueType::Float;
  e.real = value;
  detail::dirty() = true;
}

inline void setString(const Key& key, const char* value) {
  detail::Entry& e = detail::obtain(key.id);
  e.type = detail::ValueType::String;
  e.text = (value != nullptr) ? value : "";
  detail::dirty() = true;
}

inline void defineBool(const Key& key, bool value) {
  if (!exists(key)) setBool(key, value);
}

inline void defineInt(const Key& key, int32_t value) {
  if (!exists(key)) setInt(key, value);
}

inline void defineFloat(const Key& key, float value) {
  if (!exists(key)) setFloat(key, value);
}

inline void defineString(const Key& key, const char* value) {
  if (!exists(key)) setString(key, value);
}

inline int32_t getInt(const Key& key) {
  detail::Entry* e = detail::find(key.id);
  if (e == nullptr) return 0;
  if (e->type == detail::ValueType::Float) return (int32_t)e->real;
  if (e->type == detail::ValueType::String) return (int32_t)atol(e->text.c_str());
  return e->number;
}

inline bool getBool(const Key& key) {
  return getInt(key) != 0;
}

inline float getFloat(const Key& key) {
  detail::Entry* e = detail::find(key.id);
  if (e == nullptr) return 0.0f;
  if (e->type == detail::ValueType::Float) return e->real;
  if (e->type == detail::ValueType::String) return (float)atof(e->text.c_str());
  return (float)e->number;
}

inline size_t getString(const Key& key, char* buf, size_t size) {
  detail::Entry* e = detail::find(key.id);
  if (e == nullptr) return detail::copyBounded(buf, size, nullptr, 0);

  if (e->type == detail::ValueType::String)
    return detail::copyBounded(buf, size, e->text.c_str(), e->text.size());

  std::string s = (e->type == detail::ValueType::Float) ? std::to_string(e->real)
                                                        : std::to_string(e->number);
  return detail::copyBounded(buf, size, s.c_str(), s.size());
}

}  // namespace settings
