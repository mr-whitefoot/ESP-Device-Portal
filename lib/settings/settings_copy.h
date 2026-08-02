#pragma once
#include <stddef.h>

// Копирование строкового значения в буфер вызывающего.
//
// Вынесено отдельно, потому что этим обязаны пользоваться все бэкенды: иначе
// контракт getString() разъедется между реализациями. Например, Text::toStr()
// из StringUtils при нехватке места не усекает строку, а возвращает 0, не
// тронув буфер -- вызывающий получает неинициализированную память.
//
// Контракт здесь строгий: буфер всегда завершается нулём, при нехватке места
// строка усекается, возвращается число реально записанных символов.

namespace settings {
namespace detail {

inline size_t copyBounded(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  if (dst == nullptr || dstSize == 0) return 0;

  size_t n = 0;
  if (src != nullptr) {
    n = srcLen;
    if (n > dstSize - 1) n = dstSize - 1;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
  }
  dst[n] = '\0';
  return n;
}

}  // namespace detail
}  // namespace settings
