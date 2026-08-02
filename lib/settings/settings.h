#pragma once
#include <stddef.h>
#include <stdint.h>

#include "settings_copy.h"
#include "settings_key.h"

// Слой хранения настроек.
//
// Смысл в том, чтобы код приложения не знал, чем именно они хранятся.
// Реализация выбирается на этапе сборки, все вызовы инлайнятся, виртуальных
// методов нет: устройство в прошивке одно, подменять хранилище в рантайме
// незачем, а vtable стоила бы флеша.
//
// Поверхность намеренно узкая -- четыре типа и никаких двоичных блобов.
// Чем меньше поверхность, тем реальнее заменить GyverDB на что-то другое.
// Всё, что не выражается через эти вызовы, раскладывается по отдельным
// параметрам: блоб экономит сотню байт и ломается при первом же изменении
// структуры.
//
// Имена явные (setInt, а не set): перегрузки по bool/int32/float ловят
// литералы вроде 5.0 в неожиданную сторону, а слой такого уровня должен
// читаться однозначно.
//
// Контракт:
// - defineX создаёт параметр со значением по умолчанию, если его ещё нет,
//   и НИКОГДА не трогает уже сохранённое. Это и есть свойство «добавили
//   параметр -- остальные пережили обновление»;
// - getString всегда завершает буфер нулём и усекает при нехватке места,
//   возвращая число записанных символов;
// - чтение отсутствующего параметра даёт нулевое значение, не ошибку;
// - bool хранится как int32 (0/1), чтобы тип ячейки не зависел от бэкенда;
// - setX только помечает изменение, запись на носитель делает tick() по
//   таймауту или commit() немедленно.

namespace settings {

bool begin();
void tick();
void commit();
void clear();
bool exists(const Key& key);

void defineBool(const Key& key, bool value);
void defineInt(const Key& key, int32_t value);
void defineFloat(const Key& key, float value);
void defineString(const Key& key, const char* value);

bool getBool(const Key& key);
int32_t getInt(const Key& key);
float getFloat(const Key& key);
size_t getString(const Key& key, char* buf, size_t size);

void setBool(const Key& key, bool value);
void setInt(const Key& key, int32_t value);
void setFloat(const Key& key, float value);
void setString(const Key& key, const char* value);

}  // namespace settings

#if defined(SETTINGS_BACKEND_MEMORY)
#include "backend_memory.h"
#else
#include "backend_gyverdb.h"
#endif
