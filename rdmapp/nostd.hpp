#pragma once

#include <memory>

namespace nostd {


template <class T>
struct is_unique_ptr : std::false_type {};

template <class T>
struct is_unique_ptr<std::unique_ptr<T>> : std::true_type {};

template <class T>
struct is_shared_ptr : std::false_type {};

template <class T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};


template <typename T>
inline constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

template <typename T>
inline constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

template <typename T>
inline constexpr bool is_smart_ptr_v = is_unique_ptr<T>::value || is_shared_ptr<T>::value;

} // namespace nostd
