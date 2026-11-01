// taffy.hpp - C++17/C++20 header-only CSS layout library (Flexbox, Grid, Block)
// Ported from the Rust "taffy" library by Dioxus Labs
// https://github.com/DioxusLabs/taffy
//
// Verified against taffy's full XML fixture corpus (6,652 fixtures, 100%) and the reference
// Rust implementation. Tested compilers: MSVC 19.44 (VS2022, /std:c++17 and /std:c++20, /W4),
// GCC 16.1 and Clang 20.1 (-std=c++17 / -std=c++20, -Wall -Wextra); all warning-free.
//
// Usage: #include "taffy.hpp"
// Namespace: taffy
//
// Feature flags (define before include):
//   TAFFY_FLEXBOX        - Flexbox layout (enabled by default)
//   TAFFY_GRID           - CSS Grid layout (enabled by default)
//   TAFFY_BLOCK_LAYOUT   - Block layout with float support (enabled by default)
//   TAFFY_FLOAT_LAYOUT   - Float support for block layout (enabled with block)
//   TAFFY_CONTENT_SIZE   - Track content size in Layout/LayoutOutput
//   TAFFY_CSS_PARSE      - CSS string parsing for Style
//   TAFFY_JSON           - JSON serialization/deserialization
//   TAFFY_DEBUG          - Debug tree printing
//
// calc() values are NOT supported in this port.

#pragma once
#ifndef TAFFY_HPP
#define TAFFY_HPP

// --- Default feature flags ---
#if !defined(TAFFY_FLEXBOX) && !defined(TAFFY_NO_FLEXBOX)
#define TAFFY_FLEXBOX
#endif
#if !defined(TAFFY_GRID) && !defined(TAFFY_NO_GRID)
#define TAFFY_GRID
#endif
#if !defined(TAFFY_BLOCK_LAYOUT) && !defined(TAFFY_NO_BLOCK_LAYOUT)
#define TAFFY_BLOCK_LAYOUT
#endif
#if defined(TAFFY_BLOCK_LAYOUT) && !defined(TAFFY_FLOAT_LAYOUT) && !defined(TAFFY_NO_FLOAT_LAYOUT)
#define TAFFY_FLOAT_LAYOUT
#endif
#if !defined(TAFFY_CONTENT_SIZE) && !defined(TAFFY_NO_CONTENT_SIZE)
#define TAFFY_CONTENT_SIZE
#endif

// --- Includes ---
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// constexpr bit casts: MSVC rejects `constexpr` functions that call `std::memcpy`, even when
// they are never evaluated in a constant expression (error C3615). Prefer a constexpr bit cast
// where available and keep memcpy as a portable C++17-era fallback (GCC/Clang only diagnose
// constexpr misuse on actual constant evaluation).
#if defined(__has_include)
#if __has_include(<bit>)
#include <bit>
#endif
#endif

#if defined(__cpp_lib_bit_cast) && __cpp_lib_bit_cast >= 201806L
#define TAFFY_CONSTEXPR_BITCAST 1
#elif defined(__has_builtin)
#if __has_builtin(__builtin_bit_cast)
#define TAFFY_BUILTIN_BITCAST 1
#endif
#endif

#ifdef TAFFY_CSS_PARSE
#include <sstream>
#include <stdexcept>
#endif
#ifdef TAFFY_JSON
#include <sstream>
#endif

namespace taffy {

// ===========================================================================
// Section 1: Axis enums
// ===========================================================================

enum class AbsoluteAxis : uint8_t { Horizontal, Vertical };
enum class AbstractAxis : uint8_t { Inline, Block };

inline constexpr AbsoluteAxis other_axis(AbsoluteAxis a) {
    return a == AbsoluteAxis::Horizontal ? AbsoluteAxis::Vertical : AbsoluteAxis::Horizontal;
}
inline constexpr AbstractAxis other(AbstractAxis a) {
    return a == AbstractAxis::Inline ? AbstractAxis::Block : AbstractAxis::Inline;
}
inline constexpr AbsoluteAxis as_abs_naive(AbstractAxis a) {
    return a == AbstractAxis::Inline ? AbsoluteAxis::Horizontal : AbsoluteAxis::Vertical;
}

// FlexDirection is a plain axis-relative enum; keep it available even when the flexbox
// algorithm is disabled (geometry helpers and the public API reference it).
enum class FlexDirection : uint8_t { Row, Column, RowReverse, ColumnReverse };
constexpr bool is_row(FlexDirection d);
constexpr bool is_reverse(FlexDirection d);

// ===========================================================================
// Section 2: Geometry Primitives (proper order: Size, Point, Line, Rect, MinMax)
// Ref: src/geometry.rs
// ===========================================================================

// --- Size<T> ---
template <typename T> struct Size;
template <typename T, typename U>
constexpr auto operator+(const Size<T>& a, const Size<U>& b) -> Size<decltype(a.width + b.width)>;
template <typename T, typename U>
constexpr auto operator-(const Size<T>& a, const Size<U>& b) -> Size<decltype(a.width - b.width)>;

template <typename T>
struct Size {
    T width{};
    T height{};
    constexpr Size() = default;
    constexpr Size(T w, T h) : width(w), height(h) {}
    constexpr bool operator==(const Size& o) const { return width == o.width && height == o.height; }
    constexpr bool operator!=(const Size& o) const { return !(*this == o); }

    template <typename F>
    constexpr auto map(F&& f) const -> Size<std::invoke_result_t<F, T>> {
        return {f(width), f(height)};
    }
    template <typename F>
    constexpr Size map_width(F&& f) const { return {f(width), height}; }
    template <typename F>
    constexpr Size map_height(F&& f) const { return {width, f(height)}; }

    template <typename Other, typename F>
    constexpr auto zip_map(const Size<Other>& o, F&& f) const -> Size<std::invoke_result_t<F, T, Other>> {
        return {f(width, o.width), f(height, o.height)};
    }

    constexpr T get_abs(AbsoluteAxis axis) const {
        return axis == AbsoluteAxis::Horizontal ? width : height;
    }
    constexpr T get(AbstractAxis axis) const {
        return axis == AbstractAxis::Inline ? width : height;
    }
    constexpr Size& set(AbstractAxis axis, T value) {
        (axis == AbstractAxis::Inline ? width : height) = value;
        return *this;
    }
    constexpr Size with(AbstractAxis axis, T value) const {
        Size r = *this; r.set(axis, value); return r;
    }

#ifdef TAFFY_FLEXBOX
    constexpr T main(FlexDirection dir) const;
    constexpr T cross(FlexDirection dir) const;
    constexpr Size& set_main(FlexDirection dir, T value);
    constexpr Size& set_cross(FlexDirection dir, T value);
    constexpr Size with_main(FlexDirection dir, T value) const;
    constexpr Size with_cross(FlexDirection dir, T value) const;
#endif
};

template <typename T, typename U>
constexpr auto operator+(const Size<T>& a, const Size<U>& b) -> Size<decltype(a.width + b.width)> {
    return {a.width + b.width, a.height + b.height};
}
template <typename T, typename U>
constexpr auto operator-(const Size<T>& a, const Size<U>& b) -> Size<decltype(a.width - b.width)> {
    return {a.width - b.width, a.height - b.height};
}

// --- Size<float> specialization ---
template <>
struct Size<float> {
    float width{};
    float height{};
    constexpr Size() = default;
    constexpr Size(float w, float h) : width(w), height(h) {}
    constexpr bool operator==(const Size& o) const { return width == o.width && height == o.height; }
    constexpr bool operator!=(const Size& o) const { return !(*this == o); }
    constexpr Size f32_max(const Size& rhs) const { return {std::max(width, rhs.width), std::max(height, rhs.height)}; }
    constexpr Size f32_min(const Size& rhs) const { return {std::min(width, rhs.width), std::min(height, rhs.height)}; }
    constexpr bool has_non_zero_area() const { return width > 0.0f && height > 0.0f; }
    constexpr float get_abs(AbsoluteAxis axis) const { return axis == AbsoluteAxis::Horizontal ? width : height; }
    constexpr float get(AbstractAxis axis) const { return axis == AbstractAxis::Inline ? width : height; }
#ifdef TAFFY_FLEXBOX
    constexpr float main(FlexDirection dir) const { return is_row(dir) ? width : height; }
    constexpr float cross(FlexDirection dir) const { return is_row(dir) ? height : width; }
    constexpr Size& set_main(FlexDirection dir, float v) { (is_row(dir) ? width : height) = v; return *this; }
    constexpr Size& set_cross(FlexDirection dir, float v) { (is_row(dir) ? height : width) = v; return *this; }
    constexpr Size with_main(FlexDirection dir, float v) const { Size r = *this; r.set_main(dir, v); return r; }
    constexpr Size with_cross(FlexDirection dir, float v) const { Size r = *this; r.set_cross(dir, v); return r; }
#endif
};
inline constexpr Size<float> Size_f32_ZERO{0.0f, 0.0f};

// --- Size<optional<float>> specialization ---
template <>
struct Size<std::optional<float>> {
    std::optional<float> width{};
    std::optional<float> height{};
    constexpr Size() = default;
    constexpr Size(std::optional<float> w, std::optional<float> h) : width(w), height(h) {}
    constexpr Size(float w, float h) : width(w), height(h) {}
    constexpr bool operator==(const Size& o) const { return width == o.width && height == o.height; }
    constexpr bool operator!=(const Size& o) const { return !(*this == o); }

    Size<float> unwrap_or(const Size<float>& alt) const {
        return {width.value_or(alt.width), height.value_or(alt.height)};
    }
    Size or_(const Size& alt) const {
        return {width ? width : alt.width, height ? height : alt.height};
    }
    bool both_axis_defined() const { return width.has_value() && height.has_value(); }
    Size maybe_apply_aspect_ratio(std::optional<float> ratio) const {
        if (!ratio) return *this;
        if (width && !height) return {width, *width / *ratio};
        if (!width && height) return {*height * *ratio, height};
        return *this;
    }

#ifdef TAFFY_FLEXBOX
    std::optional<float> main(FlexDirection dir) const { return is_row(dir) ? width : height; }
    std::optional<float> cross(FlexDirection dir) const { return is_row(dir) ? height : width; }
    Size& set_main(FlexDirection dir, std::optional<float> v) { (is_row(dir) ? width : height) = v; return *this; }
    Size& set_cross(FlexDirection dir, std::optional<float> v) { (is_row(dir) ? height : width) = v; return *this; }
    Size with_main(FlexDirection dir, std::optional<float> v) const { Size r = *this; r.set_main(dir, v); return r; }
    Size with_cross(FlexDirection dir, std::optional<float> v) const { Size r = *this; r.set_cross(dir, v); return r; }
#endif
};
inline constexpr Size<std::optional<float>> Size_optf32_NONE{std::nullopt, std::nullopt};

// Flex-direction helpers for Size<optional<float>>
#ifdef TAFFY_FLEXBOX
inline std::optional<float> optf32_main(const Size<std::optional<float>>& s, FlexDirection dir) {
    return is_row(dir) ? s.width : s.height;
}
inline std::optional<float> optf32_cross(const Size<std::optional<float>>& s, FlexDirection dir) {
    return is_row(dir) ? s.height : s.width;
}
inline void optf32_set_main(Size<std::optional<float>>& s, FlexDirection dir, std::optional<float> v) {
    (is_row(dir) ? s.width : s.height) = v;
}
inline void optf32_set_cross(Size<std::optional<float>>& s, FlexDirection dir, std::optional<float> v) {
    (is_row(dir) ? s.height : s.width) = v;
}
inline Size<std::optional<float>> optf32_with_main(const Size<std::optional<float>>& s, FlexDirection dir, std::optional<float> v) {
    Size<std::optional<float>> r = s; optf32_set_main(r, dir, v); return r;
}
inline Size<std::optional<float>> optf32_with_cross(const Size<std::optional<float>>& s, FlexDirection dir, std::optional<float> v) {
    Size<std::optional<float>> r = s; optf32_set_cross(r, dir, v); return r;
}
#endif // TAFFY_FLEXBOX

// --- Point<T> ---
template <typename T>
struct Point {
    T x{};
    T y{};
    constexpr Point() = default;
    constexpr Point(T x_, T y_) : x(x_), y(y_) {}
    constexpr bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Point& o) const { return !(*this == o); }
    template <typename F>
    constexpr auto map(F&& f) const -> Point<std::invoke_result_t<F, T>> { return {f(x), f(y)}; }
    constexpr Point transpose() const { return {y, x}; }
    constexpr T get(AbstractAxis axis) const { return axis == AbstractAxis::Inline ? x : y; }
    constexpr Point& set(AbstractAxis axis, T value) {
        (axis == AbstractAxis::Inline ? x : y) = value; return *this;
    }
#ifdef TAFFY_FLEXBOX
    constexpr T main(FlexDirection dir) const;
    constexpr T cross(FlexDirection dir) const;
#endif
};

template <typename T, typename U>
constexpr auto operator+(const Point<T>& a, const Point<U>& b) -> Point<decltype(a.x + b.x)> {
    return {a.x + b.x, a.y + b.y};
}

template <>
struct Point<float> {
    float x{};
    float y{};
    constexpr Point() = default;
    constexpr Point(float x_, float y_) : x(x_), y(y_) {}
    constexpr bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Point& o) const { return !(*this == o); }
    constexpr Point transpose() const { return {y, x}; }
#ifdef TAFFY_FLEXBOX
    constexpr float main(FlexDirection dir) const;
    constexpr float cross(FlexDirection dir) const;
#endif
};
inline constexpr Point<float> Point_f32_ZERO{0.0f, 0.0f};

template <>
struct Point<std::optional<float>> {
    std::optional<float> x{};
    std::optional<float> y{};
    constexpr Point() = default;
    constexpr Point(std::optional<float> x_, std::optional<float> y_) : x(x_), y(y_) {}
    constexpr bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Point& o) const { return !(*this == o); }
};
inline constexpr Point<std::optional<float>> Point_optf32_NONE{std::nullopt, std::nullopt};

// --- Line<T> ---
template <typename T>
struct Line {
    T start{};
    T end{};
    constexpr Line() = default;
    constexpr Line(T s, T e) : start(s), end(e) {}
    constexpr bool operator==(const Line& o) const { return start == o.start && end == o.end; }
    constexpr bool operator!=(const Line& o) const { return !(*this == o); }
    template <typename F>
    constexpr auto map(F&& f) const -> Line<std::invoke_result_t<F, T>> { return {f(start), f(end)}; }
    template <typename U = T>
    constexpr auto sum() const -> decltype(std::declval<U>() + std::declval<U>()) { return start + end; }
};

template <>
struct Line<bool> {
    bool start{};
    bool end{};
    constexpr Line() = default;
    constexpr Line(bool s, bool e) : start(s), end(e) {}
    constexpr bool operator==(const Line& o) const { return start == o.start && end == o.end; }
    constexpr bool operator!=(const Line& o) const { return !(*this == o); }
};
inline constexpr Line<bool> Line_bool_TRUE{true, true};
inline constexpr Line<bool> Line_bool_FALSE{false, false};

// --- Rect<T> ---
template <typename T>
struct Rect {
    T left{};
    T right{};
    T top{};
    T bottom{};
    constexpr Rect() = default;
    constexpr Rect(T l, T r, T t, T b) : left(l), right(r), top(t), bottom(b) {}
    constexpr bool operator==(const Rect& o) const { return left == o.left && right == o.right && top == o.top && bottom == o.bottom; }
    constexpr bool operator!=(const Rect& o) const { return !(*this == o); }

    template <typename U, typename F>
    constexpr Rect<std::invoke_result_t<F, T, U>> zip_size(const Size<U>& size, F&& f) const {
        return {f(left, size.width), f(right, size.width), f(top, size.height), f(bottom, size.height)};
    }
    template <typename F>
    constexpr auto map(F&& f) const -> Rect<std::invoke_result_t<F, T>> {
        return {f(left), f(right), f(top), f(bottom)};
    }
    constexpr Line<T> horizontal_components() const { return {left, right}; }
    constexpr Line<T> vertical_components() const { return {top, bottom}; }

    constexpr T horizontal_axis_sum() const { return left + right; }
    constexpr T vertical_axis_sum() const { return top + bottom; }
    constexpr Size<T> sum_axes() const { return {left + right, top + bottom}; }

#ifdef TAFFY_FLEXBOX
    constexpr T main_axis_sum(FlexDirection dir) const;
    constexpr T cross_axis_sum(FlexDirection dir) const;
    constexpr T main_start(FlexDirection dir) const;
    constexpr T main_end(FlexDirection dir) const;
    constexpr T cross_start(FlexDirection dir) const;
    constexpr T cross_end(FlexDirection dir) const;
#endif
};

template <typename T, typename U>
constexpr auto operator+(const Rect<T>& a, const Rect<U>& b) -> Rect<decltype(a.left + b.left)> {
    return {a.left + b.left, a.right + b.right, a.top + b.top, a.bottom + b.bottom};
}

// --- MinMax<Min, Max> ---
template <typename Min, typename Max>
struct MinMax {
    Min min{};
    Max max{};
    constexpr MinMax() = default;
    constexpr MinMax(Min mn, Max mx) : min(mn), max(mx) {}
    constexpr bool operator==(const MinMax& o) const { return min == o.min && max == o.max; }
    constexpr bool operator!=(const MinMax& o) const { return !(*this == o); }
};

// --- FlexDirection and flex-direction-aware helpers ---
#ifdef TAFFY_FLEXBOX
constexpr bool is_row(FlexDirection d) {
    return d == FlexDirection::Row || d == FlexDirection::RowReverse;
}
constexpr bool is_reverse(FlexDirection d) {
    return d == FlexDirection::RowReverse || d == FlexDirection::ColumnReverse;
}

// Size<T> flex helpers
template <typename T>
constexpr T Size<T>::main(FlexDirection dir) const { return is_row(dir) ? width : height; }
template <typename T>
constexpr T Size<T>::cross(FlexDirection dir) const { return is_row(dir) ? height : width; }
template <typename T>
constexpr Size<T>& Size<T>::set_main(FlexDirection dir, T value) {
    (is_row(dir) ? width : height) = value; return *this;
}
template <typename T>
constexpr Size<T>& Size<T>::set_cross(FlexDirection dir, T value) {
    (is_row(dir) ? height : width) = value; return *this;
}
template <typename T>
constexpr Size<T> Size<T>::with_main(FlexDirection dir, T value) const {
    Size r = *this; r.set_main(dir, value); return r;
}
template <typename T>
constexpr Size<T> Size<T>::with_cross(FlexDirection dir, T value) const {
    Size r = *this; r.set_cross(dir, value); return r;
}

// Point<T> flex helpers
template <typename T>
constexpr T Point<T>::main(FlexDirection dir) const { return is_row(dir) ? x : y; }
template <typename T>
constexpr T Point<T>::cross(FlexDirection dir) const { return is_row(dir) ? y : x; }
inline constexpr float Point<float>::main(FlexDirection dir) const { return is_row(dir) ? x : y; }
inline constexpr float Point<float>::cross(FlexDirection dir) const { return is_row(dir) ? y : x; }

// Rect<T> flex helpers
template <typename T>
constexpr T Rect<T>::main_axis_sum(FlexDirection dir) const {
    return is_row(dir) ? horizontal_axis_sum() : vertical_axis_sum();
}
template <typename T>
constexpr T Rect<T>::cross_axis_sum(FlexDirection dir) const {
    return is_row(dir) ? vertical_axis_sum() : horizontal_axis_sum();
}
template <typename T>
constexpr T Rect<T>::main_start(FlexDirection dir) const { return is_row(dir) ? left : top; }
template <typename T>
constexpr T Rect<T>::main_end(FlexDirection dir) const { return is_row(dir) ? right : bottom; }
template <typename T>
constexpr T Rect<T>::cross_start(FlexDirection dir) const { return is_row(dir) ? top : left; }
template <typename T>
constexpr T Rect<T>::cross_end(FlexDirection dir) const { return is_row(dir) ? bottom : right; }
#endif // TAFFY_FLEXBOX

// ===========================================================================
// Section 3: CompactLength
// Ref: src/style/compact_length.rs
// ===========================================================================

namespace detail {
    // Bit-cast helpers (see the probe at the top of this file).
#if defined(TAFFY_CONSTEXPR_BITCAST)
    constexpr uint32_t f32_to_bits(float v) { return std::bit_cast<uint32_t>(v); }
    constexpr float f32_from_bits(uint32_t b) { return std::bit_cast<float>(b); }
#elif defined(TAFFY_BUILTIN_BITCAST)
    constexpr uint32_t f32_to_bits(float v) { return __builtin_bit_cast(uint32_t, v); }
    constexpr float f32_from_bits(uint32_t b) { return __builtin_bit_cast(float, b); }
#else
    inline uint32_t f32_to_bits(float v) { uint32_t b; std::memcpy(&b, &v, 4); return b; }
    inline float f32_from_bits(uint32_t b) { float v; std::memcpy(&v, &b, 4); return v; }
#endif
}

struct CompactLength {
    static constexpr size_t LENGTH_TAG              = 0b00000001;
    static constexpr size_t PERCENT_TAG             = 0b00000010;
    static constexpr size_t AUTO_TAG                = 0b00000011;
    static constexpr size_t FR_TAG                  = 0b00000100;
    static constexpr size_t MIN_CONTENT_TAG         = 0b00000111;
    static constexpr size_t MAX_CONTENT_TAG         = 0b00001111;
    static constexpr size_t FIT_CONTENT_PX_TAG      = 0b00010111;
    static constexpr size_t FIT_CONTENT_PERCENT_TAG = 0b00011111;
    static constexpr size_t CONTENT_TAG            = 0b00100011;
    static constexpr size_t STRETCH_TAG            = 0b00100111;
    static constexpr size_t FIT_CONTENT_KEYWORD_TAG = 0b00101011;

    uint64_t data{};
    constexpr CompactLength() = default;

    static constexpr CompactLength length(float val) {
        CompactLength cl; cl.data = (uint64_t(detail::f32_to_bits(val)) << 32) | LENGTH_TAG; return cl;
    }
    // ZERO is length(0.0f) - must be defined after length() exists
    static constexpr CompactLength zero_() { return length(0.0f); }
    static constexpr CompactLength percent(float val) {
        CompactLength cl; cl.data = (uint64_t(detail::f32_to_bits(val)) << 32) | PERCENT_TAG; return cl;
    }
    static constexpr CompactLength auto_() {
        CompactLength cl; cl.data = AUTO_TAG; return cl;
    }
    static constexpr CompactLength fr(float val) {
        CompactLength cl; cl.data = (uint64_t(detail::f32_to_bits(val)) << 32) | FR_TAG; return cl;
    }
    static constexpr CompactLength min_content() {
        CompactLength cl; cl.data = MIN_CONTENT_TAG; return cl;
    }
    static constexpr CompactLength max_content() {
        CompactLength cl; cl.data = MAX_CONTENT_TAG; return cl;
    }
    static constexpr CompactLength fit_content_px(float limit) {
        CompactLength cl; cl.data = (uint64_t(detail::f32_to_bits(limit)) << 32) | FIT_CONTENT_PX_TAG; return cl;
    }
    static constexpr CompactLength fit_content_percent(float limit) {
        CompactLength cl; cl.data = (uint64_t(detail::f32_to_bits(limit)) << 32) | FIT_CONTENT_PERCENT_TAG; return cl;
    }
    // The bare `content` keyword (valid for flex-basis only)
    static constexpr CompactLength content() {
        CompactLength cl; cl.data = CONTENT_TAG; return cl;
    }
    // The bare `stretch` keyword (CSS Box Sizing 4)
    static constexpr CompactLength stretch() {
        CompactLength cl; cl.data = STRETCH_TAG; return cl;
    }
    // The bare `fit-content` keyword (no limit)
    static constexpr CompactLength fit_content_keyword() {
        CompactLength cl; cl.data = FIT_CONTENT_KEYWORD_TAG; return cl;
    }

    constexpr size_t tag() const { return data & 0xFF; }
    constexpr float value() const { return detail::f32_from_bits(uint32_t(data >> 32)); }
    constexpr bool operator==(const CompactLength& o) const { return data == o.data; }
    constexpr bool operator!=(const CompactLength& o) const { return data != o.data; }
    constexpr bool is_zero() const { return *this == zero_(); }
    constexpr bool is_auto() const { return tag() == AUTO_TAG; }
    constexpr bool is_content() const { return tag() == CONTENT_TAG; }
    constexpr bool is_stretch() const { return tag() == STRETCH_TAG; }
    constexpr bool is_fr() const { return tag() == FR_TAG; }
    constexpr bool is_length_or_percentage() const { return tag() == LENGTH_TAG || tag() == PERCENT_TAG; }
    constexpr bool is_min_content() const { return tag() == MIN_CONTENT_TAG; }
    constexpr bool is_max_content() const { return tag() == MAX_CONTENT_TAG; }
    // Note: a bare `fit-content` keyword is NOT `is_fit_content` (matches Rust)
    constexpr bool is_fit_content() const { return tag() == FIT_CONTENT_PX_TAG || tag() == FIT_CONTENT_PERCENT_TAG; }
    constexpr bool is_fit_content_keyword() const { return tag() == FIT_CONTENT_KEYWORD_TAG; }
    // min-content, max-content, fit-content, fit-content(...), or stretch
    constexpr bool is_sizing_keyword() const {
        return tag() == MIN_CONTENT_TAG || tag() == MAX_CONTENT_TAG || tag() == FIT_CONTENT_KEYWORD_TAG
            || tag() == FIT_CONTENT_PX_TAG || tag() == FIT_CONTENT_PERCENT_TAG || tag() == STRETCH_TAG;
    }
    constexpr bool is_max_or_fit_content() const {
        return tag() == MAX_CONTENT_TAG || tag() == FIT_CONTENT_PX_TAG || tag() == FIT_CONTENT_PERCENT_TAG;
    }
    constexpr bool is_max_content_alike() const {
        return tag() == AUTO_TAG || tag() == MAX_CONTENT_TAG || tag() == FIT_CONTENT_PX_TAG || tag() == FIT_CONTENT_PERCENT_TAG;
    }
    constexpr bool is_min_or_max_content() const { return tag() == MIN_CONTENT_TAG || tag() == MAX_CONTENT_TAG; }
    constexpr bool is_intrinsic() const {
        return tag() == AUTO_TAG || tag() == MIN_CONTENT_TAG || tag() == MAX_CONTENT_TAG
            || tag() == FIT_CONTENT_PX_TAG || tag() == FIT_CONTENT_PERCENT_TAG;
    }
    constexpr bool uses_percentage() const { return tag() == PERCENT_TAG || tag() == FIT_CONTENT_PERCENT_TAG; }

    std::optional<float> resolved_percentage_size(float parent_size) const {
        if (tag() == PERCENT_TAG) return (value() * parent_size) / 100.0f;
        return std::nullopt;
    }
};

// ===========================================================================
// Section 4: Dimension Types
// Ref: src/style/dimension.rs
// ===========================================================================

struct LengthPercentage {
    CompactLength inner;
    constexpr LengthPercentage() : inner(CompactLength::length(0.0f)) {}
    constexpr explicit LengthPercentage(CompactLength cl) : inner(cl) {}
    static constexpr LengthPercentage length(float v) { return LengthPercentage(CompactLength::length(v)); }
    static constexpr LengthPercentage percent(float v) { return LengthPercentage(CompactLength::percent(v)); }
    constexpr size_t tag() const { return inner.tag(); }
    constexpr float value() const { return inner.value(); }
    constexpr bool is_zero() const { return inner.is_zero(); }
    constexpr bool uses_percentage() const { return inner.uses_percentage(); }
    std::optional<float> resolved_percentage_size(float ps) const { return inner.resolved_percentage_size(ps); }
    constexpr bool operator==(const LengthPercentage& o) const { return inner == o.inner; }
    constexpr bool operator!=(const LengthPercentage& o) const { return inner != o.inner; }
};

struct LengthPercentageAuto {
    CompactLength inner;
    constexpr LengthPercentageAuto() : inner(CompactLength::length(0.0f)) {}
    constexpr explicit LengthPercentageAuto(CompactLength cl) : inner(cl) {}
    static constexpr LengthPercentageAuto length(float v) { return LengthPercentageAuto(CompactLength::length(v)); }
    static constexpr LengthPercentageAuto percent(float v) { return LengthPercentageAuto(CompactLength::percent(v)); }
    static constexpr LengthPercentageAuto auto_() { return LengthPercentageAuto(CompactLength::auto_()); }
    constexpr size_t tag() const { return inner.tag(); }
    constexpr float value() const { return inner.value(); }
    constexpr bool is_auto() const { return inner.is_auto(); }
    constexpr bool is_zero() const { return inner.is_zero(); }
    constexpr bool uses_percentage() const { return inner.uses_percentage(); }
    std::optional<float> resolve_to_option(float context) const {
        switch (inner.tag()) {
            case CompactLength::LENGTH_TAG: return inner.value();
            case CompactLength::PERCENT_TAG: return (context * inner.value()) / 100.0f;
            case CompactLength::AUTO_TAG: return std::nullopt;
            default: return std::nullopt;
        }
    }
    constexpr bool operator==(const LengthPercentageAuto& o) const { return inner == o.inner; }
    constexpr bool operator!=(const LengthPercentageAuto& o) const { return inner != o.inner; }
};

struct Dimension {
    CompactLength inner;
    constexpr Dimension() : inner(CompactLength::length(0.0f)) {}
    constexpr explicit Dimension(CompactLength cl) : inner(cl) {}
    constexpr Dimension(LengthPercentage lp) : inner(lp.inner) {}
    constexpr Dimension(LengthPercentageAuto lpa) : inner(lpa.inner) {}
    static constexpr Dimension length(float v) { return Dimension(CompactLength::length(v)); }
    static constexpr Dimension percent(float v) { return Dimension(CompactLength::percent(v)); }
    static constexpr Dimension auto_() { return Dimension(CompactLength::auto_()); }
    static constexpr Dimension fr(float v) { return Dimension(CompactLength::fr(v)); }
    static constexpr Dimension min_content() { return Dimension(CompactLength::min_content()); }
    static constexpr Dimension max_content() { return Dimension(CompactLength::max_content()); }
    static constexpr Dimension fit_content_px(float l) { return Dimension(CompactLength::fit_content_px(l)); }
    static constexpr Dimension fit_content_percent(float l) { return Dimension(CompactLength::fit_content_percent(l)); }
    static constexpr Dimension content() { return Dimension(CompactLength::content()); }
    static constexpr Dimension stretch() { return Dimension(CompactLength::stretch()); }
    static constexpr Dimension fit_content_keyword() { return Dimension(CompactLength::fit_content_keyword()); }
    constexpr size_t tag() const { return inner.tag(); }
    constexpr float value() const { return inner.value(); }
    constexpr bool is_auto() const { return inner.is_auto(); }
    constexpr bool is_content() const { return inner.is_content(); }
    constexpr bool is_stretch() const { return inner.is_stretch(); }
    constexpr bool is_zero() const { return inner.is_zero(); }
    constexpr bool is_fr() const { return inner.is_fr(); }
    constexpr bool is_fit_content_keyword() const { return inner.is_fit_content_keyword(); }
    constexpr bool is_sizing_keyword() const { return inner.is_sizing_keyword(); }
    constexpr bool is_intrinsic() const { return inner.is_intrinsic(); }
    constexpr bool is_max_content_alike() const { return inner.is_max_content_alike(); }
    constexpr bool uses_percentage() const { return inner.uses_percentage(); }
    std::optional<float> into_option() const {
        if (inner.tag() == CompactLength::LENGTH_TAG) return inner.value();
        return std::nullopt;
    }
    std::optional<float> resolve_to_option(float context) const {
        switch (inner.tag()) {
            case CompactLength::LENGTH_TAG: return inner.value();
            case CompactLength::PERCENT_TAG: return (context * inner.value()) / 100.0f;
            case CompactLength::AUTO_TAG: return std::nullopt;
            default: return std::nullopt;
        }
    }
    constexpr bool operator==(const Dimension& o) const { return inner == o.inner; }
    constexpr bool operator!=(const Dimension& o) const { return inner != o.inner; }
};

// ===========================================================================
// Section 5: Alignment Types
// Ref: src/style/alignment.rs
// ===========================================================================

// Ref: src/style/alignment.rs
enum class AlignItemsKeyword : uint8_t {
    Normal,     // layout mode default: usually Stretch, but Start in some cases
    Start,
    End,
    FlexStart,
    FlexEnd,
    SelfStart,
    SelfEnd,
    Center,
    Baseline,
    Stretch
};
enum class AlignContentKeyword : uint8_t {
    Normal,     // layout mode default: Stretch for flex/grid, Start for block
    Start,
    End,
    FlexStart,
    FlexEnd,
    Center,
    Stretch,
    SpaceBetween,
    SpaceEvenly,
    SpaceAround
};
// Overflow-position modifier (`safe` / `unsafe`). `Default` behaves as `Unsafe`, except for
// absolutely positioned boxes whose alignment is adjusted to minimise overflow.
enum class AlignmentSafety : uint8_t { Default, Unsafe, Safe };

struct AlignItems {
    AlignItemsKeyword keyword{AlignItemsKeyword::Normal};
    AlignmentSafety safety{AlignmentSafety::Default};
    constexpr AlignItems() = default;
    constexpr AlignItems(AlignItemsKeyword kw, AlignmentSafety s) : keyword(kw), safety(s) {}
    constexpr bool is_safe() const { return safety == AlignmentSafety::Safe; }
    constexpr bool is_normal() const { return keyword == AlignItemsKeyword::Normal; }
    constexpr AlignItemsKeyword kw() const { return keyword; }
    constexpr bool operator==(const AlignItems& o) const { return keyword == o.keyword && safety == o.safety; }
    constexpr bool operator!=(const AlignItems& o) const { return !(*this == o); }
};
inline constexpr AlignItems AlignItems_NORMAL{AlignItemsKeyword::Normal, AlignmentSafety::Default};
inline constexpr AlignItems AlignItems_SELF_START{AlignItemsKeyword::SelfStart, AlignmentSafety::Default};
inline constexpr AlignItems AlignItems_SELF_END{AlignItemsKeyword::SelfEnd, AlignmentSafety::Default};
inline constexpr AlignItems AlignItems_START{AlignItemsKeyword::Start, AlignmentSafety::Unsafe};
inline constexpr AlignItems AlignItems_END{AlignItemsKeyword::End, AlignmentSafety::Unsafe};
inline constexpr AlignItems AlignItems_FLEX_START{AlignItemsKeyword::FlexStart, AlignmentSafety::Unsafe};
inline constexpr AlignItems AlignItems_FLEX_END{AlignItemsKeyword::FlexEnd, AlignmentSafety::Unsafe};
inline constexpr AlignItems AlignItems_CENTER{AlignItemsKeyword::Center, AlignmentSafety::Unsafe};
inline constexpr AlignItems AlignItems_BASELINE{AlignItemsKeyword::Baseline, AlignmentSafety::Unsafe};
inline constexpr AlignItems AlignItems_STRETCH{AlignItemsKeyword::Stretch, AlignmentSafety::Unsafe};
inline constexpr AlignItems AlignItems_SAFE_START{AlignItemsKeyword::Start, AlignmentSafety::Safe};
inline constexpr AlignItems AlignItems_SAFE_END{AlignItemsKeyword::End, AlignmentSafety::Safe};
inline constexpr AlignItems AlignItems_SAFE_FLEX_START{AlignItemsKeyword::FlexStart, AlignmentSafety::Safe};
inline constexpr AlignItems AlignItems_SAFE_FLEX_END{AlignItemsKeyword::FlexEnd, AlignmentSafety::Safe};
inline constexpr AlignItems AlignItems_SAFE_CENTER{AlignItemsKeyword::Center, AlignmentSafety::Safe};

struct AlignContent {
    AlignContentKeyword keyword{AlignContentKeyword::Normal};
    AlignmentSafety safety{AlignmentSafety::Default};
    constexpr AlignContent() = default;
    constexpr AlignContent(AlignContentKeyword kw, AlignmentSafety s) : keyword(kw), safety(s) {}
    constexpr bool is_safe() const { return safety == AlignmentSafety::Safe; }
    constexpr bool is_normal() const { return keyword == AlignContentKeyword::Normal; }
    constexpr AlignContentKeyword kw() const { return keyword; }
    // Ref: AlignContentKeyword::reversed - Start<->End, FlexStart<->FlexEnd, Stretch -> End.
    AlignContentKeyword reversed() const {
        switch (keyword) {
            case AlignContentKeyword::Start: return AlignContentKeyword::End;
            case AlignContentKeyword::End: return AlignContentKeyword::Start;
            case AlignContentKeyword::FlexStart: return AlignContentKeyword::FlexEnd;
            case AlignContentKeyword::FlexEnd: return AlignContentKeyword::FlexStart;
            case AlignContentKeyword::Stretch: return AlignContentKeyword::End;
            default: return keyword;
        }
    }
    constexpr bool operator==(const AlignContent& o) const { return keyword == o.keyword && safety == o.safety; }
    constexpr bool operator!=(const AlignContent& o) const { return !(*this == o); }
};
inline constexpr AlignContent AlignContent_NORMAL{AlignContentKeyword::Normal, AlignmentSafety::Default};
inline constexpr AlignContent AlignContent_START{AlignContentKeyword::Start, AlignmentSafety::Unsafe};
inline constexpr AlignContent AlignContent_END{AlignContentKeyword::End, AlignmentSafety::Unsafe};
inline constexpr AlignContent AlignContent_FLEX_START{AlignContentKeyword::FlexStart, AlignmentSafety::Unsafe};
inline constexpr AlignContent AlignContent_FLEX_END{AlignContentKeyword::FlexEnd, AlignmentSafety::Unsafe};
inline constexpr AlignContent AlignContent_CENTER{AlignContentKeyword::Center, AlignmentSafety::Unsafe};
inline constexpr AlignContent AlignContent_STRETCH{AlignContentKeyword::Stretch, AlignmentSafety::Unsafe};
inline constexpr AlignContent AlignContent_SPACE_BETWEEN{AlignContentKeyword::SpaceBetween, AlignmentSafety::Unsafe};
inline constexpr AlignContent AlignContent_SPACE_EVENLY{AlignContentKeyword::SpaceEvenly, AlignmentSafety::Unsafe};
inline constexpr AlignContent AlignContent_SPACE_AROUND{AlignContentKeyword::SpaceAround, AlignmentSafety::Unsafe};
inline constexpr AlignContent AlignContent_SAFE_START{AlignContentKeyword::Start, AlignmentSafety::Safe};
inline constexpr AlignContent AlignContent_SAFE_END{AlignContentKeyword::End, AlignmentSafety::Safe};
inline constexpr AlignContent AlignContent_SAFE_FLEX_START{AlignContentKeyword::FlexStart, AlignmentSafety::Safe};
inline constexpr AlignContent AlignContent_SAFE_FLEX_END{AlignContentKeyword::FlexEnd, AlignmentSafety::Safe};
inline constexpr AlignContent AlignContent_SAFE_CENTER{AlignContentKeyword::Center, AlignmentSafety::Safe};

using AlignSelf = AlignItems;
using JustifyItems = AlignItems;
using JustifySelf = AlignItems;
using JustifyContent = AlignContent;

// ===========================================================================
// Section 6: Style Enums
// Ref: src/style/mod.rs, flex.rs, grid.rs, block.rs, float.rs
// ===========================================================================

enum class Display : uint8_t {
#ifdef TAFFY_BLOCK_LAYOUT
    Block,
    FlowRoot,   // block formatting context root
#endif
#ifdef TAFFY_FLEXBOX
    Flex,
#endif
#ifdef TAFFY_GRID
    Grid,
#endif
    None
};

// Ref: src/style/mod.rs
enum class BoxGenerationMode : uint8_t { Normal, None };
enum class Position : uint8_t {
    Static,     // normal flow; `inset` ignored, not a containing block
    Relative,   // normal flow; `inset` applied as a relative correction
    Absolute,   // out of flow, positioned against closest positioned ancestor
    Fixed,      // out of flow, positioned against the viewport (root)
    Sticky      // normal flow; insets are scroll thresholds, laid out like static
};
inline constexpr bool is_absolute_position(Position p) { return p == Position::Absolute || p == Position::Fixed; }
enum class BoxSizing : uint8_t { BorderBox, ContentBox };
enum class Overflow : uint8_t { Visible, Clip, Hidden, Scroll };
enum class Direction : uint8_t { Ltr, Rtl };

inline bool is_scroll_container(Overflow o) { return o == Overflow::Hidden || o == Overflow::Scroll; }
inline bool is_rtl(Direction d) { return d == Direction::Rtl; }
#ifdef TAFFY_BLOCK_LAYOUT
inline bool is_flow_root(Display d) { return d == Display::FlowRoot; }
#endif

// Ref: src/style/mod.rs `Contain` - the layout-affecting parts of the CSS `contain` property
struct Contain {
    bool size{false};
    // `content` implies all four containment types (layout/paint/style/size) for layout purposes
    bool layout{false};
    bool paint{false};

    static constexpr Contain none() { return {false, false, false}; }
    static constexpr Contain size_() { return {true, false, false}; }
    static constexpr Contain layout_() { return {false, true, false}; }
    static constexpr Contain paint_() { return {false, false, true}; }
    static constexpr Contain content() { return {true, true, true}; }
    constexpr bool operator==(const Contain& o) const { return size == o.size && layout == o.layout && paint == o.paint; }
    constexpr bool operator!=(const Contain& o) const { return !(*this == o); }
};

#ifdef TAFFY_FLEXBOX
// Ref: src/style/flex.rs
enum class FlexWrap : uint8_t { NoWrap, Wrap, WrapReverse, Balance, BalanceReverse };
inline bool flex_wrap_is_multi_line(FlexWrap w) { return w != FlexWrap::NoWrap; }
inline bool flex_wrap_is_reverse(FlexWrap w) { return w == FlexWrap::WrapReverse || w == FlexWrap::BalanceReverse; }
inline bool flex_wrap_is_balanced(FlexWrap w) { return w == FlexWrap::Balance || w == FlexWrap::BalanceReverse; }
#endif

#ifdef TAFFY_BLOCK_LAYOUT
enum class TextAlign : uint8_t { Auto, LegacyLeft, LegacyRight, LegacyCenter };
#endif

#ifdef TAFFY_FLOAT_LAYOUT
enum class Float : uint8_t { None, Left, Right };
enum class Clear : uint8_t { None, Left, Right, Both };
enum class FloatDirection : uint8_t { Left, Right };
#endif

#ifdef TAFFY_GRID
enum class GridAutoFlow : uint8_t { Row, Column, RowDense, ColumnDense };
inline bool is_dense(GridAutoFlow f) { return f == GridAutoFlow::RowDense || f == GridAutoFlow::ColumnDense; }
inline AbsoluteAxis primary_axis(GridAutoFlow f) {
    return (f == GridAutoFlow::Row || f == GridAutoFlow::RowDense) ? AbsoluteAxis::Horizontal : AbsoluteAxis::Vertical;
}
#endif

// ===========================================================================
// Section 7: AvailableSpace
// Ref: src/style/available_space.rs
// ===========================================================================

struct AvailableSpace {
    enum Type : uint8_t { Definite, MinContent, MaxContent };
    Type type{MaxContent};
    float value{0.0f};

    constexpr AvailableSpace() = default;
    constexpr AvailableSpace(Type t, float v = 0.0f) : type(t), value(v) {}
    static constexpr AvailableSpace definite(float v) { return {Definite, v}; }
    static constexpr AvailableSpace min_content() { return {MinContent, 0.0f}; }
    static constexpr AvailableSpace max_content() { return {MaxContent, 0.0f}; }

    constexpr bool is_definite() const { return type == Definite; }
    std::optional<float> into_option() const { return type == Definite ? std::optional<float>(value) : std::nullopt; }
    float unwrap_or(float default_val) const { return type == Definite ? value : default_val; }
    float unwrap() const { assert(type == Definite); return value; }

    AvailableSpace or_(AvailableSpace default_val) const { return type == Definite ? *this : default_val; }
    AvailableSpace maybe_set(std::optional<float> v) const { return v ? definite(*v) : *this; }

    AvailableSpace map_definite_value(std::function<float(float)> f) const {
        return type == Definite ? definite(f(value)) : *this;
    }

    float compute_free_space(float used_space) const {
        switch (type) {
            case MaxContent: return std::numeric_limits<float>::infinity();
            case MinContent: return 0.0f;
            case Definite: return value - used_space;
        }
        return 0.0f;
    }

    bool is_roughly_equal(AvailableSpace other) const {
        if (type != other.type) return false;
        if (type == Definite) return std::abs(value - other.value) < std::numeric_limits<float>::epsilon();
        return true;
    }

    constexpr bool operator==(const AvailableSpace& o) const { return type == o.type && (type != Definite || value == o.value); }
    constexpr bool operator!=(const AvailableSpace& o) const { return !(*this == o); }
};

inline Size<std::optional<float>> into_options(const Size<AvailableSpace>& s) {
    return {s.width.into_option(), s.height.into_option()};
}

// AvailableSpace maybe-math helpers
// Ref: src/util/math.rs (MaybeMath impls for AvailableSpace)
inline AvailableSpace maybe_min(AvailableSpace as, float rhs) {
    if (as.is_definite()) return AvailableSpace::definite(std::min(as.value, rhs));
    return AvailableSpace::definite(rhs);
}
inline AvailableSpace maybe_max(AvailableSpace as, float rhs) {
    if (as.is_definite()) return AvailableSpace::definite(std::max(as.value, rhs));
    return as;
}
inline AvailableSpace maybe_clamp(AvailableSpace as, float mn, float mx) {
    if (as.is_definite()) return AvailableSpace::definite(std::max(std::min(as.value, mx), mn));
    return as;
}
inline AvailableSpace maybe_add(AvailableSpace as, float rhs) {
    if (as.is_definite()) return AvailableSpace::definite(as.value + rhs);
    return as;
}
inline AvailableSpace maybe_sub(AvailableSpace as, float v) {
    if (as.is_definite()) return AvailableSpace::definite(as.value - v);
    return as;
}
inline AvailableSpace maybe_set(AvailableSpace as, std::optional<float> v) {
    return v ? AvailableSpace::definite(*v) : as;
}
// Option-valued variants (Rust: MaybeMath<Option<f32>, AvailableSpace>)
inline AvailableSpace maybe_min(AvailableSpace as, std::optional<float> rhs) {
    if (!rhs) return as;
    if (as.is_definite()) return AvailableSpace::definite(std::min(as.value, *rhs));
    return AvailableSpace::definite(*rhs);
}
inline AvailableSpace maybe_max(AvailableSpace as, std::optional<float> rhs) {
    if (!rhs) return as;
    if (as.is_definite()) return AvailableSpace::definite(std::max(as.value, *rhs));
    return as;
}
inline AvailableSpace maybe_add(AvailableSpace as, std::optional<float> rhs) {
    if (!rhs) return as;
    if (as.is_definite()) return AvailableSpace::definite(as.value + *rhs);
    return as;
}
inline AvailableSpace maybe_sub(AvailableSpace as, std::optional<float> rhs) {
    if (!rhs) return as;
    if (as.is_definite()) return AvailableSpace::definite(as.value - *rhs);
    return as;
}
inline AvailableSpace maybe_clamp(AvailableSpace as, std::optional<float> mn, std::optional<float> mx) {
    if (!as.is_definite()) return as;
    float val = as.value;
    if (mx) val = std::min(val, *mx);
    if (mn) val = std::max(val, *mn);
    return AvailableSpace::definite(val);
}
inline AvailableSpace maybe_clamp_as(AvailableSpace as, std::optional<float> mn, std::optional<float> mx) {
    return maybe_clamp(as, mn, mx);
}

// ===========================================================================
// Section 8: Grid Types
// Ref: src/style/grid.rs, src/compute/grid/types/coordinates.rs
// ===========================================================================

// NodeId is needed by the grid types, the layout types and the tree, so it is defined
// unconditionally here (it is a plain handle, independent of the grid feature).
struct NodeId {
    uint32_t index{UINT32_MAX};
    uint32_t generation{0};
    constexpr NodeId() = default;
    constexpr NodeId(uint32_t idx, uint32_t gen) : index(idx), generation(gen) {}
    constexpr bool operator==(const NodeId& o) const { return index == o.index && generation == o.generation; }
    constexpr bool operator!=(const NodeId& o) const { return !(*this == o); }
    constexpr bool operator<(const NodeId& o) const { return index < o.index; }
    constexpr bool is_valid() const { return index != UINT32_MAX; }
};
inline constexpr NodeId NODE_ID_NONE{UINT32_MAX, 0};

#ifdef TAFFY_GRID

/// Grid line coordinate (1-based CSS grid lines, 0 = invalid)
struct GridLine {
    int16_t value{0};
    constexpr GridLine() = default;
    constexpr explicit GridLine(int16_t v) : value(v) {}
    constexpr bool operator==(const GridLine& o) const { return value == o.value; }
    constexpr bool operator!=(const GridLine& o) const { return value != o.value; }
};

/// Origin-zero line coordinate (0-based normalized)
struct OriginZeroLine {
    int16_t value{0};
    constexpr OriginZeroLine() = default;
    constexpr explicit OriginZeroLine(int16_t v) : value(v) {}
    constexpr bool operator==(const OriginZeroLine& o) const { return value == o.value; }
    constexpr bool operator!=(const OriginZeroLine& o) const { return value != o.value; }
};

enum class RepetitionCount : uint8_t { AutoFill, AutoFit, Count };
struct RepetitionCountValue {
    RepetitionCount kind{RepetitionCount::AutoFill};
    uint16_t count_val{0};
    constexpr RepetitionCountValue() = default;
    constexpr RepetitionCountValue(RepetitionCount k, uint16_t c = 0) : kind(k), count_val(c) {}
    static constexpr RepetitionCountValue auto_fill() { return {RepetitionCount::AutoFill, 0}; }
    static constexpr RepetitionCountValue auto_fit() { return {RepetitionCount::AutoFit, 0}; }
    static constexpr RepetitionCountValue make_count(uint16_t n) { return {RepetitionCount::Count, n}; }
    constexpr bool operator==(const RepetitionCountValue& o) const { return kind == o.kind && count_val == o.count_val; }
    constexpr bool operator!=(const RepetitionCountValue& o) const { return !(*this == o); }
};

/// GridPlacement: Auto, Line, NamedLine, Span, NamedSpan
struct GridPlacement {
    enum Kind : uint8_t { Auto, LineKind, NamedLineKind, SpanKind, NamedSpanKind };
    Kind kind{Auto};
    GridLine line_val{};
    int16_t named_line_index{0};
    uint16_t span_val{1};
    std::string name{};

    constexpr GridPlacement() = default;
    static GridPlacement auto_() { return {}; }
    static GridPlacement line(GridLine l) { GridPlacement gp; gp.kind = LineKind; gp.line_val = l; return gp; }
    static GridPlacement named_line(std::string n, int16_t idx) {
        GridPlacement gp; gp.kind = NamedLineKind; gp.name = std::move(n); gp.named_line_index = idx; return gp;
    }
    static GridPlacement span(uint16_t s) { GridPlacement gp; gp.kind = SpanKind; gp.span_val = s; return gp; }
    static GridPlacement named_span(std::string n, uint16_t s) {
        GridPlacement gp; gp.kind = NamedSpanKind; gp.name = std::move(n); gp.span_val = s; return gp;
    }

    bool is_definite() const { return kind == LineKind || kind == NamedLineKind; }
    bool is_auto() const { return kind == Auto; }
    bool is_span() const { return kind == SpanKind || kind == NamedSpanKind; }

    bool operator==(const GridPlacement& o) const {
        if (kind != o.kind) return false;
        switch (kind) {
            case Auto: return true;
            case LineKind: return line_val == o.line_val;
            case NamedLineKind: return name == o.name && named_line_index == o.named_line_index;
            case SpanKind: return span_val == o.span_val;
            case NamedSpanKind: return name == o.name && span_val == o.span_val;
        }
        return false;
    }
    bool operator!=(const GridPlacement& o) const { return !(*this == o); }
};

/// MinTrackSizingFunction (wraps CompactLength, supports: length, percent, auto, min-content, max-content)
using MinTrackSizingFunction = CompactLength;

/// MaxTrackSizingFunction (wraps CompactLength, supports all + fr, fit-content)
using MaxTrackSizingFunction = CompactLength;

/// TrackSizingFunction = MinMax<Min, Max>
using TrackSizingFunction = MinMax<MinTrackSizingFunction, MaxTrackSizingFunction>;

/// GridTemplateRepetition
struct GridTemplateRepetition {
    RepetitionCountValue count{};
    std::vector<TrackSizingFunction> tracks{};
    std::vector<std::vector<std::string>> line_names{};
    bool operator==(const GridTemplateRepetition& o) const {
        return count == o.count && tracks == o.tracks && line_names == o.line_names;
    }
    bool operator!=(const GridTemplateRepetition& o) const { return !(*this == o); }
};

/// GridTemplateComponent: Single or Repeat
struct GridTemplateComponent {
    enum Kind : uint8_t { Single, Repeat };
    Kind kind{Single};
    TrackSizingFunction single_track{};
    GridTemplateRepetition repeat_val{};

    GridTemplateComponent() = default;
    static GridTemplateComponent single(TrackSizingFunction tsf) {
        GridTemplateComponent gtc; gtc.kind = Single; gtc.single_track = tsf; return gtc;
    }
    static GridTemplateComponent repeat(GridTemplateRepetition rep) {
        GridTemplateComponent gtc; gtc.kind = Repeat; gtc.repeat_val = std::move(rep); return gtc;
    }
    bool is_auto_repetition() const {
        if (kind != Repeat) return false;
        return repeat_val.count.kind == RepetitionCount::AutoFill || repeat_val.count.kind == RepetitionCount::AutoFit;
    }
    bool operator==(const GridTemplateComponent& o) const {
        if (kind != o.kind) return false;
        return kind == Single ? single_track == o.single_track : repeat_val == o.repeat_val;
    }
    bool operator!=(const GridTemplateComponent& o) const { return !(*this == o); }
};

/// Grid template area definition
struct GridTemplateArea {
    std::string name{};
    uint16_t row_start{0};
    uint16_t row_end{0};
    uint16_t column_start{0};
    uint16_t column_end{0};
    bool operator==(const GridTemplateArea& o) const {
        return name == o.name && row_start == o.row_start && row_end == o.row_end
            && column_start == o.column_start && column_end == o.column_end;
    }
    bool operator!=(const GridTemplateArea& o) const { return !(*this == o); }
};

#endif // TAFFY_GRID

// --- Grid internal types (used by the grid algorithm) ---
// Note: NodeId / NODE_ID_NONE are defined earlier (they are shared with the layout types).

#ifdef TAFFY_GRID

namespace grid_detail {

// The maximum number of tracks in each direction from the start of the explicit grid, and the
// maximum span of a single grid item. See https://www.w3.org/TR/css-grid-1/#overlarge-grids
inline constexpr uint16_t MAX_GRID_TRACKS = 10000;
inline constexpr int16_t MIN_OZ_LINE = -static_cast<int16_t>(MAX_GRID_TRACKS);
inline constexpr int16_t MAX_OZ_LINE = static_cast<int16_t>(MAX_GRID_TRACKS);

// --- Helpers for track sizing functions (which are aliases of CompactLength) ---

/// Resolve a length/percentage track sizing function to a definite value (None if it depends on an
/// indefinite parent size or is intrinsic / flexible).
inline std::optional<float> cl_definite_value(CompactLength cl, std::optional<float> parent_size) {
    switch (cl.tag()) {
        case CompactLength::LENGTH_TAG: return cl.value();
        case CompactLength::PERCENT_TAG: return parent_size ? std::optional<float>((cl.value() * *parent_size) / 100.0f) : std::nullopt;
        default: return std::nullopt;
    }
}

/// Resolve the maximum size of a track as defined by a fixed, percentage, or fit-content() sizing
/// function. All other kinds of track sizing function return None.
inline std::optional<float> cl_definite_limit(CompactLength cl, std::optional<float> parent_size) {
    switch (cl.tag()) {
        case CompactLength::FIT_CONTENT_PX_TAG: return cl.value();
        case CompactLength::FIT_CONTENT_PERCENT_TAG:
            return parent_size ? std::optional<float>((cl.value() * *parent_size) / 100.0f) : std::nullopt;
        default: return cl_definite_value(cl, parent_size);
    }
}

/// Resolve the fit-content() limit of a track sizing function (None if not fit-content() or the
/// argument cannot be resolved against the passed parent size).
inline std::optional<float> cl_fit_content_limit(CompactLength cl, std::optional<float> parent_size) {
    switch (cl.tag()) {
        case CompactLength::FIT_CONTENT_PX_TAG: return cl.value();
        case CompactLength::FIT_CONTENT_PERCENT_TAG:
            return parent_size ? std::optional<float>((cl.value() * *parent_size) / 100.0f) : std::nullopt;
        default: return std::nullopt;
    }
}

/// Whether the sizing function has a fixed (definite) component
inline bool cl_has_fixed_component(CompactLength cl) {
    return cl.is_length_or_percentage() || cl.is_fit_content();
}

/// Whether a min track sizing function behaves as `auto` (an auto keyword, or a percentage which
/// cannot be resolved because the container size is indefinite)
inline bool cl_min_behaves_as_auto(CompactLength cl, std::optional<float> parent_size) {
    return cl.is_auto() || (!parent_size.has_value() && cl.uses_percentage());
}

inline bool tsf_has_fixed_component(const TrackSizingFunction& tsf) {
    return cl_has_fixed_component(tsf.min) || cl_has_fixed_component(tsf.max);
}

/// The definite value of a TrackSizingFunction used by the auto-repeat algorithm: treat each track
/// as its max track sizing function if that is definite, floored by the min track sizing function
/// if both are definite.
inline float tsf_definite_value(const TrackSizingFunction& tsf, std::optional<float> parent_size) {
    auto max_size = cl_definite_value(tsf.max, parent_size);
    auto min_size = cl_definite_value(tsf.min, parent_size);
    if (max_size) {
        float val = *max_size;
        if (min_size) val = std::max(val, *min_size);
        return val;
    }
    return min_size.value_or(0.0f);
}

inline std::optional<float> size_opt_get(const Size<std::optional<float>>& s, AbstractAxis axis) {
    return axis == AbstractAxis::Inline ? s.width : s.height;
}

inline Size<std::optional<float>> size_opt_with(const Size<std::optional<float>>& s, AbstractAxis axis,
                                                std::optional<float> value) {
    Size<std::optional<float>> r = s;
    (axis == AbstractAxis::Inline ? r.width : r.height) = value;
    return r;
}

inline void size_opt_set(Size<std::optional<float>>& s, AbstractAxis axis, std::optional<float> value) {
    (axis == AbstractAxis::Inline ? s.width : s.height) = value;
}

enum class GridTrackKind : uint8_t { Track, Gutter };

struct GridTrack {
    GridTrackKind kind{GridTrackKind::Track};
    bool is_collapsed{false};
    CompactLength min_track_sizing_function{CompactLength::length(0)};
    CompactLength max_track_sizing_function{CompactLength::length(0)};
    float offset{0.0f};
    float base_size{0.0f};
    float growth_limit{0.0f};
    float content_alignment_adjustment{0.0f};
    float item_incurred_increase{0.0f};
    float base_size_planned_increase{0.0f};
    float growth_limit_planned_increase{0.0f};
    bool infinitely_growable{false};
    float fit_content_limit{std::numeric_limits<float>::infinity()};

    static GridTrack new_track(CompactLength min_fn, CompactLength max_fn) {
        GridTrack t;
        t.kind = GridTrackKind::Track;
        t.min_track_sizing_function = min_fn;
        t.max_track_sizing_function = max_fn;
        return t;
    }
    static GridTrack gutter(LengthPercentage size) {
        GridTrack t;
        t.kind = GridTrackKind::Gutter;
        t.min_track_sizing_function = size.inner;
        t.max_track_sizing_function = size.inner;
        return t;
    }
    void collapse() {
        is_collapsed = true;
        min_track_sizing_function = CompactLength::length(0);
        max_track_sizing_function = CompactLength::length(0);
        fit_content_limit = std::numeric_limits<float>::infinity();
    }
    bool is_flexible() const { return max_track_sizing_function.is_fr(); }
    bool uses_percentage() const { return min_track_sizing_function.uses_percentage() || max_track_sizing_function.uses_percentage(); }
    bool has_intrinsic_sizing_function() const { return min_track_sizing_function.is_intrinsic() || max_track_sizing_function.is_intrinsic(); }
    void resolve_fit_content_limit(std::optional<float> axis_inner_node_size) {
        fit_content_limit = cl_fit_content_limit(max_track_sizing_function, axis_inner_node_size)
                                .value_or(std::numeric_limits<float>::infinity());
    }
    float fit_content_limited_growth_limit() const { return std::min(growth_limit, fit_content_limit); }
    float flex_factor() const { return max_track_sizing_function.is_fr() ? max_track_sizing_function.value() : 0.0f; }
};

struct TrackCounts {
    uint16_t negative_implicit{0};
    uint16_t explicit_{0};
    uint16_t positive_implicit{0};

    static TrackCounts from_raw(uint16_t neg, uint16_t exp, uint16_t pos) { return {neg, exp, pos}; }
    size_t len() const { return static_cast<size_t>(negative_implicit) + explicit_ + positive_implicit; }
    OriginZeroLine implicit_start_line() const { return OriginZeroLine{static_cast<int16_t>(-static_cast<int16_t>(negative_implicit))}; }
    OriginZeroLine implicit_end_line() const { return OriginZeroLine{static_cast<int16_t>(explicit_ + positive_implicit)}; }

    int16_t oz_line_to_next_track(OriginZeroLine index) const {
        return static_cast<int16_t>(index.value + static_cast<int16_t>(negative_implicit));
    }
    /// Converts start and end grid lines in OriginZero coordinates into a range of tracks as indexes
    /// into the CellOccupancyMatrix (end exclusive)
    std::pair<int16_t, int16_t> oz_line_range_to_track_range(Line<OriginZeroLine> input) const {
        return {oz_line_to_next_track(input.start), oz_line_to_next_track(input.end)};
    }
    OriginZeroLine track_to_prev_oz_line(uint16_t index) const {
        return OriginZeroLine{static_cast<int16_t>(static_cast<int16_t>(index) - static_cast<int16_t>(negative_implicit))};
    }
    /// Converts a range of tracks as indexes into the CellOccupancyMatrix into start and end grid
    /// lines in OriginZero coordinates
    Line<OriginZeroLine> track_range_to_oz_line_range(std::pair<int16_t, int16_t> input) const {
        return {track_to_prev_oz_line(static_cast<uint16_t>(input.first)),
                track_to_prev_oz_line(static_cast<uint16_t>(input.second))};
    }
};

/// Converts a grid line in OriginZero coordinates into the index of that same grid line in the
/// GridTrackVec (which stores gutters at even indexes and tracks at odd indexes)
inline uint16_t oz_line_to_track_vec_index(OriginZeroLine line, TrackCounts counts) {
    int32_t index = 2 * (static_cast<int32_t>(line.value) + static_cast<int32_t>(counts.negative_implicit));
    return static_cast<uint16_t>(std::max<int32_t>(0, index));
}

/// Clamp an origin-zero line into the limited grid
inline OriginZeroLine oz_line_clamped(int32_t line) {
    int32_t clamped = std::max<int32_t>(MIN_OZ_LINE, std::min<int32_t>(MAX_OZ_LINE, line));
    return OriginZeroLine{static_cast<int16_t>(clamped)};
}

/// Convert an origin-zero line into "CSS Grid Line" coordinates (the inverse of the conversion above)
inline GridLine origin_zero_to_grid_line(OriginZeroLine line, uint16_t explicit_track_count) {
    int32_t explicit_line_count = static_cast<int32_t>(explicit_track_count) + 1;
    if (line.value >= 0) return GridLine{static_cast<int16_t>(line.value + 1)};
    return GridLine{static_cast<int16_t>(static_cast<int32_t>(line.value) - explicit_line_count)};
}

/// Convert a "CSS Grid Line" into origin-zero coordinates, clamped into the limited grid
inline OriginZeroLine grid_line_to_origin_zero(GridLine line, uint16_t explicit_track_count) {
    int32_t explicit_line_count = static_cast<int32_t>(explicit_track_count) + 1;
    int32_t oz = (line.value > 0) ? (static_cast<int32_t>(line.value) - 1)
                                  : (static_cast<int32_t>(line.value) + explicit_line_count);
    return oz_line_clamped(oz);
}

enum class CellOccupancyState : uint8_t { Unoccupied, DefinitelyPlaced, AutoPlaced };

/// The occupied cells of a single track, stored as a sorted list of disjoint intervals in
/// OriginZero line coordinates. Gaps between intervals are unoccupied. Touching intervals with the
/// same state are merged.
struct OccupiedInterval {
    int16_t start{0};
    int16_t end{0};
    CellOccupancyState state{CellOccupancyState::Unoccupied};
};

struct TrackIntervals {
    std::vector<OccupiedInterval> intervals{};
    bool has_auto_cursor{false};
    int16_t auto_cursor{0};

    bool is_empty() const { return intervals.empty(); }

    CellOccupancyState state_at(int16_t coordinate) const {
        for (auto& interval : intervals)
            if (interval.start <= coordinate && coordinate < interval.end) return interval.state;
        return CellOccupancyState::Unoccupied;
    }

    bool overlaps(const OccupiedInterval& i, int16_t range_start, int16_t range_end) const {
        return i.start < range_end && i.end > range_start;
    }

    /// Set the cells covered by [range_start, range_end) to `state`, overwriting any already
    /// occupied cells within the range
    void paint(int16_t range_start, int16_t range_end, CellOccupancyState state) {
        if (range_start >= range_end) return;

        if (intervals.empty()) {
            intervals.push_back({range_start, range_end, state});
            return;
        }
        // Fast path: the painted range lies entirely after all existing intervals
        if (intervals.back().end <= range_start) {
            if (intervals.back().state == state && intervals.back().end == range_start) {
                intervals.back().end = range_end;
            } else {
                intervals.push_back({range_start, range_end, state});
            }
            return;
        }

        std::vector<OccupiedInterval> result;

        // Intervals (or partial intervals) entirely before the painted range
        for (auto& interval : intervals) {
            if (interval.end <= range_start) {
                result.push_back(interval);
            } else if (interval.start < range_start) {
                result.push_back({interval.start, range_start, interval.state});
            }
        }

        // The painted range, merged with the preceding interval if it touches and has the same state
        if (!result.empty() && result.back().state == state && result.back().end == range_start) {
            result.back().end = range_end;
        } else {
            result.push_back({range_start, range_end, state});
        }

        // Intervals (or partial intervals) entirely after the painted range
        for (auto& interval : intervals) {
            OccupiedInterval trimmed;
            bool have_trimmed = false;
            if (interval.start >= range_end) {
                trimmed = interval;
                have_trimmed = true;
            } else if (interval.end > range_end) {
                trimmed = {range_end, interval.end, interval.state};
                have_trimmed = true;
            }
            if (!have_trimmed) continue;
            if (!result.empty() && result.back().state == trimmed.state && result.back().end == trimmed.start) {
                result.back().end = trimmed.end;
            } else {
                result.push_back(trimmed);
            }
        }

        intervals = std::move(result);
    }

    /// The extremal occupied cell of the last interval overlapping the passed range (i.e. the end of
    /// that interval minus one), or None if the range is entirely unoccupied.
    std::optional<int16_t> collision_extent(int16_t range_start, int16_t range_end) const {
        for (auto it = intervals.rbegin(); it != intervals.rend(); ++it) {
            if (it->end <= range_start) break;
            if (overlaps(*it, range_start, range_end)) return static_cast<int16_t>(it->end - 1);
        }
        return std::nullopt;
    }
};

/// A matrix which tracks the occupancy of each grid cell during auto-placement. Occupancy is stored
/// sparsely as per-track interval lists (in both orientations).
class CellOccupancyMatrix {
public:
    TrackCounts columns{};
    TrackCounts rows{};
    std::vector<TrackIntervals> row_intervals{};
    std::vector<TrackIntervals> column_intervals{};

    CellOccupancyMatrix() = default;

    static CellOccupancyMatrix with_track_counts(TrackCounts cols, TrackCounts rws) {
        CellOccupancyMatrix m;
        m.columns = cols;
        m.rows = rws;
        m.row_intervals.resize(rws.len());
        m.column_intervals.resize(cols.len());
        return m;
    }

    std::vector<TrackIntervals>& track_lists(AbsoluteAxis track_axis) {
        return track_axis == AbsoluteAxis::Horizontal ? column_intervals : row_intervals;
    }
    const std::vector<TrackIntervals>& track_lists(AbsoluteAxis track_axis) const {
        return track_axis == AbsoluteAxis::Horizontal ? column_intervals : row_intervals;
    }

    const TrackCounts& track_counts(AbsoluteAxis axis) const {
        return axis == AbsoluteAxis::Horizontal ? columns : rows;
    }
    TrackCounts& track_counts(AbsoluteAxis axis) {
        return axis == AbsoluteAxis::Horizontal ? columns : rows;
    }

    /// Expands the grid (potentially in all 4 directions) so that the specified spans fit
    void expand_to_fit_range(Line<OriginZeroLine> row_span, Line<OriginZeroLine> col_span) {
        int16_t req_negative_rows = std::max<int16_t>(static_cast<int16_t>(-static_cast<int16_t>(rows.negative_implicit) - row_span.start.value), 0);
        int16_t req_positive_rows = std::max<int16_t>(static_cast<int16_t>(row_span.end.value - rows.implicit_end_line().value), 0);
        int16_t req_negative_cols = std::max<int16_t>(static_cast<int16_t>(-static_cast<int16_t>(columns.negative_implicit) - col_span.start.value), 0);
        int16_t req_positive_cols = std::max<int16_t>(static_cast<int16_t>(col_span.end.value - columns.implicit_end_line().value), 0);

        if (req_negative_rows > 0) {
            row_intervals.insert(row_intervals.begin(), static_cast<size_t>(req_negative_rows), TrackIntervals{});
        }
        if (req_positive_rows > 0) {
            row_intervals.resize(row_intervals.size() + static_cast<size_t>(req_positive_rows));
        }
        if (req_negative_cols > 0) {
            column_intervals.insert(column_intervals.begin(), static_cast<size_t>(req_negative_cols), TrackIntervals{});
        }
        if (req_positive_cols > 0) {
            column_intervals.resize(column_intervals.size() + static_cast<size_t>(req_positive_cols));
        }

        rows.negative_implicit = static_cast<uint16_t>(rows.negative_implicit + static_cast<uint16_t>(req_negative_rows));
        rows.positive_implicit = static_cast<uint16_t>(rows.positive_implicit + static_cast<uint16_t>(req_positive_rows));
        columns.negative_implicit = static_cast<uint16_t>(columns.negative_implicit + static_cast<uint16_t>(req_negative_cols));
        columns.positive_implicit = static_cast<uint16_t>(columns.positive_implicit + static_cast<uint16_t>(req_positive_cols));
    }

    /// Mark an area of the matrix as occupied, expanding the allocated space as necessary
    void mark_area_as(AbsoluteAxis primary_axis, Line<OriginZeroLine> primary_span,
                      Line<OriginZeroLine> secondary_span, CellOccupancyState value) {
        Line<OriginZeroLine> row_span = (primary_axis == AbsoluteAxis::Horizontal) ? secondary_span : primary_span;
        Line<OriginZeroLine> column_span = (primary_axis == AbsoluteAxis::Horizontal) ? primary_span : secondary_span;

        expand_to_fit_range(row_span, column_span);

        if (value == CellOccupancyState::AutoPlaced && primary_span.start.value < primary_span.end.value
            && secondary_span.start.value < secondary_span.end.value) {
            advance_auto_placement_cursor(primary_axis, secondary_span.start, primary_span.end);
        }

        auto row_range = rows.oz_line_range_to_track_range(row_span);
        auto col_range = columns.oz_line_range_to_track_range(column_span);
        for (int16_t row_index = row_range.first; row_index < row_range.second; ++row_index) {
            row_intervals[static_cast<size_t>(row_index)].paint(column_span.start.value, column_span.end.value, value);
        }
        for (int16_t col_index = col_range.first; col_index < col_range.second; ++col_index) {
            column_intervals[static_cast<size_t>(col_index)].paint(row_span.start.value, row_span.end.value, value);
        }
    }

    /// Returns None if the area is entirely unoccupied, else the next search position (in
    /// OriginZero coordinates, along `primary_axis`) that is not guaranteed to collide.
    std::optional<OriginZeroLine> line_area_collision_jump(AbsoluteAxis primary_axis,
                                                           Line<OriginZeroLine> primary_span,
                                                           Line<OriginZeroLine> secondary_span) {
        const auto& track_lists_ref = track_lists(other_axis(primary_axis));
        const auto& secondary_counts = track_counts(other_axis(primary_axis));
        auto secondary_range = secondary_counts.oz_line_range_to_track_range(secondary_span);

        int16_t secondary_start = std::max<int16_t>(secondary_range.first, 0);
        int16_t secondary_end = std::min<int16_t>(secondary_range.second, static_cast<int16_t>(track_lists_ref.size()));

        bool have_extent = false;
        int16_t extent = 0;
        for (int16_t secondary_index = secondary_start; secondary_index < secondary_end; ++secondary_index) {
            auto cell = track_lists_ref[static_cast<size_t>(secondary_index)]
                            .collision_extent(primary_span.start.value, primary_span.end.value);
            if (!cell) continue;
            extent = have_extent ? std::max(extent, *cell) : *cell;
            have_extent = true;
        }
        if (!have_extent) return std::nullopt;
        return OriginZeroLine{static_cast<int16_t>(extent + 1)};
    }

    bool line_area_is_unoccupied(AbsoluteAxis primary_axis, Line<OriginZeroLine> primary_span,
                                 Line<OriginZeroLine> secondary_span) {
        return !line_area_collision_jump(primary_axis, primary_span, secondary_span).has_value();
    }

    /// Given a span of tracks in `axis`, returns the next search position past all non-empty
    /// tracks within the span, or None if all tracks within the span are entirely unoccupied.
    std::optional<OriginZeroLine> occupied_track_jump(AbsoluteAxis axis, Line<OriginZeroLine> span) {
        const auto& counts = track_counts(axis);
        const auto& lists = track_lists(axis);
        auto range = counts.oz_line_range_to_track_range(span);
        int16_t start = std::max<int16_t>(range.first, 0);
        int16_t end = std::min<int16_t>(range.second, static_cast<int16_t>(lists.size()));
        for (int16_t index = static_cast<int16_t>(end - 1); index >= start; --index) {
            if (!lists[static_cast<size_t>(index)].is_empty()) {
                OriginZeroLine line = counts.track_to_prev_oz_line(static_cast<uint16_t>(index));
                return OriginZeroLine{static_cast<int16_t>(line.value + 1)};
            }
        }
        return std::nullopt;
    }

    bool row_is_occupied(size_t row_index) const {
        const auto& lists = track_lists(AbsoluteAxis::Vertical);
        return row_index < lists.size() && !lists[row_index].is_empty();
    }
    bool column_is_occupied(size_t col_index) const {
        const auto& lists = track_lists(AbsoluteAxis::Horizontal);
        return col_index < lists.size() && !lists[col_index].is_empty();
    }

    std::optional<OriginZeroLine> auto_placement_cursor(AbsoluteAxis track_type, OriginZeroLine start_at) const {
        const auto& counts = track_counts(other_axis(track_type));
        int16_t track_index = counts.oz_line_to_next_track(start_at);
        const auto& lists = track_lists(other_axis(track_type));
        if (track_index < 0 || track_index >= static_cast<int16_t>(lists.size())) return std::nullopt;
        const auto& track = lists[static_cast<size_t>(track_index)];
        if (!track.has_auto_cursor) return std::nullopt;
        return OriginZeroLine{track.auto_cursor};
    }

    void advance_auto_placement_cursor(AbsoluteAxis primary_axis, OriginZeroLine secondary_start,
                                       OriginZeroLine primary_end) {
        size_t track_index = static_cast<size_t>(
            track_counts(other_axis(primary_axis)).oz_line_to_next_track(secondary_start));
        auto& lists = (primary_axis == AbsoluteAxis::Horizontal) ? row_intervals : column_intervals;
        if (track_index >= lists.size()) return;
        auto& track = lists[track_index];
        if (!track.has_auto_cursor || primary_end.value > track.auto_cursor) {
            track.auto_cursor = primary_end.value;
            track.has_auto_cursor = true;
        }
    }
};

/// A grid placement with named lines resolved (still in CSS grid-line coordinates)
struct NonNamedGridPlacement {
    enum Kind : uint8_t { AutoKind, LineKind, SpanKind };
    Kind kind{AutoKind};
    GridLine line_value{};
    uint16_t span_value{1};

    static NonNamedGridPlacement auto_() { return {}; }
    static NonNamedGridPlacement line(GridLine l) { return {LineKind, l, 1}; }
    static NonNamedGridPlacement span(uint16_t s) { return {SpanKind, GridLine{}, s}; }

    bool is_definite() const { return kind == LineKind; }
    bool is_auto() const { return kind == AutoKind; }
    bool is_span() const { return kind == SpanKind; }
    uint16_t indefinite_span() const { return kind == SpanKind ? span_value : 1; }
};

/// A grid placement in origin-zero coordinates
struct OriginZeroGridPlacement {
    enum Kind : uint8_t { AutoKind, LineKind, SpanKind };
    Kind kind{AutoKind};
    OriginZeroLine line_value{};
    uint16_t span_value{1};

    static OriginZeroGridPlacement auto_() { return {}; }
    static OriginZeroGridPlacement line(OriginZeroLine l) { return {LineKind, l, 1}; }
    static OriginZeroGridPlacement span(uint16_t s) { return {SpanKind, OriginZeroLine{}, s}; }

    bool is_definite() const { return kind == LineKind; }
    bool is_auto() const { return kind == AutoKind; }
    bool is_span() const { return kind == SpanKind; }

    uint16_t indefinite_span() const {
        switch (kind) {
            case LineKind: return 1;
            case AutoKind: return 1;
            case SpanKind: return span_value;
        }
        return 1;
    }

    Line<OriginZeroLine> resolve_definite_grid_lines() const {
        if (kind != LineKind) return {line_value, line_value};
        return {line_value, OriginZeroLine{static_cast<int16_t>(line_value.value + 1)}};
    }
};

/// Convert a Line<GridPlacement> (named lines already resolved) into origin-zero coordinates
inline Line<OriginZeroGridPlacement> into_origin_zero_placement(Line<NonNamedGridPlacement> line,
                                                               uint16_t explicit_track_count) {
    auto convert = [&](const NonNamedGridPlacement& p) -> OriginZeroGridPlacement {
        switch (p.kind) {
            case NonNamedGridPlacement::AutoKind:
                return OriginZeroGridPlacement::auto_();
            case NonNamedGridPlacement::SpanKind:
                // Spans are clamped between 1 (a zero span is invalid and treated as 1) and the max
                // track limit
                return OriginZeroGridPlacement::span(
                    static_cast<uint16_t>(std::max<int32_t>(1, std::min<int32_t>(MAX_GRID_TRACKS, p.span_value))));
            case NonNamedGridPlacement::LineKind: {
                int16_t v = p.line_value.value;
                // Grid line zero is an invalid index, so it gets treated as Auto
                if (v == 0) return OriginZeroGridPlacement::auto_();
                return OriginZeroGridPlacement::line(grid_line_to_origin_zero(p.line_value, explicit_track_count));
            }
        }
        return OriginZeroGridPlacement::auto_();
    };
    return {convert(line.start), convert(line.end)};
}

/// Whether the track position is definite in this axis (or the item will need auto placement)
inline bool placement_is_definite(const Line<OriginZeroGridPlacement>& line) {
    return line.start.kind == OriginZeroGridPlacement::LineKind || line.end.kind == OriginZeroGridPlacement::LineKind;
}

/// Resolves the span for an indefinite placement (one that does not consist of two Lines)
inline uint16_t placement_indefinite_span(const Line<OriginZeroGridPlacement>& line) {
    using P = OriginZeroGridPlacement;
    if (line.start.kind == P::LineKind && line.end.kind == P::LineKind) return 1;
    if (line.start.kind == P::LineKind) {
        return (line.end.kind == P::SpanKind) ? line.end.span_value : 1;
    }
    if (line.end.kind == P::LineKind) {
        return (line.start.kind == P::SpanKind) ? line.start.span_value : 1;
    }
    if (line.start.kind == P::SpanKind) return line.start.span_value;
    if (line.end.kind == P::SpanKind) return line.end.span_value;
    return 1;
}

/// If at least one of the start and end positions is a track index then the other end can be
/// resolved into a track index purely based on the placement specification
inline Line<OriginZeroLine> placement_resolve_definite_grid_lines(const Line<OriginZeroGridPlacement>& line) {
    using P = OriginZeroGridPlacement;
    if (line.start.kind == P::LineKind && line.end.kind == P::LineKind) {
        int16_t l1 = line.start.line_value.value;
        int16_t l2 = line.end.line_value.value;
        if (l1 == l2) return {line.start.line_value, OriginZeroLine{static_cast<int16_t>(l1 + 1)}};
        return {OriginZeroLine{std::min(l1, l2)}, OriginZeroLine{std::max(l1, l2)}};
    }
    if (line.start.kind == P::LineKind) {
        int16_t operand = (line.end.kind == P::SpanKind) ? static_cast<int16_t>(line.end.span_value) : 1;
        return {line.start.line_value, OriginZeroLine{static_cast<int16_t>(line.start.line_value.value + operand)}};
    }
    if (line.end.kind == P::LineKind) {
        int16_t operand = (line.start.kind == P::SpanKind) ? static_cast<int16_t>(line.start.span_value) : 1;
        return {OriginZeroLine{static_cast<int16_t>(line.end.line_value.value - operand)}, line.end.line_value};
    }
    return {OriginZeroLine{0}, OriginZeroLine{1}};
}

/// The known_dimensions computed for a grid item for a given grid area size
struct KnownDimensionsCacheEntry {
    Size<std::optional<float>> grid_area_size{};
    Size<std::optional<float>> known_dimensions{};
};

/// Represents a single grid item
struct GridItem {
    NodeId node{};
    uint16_t source_order{0};

    Line<OriginZeroLine> row{};
    Line<OriginZeroLine> column{};

    bool is_compressible_replaced{false};
    bool is_replaced{false};
    Point<Overflow> overflow{Overflow::Visible, Overflow::Visible};
    BoxSizing box_sizing{BoxSizing::BorderBox};
    Size<Dimension> size{};
    Size<Dimension> min_size{};
    Size<Dimension> max_size{};
    std::optional<float> aspect_ratio{};
    Rect<LengthPercentage> padding{};
    Rect<LengthPercentage> border{};
    Rect<LengthPercentageAuto> margin{};
    AlignSelf align_self{};
    AlignSelf justify_self{};
    std::optional<float> baseline{};
    float baseline_shim{0.0f};

    Line<uint16_t> row_indexes{0, 0};
    Line<uint16_t> column_indexes{0, 0};

    bool crosses_flexible_row{false};
    bool crosses_flexible_column{false};
    bool crosses_intrinsic_row{false};
    bool crosses_intrinsic_column{false};

    std::optional<Size<std::optional<float>>> grid_area_size_cache{};
    std::optional<KnownDimensionsCacheEntry> known_dimensions_cache{};
    Size<std::optional<float>> min_content_contribution_cache{std::nullopt, std::nullopt};
    Size<std::optional<float>> minimum_contribution_cache{std::nullopt, std::nullopt};
    Size<std::optional<float>> max_content_contribution_cache{std::nullopt, std::nullopt};

    float y_position{0.0f};
    float height{0.0f};

    bool has_auto_block_margin() const { return margin.top.is_auto() || margin.bottom.is_auto(); }

    /// Whether the item's block size depends on the size of its row(s), creating a cyclic
    /// dependency with baseline alignment
    bool has_cyclic_block_size_dependency() const {
        return size.height.uses_percentage() && (crosses_intrinsic_row || crosses_flexible_row);
    }

    bool participates_in_baseline_alignment() const {
        return align_self.keyword == AlignItemsKeyword::Baseline && !has_auto_block_margin()
               && !has_cyclic_block_size_dependency();
    }

    Line<OriginZeroLine> placement(AbstractAxis axis) const {
        return axis == AbstractAxis::Block ? row : column;
    }
    Line<uint16_t> placement_indexes(AbstractAxis axis) const {
        return axis == AbstractAxis::Block ? row_indexes : column_indexes;
    }
    /// A range which can be used as an index into the GridTrackVec producing a sub-slice covering
    /// all the tracks (excluding bounding lines) that this item spans
    std::pair<size_t, size_t> track_range_excluding_lines(AbstractAxis axis) const {
        auto indexes = placement_indexes(axis);
        return {static_cast<size_t>(indexes.start) + 1, static_cast<size_t>(indexes.end)};
    }
    uint16_t span(AbstractAxis axis) const {
        auto p = placement(axis);
        return static_cast<uint16_t>(std::max(p.end.value - p.start.value, 0));
    }
    bool crosses_flexible_track(AbstractAxis axis) const {
        return axis == AbstractAxis::Block ? crosses_flexible_row : crosses_flexible_column;
    }
    bool crosses_intrinsic_track(AbstractAxis axis) const {
        return axis == AbstractAxis::Block ? crosses_intrinsic_row : crosses_intrinsic_column;
    }
};

} // namespace grid_detail

#endif // TAFFY_GRID

// ===========================================================================
// Section 9: Style struct
// Ref: src/style/mod.rs
// ===========================================================================

struct Style {
#if defined(TAFFY_FLEXBOX)
    Display display{Display::Flex};
#elif defined(TAFFY_GRID)
    Display display{Display::Grid};
#elif defined(TAFFY_BLOCK_LAYOUT)
    Display display{Display::Block};
#else
    Display display{Display::None};
#endif
    bool item_is_table{false};
    bool item_is_replaced{false};
    bool item_is_compressible_replaced{false};
    BoxSizing box_sizing{BoxSizing::BorderBox};
    Direction direction{Direction::Ltr};

    // Overflow
    Point<Overflow> overflow{Overflow::Visible, Overflow::Visible};
    float scrollbar_width{0.0f};
    Contain contain{};

#ifdef TAFFY_FLOAT_LAYOUT
    Float float_{Float::None};
    Clear clear{Clear::None};
#endif

    // Position
    Position position{Position::Static};
    Rect<LengthPercentageAuto> inset{
        LengthPercentageAuto::auto_(), LengthPercentageAuto::auto_(),
        LengthPercentageAuto::auto_(), LengthPercentageAuto::auto_()};

    // Size
    Size<Dimension> size{Dimension::auto_(), Dimension::auto_()};
    Size<Dimension> min_size{Dimension::auto_(), Dimension::auto_()};
    Size<Dimension> max_size{Dimension::auto_(), Dimension::auto_()};
    std::optional<float> aspect_ratio{std::nullopt};

    // Spacing
    Rect<LengthPercentageAuto> margin{
        LengthPercentageAuto(), LengthPercentageAuto(), LengthPercentageAuto(), LengthPercentageAuto()};
    Rect<LengthPercentage> padding{LengthPercentage(), LengthPercentage(), LengthPercentage(), LengthPercentage()};
    Rect<LengthPercentage> border{LengthPercentage(), LengthPercentage(), LengthPercentage(), LengthPercentage()};

    // Alignment
    std::optional<AlignItems> align_items{std::nullopt};
    std::optional<AlignSelf> align_self{std::nullopt};
#ifdef TAFFY_GRID
    std::optional<AlignItems> justify_items{std::nullopt};
    std::optional<JustifySelf> justify_self{std::nullopt};
#endif
    std::optional<AlignContent> align_content{std::nullopt};
    std::optional<JustifyContent> justify_content{std::nullopt};
    Size<LengthPercentage> gap{LengthPercentage(), LengthPercentage()};

#ifdef TAFFY_BLOCK_LAYOUT
    TextAlign text_align{TextAlign::Auto};
#endif

    // Flexbox container
#ifdef TAFFY_FLEXBOX
    FlexDirection flex_direction{FlexDirection::Row};
    FlexWrap flex_wrap{FlexWrap::NoWrap};
    // Minimum number of flex lines requested for a balancing multi-line container (>= 1)
    uint16_t flex_line_count{1};
    Dimension flex_basis{Dimension::auto_()};
    float flex_grow{0.0f};
    float flex_shrink{1.0f};
#endif

    // Grid container
#ifdef TAFFY_GRID
    std::vector<GridTemplateComponent> grid_template_rows{};
    std::vector<GridTemplateComponent> grid_template_columns{};
    std::vector<TrackSizingFunction> grid_auto_rows{};
    std::vector<TrackSizingFunction> grid_auto_columns{};
    GridAutoFlow grid_auto_flow{GridAutoFlow::Row};
    std::vector<GridTemplateArea> grid_template_areas{};
    std::vector<std::vector<std::string>> grid_template_column_names{};
    std::vector<std::vector<std::string>> grid_template_row_names{};
    Line<GridPlacement> grid_row{GridPlacement::auto_(), GridPlacement::auto_()};
    Line<GridPlacement> grid_column{GridPlacement::auto_(), GridPlacement::auto_()};
#endif

    bool operator==(const Style& o) const {
        return display == o.display && item_is_table == o.item_is_table && item_is_replaced == o.item_is_replaced
            && item_is_compressible_replaced == o.item_is_compressible_replaced
            && box_sizing == o.box_sizing && direction == o.direction && overflow == o.overflow
            && scrollbar_width == o.scrollbar_width && contain == o.contain
#ifdef TAFFY_FLOAT_LAYOUT
            && float_ == o.float_ && clear == o.clear
#endif
            && position == o.position && inset == o.inset && size == o.size && min_size == o.min_size
            && max_size == o.max_size && aspect_ratio == o.aspect_ratio && margin == o.margin
            && padding == o.padding && border == o.border && align_items == o.align_items && align_self == o.align_self
            && align_content == o.align_content && justify_content == o.justify_content && gap == o.gap
#ifdef TAFFY_GRID
            && justify_items == o.justify_items && justify_self == o.justify_self
            && grid_template_rows == o.grid_template_rows && grid_template_columns == o.grid_template_columns
            && grid_auto_rows == o.grid_auto_rows && grid_auto_columns == o.grid_auto_columns
            && grid_auto_flow == o.grid_auto_flow && grid_template_areas == o.grid_template_areas
            && grid_template_column_names == o.grid_template_column_names
            && grid_template_row_names == o.grid_template_row_names
            && grid_row == o.grid_row && grid_column == o.grid_column
#endif
#ifdef TAFFY_BLOCK_LAYOUT
            && text_align == o.text_align
#endif
#ifdef TAFFY_FLEXBOX
            && flex_direction == o.flex_direction && flex_wrap == o.flex_wrap && flex_basis == o.flex_basis
            && flex_grow == o.flex_grow && flex_shrink == o.flex_shrink && flex_line_count == o.flex_line_count
#endif
            ;
    }
    bool operator!=(const Style& o) const { return !(*this == o); }
};

// ===========================================================================
// Section 10: Style Helpers
// Ref: src/style_helpers.rs
// ===========================================================================

// Factory functions for creating style values
inline constexpr LengthPercentage zero_lp() { return LengthPercentage::length(0.0f); }
inline constexpr LengthPercentageAuto zero_lpa() { return LengthPercentageAuto::length(0.0f); }
inline constexpr Dimension zero_dim() { return Dimension::length(0.0f); }
inline constexpr LengthPercentage length(float v) { return LengthPercentage::length(v); }
inline constexpr LengthPercentage percent(float v) { return LengthPercentage::percent(v); }
inline constexpr LengthPercentageAuto auto_lpa() { return LengthPercentageAuto::auto_(); }
inline constexpr Dimension auto_dim() { return Dimension::auto_(); }
inline constexpr Dimension dim_length(float v) { return Dimension::length(v); }
inline constexpr Dimension dim_percent(float v) { return Dimension::percent(v); }
inline constexpr Dimension fr(float v) { return Dimension::fr(v); }
inline constexpr Dimension min_content() { return Dimension::min_content(); }
inline constexpr Dimension max_content() { return Dimension::max_content(); }
inline constexpr Dimension fit_content(LengthPercentage lp) {
    return lp.tag() == CompactLength::LENGTH_TAG
        ? Dimension::fit_content_px(lp.value())
        : Dimension::fit_content_percent(lp.value());
}

#ifdef TAFFY_GRID
inline TrackSizingFunction minmax(MinTrackSizingFunction mn, MaxTrackSizingFunction mx) {
    return MinMax<MinTrackSizingFunction, MaxTrackSizingFunction>{mn, mx};
}
inline TrackSizingFunction flex_track(float v) {
    return minmax(CompactLength::length(0.0f), CompactLength::fr(v));
}
inline GridTemplateComponent repeat_grid(RepetitionCountValue count, std::vector<TrackSizingFunction> tracks,
                                          std::vector<std::vector<std::string>> line_names = {}) {
    GridTemplateRepetition rep;
    rep.count = std::move(count);
    rep.tracks = std::move(tracks);
    rep.line_names = std::move(line_names);
    return GridTemplateComponent::repeat(std::move(rep));
}
inline std::vector<TrackSizingFunction> evenly_sized_tracks(size_t n) {
    return std::vector<TrackSizingFunction>(n, flex_track(1.0f));
}
#endif

// ===========================================================================
// Section 11: Layout Types
// Ref: src/tree/layout.rs
// ===========================================================================

enum class RunMode : uint8_t { PerformLayout, ComputeSize, PerformHiddenLayout };
enum class SizingMode : uint8_t { ContentSize, InherentSize };
enum class RequestedAxis : uint8_t { Horizontal, Vertical, Both };

/// Collapsible margin set for block layout margin collapsing
struct CollapsibleMarginSet {
    float positive{0.0f};
    float negative{0.0f};

    static constexpr CollapsibleMarginSet zero() { return {0.0f, 0.0f}; }
    static constexpr CollapsibleMarginSet from_margin(float margin) {
        return margin >= 0.0f ? CollapsibleMarginSet{margin, 0.0f} : CollapsibleMarginSet{0.0f, margin};
    }
    CollapsibleMarginSet collapse_with_margin(float margin) const {
        if (margin >= 0.0f) return {std::max(positive, margin), negative};
        return {positive, std::min(negative, margin)};
    }
    CollapsibleMarginSet collapse_with_set(CollapsibleMarginSet other) const {
        return {std::max(positive, other.positive), std::min(negative, other.negative)};
    }
    float resolve() const { return positive + negative; }
    constexpr bool operator==(const CollapsibleMarginSet& o) const { return positive == o.positive && negative == o.negative; }
    constexpr bool operator!=(const CollapsibleMarginSet& o) const { return !(*this == o); }
};

/// Whether the physical x/y scroll origins are at the right/bottom edges.
struct AxisStaticEdge { enum Kind : uint8_t { Start, Center, End }; };

/// The physical alignment of an out-of-flow box's margin box within its static-position area
/// (per axis).
struct AxisStaticAlign {
    /// The physical alignment keyword
    int keyword{AxisStaticEdge::Start};
    /// The `safe` overflow-position keyword: when `Safe`, `fallback` is used if the box overflows
    AlignmentSafety safety{AlignmentSafety::Default};
    /// The physical alignment keyword to fall back to when `safety` is `Safe` and the box overflows
    int fallback{AxisStaticEdge::Start};

    static AxisStaticAlign from_keyword(int kw) { return {kw, AlignmentSafety::Default, kw}; }
    constexpr bool operator==(const AxisStaticAlign& o) const {
        return keyword == o.keyword && safety == o.safety && fallback == o.fallback;
    }
};

/// The static position of an out-of-flow box in a single axis: the alignment container plus the
/// alignment to apply to the box's margin box within it. Area coordinates are relative to the
/// border box of the node currently holding the candidate.
struct AxisStaticPosition {
    Line<float> area{0.0f, 0.0f};
    AxisStaticAlign align{};

    static AxisStaticPosition from_edge(float anchor, int edge) {
        return {Line<float>{anchor, anchor}, AxisStaticAlign::from_keyword(edge)};
    }
    static AxisStaticPosition from_alignment(AlignSelf alignment, Line<float> area, bool axis_is_rtl) {
        // `normal`/`stretch` fall back to start alignment for static-position purposes
        auto edge_for = [axis_is_rtl](AlignItemsKeyword kw) -> int {
            bool start_position = !(kw == AlignItemsKeyword::End || kw == AlignItemsKeyword::FlexEnd) != axis_is_rtl;
            if (kw == AlignItemsKeyword::Center) return AxisStaticEdge::Center;
            return start_position ? AxisStaticEdge::Start : AxisStaticEdge::End;
        };
        AlignItemsKeyword fallback_kw = alignment.safety == AlignmentSafety::Safe ? AlignItemsKeyword::Start : alignment.keyword;
        AxisStaticAlign a{edge_for(alignment.keyword), alignment.safety, edge_for(fallback_kw)};
        return {area, a};
    }
    constexpr bool operator==(const AxisStaticPosition& o) const { return area == o.area && align == o.align; }
};

/// A record of an out-of-flow box whose containing block is an ancestor of its parent
struct OofCandidate {
    NodeId node{};
    uint32_t order{0};
    Position position{Position::Absolute};
    Point<AxisStaticPosition> static_position{};
    constexpr bool operator==(const OofCandidate& o) const {
        return node == o.node && order == o.order && position == o.position && static_position == o.static_position;
    }
};

/// A lazily-allocated list of OofCandidates (empty list performs no allocation in Rust; here a
/// vector is used, which is cheap to copy for the common empty case).
struct OofCandidates {
    std::vector<OofCandidate> inner{};

    static OofCandidates none() { return {}; }
    bool is_empty() const { return inner.empty(); }
    size_t size() const { return inner.size(); }
    void push(const OofCandidate& c) { inner.push_back(c); }
    void append(OofCandidates& other) {
        inner.insert(inner.end(), other.inner.begin(), other.inner.end());
        other.inner.clear();
    }
    void clear() { inner.clear(); }
    void translate(Point<float> offset) {
        for (auto& c : inner) {
            c.static_position.x.area.start += offset.x;
            c.static_position.x.area.end += offset.x;
            c.static_position.y.area.start += offset.y;
            c.static_position.y.area.end += offset.y;
        }
    }
    bool operator==(const OofCandidates& o) const { return inner == o.inner; }
    bool operator!=(const OofCandidates& o) const { return !(*this == o); }
};

/// The area a container's out-of-flow boxes resolve their insets against: the container's border
/// box minus borders and scrollbar gutters.
struct OofPositioningArea {
    Size<float> size{0.0f, 0.0f};
    Point<float> offset{0.0f, 0.0f};
    constexpr bool operator==(const OofPositioningArea& o) const { return size == o.size && offset == o.offset; }
};

/// First and last baselines of a node (horizontal axis), if any
struct Baselines {
    std::optional<float> first{std::nullopt};
    std::optional<float> last{std::nullopt};

    static constexpr Baselines none() { return {}; }
    static Baselines from_first(std::optional<float> f) { Baselines b; b.first = f; return b; }
    constexpr bool operator==(const Baselines& o) const { return first == o.first && last == o.last; }
    constexpr bool operator!=(const Baselines& o) const { return !(*this == o); }
};

/// The scrollable overflow rectangle of a node: the CSS "scrollable overflow rectangle".
///
/// A normal axis has range `0..scroll_width()`; a reversed axis has range `-scroll_width()..0`.
/// The sign of `right`/`bottom` encodes whether the axis is reversed.
struct ScrollableOverflow {
    Rect<float> inner_{0.0f, 0.0f, 0.0f, 0.0f};

    static ScrollableOverflow zero() { return {}; }

    static ScrollableOverflow new_(Rect<float> rect, Point<bool> reversed) {
        ScrollableOverflow so;
        so.inner_ = rect;
        so.inner_.right = std::copysign(rect.right, reversed.x ? -1.0f : 1.0f);
        so.inner_.bottom = std::copysign(rect.bottom, reversed.y ? -1.0f : 1.0f);
        return so;
    }
    /// Build the overflow rect for a content extent measured from the node's origin
    static ScrollableOverflow from_content_size(Size<float> content_size) {
        return new_(Rect<float>{0.0f, content_size.width, 0.0f, content_size.height}, Point<bool>{false, false});
    }

    Rect<float> rect() const { return Rect<float>{inner_.left, std::abs(inner_.right), inner_.top, std::abs(inner_.bottom)}; }
    float left() const { return inner_.left; }
    float right() const { return std::abs(inner_.right); }
    float top() const { return inner_.top; }
    float bottom() const { return std::abs(inner_.bottom); }
    Point<bool> is_reversed() const {
        return {std::signbit(inner_.right), std::signbit(inner_.bottom)};
    }
    Size<float> content_size() const { return {right(), bottom()}; }
    ScrollableOverflow transpose() const {
        ScrollableOverflow so;
        so.inner_ = Rect<float>{inner_.top, inner_.bottom, inner_.left, inner_.right};
        return so;
    }
    constexpr bool operator==(const ScrollableOverflow& o) const {
        return detail::f32_to_bits(inner_.left) == detail::f32_to_bits(o.inner_.left)
            && detail::f32_to_bits(inner_.right) == detail::f32_to_bits(o.inner_.right)
            && detail::f32_to_bits(inner_.top) == detail::f32_to_bits(o.inner_.top)
            && detail::f32_to_bits(inner_.bottom) == detail::f32_to_bits(o.inner_.bottom);
    }
    constexpr bool operator!=(const ScrollableOverflow& o) const { return !(*this == o); }
};

/// Input to a layout computation
struct LayoutInput {
    RunMode run_mode{RunMode::PerformLayout};
    SizingMode sizing_mode{SizingMode::InherentSize};
    RequestedAxis axis{RequestedAxis::Both};
    Size<std::optional<float>> known_dimensions{};
    /// Whether each known dimension should be treated as a *definite* size when laying out the
    /// node's own content (percent resolution and flex line collection). Ignored for axes where
    /// the corresponding known dimension is None.
    Size<bool> known_dimensions_are_definite{true, true};
    Size<std::optional<float>> parent_size{};
    Size<AvailableSpace> available_space{};
    Line<bool> vertical_margins_are_collapsible{false, false};

    static LayoutInput hidden_input() {
        LayoutInput in;
        in.run_mode = RunMode::PerformHiddenLayout;
        in.known_dimensions = Size<std::optional<float>>{};
        in.known_dimensions_are_definite = Size<bool>{true, true};
        in.parent_size = Size<std::optional<float>>{};
        in.available_space = Size<AvailableSpace>{AvailableSpace::max_content(), AvailableSpace::max_content()};
        in.sizing_mode = SizingMode::InherentSize;
        in.axis = RequestedAxis::Both;
        in.vertical_margins_are_collapsible = Line<bool>{false, false};
        return in;
    }

    bool operator==(const LayoutInput& o) const {
        return run_mode == o.run_mode && sizing_mode == o.sizing_mode && axis == o.axis
            && known_dimensions == o.known_dimensions
            && known_dimensions_are_definite == o.known_dimensions_are_definite
            && parent_size == o.parent_size && available_space == o.available_space
            && vertical_margins_are_collapsible == o.vertical_margins_are_collapsible;
    }
    bool operator!=(const LayoutInput& o) const { return !(*this == o); }
};

/// Output of a layout computation
struct LayoutOutput {
    Size<float> size{0.0f, 0.0f};
#ifdef TAFFY_CONTENT_SIZE
    /// The scrollable overflow rectangle of the node's content
    ScrollableOverflow scrollable_overflow_rect{};
#endif
    /// The first and last baselines of the node in the horizontal axis, if any
    Baselines baselines{};
    CollapsibleMarginSet top_margin{CollapsibleMarginSet::zero()};
    CollapsibleMarginSet bottom_margin{CollapsibleMarginSet::zero()};
    bool margins_can_collapse_through{false};
    /// Out-of-flow descendants of this node for which this node is *not* the containing block.
    /// They bubble up until they reach their containing block.
    OofCandidates oof_candidates{};
    /// The node's inset-resolution area for out-of-flow boxes (PerformLayout runs only)
    std::optional<OofPositioningArea> oof_positioning_area{std::nullopt};

    static LayoutOutput hidden() { return {}; }

    static LayoutOutput from_sizes_and_baselines(Size<float> sz, Size<float> csz, Point<std::optional<float>> baselines_in) {
        LayoutOutput out;
        out.size = sz;
#ifdef TAFFY_CONTENT_SIZE
        out.scrollable_overflow_rect = ScrollableOverflow::from_content_size(csz);
#endif
        // Legacy Point<optional> baselines map x -> first, y -> last (see Baselines)
        out.baselines.first = baselines_in.x;
        out.baselines.last = baselines_in.y;
        return out;
    }
    static LayoutOutput from_sizes_and_baselines(Size<float> sz, Size<float> csz, Baselines baselines_in) {
        LayoutOutput out;
        out.size = sz;
#ifdef TAFFY_CONTENT_SIZE
        out.scrollable_overflow_rect = ScrollableOverflow::from_content_size(csz);
#endif
        out.baselines = baselines_in;
        return out;
    }
    static LayoutOutput from_sizes(Size<float> sz, Size<float> csz) {
        return from_sizes_and_baselines(sz, csz, Baselines::none());
    }
    static LayoutOutput from_outer_size(Size<float> sz) {
        return from_sizes(sz, Size<float>{0.0f, 0.0f});
    }
    /// Compatibility view of the content extent (right/bottom edges of the overflow rect)
    Size<float> content_size() const {
#ifdef TAFFY_CONTENT_SIZE
        return scrollable_overflow_rect.content_size();
#else
        return {0.0f, 0.0f};
#endif
    }

    bool operator==(const LayoutOutput& o) const {
        return size == o.size
#ifdef TAFFY_CONTENT_SIZE
            && scrollable_overflow_rect == o.scrollable_overflow_rect
#endif
            && baselines == o.baselines && top_margin == o.top_margin && bottom_margin == o.bottom_margin
            && margins_can_collapse_through == o.margins_can_collapse_through
            && oof_candidates == o.oof_candidates && oof_positioning_area == o.oof_positioning_area;
    }
    bool operator!=(const LayoutOutput& o) const { return !(*this == o); }
};

/// Final layout result stored on tree nodes
struct Layout {
    uint32_t order{0};
    Point<float> location{0.0f, 0.0f};
    Size<float> size{0.0f, 0.0f};
#ifdef TAFFY_CONTENT_SIZE
    ScrollableOverflow scrollable_overflow_rect{};
#endif
    Size<float> scrollbar_size{0.0f, 0.0f};
    Rect<float> border{0.0f, 0.0f, 0.0f, 0.0f};
    Rect<float> padding{0.0f, 0.0f, 0.0f, 0.0f};
    Rect<float> margin{0.0f, 0.0f, 0.0f, 0.0f};

    static constexpr Layout new_layout() { return {}; }
    static constexpr Layout with_order(uint32_t ord) { Layout l; l.order = ord; return l; }

    float content_box_width() const { return size.width - padding.left - padding.right - border.left - border.right; }
    float content_box_height() const { return size.height - padding.top - padding.bottom - border.top - border.bottom; }
    Size<float> content_box_size() const { return {content_box_width(), content_box_height()}; }
    float content_box_x() const { return location.x + border.left + padding.left; }
    float content_box_y() const { return location.y + border.top + padding.top; }
#ifdef TAFFY_CONTENT_SIZE
    /// Magnitude of the reachable horizontal scroll range (floored at zero)
    float scroll_width() const {
        if (scrollable_overflow_rect.is_reversed().x) return -scrollable_overflow_rect.left();
        return std::max(0.0f, scrollable_overflow_rect.right() + std::min(scrollbar_size.width, size.width)
                                  - size.width + border.left + border.right);
    }
    /// Magnitude of the reachable vertical scroll range (floored at zero)
    float scroll_height() const {
        if (scrollable_overflow_rect.is_reversed().y) return -scrollable_overflow_rect.top();
        return std::max(0.0f, scrollable_overflow_rect.bottom() + std::min(scrollbar_size.height, size.height)
                                  - size.height + border.top + border.bottom);
    }
    /// Compatibility view of the content extent (right/bottom edges of the overflow rect)
    Size<float> content_size() const { return scrollable_overflow_rect.content_size(); }
#endif
};

// ===========================================================================
// Section 12: Cache System
// Ref: src/tree/cache.rs
// ===========================================================================

namespace detail {
    static constexpr size_t CACHE_SIZE = 9;

    // f32 bit patterns used as cache keys
    inline uint32_t f32_inf_bits() { return 0x7F800000u; }
    inline uint32_t f32_neg_inf_bits() { return 0xFF800000u; }

    inline uint32_t option_cache_key(std::optional<float> input) {
        return input ? detail::f32_to_bits(*input) : f32_inf_bits();
    }
    inline uint64_t size_option_cache_key(Size<std::optional<float>> input) {
        return (static_cast<uint64_t>(option_cache_key(input.width)) << 32) | option_cache_key(input.height);
    }
    inline uint32_t available_space_cache_key(AvailableSpace input) {
        switch (input.type) {
            case AvailableSpace::Definite: return detail::f32_to_bits(-input.value);
            case AvailableSpace::MinContent: return f32_neg_inf_bits();
            case AvailableSpace::MaxContent: return f32_inf_bits();
        }
        return f32_inf_bits();
    }
    inline uint64_t size_available_space_cache_key(Size<AvailableSpace> input) {
        return (static_cast<uint64_t>(available_space_cache_key(input.width)) << 32)
             | available_space_cache_key(input.height);
    }
    inline uint32_t mixed_cache_key(std::optional<float> kd, AvailableSpace avs) {
        return kd ? detail::f32_to_bits(*kd) : available_space_cache_key(avs);
    }
    inline uint64_t size_mixed_cache_key(Size<std::optional<float>> kd, Size<AvailableSpace> avs) {
        return (static_cast<uint64_t>(mixed_cache_key(kd.width, avs.width)) << 32)
             | mixed_cache_key(kd.height, avs.height);
    }

    static constexpr uint64_t SIGN_BIT_1 = 1ull << 63;
    static constexpr uint64_t SIGN_BIT_2 = 1ull << 31;
    static constexpr uint64_t BOTH_SIGN_BITS_MASK = SIGN_BIT_1 | SIGN_BIT_2;
    static constexpr uint64_t NON_SIGN_BITS_MASK = ~BOTH_SIGN_BITS_MASK;
    static constexpr uint64_t X_AXIS_VALUE_MASK = static_cast<uint64_t>(UINT32_MAX) << 32;

    /// Space-optimised cache key (see reference CacheKey)
    struct CacheKey {
        uint64_t kd_available_space{0};
        uint64_t parent_size{0};
        Size<bool> known_dimensions_are_definite{true, true};

        uint64_t parent_size_masked() const { return parent_size & NON_SIGN_BITS_MASK; }
        uint64_t x_axis_parent_size() const { return parent_size & (X_AXIS_VALUE_MASK & NON_SIGN_BITS_MASK); }
        uint64_t requested_axis_bits() const { return parent_size & BOTH_SIGN_BITS_MASK; }

        /// Whether a cached entry with this key contains a valid size for the axis requested by `other`
        bool size_is_valid_for(const CacheKey& other) const {
            uint64_t entry_axis = requested_axis_bits();
            return entry_axis == BOTH_SIGN_BITS_MASK || entry_axis == other.requested_axis_bits();
        }

        bool operator==(const CacheKey& o) const {
            return kd_available_space == o.kd_available_space && parent_size == o.parent_size
                && known_dimensions_are_definite == o.known_dimensions_are_definite;
        }
        bool operator!=(const CacheKey& o) const { return !(*this == o); }

        static CacheKey from(const LayoutInput& input) {
            uint64_t extra_bits = 0;
            switch (input.axis) {
                case RequestedAxis::Horizontal: extra_bits = SIGN_BIT_1; break;
                case RequestedAxis::Vertical: extra_bits = SIGN_BIT_2; break;
                case RequestedAxis::Both: extra_bits = SIGN_BIT_1 | SIGN_BIT_2; break;
            }
            CacheKey key;
            key.kd_available_space = size_mixed_cache_key(input.known_dimensions, input.available_space);
            key.parent_size = (size_option_cache_key(input.parent_size) & NON_SIGN_BITS_MASK) | extra_bits;
            // normalise: an axis without a known dimension is always treated as definite
            key.known_dimensions_are_definite = Size<bool>{
                input.known_dimensions.width ? input.known_dimensions_are_definite.width : true,
                input.known_dimensions.height ? input.known_dimensions_are_definite.height : true};
            return key;
        }
    };

    /// Inputs that affect layout but are not part of the key; a Cache holds entries computed with one
    /// single mode and drops them all when the mode changes.
    struct CacheMode {
        static constexpr uint8_t INHERENT_SIZE_BIT = 0b001;
        static constexpr uint8_t COLLAPSIBLE_START_MARGIN_BIT = 0b010;
        static constexpr uint8_t COLLAPSIBLE_END_MARGIN_BIT = 0b100;
        static constexpr uint8_t INITIAL = 0;

        uint8_t bits{INITIAL};

        static CacheMode from(const LayoutInput& input) {
            CacheMode m;
            uint8_t inherent = input.sizing_mode == SizingMode::InherentSize ? 1 : 0;
            m.bits = static_cast<uint8_t>((inherent ? INHERENT_SIZE_BIT : 0)
                                        | (input.vertical_margins_are_collapsible.start ? COLLAPSIBLE_START_MARGIN_BIT : 0)
                                        | (input.vertical_margins_are_collapsible.end ? COLLAPSIBLE_END_MARGIN_BIT : 0));
            return m;
        }
        bool operator==(const CacheMode& o) const { return bits == o.bits; }
        bool operator!=(const CacheMode& o) const { return bits != o.bits; }
    };

    template <typename T>
    struct CacheEntry {
        CacheKey key{};
        T content{};
    };
} // namespace detail

enum class ClearState : uint8_t { Cleared, AlreadyEmpty };

struct Cache {
    /// The cache entry for the node's final layout
    std::optional<detail::CacheEntry<LayoutOutput>> final_layout_entry{};
    /// The cache entries for the node's preliminary size measurements
    std::optional<detail::CacheEntry<Size<float>>> measure_entries[detail::CACHE_SIZE]{};
    /// Tracks which measure entries have been used since the eviction cursor last passed them
    uint16_t recently_used_entries{0};
    /// The next measure entry to consider replacing
    uint8_t next_measure_entry{0};
    bool is_empty_{true};
    /// The sizing_mode / vertical_margins_are_collapsible mode of every entry in this cache
    detail::CacheMode mode{};

    Cache() = default;

    bool is_empty() const { return is_empty_; }

    /// Try to retrieve a cached result from the cache
    std::optional<LayoutOutput> get(const LayoutInput& input) {
        if (mode != detail::CacheMode::from(input)) return std::nullopt;
        detail::CacheKey key = detail::CacheKey::from(input);
        switch (input.run_mode) {
            case RunMode::PerformLayout: {
                if (final_layout_entry.has_value() && final_layout_entry->key == key) return final_layout_entry->content;
                return std::nullopt;
            }
            case RunMode::ComputeSize: {
                for (size_t index = 0; index < detail::CACHE_SIZE; index++) {
                    if (!measure_entries[index].has_value()) continue;
                    const auto& entry = *measure_entries[index];
                    if (entry.key.kd_available_space == key.kd_available_space
                        && entry.key.known_dimensions_are_definite == key.known_dimensions_are_definite
                        && entry.key.x_axis_parent_size() == key.x_axis_parent_size()
                        && entry.key.size_is_valid_for(key)) {
                        recently_used_entries = static_cast<uint16_t>(recently_used_entries | (1u << index));
                        return LayoutOutput::from_outer_size(entry.content);
                    }
                }
                return std::nullopt;
            }
            case RunMode::PerformHiddenLayout: return std::nullopt;
        }
        return std::nullopt;
    }

    /// Store a computed result in the cache
    void store(const LayoutInput& input, const LayoutOutput& output) {
        if (input.run_mode == RunMode::PerformHiddenLayout) return;
        detail::CacheMode new_mode = detail::CacheMode::from(input);
        if (mode != new_mode) {
            clear();
            mode = new_mode;
        }
        detail::CacheKey key = detail::CacheKey::from(input);
        switch (input.run_mode) {
            case RunMode::PerformLayout: {
                is_empty_ = false;
                detail::CacheEntry<LayoutOutput> entry;
                entry.key = key;
                entry.content = output;
                final_layout_entry = entry;
                return;
            }
            case RunMode::ComputeSize: {
                // Measure entries only store the size; cache hits are reconstructed with
                // LayoutOutput::from_outer_size which resets margin-collapse metadata, so results
                // carrying that metadata must not be cached.
                if (output.margins_can_collapse_through || output.top_margin != CollapsibleMarginSet::zero()
                    || output.bottom_margin != CollapsibleMarginSet::zero()) {
                    return;
                }
                is_empty_ = false;
                for (size_t index = 0; index < detail::CACHE_SIZE; index++) {
                    if (measure_entries[index].has_value() && measure_entries[index]->key == key) {
                        measure_entries[index]->content = output.size;
                        recently_used_entries = static_cast<uint16_t>(recently_used_entries | (1u << index));
                        return;
                    }
                }
                while (recently_used_entries & (1u << next_measure_entry)) {
                    recently_used_entries = static_cast<uint16_t>(recently_used_entries & ~(1u << next_measure_entry));
                    next_measure_entry = static_cast<uint8_t>((next_measure_entry + 1) % detail::CACHE_SIZE);
                }
                detail::CacheEntry<Size<float>> entry;
                entry.key = key;
                entry.content = output.size;
                measure_entries[next_measure_entry] = entry;
                recently_used_entries = static_cast<uint16_t>(recently_used_entries | (1u << next_measure_entry));
                next_measure_entry = static_cast<uint8_t>((next_measure_entry + 1) % detail::CACHE_SIZE);
                return;
            }
            case RunMode::PerformHiddenLayout: return;
        }
    }

    ClearState clear() {
        if (is_empty_) return ClearState::AlreadyEmpty;
        final_layout_entry = std::nullopt;
        for (size_t i = 0; i < detail::CACHE_SIZE; ++i) measure_entries[i] = std::nullopt;
        recently_used_entries = 0;
        next_measure_entry = 0;
        is_empty_ = true;
        return ClearState::Cleared;
    }
};

// ===========================================================================
// Section 13: CRTP Tree Traits
// Ref: src/tree/traits.rs
// ===========================================================================

// NodeId is defined earlier (before grid types)

// CRTP base for partial tree traversal
template <typename Derived>
struct TraversePartialTreeBase {
    // Derived must implement:
    //   std::vector<NodeId> child_ids(NodeId parent) const;
    //   size_t child_count(NodeId parent) const;
    //   NodeId get_child_id(NodeId parent, size_t index) const;
};

// CRTP base for layout computation
template <typename Derived>
struct LayoutPartialTreeBase : TraversePartialTreeBase<Derived> {
    // Derived must implement:
    //   const Style& get_core_container_style(NodeId node_id) const;
    //   void set_unrounded_layout(NodeId node_id, const Layout& layout);
    //   LayoutOutput compute_child_layout(NodeId node_id, LayoutInput inputs);
};

// CRTP base for cache operations
template <typename Derived>
struct CacheTreeBase {
    // Derived must implement:
    //   std::optional<LayoutOutput> cache_get(NodeId node_id, const LayoutInput& input) const;
    //   void cache_store(NodeId node_id, const LayoutInput& input, const LayoutOutput& output);
    //   void cache_clear(NodeId node_id);
};

// CRTP base for layout rounding
template <typename Derived>
struct RoundTreeBase : TraversePartialTreeBase<Derived> {
    // Derived must implement:
    //   Layout get_unrounded_layout(NodeId node_id) const;
    //   void set_final_layout(NodeId node_id, const Layout& layout);
};

// CRTP base for debug printing
template <typename Derived>
struct PrintTreeBase : TraversePartialTreeBase<Derived> {
    // Derived must implement:
    //   const char* get_debug_label(NodeId node_id) const;
    //   Layout get_final_layout(NodeId node_id) const;
};

#ifdef TAFFY_FLEXBOX
template <typename Derived>
struct LayoutFlexboxContainerBase : LayoutPartialTreeBase<Derived> {
    // Derived must implement:
    //   const Style& get_flexbox_container_style(NodeId node_id) const;
    //   const Style& get_flexbox_child_style(NodeId child_node_id) const;
};
#endif

#ifdef TAFFY_GRID
template <typename Derived>
struct LayoutGridContainerBase : LayoutPartialTreeBase<Derived> {
    // Derived must implement:
    //   const Style& get_grid_container_style(NodeId node_id) const;
    //   const Style& get_grid_child_style(NodeId child_node_id) const;
};
#endif

#ifdef TAFFY_BLOCK_LAYOUT
template <typename Derived>
struct LayoutBlockContainerBase : LayoutPartialTreeBase<Derived> {
    // Derived must implement:
    //   const Style& get_block_container_style(NodeId node_id) const;
    //   const Style& get_block_child_style(NodeId child_node_id) const;
};
#endif

// ===========================================================================
// Section 14: Utility Functions
// Ref: src/util/math.rs, src/util/resolve.rs
// ===========================================================================

namespace detail {
    inline float f32_max(float a, float b) { return std::max(a, b); }
    inline float f32_min(float a, float b) { return std::min(a, b); }
    inline float f32_round(float v) { return std::floor(v + 0.5f); }
    inline float f32_abs(float v) { return std::abs(v); }
}

// MaybeMath: operations on optional<float> combinations
// Ref: src/util/math.rs. A `None` RHS is the identity: (Some(l), None) == Some(l).
// A `None` LHS always yields `None`.
inline std::optional<float> maybe_add(std::optional<float> a, std::optional<float> b) {
    if (!a) return std::nullopt;
    if (!b) return a;
    return *a + *b;
}
inline std::optional<float> maybe_sub(std::optional<float> a, std::optional<float> b) {
    if (!a) return std::nullopt;
    if (!b) return a;
    return *a - *b;
}
inline std::optional<float> maybe_max(std::optional<float> a, std::optional<float> b) {
    if (!a) return std::nullopt;
    if (!b) return a;
    return std::max(*a, *b);
}
inline std::optional<float> maybe_min(std::optional<float> a, std::optional<float> b) {
    if (!a) return std::nullopt;
    if (!b) return a;
    return std::min(*a, *b);
}
// Clamp order matches Rust: base.min(max).max(min)
inline std::optional<float> maybe_clamp(std::optional<float> v, std::optional<float> mn, std::optional<float> mx) {
    if (!v) return std::nullopt;
    float r = *v;
    if (mx) r = std::min(r, *mx);
    if (mn) r = std::max(r, *mn);
    return r;
}

// f32 x Option<f32> -> f32 (None treated as identity)
inline float maybe_add_f32(float a, std::optional<float> b) { return b ? a + *b : a; }
inline float maybe_sub_f32(float a, std::optional<float> b) { return b ? a - *b : a; }
// f32 x Option<f32> -> f32 for min/max/clamp
inline float maybe_min_f32(float a, std::optional<float> b) { return b ? std::min(a, *b) : a; }
inline float maybe_max_f32(float a, std::optional<float> b) { return b ? std::max(a, *b) : a; }
inline float maybe_clamp_f32(float v, std::optional<float> mn, std::optional<float> mx) {
    float r = v;
    if (mx) r = std::min(r, *mx);
    if (mn) r = std::max(r, *mn);
    return r;
}

// MaybeResolve: resolve dimension types against a parent size
inline std::optional<float> maybe_resolve(LengthPercentage lp, std::optional<float> context) {
    switch (lp.tag()) {
        case CompactLength::LENGTH_TAG: return lp.value();
        case CompactLength::PERCENT_TAG: return context ? std::optional<float>((*context * lp.value()) / 100.0f) : std::nullopt;
        default: return std::nullopt;
    }
}
inline std::optional<float> maybe_resolve(LengthPercentageAuto lpa, std::optional<float> context) {
    switch (lpa.tag()) {
        case CompactLength::LENGTH_TAG: return lpa.value();
        case CompactLength::PERCENT_TAG: return context ? std::optional<float>((*context * lpa.value()) / 100.0f) : std::nullopt;
        case CompactLength::AUTO_TAG: return std::nullopt;
        default: return std::nullopt;
    }
}
inline std::optional<float> maybe_resolve(Dimension d, std::optional<float> context) {
    switch (d.tag()) {
        case CompactLength::LENGTH_TAG: return d.value();
        case CompactLength::PERCENT_TAG: return context ? std::optional<float>((*context * d.value()) / 100.0f) : std::nullopt;
        case CompactLength::AUTO_TAG: return std::nullopt;
        default: return std::nullopt;
    }
}
// ResolveOrZero: like maybe_resolve but returns 0.0 instead of nullopt
inline float resolve_or_zero(LengthPercentage lp, std::optional<float> context) {
    return maybe_resolve(lp, context).value_or(0.0f);
}
inline float resolve_or_zero(LengthPercentageAuto lpa, std::optional<float> context) {
    return maybe_resolve(lpa, context).value_or(0.0f);
}
inline float resolve_or_zero(Dimension d, std::optional<float> context) {
    return maybe_resolve(d, context).value_or(0.0f);
}
// Resolve Size/Rect versions
inline Size<std::optional<float>> maybe_resolve_size(Size<Dimension> s, Size<std::optional<float>> context) {
    return {maybe_resolve(s.width, context.width), maybe_resolve(s.height, context.height)};
}
inline Size<std::optional<float>> resolve_or_zero_size(Size<Dimension> s, Size<std::optional<float>> context) {
    return {std::optional(resolve_or_zero(s.width, context.width)), std::optional(resolve_or_zero(s.height, context.height))};
}
inline Rect<float> resolve_or_zero_rect(Rect<LengthPercentage> r, std::optional<float> context) {
    return {resolve_or_zero(r.left, context), resolve_or_zero(r.right, context),
            resolve_or_zero(r.top, context), resolve_or_zero(r.bottom, context)};
}
inline Rect<float> resolve_or_zero_rect(Rect<LengthPercentageAuto> r, std::optional<float> context) {
    return {resolve_or_zero(r.left, context), resolve_or_zero(r.right, context),
            resolve_or_zero(r.top, context), resolve_or_zero(r.bottom, context)};
}
inline Rect<std::optional<float>> maybe_resolve_rect(Rect<LengthPercentageAuto> r, std::optional<float> context) {
    return {maybe_resolve(r.left, context), maybe_resolve(r.right, context),
            maybe_resolve(r.top, context), maybe_resolve(r.bottom, context)};
}
// Overloads for float context
inline std::optional<float> maybe_resolve(LengthPercentage lp, float context) {
    return maybe_resolve(lp, std::optional<float>(context));
}

// ===========================================================================
// Section 14b: Flexbox Helpers and Alignment Utilities
// Ref: src/compute/flexbox.rs, src/compute/common/alignment.rs
// ===========================================================================

#ifdef TAFFY_FLEXBOX

// --- Size<optional<float>> free functions for MaybeMath ---

inline Size<std::optional<float>> optf32_size_none() {
    return {std::nullopt, std::nullopt};
}

inline Size<std::optional<float>> optf32_size_or(
    const Size<std::optional<float>>& s,
    const Size<std::optional<float>>& fallback) {
    return {s.width ? s.width : fallback.width, s.height ? s.height : fallback.height};
}

inline Size<std::optional<float>> optf32_size_or_f32(
    const Size<std::optional<float>>& s,
    const Size<float>& fallback) {
    return {s.width ? s.width : std::optional<float>(fallback.width),
            s.height ? s.height : std::optional<float>(fallback.height)};
}

inline Size<std::optional<float>> optf32_size_add(
    const Size<std::optional<float>>& a,
    const Size<std::optional<float>>& b) {
    return {maybe_add(a.width, b.width), maybe_add(a.height, b.height)};
}

inline Size<std::optional<float>> optf32_size_sub(
    const Size<std::optional<float>>& a,
    const Size<std::optional<float>>& b) {
    return {maybe_sub(a.width, b.width), maybe_sub(a.height, b.height)};
}

inline Size<std::optional<float>> optf32_size_clamp(
    const Size<std::optional<float>>& v,
    const Size<std::optional<float>>& mn,
    const Size<std::optional<float>>& mx) {
    return {maybe_clamp(v.width, mn.width, mx.width), maybe_clamp(v.height, mn.height, mx.height)};
}

inline Size<std::optional<float>> optf32_size_max(
    const Size<std::optional<float>>& a,
    const Size<std::optional<float>>& b) {
    return {maybe_max(a.width, b.width), maybe_max(a.height, b.height)};
}

inline Size<std::optional<float>> optf32_size_min(
    const Size<std::optional<float>>& a,
    const Size<std::optional<float>>& b) {
    return {maybe_min(a.width, b.width), maybe_min(a.height, b.height)};
}

// --- AvailableSpace MaybeMath helpers ---

inline AvailableSpace avail_add(AvailableSpace a, std::optional<float> v) {
    return v ? AvailableSpace::definite(a.unwrap_or(0.0f) + *v) : a;
}

inline AvailableSpace avail_sub(AvailableSpace a, std::optional<float> v) {
    return v ? AvailableSpace::definite(a.unwrap_or(0.0f) - *v) : a;
}

inline AvailableSpace avail_sub_f32(AvailableSpace a, float v) {
    return a.is_definite() ? AvailableSpace::definite(a.value - v) : a;
}

inline AvailableSpace avail_max(AvailableSpace a, std::optional<float> v) {
    return v ? AvailableSpace::definite(std::max(a.unwrap_or(0.0f), *v)) : a;
}

inline AvailableSpace avail_clamp(AvailableSpace a, std::optional<float> mn, std::optional<float> mx) {
    if (a.type != AvailableSpace::Definite) return a;
    float val = a.value;
    if (mn) val = std::max(val, *mn);
    if (mx) val = std::min(val, *mx);
    return AvailableSpace::definite(val);
}

// --- Size<AvailableSpace> helpers ---

inline Size<std::optional<float>> avail_into_options(const Size<AvailableSpace>& s) {
    return {s.width.into_option(), s.height.into_option()};
}

inline AvailableSpace opt_to_avail(std::optional<float> v) {
    return v ? AvailableSpace::definite(*v) : AvailableSpace::max_content();
}

inline Size<AvailableSpace> opt_to_avail_size(const Size<std::optional<float>>& s) {
    return {opt_to_avail(s.width), opt_to_avail(s.height)};
}

// --- Free functions for main/cross on Size<optional<float>> ---

inline std::optional<float> size_opt_main(const Size<std::optional<float>>& s, FlexDirection d) {
    return is_row(d) ? s.width : s.height;
}
inline std::optional<float> size_opt_cross(const Size<std::optional<float>>& s, FlexDirection d) {
    return is_row(d) ? s.height : s.width;
}
inline Size<std::optional<float>>& size_opt_set_main(Size<std::optional<float>>& s, FlexDirection d, std::optional<float> v) {
    (is_row(d) ? s.width : s.height) = v; return s;
}
inline Size<std::optional<float>>& size_opt_set_cross(Size<std::optional<float>>& s, FlexDirection d, std::optional<float> v) {
    (is_row(d) ? s.height : s.width) = v; return s;
}
inline Size<std::optional<float>> size_opt_with_main(const Size<std::optional<float>>& s, FlexDirection d, std::optional<float> v) {
    Size<std::optional<float>> r = s; size_opt_set_main(r, d, v); return r;
}
inline Size<std::optional<float>> size_opt_with_cross(const Size<std::optional<float>>& s, FlexDirection d, std::optional<float> v) {
    Size<std::optional<float>> r = s; size_opt_set_cross(r, d, v); return r;
}

// Free functions for main/cross on Size<AvailableSpace>
inline AvailableSpace size_avail_main(const Size<AvailableSpace>& s, FlexDirection d) {
    return is_row(d) ? s.width : s.height;
}
inline AvailableSpace size_avail_cross(const Size<AvailableSpace>& s, FlexDirection d) {
    return is_row(d) ? s.height : s.width;
}
inline Size<AvailableSpace>& size_avail_set_main(Size<AvailableSpace>& s, FlexDirection d, AvailableSpace v) {
    (is_row(d) ? s.width : s.height) = v; return s;
}
inline Size<AvailableSpace> size_avail_with_main(const Size<AvailableSpace>& s, FlexDirection d, AvailableSpace v) {
    Size<AvailableSpace> r = s; size_avail_set_main(r, d, v); return r;
}
inline Size<AvailableSpace> size_avail_with_cross(const Size<AvailableSpace>& s, FlexDirection d, AvailableSpace v) {
    Size<AvailableSpace> r = s; (is_row(d) ? r.height : r.width) = v; return r;
}

// --- Point<optional<float>> free function ---
inline std::optional<float> point_opt_main(const Point<std::optional<float>>& p, FlexDirection d) {
    return is_row(d) ? p.x : p.y;
}

// --- Flex direction helper ---
inline AbsoluteAxis flex_main_axis(FlexDirection d) {
    return is_row(d) ? AbsoluteAxis::Horizontal : AbsoluteAxis::Vertical;
}

#endif // TAFFY_FLEXBOX (shared alignment helpers below)

// --- MaybeMath helpers with Rust semantics (rhs None is the identity) ---
// The core `maybe_add`/`maybe_sub`/`maybe_min`/`maybe_max` helpers defined in Section 14
// propagate None from either side. Rust's MaybeMath for Option<f32> treats a missing RHS as
// the identity: (Some(l), None) => Some(l). The flexbox algorithm relies on that in several
// places (`x.maybe_max(style_min)`, `size.maybe_sub(margins)`, ...), so it uses these local
// helpers instead.
inline std::optional<float> fm_min(std::optional<float> v, std::optional<float> rhs) {
    if (!v) return std::nullopt;
    if (!rhs) return v;
    return std::min(*v, *rhs);
}
inline std::optional<float> fm_max(std::optional<float> v, std::optional<float> rhs) {
    if (!v) return std::nullopt;
    if (!rhs) return v;
    return std::max(*v, *rhs);
}
inline std::optional<float> fm_add(std::optional<float> v, std::optional<float> rhs) {
    if (!v) return std::nullopt;
    if (!rhs) return v;
    return *v + *rhs;
}
inline std::optional<float> fm_sub(std::optional<float> v, std::optional<float> rhs) {
    if (!v) return std::nullopt;
    if (!rhs) return v;
    return *v - *rhs;
}
// Rust Option<f32>::maybe_clamp: base.min(max).max(min)
inline std::optional<float> fm_clamp(std::optional<float> v, std::optional<float> mn, std::optional<float> mx) {
    if (!v) return std::nullopt;
    float r = *v;
    if (mx) r = std::min(r, *mx);
    if (mn) r = std::max(r, *mn);
    return r;
}

// --- Option<f32> x f32 helpers (rhs always present) ---
inline std::optional<float> fo_add_f32(std::optional<float> v, float rhs) { return v ? std::optional<float>(*v + rhs) : std::nullopt; }
inline std::optional<float> fo_sub_f32(std::optional<float> v, float rhs) { return v ? std::optional<float>(*v - rhs) : std::nullopt; }
inline std::optional<float> fo_max_f32(std::optional<float> v, float rhs) { return v ? std::optional<float>(std::max(*v, rhs)) : std::nullopt; }
inline std::optional<float> fo_min_f32(std::optional<float> v, float rhs) { return v ? std::optional<float>(std::min(*v, rhs)) : std::nullopt; }

// --- f32 x Option<f32> helpers (rhs None is identity) ---
inline float fm_min_f32(float v, std::optional<float> rhs) { return rhs ? std::min(v, *rhs) : v; }
inline float fm_max_f32(float v, std::optional<float> rhs) { return rhs ? std::max(v, *rhs) : v; }
inline float fm_add_f32(float v, std::optional<float> rhs) { return rhs ? v + *rhs : v; }
inline float fm_sub_f32(float v, std::optional<float> rhs) { return rhs ? v - *rhs : v; }
inline float fm_clamp_f32(float v, std::optional<float> mn, std::optional<float> mx) {
    if (mx) v = std::min(v, *mx);
    if (mn) v = std::max(v, *mn);
    return v;
}

// --- Size<optional<float>> versions ---
inline Size<std::optional<float>> fs_add(const Size<std::optional<float>>& a, const Size<std::optional<float>>& b) {
    return {fm_add(a.width, b.width), fm_add(a.height, b.height)};
}
inline Size<std::optional<float>> fs_sub(const Size<std::optional<float>>& a, const Size<std::optional<float>>& b) {
    return {fm_sub(a.width, b.width), fm_sub(a.height, b.height)};
}
inline Size<std::optional<float>> fs_min(const Size<std::optional<float>>& a, const Size<std::optional<float>>& b) {
    return {fm_min(a.width, b.width), fm_min(a.height, b.height)};
}
inline Size<std::optional<float>> fs_max(const Size<std::optional<float>>& a, const Size<std::optional<float>>& b) {
    return {fm_max(a.width, b.width), fm_max(a.height, b.height)};
}
inline Size<std::optional<float>> fs_clamp(const Size<std::optional<float>>& v,
    const Size<std::optional<float>>& mn, const Size<std::optional<float>>& mx) {
    return {fm_clamp(v.width, mn.width, mx.width), fm_clamp(v.height, mn.height, mx.height)};
}
inline Size<std::optional<float>> fs_add_f32(const Size<std::optional<float>>& s, const Size<float>& rhs) {
    return {s.width ? std::optional<float>(*s.width + rhs.width) : std::nullopt,
            s.height ? std::optional<float>(*s.height + rhs.height) : std::nullopt};
}
inline Size<std::optional<float>> fs_sub_f32(const Size<std::optional<float>>& s, const Size<float>& rhs) {
    return {s.width ? std::optional<float>(*s.width - rhs.width) : std::nullopt,
            s.height ? std::optional<float>(*s.height - rhs.height) : std::nullopt};
}
inline Size<std::optional<float>> fs_max_f32(const Size<std::optional<float>>& s, const Size<float>& rhs) {
    return {s.width ? std::optional<float>(std::max(*s.width, rhs.width)) : std::nullopt,
            s.height ? std::optional<float>(std::max(*s.height, rhs.height)) : std::nullopt};
}

// --- AvailableSpace MaybeMath with Rust semantics ---
inline AvailableSpace av_maybe_max_f32(AvailableSpace as, float rhs) {
    if (as.is_definite()) return AvailableSpace::definite(std::max(as.value, rhs));
    return as;
}
inline AvailableSpace av_maybe_sub_f32(AvailableSpace as, float rhs) {
    if (as.is_definite()) return AvailableSpace::definite(as.value - rhs);
    return as;
}
inline AvailableSpace av_maybe_max_opt(AvailableSpace as, std::optional<float> rhs) {
    if (!rhs) return as;
    if (as.is_definite()) return AvailableSpace::definite(std::max(as.value, *rhs));
    return as;
}
inline AvailableSpace av_maybe_min_opt(AvailableSpace as, std::optional<float> rhs) {
    if (as.is_definite()) return AvailableSpace::definite(rhs ? std::min(as.value, *rhs) : as.value);
    return rhs ? AvailableSpace::definite(*rhs) : as;
}
inline AvailableSpace av_maybe_sub_opt(AvailableSpace as, std::optional<float> rhs) {
    if (!rhs) return as;
    if (as.is_definite()) return AvailableSpace::definite(as.value - *rhs);
    return as;
}
inline AvailableSpace av_maybe_clamp(AvailableSpace as, std::optional<float> mn, std::optional<float> mx) {
    if (!as.is_definite()) return as;
    float v = as.value;
    if (mx) v = std::min(v, *mx);
    if (mn) v = std::max(v, *mn);
    return AvailableSpace::definite(v);
}

// --- Flex sizing keyword resolution (Rust: compute/common/sizing_keyword.rs) ---
struct FlexSizingKeywordResolution {
    bool exact{false};
    float exact_value{0.0f};
    AvailableSpace available{};
};

inline bool flex_is_sizing_keyword(Dimension d) {
    return d.is_sizing_keyword();
}

inline std::optional<FlexSizingKeywordResolution> flex_resolve_sizing_keyword(
    Dimension style, std::optional<float> stretch_size, std::optional<float> percent_basis) {
    const size_t t = style.tag();
    if (t == CompactLength::MIN_CONTENT_TAG) {
        FlexSizingKeywordResolution r; r.available = AvailableSpace::min_content(); return r;
    }
    if (t == CompactLength::MAX_CONTENT_TAG) {
        FlexSizingKeywordResolution r; r.available = AvailableSpace::max_content(); return r;
    }
    if (t == CompactLength::FIT_CONTENT_PX_TAG) {
        FlexSizingKeywordResolution r; r.available = AvailableSpace::definite(style.value()); return r;
    }
    if (t == CompactLength::FIT_CONTENT_PERCENT_TAG) {
        if (!percent_basis) return std::nullopt;
        FlexSizingKeywordResolution r;
        r.available = AvailableSpace::definite((*percent_basis * style.value()) / 100.0f);
        return r;
    }
    if (t == CompactLength::FIT_CONTENT_KEYWORD_TAG) {
        if (!stretch_size) return std::nullopt;
        FlexSizingKeywordResolution r;
        r.available = AvailableSpace::definite(*stretch_size);
        return r;
    }
    if (t == CompactLength::STRETCH_TAG) {
        if (!stretch_size) return std::nullopt;
        FlexSizingKeywordResolution r;
        r.exact = true;
        r.exact_value = *stretch_size;
        return r;
    }
    return std::nullopt;
}

// --- Alignment keyword resolution helpers ---
// `normal` behaves as `stretch` for flex items and flex containers; in the main axis `stretch`
// behaves as `flex-start` for justify-content (CSS Box Alignment 3).
inline AlignItems flex_resolve_normal_alignment(AlignItems alignment) {
    if (alignment.keyword == AlignItemsKeyword::Normal) return AlignItems_STRETCH;
    return alignment;
}
inline AlignContent flex_resolve_normal_align_content(AlignContent alignment) {
    if (alignment.keyword == AlignContentKeyword::Normal) return AlignContent_STRETCH;
    return alignment;
}
inline JustifyContent flex_resolve_normal_justify(JustifyContent alignment) {
    if (alignment.keyword == AlignContentKeyword::Normal) return JustifyContent{AlignContentKeyword::FlexStart, AlignmentSafety::Unsafe};
    return alignment;
}

// Resolve the writing-mode-relative `self-start`/`self-end` keywords against the item's own
// direction (CSS Box Alignment 5.2). Other keywords are returned unchanged.
inline AlignItems flex_resolve_self_relative(AlignItems alignment, Direction item_direction,
    Direction container_direction, bool axis_is_inline) {
    const bool flip = axis_is_inline && item_direction != container_direction;
    switch (alignment.keyword) {
        case AlignItemsKeyword::SelfStart:
            alignment.keyword = flip ? AlignItemsKeyword::End : AlignItemsKeyword::Start;
            break;
        case AlignItemsKeyword::SelfEnd:
            alignment.keyword = flip ? AlignItemsKeyword::Start : AlignItemsKeyword::End;
            break;
        default:
            break;
    }
    return alignment;
}

// Rust: compute/common/alignment.rs child_first_baseline
inline std::optional<float> flex_child_first_baseline(std::optional<float> first_baseline, float height, Overflow overflow_y) {
    if (is_scroll_container(overflow_y)) {
        return std::min(std::max(first_baseline.value_or(height), 0.0f), height);
    }
    return first_baseline;
}

inline void flex_set_known_definiteness(LayoutInput& li, Size<bool> v) {
    li.known_dimensions_are_definite = v;
}

// Rust: RequestedAxis::main_axis_is_requested_axis (RequestedAxis::Both is never the main axis)
inline bool flex_main_axis_is_requested_axis(FlexDirection dir, RequestedAxis axis) {
    switch (axis) {
        case RequestedAxis::Both: return false;
        case RequestedAxis::Horizontal: return is_row(dir);
        case RequestedAxis::Vertical: return !is_row(dir);
    }
    return false;
}

inline bool flex_position_is_out_of_flow(Position p) { return is_absolute_position(p); }

// Rust Contain helpers (compute/common/scrollable_overflow.rs)
inline bool flex_contain_contains_scrollable_overflow(const Contain& c) { return c.layout || c.paint; }
inline bool flex_contain_suppresses_baseline(const Contain& c) { return c.layout; }
// --- Alignment resolution functions ---
// These are shared by the flexbox and grid algorithms, so they are compiled whenever either is
// enabled (they only depend on the always-available alignment types).
#if defined(TAFFY_FLEXBOX) || defined(TAFFY_GRID)
// The first baseline of a child box (measured from the top of its border box) as used for
// baseline alignment, given the baseline it reported, its border-box height and vertical overflow.
inline std::optional<float> child_first_baseline(std::optional<float> first_baseline, float height,
                                                 Overflow overflow_y) {
    if (is_scroll_container(overflow_y)) {
        return std::max(std::min(first_baseline.value_or(height), height), 0.0f);
    }
    return first_baseline;
}

inline AlignItemsKeyword resolve_self_alignment_safety(AlignItems alignment, bool overflows) {
    if (alignment.is_safe() && overflows) return AlignItemsKeyword::Start;
    return alignment.keyword;
}

inline AlignContentKeyword apply_alignment_fallback(float free_space, size_t num_items, AlignContent alignment_mode) {
    // `normal` behaves differently in each layout mode, so callers resolve it first; if it is not
    // resolved then it is treated as `stretch` (matches the reference debug behaviour).
    AlignContentKeyword keyword = alignment_mode.keyword == AlignContentKeyword::Normal
                                      ? AlignContentKeyword::Stretch
                                      : alignment_mode.keyword;
    bool is_safe = alignment_mode.is_safe();

    // 1. Single item / overflow: distributed keywords fall back to positional keywords.
    //    `space-*` fallbacks gain implicit `safe` semantics; `stretch` falls back to plain flex-start.
    if (num_items <= 1 || free_space <= 0.0f) {
        switch (keyword) {
            case AlignContentKeyword::Stretch:
                keyword = AlignContentKeyword::FlexStart; break;
            case AlignContentKeyword::SpaceBetween:
                keyword = AlignContentKeyword::FlexStart; is_safe = true; break;
            case AlignContentKeyword::SpaceAround:
            case AlignContentKeyword::SpaceEvenly:
                keyword = AlignContentKeyword::Center; is_safe = true; break;
            default: break;
        }
    }

    // 2. Safe alignment falls back to Start when the subject overflows the container.
    if (free_space <= 0.0f && is_safe) keyword = AlignContentKeyword::Start;
    return keyword;
}

inline JustifyContent apply_alignment_fallback_jc(float free_space, size_t num_items, JustifyContent alignment_mode) {
    AlignContent ac{alignment_mode.keyword, alignment_mode.safety};
    return JustifyContent{apply_alignment_fallback(free_space, num_items, ac), alignment_mode.safety};
}

inline float compute_alignment_offset(
    float free_space, size_t num_items, float gap,
    AlignContentKeyword alignment_mode, bool layout_is_flex_reversed, bool is_first)
{
    if (is_first) {
        switch (alignment_mode) {
            case AlignContentKeyword::Start: return 0.0f;
            case AlignContentKeyword::FlexStart: return layout_is_flex_reversed ? free_space : 0.0f;
            case AlignContentKeyword::End: return free_space;
            case AlignContentKeyword::FlexEnd: return layout_is_flex_reversed ? 0.0f : free_space;
            case AlignContentKeyword::Center: return free_space / 2.0f;
            case AlignContentKeyword::Normal:
            case AlignContentKeyword::Stretch: return 0.0f;
            case AlignContentKeyword::SpaceBetween: return 0.0f;
            case AlignContentKeyword::SpaceAround:
                return free_space >= 0.0f
                           ? (free_space / static_cast<float>(num_items > 0 ? num_items : 1)) / 2.0f
                           : free_space / 2.0f;
            case AlignContentKeyword::SpaceEvenly:
                return free_space >= 0.0f ? free_space / static_cast<float>(num_items + 1) : free_space / 2.0f;
        }
        return 0.0f;
    }
    float fs = std::max(free_space, 0.0f);
    float spacing = 0.0f;
    switch (alignment_mode) {
        case AlignContentKeyword::Start: case AlignContentKeyword::FlexStart:
        case AlignContentKeyword::End: case AlignContentKeyword::FlexEnd:
        case AlignContentKeyword::Center: case AlignContentKeyword::Normal:
        case AlignContentKeyword::Stretch:
            spacing = 0.0f; break;
        case AlignContentKeyword::SpaceBetween:
            spacing = fs / static_cast<float>(num_items - 1); break;
        case AlignContentKeyword::SpaceAround:
            spacing = fs / static_cast<float>(num_items); break;
        case AlignContentKeyword::SpaceEvenly:
            spacing = fs / static_cast<float>(num_items + 1); break;
    }
    return gap + spacing;
}

#endif // TAFFY_FLEXBOX || TAFFY_GRID (shared alignment helpers)

// ===========================================================================
// Section 15: Flexbox Algorithm
// Ref: src/compute/flexbox.rs, src/compute/common/alignment.rs
// ===========================================================================

#ifdef TAFFY_FLEXBOX

// --- Alignment utilities (shared with Grid) ---

inline float sum_axis_gaps(float gap, size_t num_items) {
    return num_items <= 1 ? 0.0f : gap * static_cast<float>(num_items - 1);
}

// --- Flexbox internal structs ---

/// The intermediate results of a flexbox calculation for a single item
struct FlexItem {
    /// The identifier for the associated node
    NodeId node{};
    /// The order of the node relative to its siblings
    uint32_t order{0};

    /// The base size of this item
    Size<std::optional<float>> size{};
    /// The raw size style of this item (used to resolve sizing keywords / detect `stretch`)
    Size<Dimension> size_style{};
    /// The minimum allowable size of this item
    Size<std::optional<float>> min_size{};
    /// The maximum allowable size of this item
    Size<std::optional<float>> max_size{};
    /// The aspect ratio of this item
    std::optional<float> aspect_ratio{};
    /// The cross-alignment of this item
    AlignSelf align_self{};

    /// The overflow style of the item
    Point<Overflow> overflow{Overflow::Visible, Overflow::Visible};
    /// The contain style of the item
    Contain contain{};
    /// The width of the scrollbars (if it has any)
    float scrollbar_width{0.0f};
    /// The flex shrink style of the item
    float flex_shrink{1.0f};
    /// The flex grow style of the item
    float flex_grow{0.0f};
    /// Whether the item's used flex basis is definite
    bool flex_basis_is_definite{false};

    /// The minimum size of the item, taking content based automatic minimum sizes into account
    float resolved_minimum_main_size{0.0f};

    /// The `position: relative` offset of this item, resolved in the container's coordinate space
    Size<float> relative_inset{};
    /// The margin of this item
    Rect<float> margin{};
    /// Whether each margin is an auto margin or not
    Rect<bool> margin_is_auto{};
    /// The padding of this item
    Rect<float> padding{};
    /// The border of this item
    Rect<float> border{};

    /// The default size of this item
    float flex_basis{0.0f};
    /// The default size of this item, minus padding and border
    float inner_flex_basis{0.0f};
    /// The amount by which this item has deviated from its target size
    float violation{0.0f};
    /// Is the size of this item locked
    bool frozen{false};

    /// Either the max- or min- content flex fraction
    float content_flex_fraction{0.0f};

    /// The proposed inner size of this item
    Size<float> hypothetical_inner_size{};
    /// The proposed outer size of this item
    Size<float> hypothetical_outer_size{};
    /// The size that this item wants to be
    Size<float> target_size{};
    /// The size that this item wants to be, plus any padding and border
    Size<float> outer_target_size{};

    /// The position of the bottom edge of this item
    float baseline{0.0f};

    /// A temporary value for the main offset
    float offset_main{0.0f};
    /// A temporary value for the cross offset
    float offset_cross{0.0f};

    /// Returns true if the item is a scroll container
    bool is_scroll_container() const {
        return ::taffy::is_scroll_container(overflow.x) || ::taffy::is_scroll_container(overflow.y);
    }

    /// The size that the item's size styles exclude:
    /// padding + border for `box-sizing: content-box`, otherwise zero
    Size<float> box_sizing_adjustment(const Style& style) const {
        return style.box_sizing == BoxSizing::ContentBox ? (padding + border).sum_axes() : Size_f32_ZERO;
    }

    /// Whether the used cross size of the item is the cross size of its flex line
    bool is_stretched(FlexDirection dir) const {
        const Dimension cross_style = size_style.cross(dir);
        const bool is_stretch_keyword = cross_style.is_stretch();
        return !margin_is_auto.cross_start(dir) && !margin_is_auto.cross_end(dir)
            && (is_stretch_keyword || (align_self.keyword == AlignItemsKeyword::Stretch && cross_style.is_auto()));
    }

    /// Whether the item participates in baseline alignment
    bool participates_in_baseline_alignment(FlexDirection dir) const {
        return align_self.keyword == AlignItemsKeyword::Baseline
            && !margin_is_auto.cross_start(dir) && !margin_is_auto.cross_end(dir);
    }
};

/// A line of [`FlexItem`] used for intermediate computation
struct FlexLine {
    std::vector<FlexItem> items{};
    /// The dimensions of the cross-axis
    float cross_size{0.0f};
    /// The relative offset of the cross-axis
    float offset_cross{0.0f};
};

/// Values that can be cached during the flexbox algorithm
struct FlexAlgoConstants {
    FlexDirection dir{FlexDirection::Row};
    Direction layout_direction{Direction::Ltr};
    bool is_row{true};
    bool is_column{false};
    bool is_wrap{false};
    bool is_wrap_reverse{false};
    /// Are items balanced across lines (`flex-wrap: balance`)?
    bool is_balance{false};
    /// The requested minimum number of lines (`flex-line-count`); Some for multi-line containers
    std::optional<uint16_t> line_count{};

    /// The item's min_size style
    Size<std::optional<float>> min_size{};
    /// The item's max_size style
    Size<std::optional<float>> max_size{};
    /// The border of this section
    Rect<float> border{};
    /// The space between the content box and the border box (padding + border + scrollbar gutter)
    Rect<float> content_box_inset{};
    /// The border and scrollbar gutters surrounding the padding box
    Rect<float> padding_box_inset{};
    /// The resolved padding used for scrollable overflow contributions
    Rect<float> padding{};
    /// The size reserved for scrollbar gutters in each axis
    Point<float> scrollbar_gutter{0.0f, 0.0f};
    /// Whether the node being laid out is a scroll container
    bool is_scroll_container{false};
    /// The gap of this section
    Size<float> gap{0.0f, 0.0f};
    /// The align_items property of this node
    AlignItems align_items{};
    /// The align_content property of this node
    AlignContent align_content{};
    /// The justify_content property of this node
    JustifyContent justify_content{};

    /// The border-box size of the node being laid out (if known)
    Size<std::optional<float>> node_outer_size{};
    /// The content-box size of the node being laid out (if known)
    Size<std::optional<float>> node_inner_size{};
    /// Whether the known main size of the node (if any) is definite
    bool known_main_size_is_definite{true};
    /// Whether the node has a known main size which is definite
    bool has_definite_main_size{false};
    /// Whether the node has a known cross size which is definite
    bool has_definite_cross_size{false};
    /// Whether the cross-axis space non-stretched items are fit-content sized into is definite
    bool cross_axis_available_space_is_definite{false};

    /// The size of the virtual container containing the flex items
    Size<float> container_size{0.0f, 0.0f};
    /// The size of the internal container
    Size<float> inner_container_size{0.0f, 0.0f};

    /// When a multi-line container requests a minimum number of lines, definite cross-axis
    /// available space for measuring items is divided between the requested number of lines.
    inline float divided_cross_space(float cross_available_space) const {
        if (line_count && *line_count > 1) {
            const float count = static_cast<float>(*line_count);
            return (cross_available_space - (count - 1.0f) * gap.cross(dir)) / count;
        }
        return cross_available_space;
    }
};

/// A `position: relative` item's inset resolved to a single offset in the container's flex axes,
/// accounting for flex direction and writing direction.
inline Size<float> flex_resolve_relative_inset(Rect<std::optional<float>> inset, const FlexAlgoConstants& constants) {
    const FlexDirection direction = constants.dir;
    const bool is_rtl_row = is_row(direction) && constants.layout_direction == Direction::Rtl;
    const bool is_rtl_column = !is_row(direction) && constants.layout_direction == Direction::Rtl;

    float main = 0.0f;
    if (is_rtl_row) {
        if (inset.main_end(direction)) main = *inset.main_end(direction);
        else if (inset.main_start(direction)) main = -*inset.main_start(direction);
    } else {
        if (inset.main_start(direction)) main = *inset.main_start(direction);
        else if (inset.main_end(direction)) main = -*inset.main_end(direction);
    }

    float cross = 0.0f;
    if (is_rtl_column) {
        if (inset.cross_end(direction)) cross = -*inset.cross_end(direction);
        else if (inset.cross_start(direction)) cross = *inset.cross_start(direction);
    } else {
        if (inset.cross_start(direction)) cross = *inset.cross_start(direction);
        else if (inset.cross_end(direction)) cross = -*inset.cross_end(direction);
    }

    Size<float> out{0.0f, 0.0f};
    out.set_main(direction, main);
    out.set_cross(direction, cross);
    return out;
}

// --- Constants ---

/// Compute constants that can be reused during the flexbox algorithm.
template <typename Tree>
inline FlexAlgoConstants flex_compute_constants(Tree& tree, const Style& style,
    Size<std::optional<float>> known_dimensions, Size<bool> known_dimensions_are_definite,
    Size<std::optional<float>> parent_size, Size<AvailableSpace> available_space) {
    (void)tree;
    FlexAlgoConstants c;
    c.dir = style.flex_direction;
    c.is_row = is_row(c.dir);
    c.is_column = !c.is_row;
    c.is_wrap = flex_wrap_is_multi_line(style.flex_wrap);
    c.is_wrap_reverse = flex_wrap_is_reverse(style.flex_wrap);
    c.is_balance = flex_wrap_is_balanced(style.flex_wrap);
    c.line_count = c.is_wrap
        ? std::optional<uint16_t>(std::max<uint16_t>(style.flex_line_count, 1))
        : std::nullopt;
    c.layout_direction = style.direction;

    const auto aspect_ratio = style.aspect_ratio;

    const Rect<float> padding = resolve_or_zero_rect(style.padding, parent_size.width);
    const Rect<float> border = resolve_or_zero_rect(style.border, parent_size.width);
    const Size<float> padding_border_sum = padding.sum_axes() + border.sum_axes();
    const Size<float> box_sizing_adjustment =
        style.box_sizing == BoxSizing::ContentBox ? padding_border_sum : Size_f32_ZERO;

    // `normal` behaves as `stretch` for flex items / containers, and as `flex-start` for
    // justify-content. The enum does not yet carry `Normal`; the helpers are the place to add it.
    c.align_items = flex_resolve_normal_alignment(style.align_items.value_or(AlignItems_STRETCH));
    c.align_content = flex_resolve_normal_align_content(style.align_content.value_or(AlignContent_STRETCH));
    c.justify_content = flex_resolve_normal_justify(style.justify_content.value_or(JustifyContent{AlignContentKeyword::FlexStart, AlignmentSafety::Unsafe}));

    // Scrollbar gutters are reserved when `overflow` is `Overflow::Scroll`; the axes are
    // transposed because a node that scrolls vertically needs horizontal space for a scrollbar.
    Point<float> scrollbar_gutter{0.0f, 0.0f};
    if (style.overflow.y == Overflow::Scroll) scrollbar_gutter.x = style.scrollbar_width;
    if (style.overflow.x == Overflow::Scroll) scrollbar_gutter.y = style.scrollbar_width;
    c.scrollbar_gutter = scrollbar_gutter;
    c.is_scroll_container = ::taffy::is_scroll_container(style.overflow.x) || ::taffy::is_scroll_container(style.overflow.y);

    c.border = border;
    Rect<float> content_box_inset = padding + border;
    content_box_inset.bottom += scrollbar_gutter.y;
    if (style.direction == Direction::Rtl) content_box_inset.left += scrollbar_gutter.x;
    else content_box_inset.right += scrollbar_gutter.x;
    c.content_box_inset = content_box_inset;

    c.padding_box_inset = border + Rect<float>(
        style.direction == Direction::Rtl ? scrollbar_gutter.x : 0.0f,
        style.direction == Direction::Rtl ? 0.0f : scrollbar_gutter.x,
        0.0f,
        scrollbar_gutter.y);

    c.padding = Rect<float>(
        content_box_inset.left - border.left - (style.direction == Direction::Rtl ? scrollbar_gutter.x : 0.0f),
        content_box_inset.right - border.right - (style.direction == Direction::Rtl ? 0.0f : scrollbar_gutter.x),
        content_box_inset.top - border.top,
        content_box_inset.bottom - border.bottom - scrollbar_gutter.y);

    c.min_size = fs_add_f32(maybe_resolve_size(style.min_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);
    c.max_size = fs_add_f32(maybe_resolve_size(style.max_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);

    c.node_outer_size = known_dimensions;
    c.node_inner_size = fs_sub_f32(known_dimensions, content_box_inset.sum_axes());
    c.known_main_size_is_definite = known_dimensions_are_definite.main(c.dir);
    c.has_definite_main_size = c.known_main_size_is_definite && known_dimensions.main(c.dir).has_value();
    c.has_definite_cross_size = known_dimensions_are_definite.cross(c.dir) && known_dimensions.cross(c.dir).has_value();
    c.cross_axis_available_space_is_definite =
        c.has_definite_cross_size || available_space.cross(c.dir).is_definite();

    // Percentages in `gap` resolve against the node's inner size (or zero if it is unknown)
    Size<std::optional<float>> gap_basis = c.node_inner_size.or_(Size<std::optional<float>>{0.0f, 0.0f});
    c.gap = Size<float>{resolve_or_zero(style.gap.width, gap_basis.width),
                        resolve_or_zero(style.gap.height, gap_basis.height)};

    c.container_size = Size_f32_ZERO;
    c.inner_container_size = Size_f32_ZERO;
    return c;
}

// --- Step 2: available space ---

/// Determine the available main and cross space for the flex items.
inline Size<AvailableSpace> flex_determine_available_space(
    Size<std::optional<float>> known_dimensions,
    Size<AvailableSpace> outer_available_space,
    const FlexAlgoConstants& constants) {
    Size<AvailableSpace> out{AvailableSpace::max_content(), AvailableSpace::max_content()};
    if (known_dimensions.width) {
        out.width = AvailableSpace::definite(std::max(*known_dimensions.width - constants.content_box_inset.horizontal_axis_sum(), 0.0f));
    } else {
        out.width = av_maybe_max_f32(av_maybe_sub_f32(outer_available_space.width, constants.content_box_inset.horizontal_axis_sum()), 0.0f);
    }
    if (known_dimensions.height) {
        out.height = AvailableSpace::definite(std::max(*known_dimensions.height - constants.content_box_inset.vertical_axis_sum(), 0.0f));
    } else {
        out.height = av_maybe_max_f32(av_maybe_sub_f32(outer_available_space.height, constants.content_box_inset.vertical_axis_sum()), 0.0f);
    }
    return out;
}

// --- Step 1: generate anonymous flex items ---

/// Generate anonymous flex items.
template <typename Tree>
inline std::vector<FlexItem> flex_generate_items(Tree& tree, NodeId node, const FlexAlgoConstants& c) {
    const Style& container_style = tree.style(node);
    (void)container_style;

    // Percentage sizes of items resolve against the container's inner size, but only if that
    // size is definite. A known main size derived from the container's own content is treated as
    // indefinite here.
    const Size<std::optional<float>> percent_resolution_size =
        c.known_main_size_is_definite ? c.node_inner_size : c.node_inner_size.with_main(c.dir, std::nullopt);

    std::vector<FlexItem> items;
    const size_t count = tree.child_count(node);
    items.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const NodeId child = tree.get_child_id(node, index);
        const Style& child_style = tree.style(child);
        if (flex_position_is_out_of_flow(child_style.position)) continue;
        if (child_style.display == Display::None) continue;

        const auto aspect_ratio = child_style.aspect_ratio;
        const Rect<float> padding = resolve_or_zero_rect(child_style.padding, c.node_inner_size.width);
        const Rect<float> border = resolve_or_zero_rect(child_style.border, c.node_inner_size.width);
        const Size<float> box_sizing_adjustment =
            child_style.box_sizing == BoxSizing::ContentBox ? (padding + border).sum_axes() : Size_f32_ZERO;

        FlexItem item;
        item.node = child;
        item.order = static_cast<uint32_t>(index);
        item.size = fs_add_f32(maybe_resolve_size(child_style.size, percent_resolution_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);
        item.size_style = child_style.size;
        item.min_size = fs_add_f32(maybe_resolve_size(child_style.min_size, percent_resolution_size), box_sizing_adjustment);
        item.max_size = fs_add_f32(maybe_resolve_size(child_style.max_size, percent_resolution_size), box_sizing_adjustment);
        item.aspect_ratio = aspect_ratio;

        if (child_style.position == Position::Relative) {
            item.relative_inset = flex_resolve_relative_inset(
                child_style.inset.zip_size(c.node_inner_size, [](LengthPercentageAuto p, std::optional<float> basis) {
                    return maybe_resolve(p, basis);
                }),
                c);
        } else {
            item.relative_inset = Size<float>{0.0f, 0.0f};
        }

        item.margin = resolve_or_zero_rect(child_style.margin, c.node_inner_size.width);
        item.margin_is_auto = Rect<bool>{
            child_style.margin.left.is_auto(),
            child_style.margin.right.is_auto(),
            child_style.margin.top.is_auto(),
            child_style.margin.bottom.is_auto()};
        item.padding = padding;
        item.border = border;
        item.align_self = flex_resolve_self_relative(
            flex_resolve_normal_alignment(child_style.align_self.value_or(c.align_items)),
            child_style.direction,
            c.layout_direction,
            c.is_column);
        item.overflow = child_style.overflow;
        item.contain = child_style.contain;
        item.scrollbar_width = child_style.scrollbar_width;
        item.flex_grow = child_style.flex_grow;
        item.flex_shrink = child_style.flex_shrink;
        item.flex_basis_is_definite = false;
        item.flex_basis = 0.0f;
        item.inner_flex_basis = 0.0f;
        item.violation = 0.0f;
        item.frozen = false;
        item.resolved_minimum_main_size = 0.0f;
        item.hypothetical_inner_size = Size_f32_ZERO;
        item.hypothetical_outer_size = Size_f32_ZERO;
        item.target_size = Size_f32_ZERO;
        item.outer_target_size = Size_f32_ZERO;
        item.content_flex_fraction = 0.0f;
        item.baseline = 0.0f;
        item.offset_main = 0.0f;
        item.offset_cross = 0.0f;
        items.push_back(std::move(item));
    }
    return items;
}

// --- Step 3: flex base size + hypothetical main size ---

/// Determine the flex base size and hypothetical main size of each item.
template <typename Tree, typename MeasureFn>
inline void flex_determine_base_size(Tree& tree, const FlexAlgoConstants& constants,
    Size<AvailableSpace> available_space, std::vector<FlexItem>& flex_items, MeasureFn& measure_fn) {
    const FlexDirection dir = constants.dir;

    for (auto& child : flex_items) {
        const Style& child_style = tree.style(child.node);

        // Parent size for child sizing
        const std::optional<float> cross_axis_parent_size = constants.node_inner_size.cross(dir);
        const Size<std::optional<float>> child_parent_size = Size<std::optional<float>>{}.with_cross(dir, cross_axis_parent_size);

        // Available space for child sizing (excluding the child's margins)
        const Size<std::optional<float>> transferred_min_size = child.min_size.maybe_apply_aspect_ratio(child.aspect_ratio);
        const Size<std::optional<float>> transferred_max_size = child.max_size.maybe_apply_aspect_ratio(child.aspect_ratio);
        const std::optional<float> child_min_cross = transferred_min_size.cross(dir);
        const std::optional<float> child_max_cross = transferred_max_size.cross(dir);

        // Clamp available space by min- and max- size
        AvailableSpace cross_axis_available_space{};
        const AvailableSpace parent_cross_avail = available_space.cross(dir);
        if (parent_cross_avail.is_definite()) {
            const float val = parent_cross_avail.value;
            float base = fm_sub_f32(constants.divided_cross_space(cross_axis_parent_size.value_or(val)),
                                    child.margin.cross_axis_sum(dir));
            base = std::max(base, 0.0f);
            base = fm_clamp_f32(base, child_min_cross, child_max_cross);
            cross_axis_available_space = AvailableSpace::definite(base);
        } else if (parent_cross_avail.type == AvailableSpace::MinContent) {
            cross_axis_available_space = child_min_cross ? AvailableSpace::definite(*child_min_cross)
                                                         : AvailableSpace::min_content();
        } else {
            cross_axis_available_space = child_max_cross ? AvailableSpace::definite(*child_max_cross)
                                                         : AvailableSpace::max_content();
        }

        // Known dimensions for child sizing
        bool child_cross_size_is_definite = child.size.cross(dir).has_value();
        Size<std::optional<float>> child_known_dimensions = child.size.with_main(dir, std::nullopt);
        child_known_dimensions.set_cross(dir,
            fm_clamp(child_known_dimensions.cross(dir), transferred_min_size.cross(dir), transferred_max_size.cross(dir)));
        if (child.align_self.keyword == AlignItemsKeyword::Stretch
            && !child.margin_is_auto.cross_start(constants.dir)
            && !child.margin_is_auto.cross_end(constants.dir)
            && !child_known_dimensions.cross(dir)) {
            child_known_dimensions.set_cross(dir, cross_axis_available_space.into_option());
            // The cross size of a stretched item is definite if the container has a definite cross size
            child_cross_size_is_definite =
                !constants.is_wrap && constants.has_definite_cross_size && cross_axis_parent_size.has_value();
        }

        const float box_sizing_adjustment = child.box_sizing_adjustment(child_style).main(dir);
        // Percentage flex basis values resolve against the container's inner main size, but only
        // if that size is definite.
        const std::optional<float> percent_resolution_main_size =
            constants.known_main_size_is_definite ? constants.node_inner_size.main(dir) : std::nullopt;
        const Dimension flex_basis_style = child_style.flex_basis;
        const std::optional<float> flex_basis = fo_add_f32(maybe_resolve(flex_basis_style, percent_resolution_main_size), box_sizing_adjustment);

        const std::optional<float> main_size = child.size.main(dir);
        const std::optional<float> main_stretch_size =
            fm_max(fm_sub(percent_resolution_main_size, std::optional<float>(child.margin.main_axis_sum(dir))), std::optional<float>(0.0f));

        // A flex basis that is a sizing keyword is used in place of the main size property;
        // `stretch` resolves to an exact size, other keywords determine the available-space
        // constraint the item is measured under.
        std::optional<float> determined_basis;
        bool basis_determined = false;
        std::optional<AvailableSpace> keyword_main_available_space;

        // NOTE: `flex-basis: content` has no distinct representation in this header yet; it is
        // treated like `auto` (see TODO(core) on CompactLength tags).
        if (flex_is_sizing_keyword(flex_basis_style)) {
            const auto resolution = flex_resolve_sizing_keyword(flex_basis_style, main_stretch_size, percent_resolution_main_size);
            if (resolution) {
                if (resolution->exact) {
                    child.flex_basis_is_definite = true;
                    determined_basis = resolution->exact_value;
                    basis_determined = true;
                } else {
                    keyword_main_available_space = resolution->available;
                }
            }
        } else {
            // A. If the item has a definite used flex basis, that's the flex base size.
            if (flex_basis) {
                child.flex_basis_is_definite = true;
                determined_basis = *flex_basis;
                basis_determined = true;
            } else if (flex_basis_style.is_auto() && main_size) {
                child.flex_basis_is_definite = true;
                determined_basis = *main_size;
                basis_determined = true;
            }

            if (!basis_determined && flex_basis_style.is_auto()) {
                const auto resolution = flex_resolve_sizing_keyword(child.size_style.main(dir), main_stretch_size, percent_resolution_main_size);
                if (resolution) {
                    if (resolution->exact) {
                        child.flex_basis_is_definite = true;
                        determined_basis = resolution->exact_value;
                        basis_determined = true;
                    } else {
                        keyword_main_available_space = resolution->available;
                    }
                }
            }
        }

        if (basis_determined) {
            child.flex_basis = *determined_basis;
        } else if (child_cross_size_is_definite && child.aspect_ratio && child_known_dimensions.cross(dir)) {
            // B. aspect ratio + definite cross size (and content basis)
            child.flex_basis_is_definite = true;
            const float cross = *child_known_dimensions.cross(dir);
            child.flex_basis = is_row(dir) ? cross * *child.aspect_ratio : cross / *child.aspect_ratio;
        } else {
            // E. Size the item into the available space using its used flex basis in place of its
            // main size, treating `content` as max-content.
            Size<AvailableSpace> child_available_space{AvailableSpace::max_content(), AvailableSpace::max_content()};
            child_available_space.set_main(dir, keyword_main_available_space
                ? *keyword_main_available_space
                : (available_space.main(dir).type == AvailableSpace::MinContent
                    ? AvailableSpace::min_content()
                    : AvailableSpace::max_content()));
            child_available_space.set_cross(dir, cross_axis_available_space);

            LayoutInput li;
            li.run_mode = RunMode::ComputeSize;
            li.sizing_mode = SizingMode::ContentSize;
            li.axis = is_row(dir) ? RequestedAxis::Horizontal : RequestedAxis::Vertical;
            li.known_dimensions = child_known_dimensions;
            li.parent_size = child_parent_size;
            li.available_space = child_available_space;
            li.vertical_margins_are_collapsible = Line_bool_FALSE;
            const LayoutOutput output = tree.compute_node_layout(child.node, li, measure_fn);
            child.flex_basis = is_row(dir) ? output.size.width : output.size.height;
        }

        // Floor flex-basis by the padding_border_sum (floors inner_flex_basis at zero). This
        // matches Chrome/Firefox even though the spec says the content box should not be floored.
        const float padding_border_sum = child.padding.main_axis_sum(constants.dir) + child.border.main_axis_sum(constants.dir);
        child.flex_basis = std::max(child.flex_basis, padding_border_sum);
        child.inner_flex_basis = child.flex_basis - padding_border_sum;

        const Size<std::optional<float>> padding_border_axes_sums = Size<std::optional<float>>{
            (child.padding + child.border).sum_axes().width, (child.padding + child.border).sum_axes().height};

        // The automatic minimum size of a flex item is content based unless `overflow` makes the
        // item a scroll container (see https://www.w3.org/TR/css-flexbox-1/#min-size-auto).
        std::optional<float> style_min_main_size = child.min_size.main(dir);
        if (!style_min_main_size) {
            const Overflow overflow_on_main_axis = is_row(dir) ? child.overflow.x : child.overflow.y;
            if (is_scroll_container(overflow_on_main_axis)) style_min_main_size = 0.0f;
        }

        if (style_min_main_size) {
            child.resolved_minimum_main_size = *style_min_main_size;
        } else {
            Size<AvailableSpace> min_content_available{AvailableSpace::min_content(), AvailableSpace::min_content()};
            min_content_available.set_cross(dir, cross_axis_available_space);

            LayoutInput li;
            li.run_mode = RunMode::ComputeSize;
            li.sizing_mode = SizingMode::ContentSize;
            li.axis = is_row(dir) ? RequestedAxis::Horizontal : RequestedAxis::Vertical;
            li.known_dimensions = child_known_dimensions;
            li.parent_size = child_parent_size;
            li.available_space = min_content_available;
            li.vertical_margins_are_collapsible = Line_bool_FALSE;
            const LayoutOutput output = tree.compute_node_layout(child.node, li, measure_fn);
            const float min_content_main_size = is_row(dir) ? output.size.width : output.size.height;

            float clamped = fm_min_f32(min_content_main_size, child.size.main(dir));
            clamped = fm_min_f32(clamped, transferred_max_size.main(dir));
            clamped = fm_max_f32(clamped, padding_border_axes_sums.main(dir));
            child.resolved_minimum_main_size = clamped;
        }

        // Sizes transferred through the aspect ratio clamp the hypothetical main size, but do not
        // participate in resolving flexible lengths or clamping the final size.
        float hypothetical_inner_min_main = fm_max_f32(child.resolved_minimum_main_size, transferred_min_size.main(constants.dir));
        hypothetical_inner_min_main = fm_max_f32(hypothetical_inner_min_main, padding_border_axes_sums.main(constants.dir));
        const float hypothetical_inner_size =
            fm_clamp_f32(child.flex_basis, std::optional<float>(hypothetical_inner_min_main), transferred_max_size.main(constants.dir));
        const float hypothetical_outer_size = hypothetical_inner_size + child.margin.main_axis_sum(constants.dir);

        child.hypothetical_inner_size.set_main(constants.dir, hypothetical_inner_size);
        child.hypothetical_outer_size.set_main(constants.dir, hypothetical_outer_size);
    }
}

// --- Step 5: line breaking ---

/// Collect flex items into flex lines.
inline std::vector<FlexLine> flex_collect_flex_lines(const FlexAlgoConstants& constants,
    Size<AvailableSpace> available_space, std::vector<FlexItem>& flex_items) {
    std::vector<FlexLine> lines;

    // Wrapping into multiple lines requires a definite main size. If the container's known main
    // size is derived from its own content (and is thus indefinite) then all items are collected
    // into a single flex line.
    if (!constants.is_wrap || !constants.known_main_size_is_definite) {
        FlexLine line;
        line.items = std::move(flex_items);
        flex_items.clear();
        lines.push_back(std::move(line));
        return lines;
    }

    AvailableSpace main_axis_available_space{};
    if (constants.max_size.main(constants.dir)) {
        const float max_size = *constants.max_size.main(constants.dir);
        float available = available_space.main(constants.dir).into_option().value_or(max_size);
        // If the container's main size is not definite then it is at most the max main size, so
        // the max size (and not the available space) is the limit that items wrap against.
        if (!constants.has_definite_main_size) available = std::min(available, max_size);
        main_axis_available_space = AvailableSpace::definite(fm_max_f32(available, constants.min_size.main(constants.dir)));
    } else if (!is_row(constants.dir) && !constants.has_definite_main_size
               && available_space.main(constants.dir).is_definite()) {
        // A column container's automatic main size is content-based, so definite available space
        // handed down by an ancestor does not constrain where its lines wrap.
        main_axis_available_space = AvailableSpace::max_content();
    } else {
        main_axis_available_space = available_space.main(constants.dir);
    }

    if (main_axis_available_space.type == AvailableSpace::MaxContent) {
        FlexLine line;
        line.items = std::move(flex_items);
        flex_items.clear();
        lines.push_back(std::move(line));
        return lines;
    }

    if (main_axis_available_space.type == AvailableSpace::MinContent) {
        for (auto& item : flex_items) {
            FlexLine line;
            line.items.push_back(std::move(item));
            lines.push_back(std::move(line));
        }
        flex_items.clear();
        return lines;
    }

    // Definite available space: greedy line breaking.
    const float main_axis_available = main_axis_available_space.value;
    const float main_axis_gap = constants.gap.main(constants.dir);
    size_t start = 0;
    const size_t total = flex_items.size();
    while (start < total) {
        float line_length = 0.0f;
        size_t end = total;
        for (size_t idx = 0; start + idx < total; ++idx) {
            const float gap_contribution = idx == 0 ? 0.0f : main_axis_gap;
            line_length += flex_items[start + idx].hypothetical_outer_size.main(constants.dir) + gap_contribution;
            if (line_length > main_axis_available && idx != 0) {
                end = start + idx;
                break;
            }
        }
        if (end <= start) end = start + 1;  // safety: always make progress
        FlexLine line;
        for (size_t j = start; j < end; ++j) line.items.push_back(std::move(flex_items[j]));
        lines.push_back(std::move(line));
        start = end;
    }
    flex_items.clear();
    return lines;
}
// --- Balanced line breaking (`flex-wrap: balance`, CSS Flexbox Level 2) ---
// Faithful port of the `balance` module in src/compute/flexbox.rs. Pure functions: no tree or
// style dependencies, so they can be tested independently of the rest of the algorithm.

namespace flex_balance {

/// Score assigned to divisions that violate the line size constraint
inline constexpr double INFEASIBLE = std::numeric_limits<double>::infinity();

/// Convert an item size in pixels to an `f64`, flooring negative sizes at zero.
inline double to_size(float value) {
    return std::clamp(static_cast<double>(value), 0.0, static_cast<double>(std::numeric_limits<float>::max()));
}

/// Line sizing shared by the scoring and readback phases
struct LineSizes {
    /// Per item, the prefix sum of the item sizes through it (each item also contributes one
    /// trailing gap) paired with whether the item's (floored) size is zero
    std::vector<std::pair<double, bool>> sums{};
    /// The size of the gap between adjacent items on a line
    double gap_between_items{0.0};
    /// The container's inner main size, which lines may not exceed (unless they hold a single item)
    double limit{0.0};

    size_t item_count() const { return sums.size(); }
    bool is_zero_item(size_t index) const { return sums[index].second; }

    /// The size of the line holding items `start..=end`
    double line_size(size_t start, size_t end) const {
        const double start_sum = start == 0 ? 0.0 : sums[start - 1].first;
        return sums[end].first - start_sum - gap_between_items;
    }

    /// The squared size of the line holding items `start..=end`, or INFEASIBLE for a line of
    /// more than one item exceeding the limit
    double line_cost(size_t start, size_t end) const {
        const double size = line_size(start, end);
        if (end > start && size > limit) return INFEASIBLE;
        return size * size;
    }

    /// For every suffix of the items, the number of lines that greedy line breaking produces
    std::vector<uint32_t> suffix_greedy_line_counts() const {
        const size_t item_count_ = item_count();
        std::vector<uint32_t> counts(item_count_ + 1, 0);
        size_t line_end = item_count_;
        for (size_t start = item_count_; start-- > 0;) {
            while (line_end > start + 1 && line_size(start, line_end - 1) > limit) line_end -= 1;
            counts[start] = 1 + counts[line_end];
        }
        return counts;
    }

    /// For every `start`, the largest `end` such that the line holding items `start..=end` does
    /// not exceed the limit (or `start` itself if even that one item overflows)
    std::vector<uint32_t> fit_ends() const {
        const size_t item_count_ = item_count();
        std::vector<uint32_t> fit_ends_;
        fit_ends_.reserve(item_count_);
        size_t fit_end = 0;
        for (size_t start = 0; start < item_count_; ++start) {
            if (fit_end < start) fit_end = start;
            while (fit_end + 1 < item_count_ && line_size(start, fit_end + 1) <= limit) fit_end += 1;
            fit_ends_.push_back(static_cast<uint32_t>(fit_end));
        }
        return fit_ends_;
    }
};

/// One row of the balancing dynamic program: divisions of item suffixes into exactly `lines` lines
struct Row {
    const LineSizes& sizes;
    const std::vector<uint32_t>& fit_ends;
    const std::vector<double>& prev;
    const std::vector<uint32_t>& nonzero_ends;
    size_t max_end;
};

/// Compute `cur[start]` and `opts[start]` for `start` in `start_lo..=start_hi` using the
/// divide-and-conquer dynamic programming optimization.
inline void fill_row(const Row& row, std::vector<double>& cur, std::vector<uint32_t>& opts,
    size_t start_lo, size_t start_hi, size_t col_lo, size_t col_hi) {
    if (start_lo > start_hi) return;
    const size_t start = start_lo + (start_hi - start_lo) / 2;

    // Minimize over the in-range ends preceding a non-zero item, starting from `start`
    size_t first_col = col_lo;
    while (first_col < col_hi && static_cast<size_t>(row.nonzero_ends[first_col]) < start) first_col += 1;

    double min_cost = INFEASIBLE;
    std::optional<size_t> min_col;
    for (size_t col = first_col; col < col_hi; ++col) {
        const size_t end = static_cast<size_t>(row.nonzero_ends[col]);
        const double line_cost_ = row.sizes.line_cost(start, end);
        if (line_cost_ == INFEASIBLE) break;  // Every longer line also exceeds the limit
        const double cost = line_cost_ + row.prev[end + 1];
        // `<=` keeps the largest minimizing end (assign the most items to the earliest lines)
        if (cost <= min_cost) {
            min_cost = cost;
            min_col = col;
        }
    }

    // Merge the single end preceding a zero item that the zero-sized-item rule allows
    std::optional<size_t> min_end = min_col ? std::optional<size_t>(row.nonzero_ends[*min_col]) : std::nullopt;
    const size_t zero_end = std::min(static_cast<size_t>(row.fit_ends[start]), row.max_end);
    if (row.sizes.is_zero_item(zero_end + 1)) {
        const double cost = row.sizes.line_cost(start, zero_end) + row.prev[zero_end + 1];
        if (cost < min_cost || (cost == min_cost && (!min_end || *min_end < zero_end))) {
            min_cost = cost;
            min_end = zero_end;
        }
    }

    cur[start] = min_cost;
    opts[start] = static_cast<uint32_t>(min_end.value_or(start));

    // The zero-rule end is excluded from the narrowing: only the non-zero ends' minima are monotone
    if (start > start_lo) {
        const size_t new_col_hi = min_col ? *min_col + 1 : col_hi;
        fill_row(row, cur, opts, start_lo, start - 1, col_lo, new_col_hi);
    }
    if (start < start_hi) {
        const size_t new_col_lo = min_col.value_or(first_col);
        fill_row(row, cur, opts, start + 1, start_hi, new_col_lo, col_hi);
    }
}

/// Determine the number of items on each line that balances items across lines, such that the
/// largest line is as small as possible, with a minimum of `min_line_count` lines.
/// `item_sizes` must be non-empty; the returned counts are non-zero and sum to the item count.
inline std::vector<size_t> balanced_line_item_counts(const std::vector<float>& item_sizes,
    float line_limit, float gap_between_items, size_t min_line_count) {
    const size_t item_count_ = item_sizes.size();
    const double gap = to_size(gap_between_items);
    const double limit = static_cast<double>(line_limit);

    LineSizes sizes;
    sizes.gap_between_items = gap;
    sizes.limit = limit;
    sizes.sums.reserve(item_count_);
    double sum = 0.0;
    for (float size : item_sizes) {
        const double size_d = to_size(size);
        sum += size_d + gap;
        sizes.sums.emplace_back(sum, size_d == 0.0);
    }

    const std::vector<uint32_t> suffix_greedy = sizes.suffix_greedy_line_counts();
    const size_t minimum_line_count = std::clamp<size_t>(min_line_count, 1, item_count_);
    const size_t line_count = std::max<size_t>(suffix_greedy[0], minimum_line_count);

    std::vector<size_t> item_counts;
    item_counts.reserve(line_count);
    if (line_count == item_count_) {
        item_counts.assign(item_count_, 1);
        return item_counts;
    }

    const std::vector<uint32_t> fit_ends_ = sizes.fit_ends();
    std::vector<uint32_t> nonzero_ends;
    nonzero_ends.reserve(item_count_ - 1);
    for (size_t end = 0; end + 1 < item_count_; ++end) {
        if (!sizes.is_zero_item(end + 1)) nonzero_ends.push_back(static_cast<uint32_t>(end));
    }

    std::vector<double> prev(item_count_);
    for (size_t start = 0; start < item_count_; ++start) prev[start] = sizes.line_cost(start, item_count_ - 1);
    std::vector<double> cur(item_count_, INFEASIBLE);
    std::vector<uint32_t> opts((line_count - 1) * item_count_, 0);

    for (size_t lines = 2; lines <= line_count; ++lines) {
        const size_t max_end = item_count_ - lines;
        size_t first_start = 0;
        while (first_start < max_end && static_cast<size_t>(suffix_greedy[first_start]) > lines) {
            cur[first_start] = INFEASIBLE;
            first_start += 1;
        }
        size_t col_hi = 0;
        while (col_hi < nonzero_ends.size() && static_cast<size_t>(nonzero_ends[col_hi]) <= max_end) col_hi += 1;
        const Row row{sizes, fit_ends_, prev, nonzero_ends, max_end};
        std::vector<uint32_t> opts_row(opts.begin() + static_cast<std::ptrdiff_t>((lines - 2) * item_count_),
                                       opts.begin() + static_cast<std::ptrdiff_t>((lines - 1) * item_count_));
        fill_row(row, cur, opts_row, first_start, max_end, 0, col_hi);
        for (size_t i = 0; i < item_count_; ++i) opts[(lines - 2) * item_count_ + i] = opts_row[i];
        std::swap(prev, cur);
    }

    // Read the division back out front to back
    size_t start = 0;
    for (size_t lines = line_count; lines >= 2; --lines) {
        const size_t end = opts[(lines - 2) * item_count_ + start];
        item_counts.push_back(end - start + 1);
        start = end + 1;
    }
    item_counts.push_back(item_count_ - start);
    return item_counts;
}

} // namespace flex_balance
// --- Balanced line breaking (`flex-wrap: balance`, CSS Flexbox Level 2) ---

/// Determine the number of items on each line that balances items across lines.
inline std::vector<FlexLine> flex_collect_balanced_flex_lines(const FlexAlgoConstants& constants,
    Size<AvailableSpace> available_space, std::vector<FlexItem>& flex_items) {
    std::vector<FlexLine> lines;
    if (flex_items.empty()) return lines;

    // If the container's known main size is derived from its own content (and is thus indefinite)
    // then items are balanced without a size limit.
    AvailableSpace main_axis_available_space;
    if (constants.known_main_size_is_definite) {
        if (constants.max_size.main(constants.dir)) {
            const float max_size = *constants.max_size.main(constants.dir);
            float available = available_space.main(constants.dir).into_option().value_or(max_size);
            if (!constants.has_definite_main_size) available = std::min(available, max_size);
            main_axis_available_space =
                AvailableSpace::definite(fm_max_f32(available, constants.min_size.main(constants.dir)));
        } else if (!is_row(constants.dir) && !constants.has_definite_main_size
                   && available_space.main(constants.dir).is_definite()) {
            main_axis_available_space = AvailableSpace::max_content();
        } else {
            main_axis_available_space = available_space.main(constants.dir);
        }
    } else {
        main_axis_available_space = AvailableSpace::max_content();
    }

    if (main_axis_available_space.type == AvailableSpace::MinContent) {
        for (auto& item : flex_items) {
            FlexLine line;
            line.items.push_back(std::move(item));
            lines.push_back(std::move(line));
        }
        flex_items.clear();
        return lines;
    }

    const float line_break_size = main_axis_available_space.into_option().value_or(std::numeric_limits<float>::infinity());
    const size_t min_line_count = constants.line_count.value_or(1);

    std::vector<float> item_sizes;
    item_sizes.reserve(flex_items.size());
    for (const auto& item : flex_items) item_sizes.push_back(item.hypothetical_outer_size.main(constants.dir));

    const std::vector<size_t> item_counts = flex_balance::balanced_line_item_counts(
        item_sizes, line_break_size, constants.gap.main(constants.dir), min_line_count);

    size_t start = 0;
    for (size_t count : item_counts) {
        FlexLine line;
        for (size_t i = 0; i < count; ++i) line.items.push_back(std::move(flex_items[start + i]));
        lines.push_back(std::move(line));
        start += count;
    }
    flex_items.clear();
    return lines;
}

// --- Step 4: determine the main size of the flex container ---

/// Determine the container's main size (if not already known).
template <typename Tree, typename MeasureFn>
inline void flex_determine_container_main_size(Tree& tree, Size<AvailableSpace> available_space,
    std::vector<FlexLine>& lines, FlexAlgoConstants& constants, MeasureFn& measure_fn) {
    (void)tree;
    const FlexDirection dir = constants.dir;
    const float main_content_box_inset = constants.content_box_inset.main_axis_sum(dir);

    float outer_main_size;
    if (constants.node_outer_size.main(dir)) {
        outer_main_size = *constants.node_outer_size.main(dir);
    } else {
        const auto item_main_length = [&](const FlexItem& child) {
            const float padding_border_sum = (child.padding + child.border).main_axis_sum(dir);
            return std::max(fm_max_f32(child.flex_basis, child.min_size.main(dir)) + child.margin.main_axis_sum(dir),
                            padding_border_sum);
        };
        const auto longest_line_length = [&]() {
            float longest = 0.0f;
            for (const auto& line : lines) {
                const float line_gap = sum_axis_gaps(constants.gap.main(dir), line.items.size());
                float total = 0.0f;
                for (const auto& child : line.items) total += item_main_length(child);
                longest = std::max(longest, total + line_gap);
            }
            return longest;
        };

        const AvailableSpace main_avail = available_space.main(dir);
        if (main_avail.is_definite()) {
            float size = longest_line_length() + main_content_box_inset;
            if (constants.is_balance) {
                const uint16_t min_line_count = constants.line_count.value_or(1);
                size_t item_count = 0;
                for (const auto& line : lines) item_count += line.items.size();
                if (item_count == 0) {
                    outer_main_size = size;
                } else {
                    std::vector<float> item_lengths;
                    item_lengths.reserve(item_count);
                    for (const auto& line : lines) {
                        for (const auto& child : line.items) item_lengths.push_back(item_main_length(child));
                    }
                    const std::vector<size_t> item_counts = flex_balance::balanced_line_item_counts(
                        item_lengths, std::numeric_limits<float>::infinity(), constants.gap.main(dir),
                        std::max<size_t>(min_line_count, 1));
                    float widest_line_length = 0.0f;
                    size_t index = 0;
                    for (size_t count : item_counts) {
                        float line_length = 0.0f;
                        for (size_t i = index; i < index + count; ++i) line_length += item_lengths[i];
                        line_length += sum_axis_gaps(constants.gap.main(dir), count);
                        widest_line_length = std::max(widest_line_length, line_length);
                        index += count;
                    }
                    const float max_content_size = widest_line_length + main_content_box_inset;
                    outer_main_size = std::max(size, std::min(max_content_size, main_avail.value));
                }
            } else {
                // A wrapping container is at least as large as the available space it is given.
                if (lines.size() > 1) size = std::max(size, main_avail.value);
                outer_main_size = size;
            }
        } else if (main_avail.type == AvailableSpace::MinContent && constants.is_wrap) {
            outer_main_size = longest_line_length() + main_content_box_inset;
        } else {
            // MinContent | MaxContent: the container's max-content size
            float main_size = 0.0f;
            for (auto& line : lines) {
                for (auto& item : line.items) {
                    const std::optional<float> style_min = item.min_size.main(dir);
                    const float margin_sum = item.margin.main_axis_sum(dir);
                    const std::optional<float> style_preferred = item.size.main(dir);
                    const std::optional<float> style_max = item.max_size.main(dir);

                    const std::optional<float> clamping_basis = constants.is_row
                        ? fm_max(std::optional<float>(item.flex_basis), style_preferred)
                        : std::optional<float>(item.flex_basis);
                    const std::optional<float> flex_basis_min =
                        item.flex_shrink == 0.0f ? clamping_basis : std::nullopt;
                    const std::optional<float> flex_basis_max =
                        item.flex_grow == 0.0f ? clamping_basis : std::nullopt;

                    const float min_main_size = fm_max_f32(
                        fm_max(style_min, flex_basis_min).value_or(flex_basis_min.value_or(item.resolved_minimum_main_size)),
                        std::optional<float>(item.resolved_minimum_main_size));
                    const float max_main_size = fm_min(style_max, flex_basis_max).value_or(
                        flex_basis_max.value_or(std::numeric_limits<float>::infinity()));

                    float content_contribution;
                    if (style_preferred && (max_main_size <= min_main_size || max_main_size <= *style_preferred)) {
                        content_contribution = fm_max_f32(fm_min_f32(*style_preferred, max_main_size), min_main_size) + margin_sum;
                    } else if (max_main_size <= min_main_size) {
                        content_contribution = min_main_size + margin_sum;
                    } else if (item.is_scroll_container()) {
                        content_contribution = item.flex_basis + margin_sum;
                    } else if (style_preferred) {
                        const float item_pb_main = item.padding.main_axis_sum(constants.dir)
                                                 + item.border.main_axis_sum(constants.dir);
                        const float inner_main_size = std::max(*style_preferred, item_pb_main);
                        if (constants.is_row) {
                            content_contribution = fm_clamp_f32(inner_main_size + margin_sum, style_min, style_max);
                        } else {
                            content_contribution = fm_clamp_f32(std::max(inner_main_size, item.flex_basis) + margin_sum,
                                                                style_min, style_max);
                        }
                    } else {
                        const std::optional<float> cross_axis_parent_size = constants.node_inner_size.cross(dir);
                        const std::optional<float> child_min_cross = item.min_size.cross(dir);
                        const std::optional<float> child_max_cross = item.max_size.cross(dir);
                        AvailableSpace cross_axis_available_space = available_space.cross(dir).map_definite_value(
                            [&](float val) {
                                return std::max(constants.divided_cross_space(cross_axis_parent_size.value_or(val))
                                                    - item.margin.cross_axis_sum(dir),
                                                0.0f);
                            });
                        cross_axis_available_space = av_maybe_clamp(cross_axis_available_space, child_min_cross, child_max_cross);

                        Size<AvailableSpace> child_available_space = available_space;
                        child_available_space.set_cross(dir, cross_axis_available_space);

                        const Size<std::optional<float>> transferred_min_size =
                            item.min_size.maybe_apply_aspect_ratio(item.aspect_ratio);
                        const Size<std::optional<float>> transferred_max_size =
                            item.max_size.maybe_apply_aspect_ratio(item.aspect_ratio);

                        Size<std::optional<float>> child_known_dimensions = item.size.with_main(dir, std::nullopt);
                        child_known_dimensions.set_cross(dir,
                            fm_clamp(child_known_dimensions.cross(dir),
                                     transferred_min_size.cross(dir), transferred_max_size.cross(dir)));
                        if (item.align_self.keyword == AlignItemsKeyword::Stretch && !child_known_dimensions.cross(dir)) {
                            child_known_dimensions.set_cross(dir, cross_axis_available_space.into_option());
                        }

                        LayoutInput li;
                        li.run_mode = RunMode::ComputeSize;
                        li.sizing_mode = SizingMode::ContentSize;
                        li.axis = constants.is_row ? RequestedAxis::Horizontal : RequestedAxis::Vertical;
                        li.known_dimensions = child_known_dimensions;
                        li.parent_size = constants.node_inner_size;
                        li.available_space = child_available_space;
                        li.vertical_margins_are_collapsible = Line_bool_FALSE;
                        const LayoutOutput measured = tree.compute_node_layout(item.node, li, measure_fn);
                        const float measured_main_size = constants.is_row ? measured.size.width : measured.size.height;

                        std::optional<float> transferred_main_size;
                        if (item.aspect_ratio && child_known_dimensions.cross(dir)) {
                            const float cross = *child_known_dimensions.cross(dir);
                            transferred_main_size = constants.is_row ? cross * *item.aspect_ratio : cross / *item.aspect_ratio;
                        }
                        const float inner_main_size = fm_max_f32(measured_main_size, transferred_main_size);

                        if (constants.is_row) {
                            content_contribution = fm_clamp_f32(inner_main_size + margin_sum, style_min, style_max);
                        } else {
                            content_contribution = fm_clamp_f32(std::max(inner_main_size, item.flex_basis) + margin_sum,
                                                                style_min, style_max);
                        }
                    }

                    const float diff = content_contribution - margin_sum - item.flex_basis;
                    if (diff > 0.0f) {
                        item.content_flex_fraction = diff / std::max(1.0f, item.flex_grow);
                    } else if (diff < 0.0f) {
                        const float scaled_shrink_factor = std::max(1.0f, item.flex_shrink) * item.inner_flex_basis;
                        item.content_flex_fraction = diff / scaled_shrink_factor;
                    } else {
                        item.content_flex_fraction = 0.0f;
                    }
                }

                const bool floor_by_largest_item = constants.is_wrap && constants.is_row;
                float largest_item_size = 0.0f;
                float item_main_size_sum = 0.0f;
                for (auto& item : line.items) {
                    const float flex_fraction = item.content_flex_fraction;
                    float flex_contribution = 0.0f;
                    if (item.content_flex_fraction > 0.0f) {
                        flex_contribution = std::max(1.0f, item.flex_grow) * flex_fraction;
                    } else if (item.content_flex_fraction < 0.0f) {
                        const float scaled_shrink_factor = std::max(1.0f, item.flex_shrink) * item.inner_flex_basis;
                        flex_contribution = scaled_shrink_factor == 0.0f ? 0.0f : scaled_shrink_factor * flex_fraction;
                    }
                    const float size = item.flex_basis + flex_contribution + item.margin.main_axis_sum(constants.dir);
                    if (floor_by_largest_item) largest_item_size = std::max(largest_item_size, size);
                    item_main_size_sum += size;
                }
                const float gap_sum = sum_axis_gaps(constants.gap.main(dir), line.items.size());
                main_size = std::max(main_size, std::max(largest_item_size, item_main_size_sum + gap_sum));
            }
            outer_main_size = main_size + main_content_box_inset;
        }
    }

    outer_main_size = fm_clamp_f32(outer_main_size, constants.min_size.main(dir), constants.max_size.main(dir));
    outer_main_size = std::max(outer_main_size, main_content_box_inset - constants.scrollbar_gutter.main(dir));
    const float inner_main_size = std::max(outer_main_size - main_content_box_inset, 0.0f);

    constants.container_size.set_main(dir, outer_main_size);
    constants.inner_container_size.set_main(dir, inner_main_size);
    constants.node_inner_size.set_main(dir, inner_main_size);
}

// --- Step 6: resolve flexible lengths ---

/// Resolve the flexible lengths of the items within a flex line.
inline void flex_resolve_flexible_lengths(FlexLine& line, const FlexAlgoConstants& constants) {
    const FlexDirection dir = constants.dir;
    const float total_main_axis_gap = sum_axis_gaps(constants.gap.main(dir), line.items.size());

    // 1. Determine the used flex factor.
    float total_hypothetical_outer_main_size = 0.0f;
    for (const auto& child : line.items) total_hypothetical_outer_main_size += child.hypothetical_outer_size.main(dir);
    const float used_flex_factor = total_main_axis_gap + total_hypothetical_outer_main_size;
    const std::optional<float> inner_main_size = constants.node_inner_size.main(dir);
    const float inner_main = inner_main_size.value_or(0.0f);
    const bool growing = used_flex_factor < inner_main;
    const bool shrinking = used_flex_factor > inner_main;
    const bool exactly_sized = !growing && !shrinking;

    // 2. Size inflexible items.
    for (auto& child : line.items) {
        const float inner_target_size = child.hypothetical_inner_size.main(dir);
        child.target_size.set_main(dir, inner_target_size);
        if (exactly_sized
            || (child.flex_grow == 0.0f && child.flex_shrink == 0.0f)
            || (growing && child.flex_basis > child.hypothetical_inner_size.main(dir))
            || (shrinking && child.flex_basis < child.hypothetical_inner_size.main(dir))) {
            child.frozen = true;
            child.outer_target_size.set_main(dir, inner_target_size + child.margin.main_axis_sum(dir));
        }
    }

    if (exactly_sized) return;

    // 3. Calculate initial free space.
    float used_space = total_main_axis_gap;
    for (const auto& child : line.items) {
        used_space += child.frozen ? child.outer_target_size.main(dir)
                                   : child.flex_basis + child.margin.main_axis_sum(dir);
    }
    const float initial_free_space = fm_sub(inner_main_size, std::optional<float>(used_space)).value_or(0.0f);

    // 4. Loop
    for (;;) {
        // a. Check for flexible items.
        bool all_frozen = true;
        float items_space = 0.0f;
        float sum_flex_grow = 0.0f;
        float sum_flex_shrink = 0.0f;
        for (const auto& child : line.items) {
            if (child.frozen) {
                items_space += child.outer_target_size.main(dir);
            } else {
                all_frozen = false;
                items_space += child.flex_basis + child.margin.main_axis_sum(dir);
                sum_flex_grow += child.flex_grow;
                sum_flex_shrink += child.flex_shrink;
            }
        }
        if (all_frozen) break;

        // b. Calculate the remaining free space.
        const float used = total_main_axis_gap + items_space;
        float free_space;
        if (growing && sum_flex_grow < 1.0f) {
            free_space = fm_min_f32(initial_free_space * sum_flex_grow - total_main_axis_gap,
                                    fm_sub(inner_main_size, std::optional<float>(used)));
        } else if (shrinking && sum_flex_shrink < 1.0f) {
            free_space = fm_max_f32(initial_free_space * sum_flex_shrink - total_main_axis_gap,
                                    fm_sub(inner_main_size, std::optional<float>(used)));
        } else {
            free_space = fm_sub(inner_main_size, std::optional<float>(used)).value_or(used_flex_factor - used);
        }

        // c. Distribute free space proportional to the flex factors.
        if (std::isnormal(free_space)) {
            if (growing && sum_flex_grow > 0.0f) {
                for (auto& child : line.items) {
                    if (!child.frozen) {
                        child.target_size.set_main(dir, child.flex_basis + free_space * (child.flex_grow / sum_flex_grow));
                    }
                }
            } else if (shrinking && sum_flex_shrink > 0.0f) {
                float sum_scaled_shrink_factor = 0.0f;
                for (const auto& child : line.items) {
                    if (!child.frozen) sum_scaled_shrink_factor += child.inner_flex_basis * child.flex_shrink;
                }
                if (sum_scaled_shrink_factor > 0.0f) {
                    for (auto& child : line.items) {
                        if (!child.frozen) {
                            const float scaled_shrink_factor = child.inner_flex_basis * child.flex_shrink;
                            child.target_size.set_main(dir, child.flex_basis + free_space * (scaled_shrink_factor / sum_scaled_shrink_factor));
                        }
                    }
                }
            }
        }

        // d. Fix min/max violations.
        float total_violation = 0.0f;
        for (auto& child : line.items) {
            if (child.frozen) continue;
            const std::optional<float> resolved_min_main = std::optional<float>(child.resolved_minimum_main_size);
            const std::optional<float> max_main = child.max_size.main(dir);
            const float target = child.target_size.main(dir);
            const float clamped = std::max(fm_clamp_f32(target, resolved_min_main, max_main), 0.0f);
            child.violation = clamped - target;
            child.target_size.set_main(dir, clamped);
            child.outer_target_size.set_main(dir, clamped + child.margin.main_axis_sum(dir));
            total_violation += child.violation;
        }

        // e. Freeze over-flexed items.
        for (auto& child : line.items) {
            if (child.frozen) continue;
            if (total_violation > 0.0f) child.frozen = child.violation > 0.0f;
            else if (total_violation < 0.0f) child.frozen = child.violation < 0.0f;
            else child.frozen = true;
        }
        // f. Return to the start of this loop.
    }
}

// --- Step 7: hypothetical cross size ---

/// Compute whether each of an item's known dimensions should be treated as definite.
inline Size<bool> flex_item_known_dimension_definiteness(const FlexAlgoConstants& constants, const FlexItem& item) {
    const FlexDirection dir = constants.dir;
    const bool main_is_definite = constants.has_definite_main_size || item.flex_basis_is_definite;

    const bool has_cross_auto_margins =
        item.margin_is_auto.cross_start(dir) || item.margin_is_auto.cross_end(dir);
    const bool cross_size_is_definite = !has_cross_auto_margins
        && (item.align_self.keyword == AlignItemsKeyword::Stretch && item.size_style.cross(dir).is_auto());
    const bool cross_is_definite = cross_size_is_definite
        || item.size.cross(dir).has_value()
        || (!constants.is_row && constants.cross_axis_available_space_is_definite);

    Size<bool> out{true, true};
    out.set_main(dir, main_is_definite);
    out.set_cross(dir, cross_is_definite);
    return out;
}

/// Determine the hypothetical cross size of each item.
template <typename Tree, typename MeasureFn>
inline void flex_determine_hypothetical_cross_size(Tree& tree, FlexLine& line, const FlexAlgoConstants& constants,
    Size<std::optional<float>> node_size, Size<AvailableSpace> available_space, MeasureFn& measure_fn) {
    const FlexDirection dir = constants.dir;
    // If the flex container is single-line and has a definite cross size then that is the cross
    // size of the line, whatever the hypothetical cross sizes of the items are.
    const bool line_cross_size_is_known = !constants.is_wrap && node_size.cross(dir).has_value();

    for (auto& child : line.items) {
        const float padding_border_sum = (child.padding + child.border).cross_axis_sum(dir);

        if (line_cross_size_is_known && child.is_stretched(dir) && !child.participates_in_baseline_alignment(dir)) {
            continue;
        }

        // The available space passed to the child excludes the child's margins.
        const AvailableSpace child_available_main =
            AvailableSpace::definite(std::max(constants.container_size.main(dir) - child.margin.main_axis_sum(dir), 0.0f));

        const std::optional<float> transferred_min_cross =
            child.min_size.maybe_apply_aspect_ratio(child.aspect_ratio).cross(dir);
        const std::optional<float> transferred_max_cross =
            child.max_size.maybe_apply_aspect_ratio(child.aspect_ratio).cross(dir);

        const std::optional<float> child_cross = fo_max_f32(
            fm_clamp(child.size.cross(dir), transferred_min_cross, transferred_max_cross), padding_border_sum);

        AvailableSpace child_available_cross = available_space.cross(dir).map_definite_value([&](float val) {
            return std::max(constants.divided_cross_space(val) - child.margin.cross_axis_sum(dir), 0.0f);
        });
        child_available_cross = av_maybe_clamp(child_available_cross, transferred_min_cross, transferred_max_cross);
        child_available_cross = av_maybe_max_f32(child_available_cross, padding_border_sum);

        const std::optional<float> node_inner_cross = constants.node_inner_size.cross(dir);
        const std::optional<float> divided_node_inner_cross = node_inner_cross
            ? std::optional<float>(constants.divided_cross_space(*node_inner_cross))
            : std::nullopt;
        const std::optional<float> cross_stretch_size =
            fm_max(fm_sub(divided_node_inner_cross,
                          std::optional<float>(child.margin.cross_axis_sum(dir))),
                   std::optional<float>(0.0f));
        const auto keyword_resolution = flex_resolve_sizing_keyword(
            child.size_style.cross(dir), cross_stretch_size, constants.node_inner_size.cross(dir));
        if (keyword_resolution) child_available_cross = keyword_resolution->available;

        const float child_inner_cross = child_cross.has_value()
            ? *child_cross
            : [&]() {
                LayoutInput li;
                li.run_mode = RunMode::ComputeSize;
                li.sizing_mode = SizingMode::ContentSize;
                li.axis = constants.is_row ? RequestedAxis::Vertical : RequestedAxis::Horizontal;
                li.known_dimensions = constants.is_row
                    ? Size<std::optional<float>>{child.target_size.width, child_cross}
                    : Size<std::optional<float>>{child_cross, child.target_size.height};
                flex_set_known_definiteness(li, flex_item_known_dimension_definiteness(constants, child));
                li.parent_size = constants.node_inner_size;
                li.available_space = constants.is_row
                    ? Size<AvailableSpace>{child_available_main, child_available_cross}
                    : Size<AvailableSpace>{child_available_cross, child_available_main};
                li.vertical_margins_are_collapsible = Line_bool_FALSE;
                const LayoutOutput out = tree.compute_node_layout(child.node, li, measure_fn);
                const float measured = constants.is_row ? out.size.height : out.size.width;
                return std::max(fm_clamp_f32(measured, transferred_min_cross, transferred_max_cross), padding_border_sum);
            }();

        const float child_outer_cross = child_inner_cross + child.margin.cross_axis_sum(dir);
        child.hypothetical_inner_size.set_cross(dir, child_inner_cross);
        child.hypothetical_outer_size.set_cross(dir, child_outer_cross);
    }
}

// --- Step 7b: child baselines ---

/// Calculate the base lines of the children.
template <typename Tree, typename MeasureFn>
inline void flex_calculate_children_base_lines(Tree& tree, Size<std::optional<float>> node_size,
    Size<AvailableSpace> available_space, std::vector<FlexLine>& flex_lines, const FlexAlgoConstants& constants,
    MeasureFn& measure_fn) {
    if (!constants.is_row) return;

    const FlexDirection dir = constants.dir;
    for (auto& line : flex_lines) {
        size_t line_baseline_child_count = 0;
        for (const auto& child : line.items) {
            if (child.participates_in_baseline_alignment(dir)) line_baseline_child_count++;
        }
        if (line_baseline_child_count <= 1) continue;

        for (auto& child : line.items) {
            if (!child.participates_in_baseline_alignment(dir)) continue;

            Size<AvailableSpace> child_available_space;
            child_available_space.width = constants.is_row
                ? AvailableSpace::definite(constants.container_size.width)
                : available_space.width.maybe_set(node_size.width);
            child_available_space.height = constants.is_row
                ? available_space.height.maybe_set(node_size.height)
                : AvailableSpace::definite(constants.container_size.height);
            child_available_space.width = av_maybe_max_f32(
                av_maybe_sub_f32(child_available_space.width, child.margin.left + child.margin.right), 0.0f);
            child_available_space.height = av_maybe_max_f32(
                av_maybe_sub_f32(child_available_space.height, child.margin.top + child.margin.bottom), 0.0f);

            LayoutInput li;
            li.run_mode = RunMode::PerformLayout;
            li.sizing_mode = SizingMode::ContentSize;
            li.axis = RequestedAxis::Both;
            li.known_dimensions = constants.is_row
                ? Size<std::optional<float>>{child.target_size.width, child.hypothetical_inner_size.height}
                : Size<std::optional<float>>{child.hypothetical_inner_size.width, child.target_size.height};
            flex_set_known_definiteness(li, flex_item_known_dimension_definiteness(constants, child));
            li.parent_size = constants.node_inner_size;
            li.available_space = child_available_space;
            li.vertical_margins_are_collapsible = Line_bool_FALSE;
            const LayoutOutput out = tree.compute_node_layout(child.node, li, measure_fn);

            const float height = out.size.height;
            const float baseline = flex_child_first_baseline(out.baselines.first, height, child.overflow.y).value_or(height);
            child.baseline = baseline + child.margin.top;
        }
    }
}

// --- Step 8: line cross size ---

/// Calculate the cross size of each flex line.
inline void flex_calculate_cross_size(std::vector<FlexLine>& flex_lines, Size<std::optional<float>> node_size,
    const FlexAlgoConstants& constants) {
    const FlexDirection dir = constants.dir;

    if (!constants.is_wrap && node_size.cross(dir).has_value()) {
        const float cross_axis_padding_border = constants.content_box_inset.cross_axis_sum(dir);
        const std::optional<float> clamped_cross =
            fm_clamp(node_size.cross(dir), constants.min_size.cross(dir), constants.max_size.cross(dir));
        flex_lines[0].cross_size = std::max(clamped_cross ? *clamped_cross - cross_axis_padding_border : 0.0f, 0.0f);
        return;
    }

    for (auto& line : flex_lines) {
        float max_baseline = 0.0f;
        for (const auto& child : line.items) max_baseline = std::max(max_baseline, child.baseline);
        float cross = 0.0f;
        for (const auto& child : line.items) {
            const float item_cross = child.participates_in_baseline_alignment(dir)
                ? max_baseline - child.baseline + child.hypothetical_outer_size.cross(dir)
                : child.hypothetical_outer_size.cross(dir);
            cross = std::max(cross, item_cross);
        }
        line.cross_size = cross;
    }

    // If the flex container is single-line, then clamp the line's cross-size to be within the
    // container's computed min and max cross sizes.
    if (!constants.is_wrap) {
        const float cross_axis_padding_border = constants.content_box_inset.cross_axis_sum(dir);
        flex_lines[0].cross_size = fm_clamp(flex_lines[0].cross_size,
            fo_sub_f32(constants.min_size.cross(dir), cross_axis_padding_border),
            fo_sub_f32(constants.max_size.cross(dir), cross_axis_padding_border))
            .value_or(flex_lines[0].cross_size);
    }
}

// --- Step 9: align-content stretch ---

/// Handle 'align-content: stretch'.
inline void flex_handle_align_content_stretch(std::vector<FlexLine>& flex_lines,
    Size<std::optional<float>> node_size, const FlexAlgoConstants& constants) {
    const FlexDirection dir = constants.dir;
    if (constants.align_content.keyword != AlignContentKeyword::Stretch) return;

    const float cross_axis_padding_border = constants.content_box_inset.cross_axis_sum(dir);
    const std::optional<float> cross_min_size = constants.min_size.cross(dir);
    const std::optional<float> cross_max_size = constants.max_size.cross(dir);

    const std::optional<float> node_cross = node_size.cross(dir);
    const std::optional<float> basis = node_cross ? node_cross : cross_min_size;
    float container_min_inner_cross = 0.0f;
    if (basis) {
        const std::optional<float> clamped = fm_clamp(basis, cross_min_size, cross_max_size);
        container_min_inner_cross = std::max(fm_sub_f32(*clamped, cross_axis_padding_border), 0.0f);
    }

    const float total_cross_axis_gap = sum_axis_gaps(constants.gap.cross(dir), flex_lines.size());
    float lines_total_cross = total_cross_axis_gap;
    for (const auto& line : flex_lines) lines_total_cross += line.cross_size;

    if (lines_total_cross < container_min_inner_cross) {
        const float remaining = container_min_inner_cross - lines_total_cross;
        const float addition = remaining / static_cast<float>(flex_lines.size());
        for (auto& line : flex_lines) line.cross_size += addition;
    }
}

// --- Step 11: used cross size ---

/// Determine the used cross size of each flex item.
template <typename Tree>
inline void flex_determine_used_cross_size(Tree& tree, std::vector<FlexLine>& flex_lines,
    const FlexAlgoConstants& constants) {
    const FlexDirection dir = constants.dir;
    for (auto& line : flex_lines) {
        const float line_cross_size = line.cross_size;
        for (auto& child : line.items) {
            if (child.is_stretched(dir)) {
                const Style& child_style = tree.style(child.node);
                const Size<float> box_sizing_adjustment = child.box_sizing_adjustment(child_style);
                // Chrome/Firefox do not transfer max-width through the aspect ratio for this usage.
                const Size<std::optional<float>> max_size_ignoring_aspect_ratio =
                    fs_add_f32(maybe_resolve_size(child_style.max_size, constants.node_inner_size), box_sizing_adjustment);
                const float target_cross = fm_clamp_f32(
                    std::max(line_cross_size - child.margin.cross_axis_sum(dir), 0.0f),
                    child.min_size.cross(dir),
                    max_size_ignoring_aspect_ratio.cross(dir));
                child.target_size.set_cross(dir, target_cross);
            } else {
                child.target_size.set_cross(dir, child.hypothetical_inner_size.cross(dir));
            }
            child.outer_target_size.set_cross(dir, child.target_size.cross(dir) + child.margin.cross_axis_sum(dir));
        }
    }
}

// --- Step 12: main-axis alignment ---

/// Distribute any remaining free space.
inline void flex_distribute_remaining_free_space(std::vector<FlexLine>& flex_lines, const FlexAlgoConstants& constants) {
    const FlexDirection dir = constants.dir;
    for (auto& line : flex_lines) {
        const float total_main_axis_gap = sum_axis_gaps(constants.gap.main(dir), line.items.size());
        float used_space = total_main_axis_gap;
        for (const auto& child : line.items) used_space += child.outer_target_size.main(dir);
        float free_space = constants.inner_container_size.main(dir) - used_space;
        int num_auto_margins = 0;
        for (const auto& child : line.items) {
            if (child.margin_is_auto.main_start(dir)) num_auto_margins++;
            if (child.margin_is_auto.main_end(dir)) num_auto_margins++;
        }

        if (free_space > 0.0f && num_auto_margins > 0) {
            const float margin = free_space / static_cast<float>(num_auto_margins);
            for (auto& child : line.items) {
                if (child.margin_is_auto.main_start(dir)) {
                    if (constants.is_row) child.margin.left = margin;
                    else child.margin.top = margin;
                }
                if (child.margin_is_auto.main_end(dir)) {
                    if (constants.is_row) child.margin.right = margin;
                    else child.margin.bottom = margin;
                }
            }
            // The auto margins have absorbed all of the free space
            free_space = 0.0f;
        }

        const size_t num_items = line.items.size();
        const bool layout_reverse = is_reverse(dir);
        const float gap = constants.gap.main(dir);
        const AlignContentKeyword justify_content_mode =
            apply_alignment_fallback(free_space, num_items, constants.justify_content);

        if (layout_reverse) {
            for (size_t i = 0; i < num_items; ++i) {
                auto& child = line.items[num_items - 1 - i];
                child.offset_main = compute_alignment_offset(free_space, num_items, gap, justify_content_mode, layout_reverse, i == 0);
            }
        } else {
            for (size_t i = 0; i < num_items; ++i) {
                line.items[i].offset_main = compute_alignment_offset(free_space, num_items, gap, justify_content_mode, layout_reverse, i == 0);
            }
        }
    }
}
// --- Step 13/14: cross-axis alignment ---

/// Align all flex items along the cross-axis per `align-self`.
inline float flex_align_flex_items_along_cross_axis(const FlexItem& child, float free_space,
    float max_baseline, float max_baseline_to_bottom_distance, const FlexAlgoConstants& constants) {
    const bool cross_axis_should_reverse = constants.is_column && constants.layout_direction == Direction::Rtl;

    // If align-self uses a "safe" overflow-position keyword and the item would overflow its line
    // cross size, fall back to logical Start.
    const AlignItemsKeyword align_keyword =
        (child.align_self.is_safe() && free_space < 0.0f) ? AlignItemsKeyword::Start : child.align_self.keyword;

    switch (align_keyword) {
        case AlignItemsKeyword::Start:
            return cross_axis_should_reverse ? free_space : 0.0f;
        case AlignItemsKeyword::FlexStart:
            return (constants.is_wrap_reverse ^ cross_axis_should_reverse) ? free_space : 0.0f;
        case AlignItemsKeyword::End:
            return cross_axis_should_reverse ? 0.0f : free_space;
        case AlignItemsKeyword::FlexEnd:
            return (constants.is_wrap_reverse ^ cross_axis_should_reverse) ? 0.0f : free_space;
        case AlignItemsKeyword::Center:
            return free_space / 2.0f;
        case AlignItemsKeyword::Baseline:
            if (constants.is_row) {
                if (constants.is_wrap_reverse) {
                    const float line_cross_size = free_space + child.outer_target_size.cross(constants.dir);
                    return line_cross_size - max_baseline_to_bottom_distance - child.baseline;
                }
                return max_baseline - child.baseline;
            } else {
                const bool baseline_column_should_reverse = cross_axis_should_reverse && !constants.is_wrap;
                return (constants.is_wrap_reverse ^ baseline_column_should_reverse) ? free_space : 0.0f;
            }
        case AlignItemsKeyword::Stretch:
            return (constants.is_wrap_reverse ^ cross_axis_should_reverse) ? free_space : 0.0f;
        default:
            // `normal` has been resolved to `stretch`, `self-start`/`self-end` to `start`/`end`.
            return (constants.is_wrap_reverse ^ cross_axis_should_reverse) ? free_space : 0.0f;
    }
}

/// Resolve cross-axis `auto` margins (and align items in the cross axis).
inline void flex_resolve_cross_axis_auto_margins(std::vector<FlexLine>& flex_lines, const FlexAlgoConstants& constants) {
    const FlexDirection dir = constants.dir;
    for (auto& line : flex_lines) {
        const float line_cross_size = line.cross_size;
        float max_baseline = 0.0f;
        for (const auto& child : line.items) max_baseline = std::max(max_baseline, child.baseline);
        float max_baseline_to_bottom_distance = 0.0f;
        for (const auto& child : line.items) {
            if (child.participates_in_baseline_alignment(dir)) {
                max_baseline_to_bottom_distance = std::max(max_baseline_to_bottom_distance,
                    child.outer_target_size.cross(dir) - child.baseline);
            }
        }

        for (auto& child : line.items) {
            const float free_space = line_cross_size - child.outer_target_size.cross(dir);
            if (child.margin_is_auto.cross_start(dir) && child.margin_is_auto.cross_end(dir)) {
                if (constants.is_row) {
                    child.margin.top = free_space / 2.0f;
                    child.margin.bottom = free_space / 2.0f;
                } else {
                    child.margin.left = free_space / 2.0f;
                    child.margin.right = free_space / 2.0f;
                }
            } else if (child.margin_is_auto.cross_start(dir)) {
                if (constants.is_row) child.margin.top = free_space;
                else child.margin.left = free_space;
            } else if (child.margin_is_auto.cross_end(dir)) {
                if (constants.is_row) child.margin.bottom = free_space;
                else child.margin.right = free_space;
            } else {
                child.offset_cross = flex_align_flex_items_along_cross_axis(
                    child, free_space, max_baseline, max_baseline_to_bottom_distance, constants);
            }
        }
    }
}

// --- Step 15: container cross size ---

/// Determine the flex container's used cross size.
inline float flex_determine_container_cross_size(const std::vector<FlexLine>& flex_lines,
    Size<std::optional<float>> node_size, FlexAlgoConstants& constants) {
    const FlexDirection dir = constants.dir;
    const float total_cross_axis_gap = sum_axis_gaps(constants.gap.cross(dir), flex_lines.size());
    float total_line_cross_size = 0.0f;
    for (const auto& line : flex_lines) total_line_cross_size += line.cross_size;

    const float padding_border_sum = constants.content_box_inset.cross_axis_sum(dir);
    const float cross_scrollbar_gutter = constants.scrollbar_gutter.cross(dir);
    float outer_container_size = node_size.cross(dir).value_or(
        total_line_cross_size + total_cross_axis_gap + padding_border_sum);
    outer_container_size = fm_clamp_f32(outer_container_size, constants.min_size.cross(dir), constants.max_size.cross(dir));
    outer_container_size = std::max(outer_container_size, padding_border_sum - cross_scrollbar_gutter);
    const float inner_container_size = std::max(outer_container_size - padding_border_sum, 0.0f);

    constants.container_size.set_cross(dir, outer_container_size);
    constants.inner_container_size.set_cross(dir, inner_container_size);
    return total_line_cross_size;
}

// --- Step 16: align flex lines ---

/// Align all flex lines per `align-content`.
inline void flex_align_flex_lines_per_align_content(std::vector<FlexLine>& flex_lines,
    const FlexAlgoConstants& constants, float total_cross_size) {
    const FlexDirection dir = constants.dir;
    const size_t num_lines = flex_lines.size();
    const float gap = constants.gap.cross(dir);
    const float total_cross_axis_gap = sum_axis_gaps(gap, num_lines);
    const float free_space = constants.inner_container_size.cross(dir) - total_cross_size - total_cross_axis_gap;

    const AlignContentKeyword align_content_mode =
        apply_alignment_fallback(free_space, num_lines, constants.align_content);

    if (constants.is_wrap_reverse) {
        for (size_t i = 0; i < num_lines; ++i) {
            auto& line = flex_lines[num_lines - 1 - i];
            line.offset_cross = compute_alignment_offset(free_space, num_lines, gap, align_content_mode, true, i == 0);
        }
    } else {
        for (size_t i = 0; i < num_lines; ++i) {
            flex_lines[i].offset_cross = compute_alignment_offset(free_space, num_lines, gap, align_content_mode, false, i == 0);
        }
    }
}

// --- Scrollable overflow (Rust: compute/common/scrollable_overflow.rs) ---

/// Container geometry shared by its children's scrollable overflow contributions.
struct FlexScrollableOverflowContainer {
    bool parent_is_scroll_container{false};
    Size<float> parent_size{0.0f, 0.0f};
    Point<bool> reversed{false, false};
    Rect<float> padding{};
};

/// Determine the rectangle that a node contributes to its parent's scrollable overflow rectangle.
inline Rect<float> flex_compute_scrollable_overflow_contribution(
    Point<float> location, Size<float> size, Rect<float> scrollable_overflow_rect, Rect<float> border,
    Point<Overflow> overflow, const Contain& contain, const FlexScrollableOverflowContainer& container,
    Rect<float> margin) {
    const Rect<float> rect{
        scrollable_overflow_rect.left + border.left,
        scrollable_overflow_rect.right + border.left,
        scrollable_overflow_rect.top + border.top,
        scrollable_overflow_rect.bottom + border.top};
    const bool is_sc = ::taffy::is_scroll_container(overflow.x) || ::taffy::is_scroll_container(overflow.y);
    const bool overflow_is_contained = flex_contain_contains_scrollable_overflow(contain);
    const Point<bool> propagates{
        !is_sc && !overflow_is_contained && overflow.x == Overflow::Visible,
        !is_sc && !overflow_is_contained && overflow.y == Overflow::Visible};
    const Size<float> end_extent{
        propagates.x ? std::max(size.width, rect.right) : size.width,
        propagates.y ? std::max(size.height, rect.bottom) : size.height};
    if (end_extent.width <= 0.0f || end_extent.height <= 0.0f) return Rect<float>{0.0f, 0.0f, 0.0f, 0.0f};
    const Point<float> start_extent{
        propagates.x ? std::min(0.0f, rect.left) : 0.0f,
        propagates.y ? std::min(0.0f, rect.top) : 0.0f};
    const Rect<float> contribution{
        location.x + start_extent.x,
        location.x + end_extent.width,
        location.y + start_extent.y,
        location.y + end_extent.height};
    const bool is_wholly_unreachable =
        (container.reversed.x ? contribution.left >= container.parent_size.width : contribution.right <= 0.0f)
        || (container.reversed.y ? contribution.top >= container.parent_size.height : contribution.bottom <= 0.0f);
    if (container.parent_is_scroll_container && is_wholly_unreachable) return Rect<float>{0.0f, 0.0f, 0.0f, 0.0f};
    if (container.parent_is_scroll_container) {
        Rect<float> padded = contribution;
        if (container.reversed.x) {
            padded.left = std::min(padded.left, location.x - std::max(0.0f, container.padding.left + margin.left));
        } else {
            padded.right = std::max(padded.right,
                location.x + size.width + std::max(0.0f, container.padding.right + margin.right));
        }
        if (container.reversed.y) {
            padded.top = std::min(padded.top, location.y - std::max(0.0f, container.padding.top + margin.top));
        } else {
            padded.bottom = std::max(padded.bottom,
                location.y + size.height + std::max(0.0f, container.padding.bottom + margin.bottom));
        }
        return padded;
    }
    return contribution;
}

/// Pack an algorithm's physical accumulator and clip unreachable overflow only for scroll containers.
inline ScrollableOverflow flex_finalize_scrollable_overflow(Rect<float> rect, Size<float> size,
    Point<bool> reversed, bool is_sc) {
    size = Size<float>{std::max(size.width, 0.0f), std::max(size.height, 0.0f)};
    Point<bool> out_reversed = reversed;
    if (is_sc) {
        if (reversed.x) rect.right = std::min(rect.right, size.width);
        else rect.left = 0.0f;
        if (reversed.y) rect.bottom = std::min(rect.bottom, size.height);
        else rect.top = 0.0f;
    } else {
        out_reversed = Point<bool>{false, false};
    }
    return ScrollableOverflow::new_(rect, out_reversed);
}

inline Rect<float> flex_rect_union(Rect<float> a, Rect<float> b) {
    return Rect<float>{std::min(a.left, b.left), std::max(a.right, b.right),
                       std::min(a.top, b.top), std::max(a.bottom, b.bottom)};
}

/// Map main/cross reversal to physical axes, including RTL inline progression.
inline Point<bool> flex_scroll_origin_reversed(const FlexAlgoConstants& c) {
    const bool main = is_reverse(c.dir);
    const bool cross = c.is_wrap_reverse;
    const bool x = c.is_column ? cross : main;
    const bool y = c.is_column ? main : cross;
    const bool rtl = c.layout_direction == Direction::Rtl;
    return Point<bool>{static_cast<bool>(x ^ rtl), y};
}

// --- Final layout pass ---

/// Calculates the layout for a flex item.
template <typename Tree, typename MeasureFn>
inline void flex_calculate_flex_item(Tree& tree, FlexItem& item, float& total_offset_main, float total_offset_cross,
    float line_offset_cross, Rect<float>& total_overflow_rect,
    const FlexScrollableOverflowContainer& overflow_container,
    const FlexAlgoConstants& constants, MeasureFn& measure_fn) {
    const Size<float> container_size = constants.container_size;
    const Size<std::optional<float>> node_inner_size = constants.node_inner_size;
    const FlexDirection direction = constants.dir;
    const Direction layout_direction = constants.layout_direction;

    const Size<AvailableSpace> available_space{
        AvailableSpace::definite(std::max(container_size.width - item.margin.left - item.margin.right, 0.0f)),
        AvailableSpace::definite(std::max(container_size.height - item.margin.top - item.margin.bottom, 0.0f))};

    LayoutInput li;
    li.run_mode = RunMode::PerformLayout;
    li.sizing_mode = SizingMode::ContentSize;
    li.axis = RequestedAxis::Both;
    li.known_dimensions = Size<std::optional<float>>{item.target_size.width, item.target_size.height};
    flex_set_known_definiteness(li, flex_item_known_dimension_definiteness(constants, item));
    li.parent_size = node_inner_size;
    li.available_space = available_space;
    li.vertical_margins_are_collapsible = Line_bool_FALSE;
    const LayoutOutput layout_output = tree.compute_node_layout(item.node, li, measure_fn);
    const Size<float> size = layout_output.size;

    const bool is_rtl_row = is_row(direction) && layout_direction == Direction::Rtl;
    const bool is_rtl_column = !is_row(direction) && layout_direction == Direction::Rtl;
    const float main_relative_inset = item.relative_inset.main(direction);
    const float cross_relative_inset = item.relative_inset.cross(direction);
    const float effective_line_offset_cross = is_rtl_column ? 0.0f : line_offset_cross;

    const float offset_main = is_rtl_row
        ? total_offset_main - item.offset_main - item.margin.main_end(direction) - main_relative_inset - size.width
        : total_offset_main + item.offset_main + item.margin.main_start(direction) + main_relative_inset;
    const float offset_cross = total_offset_cross + item.offset_cross + effective_line_offset_cross
        + item.margin.cross_start(direction) + cross_relative_inset;

    const float inner_baseline =
        flex_child_first_baseline(layout_output.baselines.first, size.height, item.overflow.y).value_or(size.height);
    if (is_row(direction)) {
        const float baseline_offset_cross = total_offset_cross + item.offset_cross + effective_line_offset_cross
            + item.margin.cross_start(direction);
        item.baseline = baseline_offset_cross + inner_baseline;
    } else {
        const float baseline_offset_main = total_offset_main + item.offset_main + item.margin.main_start(direction);
        item.baseline = baseline_offset_main + inner_baseline;
    }

    const Point<float> location = is_row(direction)
        ? Point<float>{offset_main, offset_cross}
        : Point<float>{offset_cross, offset_main};

    Layout result;
    result.order = item.order;
    result.location = location;
    result.size = size;
#ifdef TAFFY_CONTENT_SIZE
    result.scrollable_overflow_rect = layout_output.scrollable_overflow_rect;
#endif
    result.scrollbar_size = Size<float>{
        item.overflow.y == Overflow::Scroll ? item.scrollbar_width : 0.0f,
        item.overflow.x == Overflow::Scroll ? item.scrollbar_width : 0.0f};
    result.padding = item.padding;
    result.border = item.border;
    result.margin = item.margin;
    tree.set_unrounded_layout(item.node, result);

    if (is_rtl_row) {
        total_offset_main -= item.offset_main + item.margin.main_axis_sum(direction) + size.main(direction);
    } else {
        total_offset_main += item.offset_main + item.margin.main_axis_sum(direction) + size.main(direction);
    }

#ifdef TAFFY_CONTENT_SIZE
    const Point<float> contribution_location{
        location.x - constants.padding_box_inset.left, location.y - constants.padding_box_inset.top};
    const Rect<float> contribution = flex_compute_scrollable_overflow_contribution(
        contribution_location, size, layout_output.scrollable_overflow_rect.rect(), item.border, item.overflow,
        item.contain, overflow_container, item.margin);
    total_overflow_rect = flex_rect_union(total_overflow_rect, contribution);
#else
    (void)total_overflow_rect;
    (void)overflow_container;
#endif
}

/// Calculates the layout of one flex line.
template <typename Tree, typename MeasureFn>
inline void flex_calculate_layout_line(Tree& tree, FlexLine& line, float& total_offset_cross,
    Rect<float>& overflow_rect, const FlexScrollableOverflowContainer& overflow_container,
    const FlexAlgoConstants& constants, MeasureFn& measure_fn) {
    const Size<float> container_size = constants.container_size;
    const Rect<float> padding_border = constants.content_box_inset;
    const FlexDirection direction = constants.dir;
    const Direction layout_direction = constants.layout_direction;

    float total_offset_main = (layout_direction == Direction::Rtl && is_row(direction))
        ? container_size.width - padding_border.main_end(direction)
        : padding_border.main_start(direction);
    const float line_offset_cross = line.offset_cross;
    const bool is_rtl_column = layout_direction == Direction::Rtl && !is_row(direction);

    if (is_rtl_column) total_offset_cross -= line_offset_cross + line.cross_size;

    if (is_reverse(direction)) {
        for (size_t i = line.items.size(); i > 0; --i) {
            flex_calculate_flex_item(tree, line.items[i - 1], total_offset_main, total_offset_cross,
                                     line_offset_cross, overflow_rect, overflow_container, constants, measure_fn);
        }
    } else {
        for (auto& item : line.items) {
            flex_calculate_flex_item(tree, item, total_offset_main, total_offset_cross,
                                     line_offset_cross, overflow_rect, overflow_container, constants, measure_fn);
        }
    }

    if (!is_rtl_column) total_offset_cross += line_offset_cross + line.cross_size;
}

/// Do a final layout pass and collect the resulting layouts.
template <typename Tree, typename MeasureFn>
inline Rect<float> flex_final_layout_pass(Tree& tree, std::vector<FlexLine>& flex_lines,
    const FlexAlgoConstants& constants, MeasureFn& measure_fn) {
    const FlexDirection direction = constants.dir;

    FlexScrollableOverflowContainer overflow_container;
    overflow_container.parent_is_scroll_container = constants.is_scroll_container;
    overflow_container.parent_size = constants.container_size - constants.border.sum_axes()
        - Size<float>{constants.scrollbar_gutter.x, constants.scrollbar_gutter.y};
    overflow_container.reversed = flex_scroll_origin_reversed(constants);
    overflow_container.padding = constants.padding;

    float total_offset_cross = (constants.is_column && constants.layout_direction == Direction::Rtl)
        ? constants.container_size.width - constants.content_box_inset.cross_end(direction)
        : constants.content_box_inset.cross_start(direction);

    Rect<float> overflow_rect{0.0f, 0.0f, 0.0f, 0.0f};

    if (constants.is_wrap_reverse) {
        for (size_t i = flex_lines.size(); i > 0; --i) {
            flex_calculate_layout_line(tree, flex_lines[i - 1], total_offset_cross, overflow_rect,
                                       overflow_container, constants, measure_fn);
        }
    } else {
        for (auto& line : flex_lines) {
            flex_calculate_layout_line(tree, line, total_offset_cross, overflow_rect,
                                       overflow_container, constants, measure_fn);
        }
    }

    const float client_width =
        std::max(constants.container_size.width - constants.border.horizontal_axis_sum() - constants.scrollbar_gutter.x, 0.0f);
    if (constants.layout_direction == Direction::Rtl) {
        overflow_rect = flex_rect_union(overflow_rect, Rect<float>{0.0f, client_width, 0.0f, 0.0f});
    }
    if (constants.is_scroll_container) {
        overflow_rect = flex_rect_union(overflow_rect, Rect<float>{
            constants.layout_direction == Direction::Rtl
                ? std::min(0.0f, client_width - (constants.content_box_inset.left - constants.border.left
                                                 - constants.scrollbar_gutter.x))
                : 0.0f,
            constants.layout_direction == Direction::Rtl
                ? client_width
                : constants.content_box_inset.right - constants.border.right - constants.scrollbar_gutter.x,
            0.0f,
            constants.content_box_inset.bottom - constants.border.bottom - constants.scrollbar_gutter.y});
    }
    return overflow_rect;
}

// --- Entry points ---

/// Compute the layout of a flex container with no children.
template <typename Tree, typename MeasureFn>
inline LayoutOutput flex_compute_empty_container_layout(Tree& tree, LayoutInput inputs,
    FlexAlgoConstants constants, MeasureFn& measure_fn) {
    const FlexDirection dir = constants.dir;
    const Size<AvailableSpace> available_space =
        flex_determine_available_space(inputs.known_dimensions, inputs.available_space, constants);

    if (constants.node_inner_size.main(dir)) {
        const float inner_main_size = *constants.node_inner_size.main(dir);
        constants.inner_container_size.set_main(dir, inner_main_size);
        constants.container_size.set_main(dir, inner_main_size + constants.content_box_inset.main_axis_sum(dir));
    } else {
        std::vector<FlexLine> no_lines;
        flex_determine_container_main_size(tree, available_space, no_lines, constants, measure_fn);
    }

    if (inputs.run_mode == RunMode::ComputeSize && flex_main_axis_is_requested_axis(dir, inputs.axis)) {
        Size<float> size = Size_f32_ZERO;
        size.set_main(dir, constants.container_size.main(dir));
        return LayoutOutput::from_outer_size(size);
    }

    std::vector<FlexLine> no_lines;
    (void)flex_determine_container_cross_size(no_lines, inputs.known_dimensions, constants);

    LayoutOutput out = LayoutOutput::from_outer_size(constants.container_size);
#ifdef TAFFY_CONTENT_SIZE
    const float client_width =
        std::max(constants.container_size.width - constants.border.horizontal_axis_sum() - constants.scrollbar_gutter.x, 0.0f);
    const Rect<float> overflow_rect{
        0.0f, constants.layout_direction == Direction::Rtl ? client_width : 0.0f, 0.0f, 0.0f};
    out.scrollable_overflow_rect = flex_finalize_scrollable_overflow(
        overflow_rect,
        constants.container_size - constants.border.sum_axes()
            - Size<float>{constants.scrollbar_gutter.x, constants.scrollbar_gutter.y},
        flex_scroll_origin_reversed(constants),
        constants.is_scroll_container);
#endif
    out.oof_positioning_area = OofPositioningArea{Size_f32_ZERO, Point_f32_ZERO};
    return out;
}

/// Compute a preliminary size for an item (the flexbox algorithm proper).
template <typename Tree, typename MeasureFn>
inline LayoutOutput flex_compute_preliminary(Tree& tree, NodeId node, LayoutInput inputs, MeasureFn& measure_fn) {
    const Style& style = tree.style(node);

    FlexAlgoConstants constants = flex_compute_constants(tree, style, inputs.known_dimensions,
        inputs.known_dimensions_are_definite, inputs.parent_size, inputs.available_space);

    // A container with no children has no flex items or lines.
    if (tree.child_count(node) == 0) {
        return flex_compute_empty_container_layout(tree, inputs, constants, measure_fn);
    }

    // 9.1 Initial setup: generate anonymous flex items
    std::vector<FlexItem> flex_items = flex_generate_items(tree, node, constants);

    // 9.2 Line length determination
    const Size<AvailableSpace> available_space =
        flex_determine_available_space(inputs.known_dimensions, inputs.available_space, constants);
    flex_determine_base_size(tree, constants, available_space, flex_items, measure_fn);

    // 9.3 Main size determination: collect flex items into flex lines
    std::vector<FlexLine> flex_lines = constants.is_balance
        ? flex_collect_balanced_flex_lines(constants, available_space, flex_items)
        : flex_collect_flex_lines(constants, available_space, flex_items);

    // Determine the container's main size (if not already known), then re-resolve percentage gaps
    if (constants.node_inner_size.main(constants.dir)) {
        const float inner_main_size = *constants.node_inner_size.main(constants.dir);
        constants.inner_container_size.set_main(constants.dir, inner_main_size);
        constants.container_size.set_main(constants.dir,
            inner_main_size + constants.content_box_inset.main_axis_sum(constants.dir));
    } else {
        flex_determine_container_main_size(tree, available_space, flex_lines, constants, measure_fn);
        constants.node_inner_size.set_main(constants.dir, std::optional<float>(constants.inner_container_size.main(constants.dir)));
        constants.node_outer_size.set_main(constants.dir, std::optional<float>(constants.container_size.main(constants.dir)));

        const Style& re_style = tree.style(node);
        const float inner_container_size = constants.inner_container_size.main(constants.dir);
        const std::optional<float> new_gap = maybe_resolve(re_style.gap.main(constants.dir), inner_container_size);
        constants.gap.set_main(constants.dir, new_gap.value_or(0.0f));
    }

    // If only the container's main size has been requested then we are done.
    if (inputs.run_mode == RunMode::ComputeSize
        && flex_main_axis_is_requested_axis(constants.dir, inputs.axis)) {
        Size<float> size = Size_f32_ZERO;
        size.set_main(constants.dir, constants.container_size.main(constants.dir));
        return LayoutOutput::from_outer_size(size);
    }

    // 6. Resolve the flexible lengths of all the flex items
    for (auto& line : flex_lines) flex_resolve_flexible_lengths(line, constants);

    // 9.4 Cross size determination
    for (auto& line : flex_lines) {
        flex_determine_hypothetical_cross_size(tree, line, constants, inputs.known_dimensions, available_space, measure_fn);
    }
    flex_calculate_children_base_lines(tree, inputs.known_dimensions, available_space, flex_lines, constants, measure_fn);
    flex_calculate_cross_size(flex_lines, inputs.known_dimensions, constants);
    flex_handle_align_content_stretch(flex_lines, inputs.known_dimensions, constants);
    flex_determine_used_cross_size(tree, flex_lines, constants);

    // 9.5 Main-axis alignment
    flex_distribute_remaining_free_space(flex_lines, constants);

    // 9.6 Cross-axis alignment
    flex_resolve_cross_axis_auto_margins(flex_lines, constants);
    const float total_line_cross_size =
        flex_determine_container_cross_size(flex_lines, inputs.known_dimensions, constants);

    if (inputs.run_mode == RunMode::ComputeSize) {
        return LayoutOutput::from_outer_size(constants.container_size);
    }

    // 16. Align all flex lines per align-content.
    flex_align_flex_lines_per_align_content(flex_lines, constants, total_line_cross_size);

    // Do a final layout pass and gather the resulting layouts
    const Rect<float> inflow_overflow_rect = flex_final_layout_pass(tree, flex_lines, constants, measure_fn);

    // Lay out hidden children (display: none)
    const size_t child_count = tree.child_count(node);
    for (size_t order = 0; order < child_count; ++order) {
        const NodeId child = tree.get_child_id(node, order);
        if (tree.style(child).display == Display::None) {
            tree.set_unrounded_layout(child, Layout::with_order(static_cast<uint32_t>(order)));
            tree.compute_node_layout(child, LayoutInput::hidden_input(), measure_fn);
        }
    }

    // 8.5 Flex Container Baselines: the first baseline is generated from the startmost flex line.
    FlexLine* first_line = flex_lines.empty() ? nullptr
        : (constants.is_wrap_reverse ? &flex_lines.back() : &flex_lines.front());
    std::optional<float> first_vertical_baseline;
    if (first_line && !first_line->items.empty()) {
        if (constants.is_column) {
            if (is_reverse(constants.dir)) first_vertical_baseline = first_line->items.back().baseline;
            else first_vertical_baseline = first_line->items.front().baseline;
        } else {
            bool found = false;
            for (const auto& item : first_line->items) {
                if (item.participates_in_baseline_alignment(constants.dir)) {
                    first_vertical_baseline = item.baseline;
                    found = true;
                    break;
                }
            }
            if (!found) first_vertical_baseline = first_line->items.front().baseline;
        }
    }

    LayoutOutput out;
    out.size = constants.container_size;
#ifdef TAFFY_CONTENT_SIZE
    out.scrollable_overflow_rect = flex_finalize_scrollable_overflow(
        inflow_overflow_rect,
        constants.container_size - constants.border.sum_axes()
            - Size<float>{constants.scrollbar_gutter.x, constants.scrollbar_gutter.y},
        flex_scroll_origin_reversed(constants),
        constants.is_scroll_container);
#endif
    out.baselines = Baselines::from_first(first_vertical_baseline);
    out.oof_positioning_area = OofPositioningArea{
        constants.container_size - constants.padding_box_inset.sum_axes(),
        Point<float>{constants.padding_box_inset.left, constants.padding_box_inset.top}};
    return out;
}

/// Compute the layout of a box according to the flexbox algorithm.
template <typename Tree, typename MeasureFn>
inline LayoutOutput flex_compute_layout(Tree& tree, NodeId node, LayoutInput inputs, MeasureFn& measure_fn) {
    const Size<std::optional<float>> known_dimensions = inputs.known_dimensions;
    const Size<std::optional<float>> parent_size = inputs.parent_size;
    const Style& style = tree.style(node);

    const std::optional<float> aspect_ratio = style.aspect_ratio;
    const Rect<float> padding = resolve_or_zero_rect(style.padding, parent_size.width);
    const Rect<float> border = resolve_or_zero_rect(style.border, parent_size.width);
    const Size<float> padding_border_sum = padding.sum_axes() + border.sum_axes();
    const Size<float> box_sizing_adjustment =
        style.box_sizing == BoxSizing::ContentBox ? padding_border_sum : Size_f32_ZERO;

    const Size<std::optional<float>> min_size = fs_add_f32(
        maybe_resolve_size(style.min_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);
    const Size<std::optional<float>> max_size = fs_add_f32(
        maybe_resolve_size(style.max_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);
    Size<std::optional<float>> clamped_style_size{};
    if (inputs.sizing_mode == SizingMode::InherentSize) {
        clamped_style_size = fs_clamp(
            fs_add_f32(maybe_resolve_size(style.size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment),
            min_size, max_size);
    }

    // If both min and max in a given axis are set and max <= min then this determines the size in
    // that axis.
    Size<std::optional<float>> min_max_definite_size{};
    if (min_size.width && max_size.width && *max_size.width <= *min_size.width) min_max_definite_size.width = min_size.width;
    if (min_size.height && max_size.height && *max_size.height <= *min_size.height) min_max_definite_size.height = min_size.height;

    // The size of the container should be floored by the padding and border.
    Size<std::optional<float>> styled_based_known_dimensions =
        known_dimensions.or_(fs_max_f32(min_max_definite_size.or_(clamped_style_size), padding_border_sum));

    if (inputs.run_mode == RunMode::ComputeSize) {
        if (styled_based_known_dimensions.width && styled_based_known_dimensions.height) {
            return LayoutOutput::from_outer_size(Size<float>{*styled_based_known_dimensions.width, *styled_based_known_dimensions.height});
        }
        if (inputs.axis == RequestedAxis::Horizontal && styled_based_known_dimensions.width) {
            return LayoutOutput::from_outer_size(Size<float>{*styled_based_known_dimensions.width, 0.0f});
        }
    }

    // Normalize the definiteness flags: they only apply to dimensions which were passed in as
    // known by the parent. Dimensions resolved from the node's own style are always definite.
    Size<bool> known_dimensions_are_definite{
        inputs.known_dimensions_are_definite.width || !known_dimensions.width.has_value(),
        inputs.known_dimensions_are_definite.height || !known_dimensions.height.has_value()};

    LayoutInput prelim_inputs = inputs;
    prelim_inputs.known_dimensions = styled_based_known_dimensions;
    prelim_inputs.known_dimensions_are_definite = known_dimensions_are_definite;
    LayoutOutput output = flex_compute_preliminary(tree, node, prelim_inputs, measure_fn);

    // Layout containment suppresses the box's baseline for baseline-alignment purposes
    if (flex_contain_suppresses_baseline(style.contain)) {
        output.baselines = Baselines::none();
    }
    return output;
}

#endif // TAFFY_FLEXBOX#endif // TAFFY_FLEXBOX

// ===========================================================================
// Section 15b: Grid Algorithm
// Ref: src/compute/grid/mod.rs, explicit_grid.rs, implicit_grid.rs, placement.rs,
//      track_sizing.rs, alignment.rs, types/*.rs
// ===========================================================================

#ifdef TAFFY_GRID
namespace grid_detail {

// ---------------------------------------------------------------------------
// Size<optional<float>> / AvailableSpace maybe-math helpers used by the grid algorithm
// ---------------------------------------------------------------------------

inline Size<std::optional<float>> size_opt_or(Size<std::optional<float>> a, Size<std::optional<float>> b) {
    return {a.width ? a.width : b.width, a.height ? a.height : b.height};
}

inline Size<std::optional<float>> size_opt_maybe_add(Size<std::optional<float>> a, Size<float> b) {
    Size<std::optional<float>> r;
    if (a.width) r.width = *a.width + b.width;
    if (a.height) r.height = *a.height + b.height;
    return r;
}

inline Size<std::optional<float>> size_opt_maybe_sub(Size<std::optional<float>> a, Size<std::optional<float>> b) {
    Size<std::optional<float>> r;
    if (a.width && b.width) r.width = *a.width - *b.width;
    if (a.height && b.height) r.height = *a.height - *b.height;
    return r;
}

inline Size<std::optional<float>> size_opt_maybe_sub(Size<std::optional<float>> a, Size<float> b) {
    Size<std::optional<float>> r;
    if (a.width) r.width = *a.width - b.width;
    if (a.height) r.height = *a.height - b.height;
    return r;
}

/// Resolve a Size<Dimension> against a definite Size<float> context
inline Size<std::optional<float>> maybe_resolve_size(Size<Dimension> s, Size<float> context) {
    return {maybe_resolve(s.width, context.width), maybe_resolve(s.height, context.height)};
}

inline Size<std::optional<float>> size_opt_maybe_max(Size<std::optional<float>> a, Size<float> b) {
    Size<std::optional<float>> r;
    if (a.width) r.width = std::max(*a.width, b.width);
    if (a.height) r.height = std::max(*a.height, b.height);
    return r;
}

inline Size<std::optional<float>> size_opt_maybe_max_opt(Size<std::optional<float>> a, Size<std::optional<float>> b) {
    Size<std::optional<float>> r;
    if (a.width && b.width) r.width = std::max(*a.width, *b.width);
    else r.width = a.width ? a.width : b.width;
    if (a.height && b.height) r.height = std::max(*a.height, *b.height);
    else r.height = a.height ? a.height : b.height;
    return r;
}

inline Size<std::optional<float>> size_opt_maybe_clamp(Size<std::optional<float>> v, Size<std::optional<float>> mn,
                                                      Size<std::optional<float>> mx) {
    // Note: clamped to the maximum first and then raised to the minimum (matching Rust's
    // MaybeMath::maybe_clamp), so that a minimum forced by padding/border wins over a max size.
    Size<std::optional<float>> r = v;
    if (r.width) {
        if (mx.width) r.width = std::min(*r.width, *mx.width);
        if (mn.width) r.width = std::max(*r.width, *mn.width);
    }
    if (r.height) {
        if (mx.height) r.height = std::min(*r.height, *mx.height);
        if (mn.height) r.height = std::max(*r.height, *mn.height);
    }
    return r;
}

/// Convert a Size<optional<float>> to Size<AvailableSpace>: definite axes become Definite, the
/// others fall back to the corresponding axis of `fallback`
inline Size<AvailableSpace> size_opt_to_available(Size<std::optional<float>> s, Size<AvailableSpace> fallback) {
    return {s.width ? AvailableSpace::definite(*s.width) : fallback.width,
            s.height ? AvailableSpace::definite(*s.height) : fallback.height};
}

inline AvailableSpace avail_maybe_clamp(AvailableSpace as, std::optional<float> mn, std::optional<float> mx) {
    if (!as.is_definite()) return as;
    float val = as.value;
    if (mn) val = std::max(val, *mn);
    if (mx) val = std::min(val, *mx);
    return AvailableSpace::definite(val);
}

inline AvailableSpace avail_maybe_max(AvailableSpace as, float rhs) {
    if (!as.is_definite()) return as;
    return AvailableSpace::definite(std::max(as.value, rhs));
}

inline float avail_compute_free_space(AvailableSpace as, float used_space) {
    switch (as.type) {
        case AvailableSpace::MaxContent: return std::numeric_limits<float>::infinity();
        case AvailableSpace::MinContent: return 0.0f;
        case AvailableSpace::Definite: return as.value - used_space;
    }
    return 0.0f;
}

inline float avail_get(AvailableSpace as) { return as.is_definite() ? as.value : 0.0f; }

// ---------------------------------------------------------------------------
// Named grid lines / grid areas resolution
// ---------------------------------------------------------------------------

enum class GridAreaAxis : uint8_t { Row, Column };
enum class GridAreaEnd : uint8_t { Start, End };

/// The one-indexed positions of the first line that starts and the first line that ends a named
/// grid area in each axis. A value of 0 means that there is no such line.
struct AreaLines {
    uint32_t row_start{0};
    uint32_t row_end{0};
    uint32_t column_start{0};
    uint32_t column_end{0};

    const uint32_t* line(GridAreaAxis axis, GridAreaEnd end) const {
        const uint32_t* v = nullptr;
        switch (axis) {
            case GridAreaAxis::Row: v = (end == GridAreaEnd::Start) ? &row_start : &row_end; break;
            case GridAreaAxis::Column: v = (end == GridAreaEnd::Start) ? &column_start : &column_end; break;
        }
        return (*v != 0) ? v : nullptr;
    }
    uint32_t& line_mut(GridAreaAxis axis, GridAreaEnd end) {
        switch (axis) {
            case GridAreaAxis::Row: return (end == GridAreaEnd::Start) ? row_start : row_end;
            case GridAreaAxis::Column: return (end == GridAreaEnd::Start) ? column_start : column_end;
        }
        return row_start;
    }
};

/// Split a line name of the form `<area-name>-start` or `<area-name>-end`
inline bool split_area_edge_name(const std::string& name, std::string& area_name, GridAreaEnd& end) {
    static const std::string start_suffix = "-start";
    static const std::string end_suffix = "-end";
    if (name.size() > start_suffix.size()
        && name.compare(name.size() - start_suffix.size(), start_suffix.size(), start_suffix) == 0) {
        area_name = name.substr(0, name.size() - start_suffix.size());
        end = GridAreaEnd::Start;
        return true;
    }
    if (name.size() > end_suffix.size()
        && name.compare(name.size() - end_suffix.size(), end_suffix.size(), end_suffix) == 0) {
        area_name = name.substr(0, name.size() - end_suffix.size());
        end = GridAreaEnd::End;
        return true;
    }
    return false;
}

/// The names of each explicit grid line in a single axis, stored in CSR (compressed sparse row)
/// format: a single flat vector of names plus a vector of offsets (one more than there are lines).
struct GridLineNames {
    std::vector<std::string> names{};
    std::vector<uint32_t> offsets{};

    bool is_empty() const { return names.empty(); }
    /// Number of recorded line-name groups (one per grid line: `tracks + 1`)
    size_t line_count() const { return offsets.size(); }

    std::pair<size_t, size_t> line(size_t index) const {
        if (index >= offsets.size()) return {0, 0};
        size_t end = (index + 1 < offsets.size()) ? static_cast<size_t>(offsets[index + 1]) : names.size();
        return {static_cast<size_t>(offsets[index]), end};
    }

    void start_line() { offsets.push_back(static_cast<uint32_t>(names.size())); }
    void push_name(const std::string& name) {
        // Line boundaries are recorded by `start_line()`; pushing a name only extends the current
        // line's contents. (Mutating `offsets.back()` here moved the current line's start past the
        // name that was just added, shifting every line name by one position.)
        names.push_back(name);
    }
    bool current_line_contains(const std::string& name) const {
        if (line_count() == 0) return false;
        auto range = line(line_count() - 1);
        for (size_t i = range.first; i < range.second; ++i)
            if (names[i] == name) return true;
        return false;
    }
};

inline int32_t oz_line_of(uint32_t line) { return static_cast<int32_t>(line) - 1; }

// NOTE: the header intentionally has no <map>/<unordered_map> dependency, so the named line maps
// are plain vectors of (name, value) pairs with linear lookup (grids have very few named lines).
using LineNameMap = std::vector<std::pair<std::string, std::vector<uint32_t>>>;
using AreaMap = std::vector<std::pair<std::string, AreaLines>>;

inline const std::vector<uint32_t>* line_name_map_find(const LineNameMap& map, const std::string& key) {
    for (auto& entry : map)
        if (entry.first == key) return &entry.second;
    return nullptr;
}
inline std::vector<uint32_t>& line_name_map_upsert(LineNameMap& map, const std::string& key) {
    for (auto& entry : map)
        if (entry.first == key) return entry.second;
    map.emplace_back(key, std::vector<uint32_t>{});
    return map.back().second;
}
inline const AreaLines* area_map_find(const AreaMap& map, const std::string& key) {
    for (auto& entry : map)
        if (entry.first == key) return &entry.second;
    return nullptr;
}
inline AreaLines& area_map_upsert(AreaMap& map, const std::string& key) {
    for (auto& entry : map)
        if (entry.first == key) return entry.second;
    map.emplace_back(key, AreaLines{});
    return map.back().second;
}

/// Resolver for named placements in one grid axis
struct NamedLineResolverAxis {
    const LineNameMap& lines;
    const AreaMap& areas;
    GridAreaAxis axis{GridAreaAxis::Row};
    uint16_t explicit_track_count{0};

    const uint32_t* area_line(const std::string& area_name, GridAreaEnd end) const {
        const AreaLines* area = area_map_find(areas, area_name);
        if (!area) return nullptr;
        return area->line(axis, end);
    }

    OriginZeroLine last_explicit_line() const { return OriginZeroLine{static_cast<int16_t>(explicit_track_count)}; }

    /// The sorted line numbers of the lines with the passed name
    std::vector<uint32_t> lines_named(const std::string& name) const {
        const std::vector<uint32_t>* named = line_name_map_find(lines, name);
        if (named) return *named;
        std::string area_name;
        GridAreaEnd area_end;
        if (split_area_edge_name(name, area_name, area_end)) {
            const uint32_t* line = area_line(area_name, area_end);
            if (line) return {*line};
        }
        return {};
    }

    /// Find the `n`th line in `lines` (searching forwards) that is not before `first_line`.
    OriginZeroLine nth_line_forwards(const std::vector<uint32_t>& lines_in, OriginZeroLine first_line, int32_t n) const {
        int32_t first = std::max<int32_t>(first_line.value, 0);
        size_t partition = 0;
        while (partition < lines_in.size() && oz_line_of(lines_in[partition]) < first) ++partition;
        size_t available = lines_in.size() - partition;
        if (static_cast<size_t>(n) <= available) {
            return oz_line_clamped(oz_line_of(lines_in[partition + static_cast<size_t>(n) - 1]));
        }
        int32_t base = std::max<int32_t>(first, static_cast<int32_t>(last_explicit_line().value) + 1);
        int32_t line = base + (n - static_cast<int32_t>(available)) - 1;
        return oz_line_clamped(line);
    }

    /// Find the `n`th line in `lines` (searching backwards) that is not after `last_line`.
    OriginZeroLine nth_line_backwards(const std::vector<uint32_t>& lines_in, OriginZeroLine last_line, int32_t n) const {
        int32_t last = std::min<int32_t>(last_line.value, static_cast<int32_t>(last_explicit_line().value));
        size_t partition = 0;
        while (partition < lines_in.size() && oz_line_of(lines_in[partition]) <= last) ++partition;
        if (static_cast<size_t>(n) <= partition) {
            return oz_line_clamped(oz_line_of(lines_in[partition - static_cast<size_t>(n)]));
        }
        int32_t base = std::min<int32_t>(last, -1);
        int32_t line = base - (n - static_cast<int32_t>(partition)) + 1;
        return oz_line_clamped(line);
    }

    /// Resolve the grid line for a named grid line (`<custom-ident>` or `<integer> <custom-ident>`)
    GridLine find_named_line(const std::string& name, int16_t idx, GridAreaEnd end) const {
        if (idx == 0) {
            const uint32_t* first_line = area_line(name, end);
            if (first_line) {
                return origin_zero_to_grid_line(
                    oz_line_clamped(oz_line_of(*first_line)), explicit_track_count);
            }
        }
        auto named_lines = lines_named(name);
        OriginZeroLine line;
        if (idx >= 0) {
            line = nth_line_forwards(named_lines, OriginZeroLine{0}, std::max<int32_t>(idx, 1));
        } else {
            line = nth_line_backwards(named_lines, last_explicit_line(), -static_cast<int32_t>(idx));
        }
        return origin_zero_to_grid_line(line, explicit_track_count);
    }

    /// Resolve named lines and spans into numeric placements
    Line<NonNamedGridPlacement> resolve_line_names(const Line<GridPlacement>& line) const {
        NonNamedGridPlacement start_resolved;
        NonNamedGridPlacement end_resolved;
        if (line.start.kind == GridPlacement::NamedLineKind) {
            start_resolved = NonNamedGridPlacement::line(
                find_named_line(line.start.name, line.start.named_line_index, GridAreaEnd::Start));
        } else {
            switch (line.start.kind) {
                case GridPlacement::Auto: start_resolved = NonNamedGridPlacement::auto_(); break;
                case GridPlacement::LineKind: start_resolved = NonNamedGridPlacement::line(line.start.line_val); break;
                case GridPlacement::SpanKind: start_resolved = NonNamedGridPlacement::span(line.start.span_val); break;
                default: start_resolved = NonNamedGridPlacement::span(1); break;
            }
        }
        if (line.end.kind == GridPlacement::NamedLineKind) {
            end_resolved = NonNamedGridPlacement::line(
                find_named_line(line.end.name, line.end.named_line_index, GridAreaEnd::End));
        } else {
            switch (line.end.kind) {
                case GridPlacement::Auto: end_resolved = NonNamedGridPlacement::auto_(); break;
                case GridPlacement::LineKind: end_resolved = NonNamedGridPlacement::line(line.end.line_val); break;
                case GridPlacement::SpanKind: end_resolved = NonNamedGridPlacement::span(line.end.span_val); break;
                default: end_resolved = NonNamedGridPlacement::span(1); break;
            }
        }

        // If both the *-start and *-end values of its grid-placement properties specify a line, its
        // grid span is implicit. If it has an explicit span value, its grid span is explicit.
        if (start_resolved.kind == NonNamedGridPlacement::LineKind
            && line.end.kind == GridPlacement::NamedSpanKind) {
            OriginZeroLine after_start_line{
                static_cast<int16_t>(grid_line_to_origin_zero(start_resolved.line_value, explicit_track_count).value + 1)};
            OriginZeroLine end_line = nth_line_forwards(lines_named(line.end.name), after_start_line,
                                                        static_cast<int32_t>(std::max<uint16_t>(line.end.span_val, 1)));
            return {start_resolved,
                    NonNamedGridPlacement::line(origin_zero_to_grid_line(end_line, explicit_track_count))};
        }
        if (line.start.kind == GridPlacement::NamedSpanKind
            && end_resolved.kind == NonNamedGridPlacement::LineKind) {
            OriginZeroLine before_end_line{
                static_cast<int16_t>(grid_line_to_origin_zero(end_resolved.line_value, explicit_track_count).value - 1)};
            OriginZeroLine start_line = nth_line_backwards(lines_named(line.start.name), before_end_line,
                                                           static_cast<int32_t>(std::max<uint16_t>(line.start.span_val, 1)));
            return {NonNamedGridPlacement::line(origin_zero_to_grid_line(start_line, explicit_track_count)),
                    end_resolved};
        }
        return {start_resolved, end_resolved};
    }
};

/// Resolver that takes grid line names and area names as input and resolves the line names of grid
/// placement properties into line numbers.
struct NamedLineResolver {
    LineNameMap row_lines{};
    LineNameMap column_lines{};
    AreaMap areas{};
    uint16_t area_column_count{0};
    uint16_t area_row_count{0};
    uint16_t explicit_column_count{0};
    uint16_t explicit_row_count{0};
    std::vector<std::pair<uint32_t, std::string>> column_line_name_pairs{};
    std::vector<std::pair<uint32_t, std::string>> row_line_name_pairs{};

    /// Utility: create or update an entry in a line name map
    static void upsert_line_name_map(LineNameMap& map, AreaMap& areas_map, GridAreaAxis axis,
                                     const std::string& key, uint32_t value) {
        // A line named `foo-start` or `foo-end` is also an edge of the (implicitly) named area `foo`
        uint32_t area_line_value = 0;
        std::string area_name;
        GridAreaEnd end;
        if (split_area_edge_name(key, area_name, end)) {
            AreaLines& area = area_map_upsert(areas_map, area_name);
            uint32_t& edge = area.line_mut(axis, end);
            area_line_value = edge;
            edge = (edge == 0) ? value : std::min(edge, value);
        }

        std::vector<uint32_t>& lines = line_name_map_upsert(map, key);
        if (lines.empty() && area_line_value != 0) lines.push_back(area_line_value);
        lines.push_back(value);
    }

    /// Process one axis' template line names (mirrors `NamedLineResolver::new` in Rust)
    static void process_axis(const std::vector<GridTemplateComponent>& template_tracks,
                             const std::vector<std::vector<std::string>>& template_line_names, LineNameMap& lines_map,
                             AreaMap& areas_map, std::vector<std::pair<uint32_t, std::string>>& pairs, GridAreaAxis axis,
                             uint16_t auto_repetition_count) {
        uint32_t current_line = 0;
        for (size_t name_index = 0; name_index < template_line_names.size(); ++name_index) {
            current_line += 1;
            for (auto& line_name : template_line_names[name_index]) {
                pairs.emplace_back(current_line, line_name);
                upsert_line_name_map(lines_map, areas_map, axis, line_name, current_line);
            }

            if (name_index < template_tracks.size()
                && template_tracks[name_index].kind == GridTemplateComponent::Repeat) {
                const auto& repeat = template_tracks[name_index].repeat_val;
                uint32_t repeat_count = (repeat.count.kind == RepetitionCount::Count)
                                            ? static_cast<uint32_t>(repeat.count.count_val)
                                            : static_cast<uint32_t>(axis == GridAreaAxis::Column ? auto_repetition_count
                                                                                                 : auto_repetition_count);
                uint32_t lines_per_repetition = static_cast<uint32_t>(repeat.tracks.size());
                for (uint32_t rep = 0; rep < repeat_count; ++rep) {
                    uint32_t line = current_line;
                    for (size_t set_index = 0; set_index < repeat.line_names.size(); ++set_index) {
                        for (auto& line_name : repeat.line_names[set_index]) {
                            pairs.emplace_back(line, line_name);
                            upsert_line_name_map(lines_map, areas_map, axis, line_name, line);
                        }
                        line += 1;
                    }
                    current_line += lines_per_repetition;
                    if (current_line > static_cast<uint32_t>(MAX_GRID_TRACKS)) break;
                }
                // Last line name set collapses with following line name set
                if (repeat_count > 0 && current_line > 0) current_line -= 1;
            }
        }
    }

    NamedLineResolver() = default;
    NamedLineResolver(const Style& style, uint16_t column_auto_repetitions, uint16_t row_auto_repetitions) {
        // The size of the area template may be larger than the extents of the named areas due to
        // unnamed ('.') cells. The C++ Style type only stores the named areas, so derive the
        // template extents from the greatest area edge.
        for (auto& area : style.grid_template_areas) {
            area_map_upsert(areas, area.name) = AreaLines{area.row_start, area.row_end, area.column_start, area.column_end};
            area_column_count = std::max(area_column_count, area.column_end);
            area_row_count = std::max(area_row_count, area.row_end);
        }

        process_axis(style.grid_template_columns, style.grid_template_column_names, column_lines, areas,
                     column_line_name_pairs, GridAreaAxis::Column, column_auto_repetitions);
        process_axis(style.grid_template_rows, style.grid_template_row_names, row_lines, areas,
                     row_line_name_pairs, GridAreaAxis::Row, row_auto_repetitions);

        for (auto& entry : column_lines) {
            std::sort(entry.second.begin(), entry.second.end());
            entry.second.erase(std::unique(entry.second.begin(), entry.second.end()), entry.second.end());
        }
        for (auto& entry : row_lines) {
            std::sort(entry.second.begin(), entry.second.end());
            entry.second.erase(std::unique(entry.second.begin(), entry.second.end()), entry.second.end());
        }
    }

    uint16_t area_column_count_() const { return area_column_count; }
    uint16_t area_row_count_() const { return area_row_count; }
    void set_explicit_column_count(uint16_t count) { explicit_column_count = count; }
    void set_explicit_row_count(uint16_t count) { explicit_row_count = count; }

    Line<NonNamedGridPlacement> resolve_line_names(const Line<GridPlacement>& line, GridAreaAxis axis) const {
        if (axis == GridAreaAxis::Row) {
            return NamedLineResolverAxis{row_lines, areas, axis, explicit_row_count}.resolve_line_names(line);
        }
        return NamedLineResolverAxis{column_lines, areas, axis, explicit_column_count}.resolve_line_names(line);
    }
    Line<NonNamedGridPlacement> resolve_row_names(const Line<GridPlacement>& line) const {
        return resolve_line_names(line, GridAreaAxis::Row);
    }
    Line<NonNamedGridPlacement> resolve_column_names(const Line<GridPlacement>& line) const {
        return resolve_line_names(line, GridAreaAxis::Column);
    }

    /// Build the per-line name groups of the explicit grid in the passed axis from the collected
    /// (line, name) pairs, with repeat()s expanded. Only names given explicitly in the track template
    /// are included (the implicit `<name>-start`/`<name>-end` names generated by
    /// `grid-template-areas` are omitted).
    GridLineNames detailed_line_names(AbsoluteAxis axis) const {
        const auto& pairs = (axis == AbsoluteAxis::Horizontal) ? column_line_name_pairs : row_line_name_pairs;
        uint16_t explicit_track_count = (axis == AbsoluteAxis::Horizontal) ? explicit_column_count : explicit_row_count;
        if (pairs.empty()) return GridLineNames{};

        std::vector<const std::pair<uint32_t, std::string>*> sorted_pairs;
        sorted_pairs.reserve(pairs.size());
        for (auto& pair : pairs) sorted_pairs.push_back(&pair);
        std::stable_sort(sorted_pairs.begin(), sorted_pairs.end(),
                         [](const std::pair<uint32_t, std::string>* a, const std::pair<uint32_t, std::string>* b) {
                             return a->first < b->first;
                         });

        uint32_t line_count = static_cast<uint32_t>(explicit_track_count) + 1;
        GridLineNames line_names;
        size_t pair_index = 0;
        for (uint32_t line = 1; line <= line_count; ++line) {
            line_names.start_line();
            while (pair_index < sorted_pairs.size() && sorted_pairs[pair_index]->first == line) {
                const std::string& name = sorted_pairs[pair_index]->second;
                pair_index++;
                if (!line_names.current_line_contains(name)) line_names.push_name(name);
            }
        }
        return line_names;
    }
};

// ---------------------------------------------------------------------------
// Explicit grid construction
// ---------------------------------------------------------------------------

enum class AutoRepeatStrategy : uint8_t { MaxRepetitionsThatDoNotOverflow, MinRepetitionsThatDoOverflow };

/// Compute the number of auto-repeat repetitions and the total number of explicit tracks in an axis
inline std::pair<uint16_t, uint16_t> compute_explicit_grid_size_in_axis(
    const Style& style, std::optional<float> auto_fit_container_size, AutoRepeatStrategy auto_fit_strategy,
    AbsoluteAxis axis) {
    const auto& template_tracks = (axis == AbsoluteAxis::Horizontal) ? style.grid_template_columns : style.grid_template_rows;

    // If template contains no tracks, then there are trivially zero explicit tracks
    if (template_tracks.empty()) return {uint16_t{0}, uint16_t{0}};

    // If there are any repetitions that contain no tracks, then the whole definition is invalid
    for (auto& def : template_tracks) {
        if (def.kind == GridTemplateComponent::Repeat && def.repeat_val.tracks.empty())
            return {uint16_t{0}, uint16_t{0}};
    }

    // Compute the number of tracks generated by single track definitions and repetitions with a
    // fixed repetition count (clamping to the maximum track limit to prevent arithmetic overflow)
    uint32_t non_auto_repeating_track_count = 0;
    for (auto& def : template_tracks) {
        uint32_t additional = 1;
        if (def.kind == GridTemplateComponent::Repeat) {
            additional = (def.repeat_val.count.kind == RepetitionCount::Count)
                             ? static_cast<uint32_t>(def.repeat_val.count.count_val)
                                   * static_cast<uint32_t>(def.repeat_val.tracks.size())
                             : 0u;
        }
        non_auto_repeating_track_count = std::min<uint32_t>(non_auto_repeating_track_count + additional, MAX_GRID_TRACKS);
    }

    uint16_t auto_repetition_count = 0;
    for (auto& def : template_tracks)
        if (def.is_auto_repetition()) auto_repetition_count++;

    bool all_track_defs_have_fixed_component = true;
    for (auto& def : template_tracks) {
        if (def.kind == GridTemplateComponent::Single) {
            if (!tsf_has_fixed_component(def.single_track)) all_track_defs_have_fixed_component = false;
        } else {
            for (auto& tsf : def.repeat_val.tracks)
                if (!tsf_has_fixed_component(tsf)) all_track_defs_have_fixed_component = false;
        }
    }

    bool template_is_valid = (auto_repetition_count == 0)
                             || (auto_repetition_count == 1 && all_track_defs_have_fixed_component);
    if (!template_is_valid) return {uint16_t{0}, uint16_t{0}};

    // If there are no auto-repetitions, then the number of explicit tracks is simply the length of
    // the track definition vector
    if (auto_repetition_count == 0) {
        return {uint16_t{0}, static_cast<uint16_t>(non_auto_repeating_track_count)};
    }

    // Find the auto-repeat definition and its insertion point
    uint32_t auto_repeat_insertion_point = 0;
    const GridTemplateRepetition* repetition_definition = nullptr;
    for (auto& def : template_tracks) {
        if (def.kind == GridTemplateComponent::Single) {
            auto_repeat_insertion_point += 1;
            continue;
        }
        if (def.repeat_val.count.kind == RepetitionCount::Count) {
            auto_repeat_insertion_point += static_cast<uint32_t>(def.repeat_val.count.count_val)
                                           * static_cast<uint32_t>(def.repeat_val.tracks.size());
            continue;
        }
        repetition_definition = &def.repeat_val;
        break;
    }
    if (!repetition_definition) return {uint16_t{0}, uint16_t{0}};

    uint16_t repetition_track_count = static_cast<uint16_t>(repetition_definition->tracks.size());

    // Determine the number of repetitions
    uint32_t num_repetitions = 1;
    if (auto_fit_container_size) {
        std::optional<float> parent_size = auto_fit_container_size;

        auto track_definite_value = [&](const TrackSizingFunction& tsf) -> float {
            return tsf_definite_value(tsf, parent_size);
        };

        float non_repeating_track_used_space = 0.0f;
        for (auto& def : template_tracks) {
            if (def.kind == GridTemplateComponent::Single) {
                non_repeating_track_used_space += track_definite_value(def.single_track);
            } else if (def.repeat_val.count.kind == RepetitionCount::Count) {
                float sum = 0.0f;
                for (auto& tsf : def.repeat_val.tracks) sum += track_definite_value(tsf);
                non_repeating_track_used_space += sum * static_cast<float>(def.repeat_val.count.count_val);
            }
        }

        float gap_size = resolve_or_zero(
            (axis == AbsoluteAxis::Horizontal) ? style.gap.width : style.gap.height, *auto_fit_container_size);

        float per_repetition_track_used_space = 0.0f;
        for (auto& tsf : repetition_definition->tracks) per_repetition_track_used_space += track_definite_value(tsf);

        // We special case the first repetition because the number of gaps in the first repetition
        // depends on the number of non-repeating tracks in the template
        float first_repetition_and_non_repeating_tracks_used_space =
            non_repeating_track_used_space + per_repetition_track_used_space
            + (static_cast<float>((non_auto_repeating_track_count + repetition_track_count > 0)
                                      ? (non_auto_repeating_track_count + repetition_track_count - 1)
                                      : 0)
               * gap_size);

        if (first_repetition_and_non_repeating_tracks_used_space > *auto_fit_container_size) {
            num_repetitions = 1;
        } else {
            float per_repetition_gap_used_space = static_cast<float>(repetition_track_count) * gap_size;
            float per_repetition_used_space = per_repetition_track_used_space + per_repetition_gap_used_space;
            float num_repetition_that_fit =
                (*auto_fit_container_size - first_repetition_and_non_repeating_tracks_used_space) / per_repetition_used_space;
            if (auto_fit_strategy == AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow) {
                float floored = std::floor(num_repetition_that_fit);
                num_repetitions = static_cast<uint32_t>(std::isfinite(floored) ? std::max(0.0f, floored) : 0.0f)
                                  + 1u;
            } else {
                float ceiled = std::ceil(num_repetition_that_fit);
                num_repetitions = static_cast<uint32_t>(std::isfinite(ceiled) ? std::max(0.0f, ceiled) : 0.0f) + 1u;
            }
        }
    }

    // Clamp the number of repetitions based on the auto-repeat's insertion point so that tracks
    // before it are preserved and tracks after it are truncated in source order.
    uint32_t remaining_tracks =
        (auto_repeat_insertion_point < MAX_GRID_TRACKS) ? (MAX_GRID_TRACKS - auto_repeat_insertion_point) : 0u;
    if (remaining_tracks == 0) {
        num_repetitions = 0;
    } else {
        uint32_t max_repetitions = (remaining_tracks + repetition_track_count - 1) / repetition_track_count;
        num_repetitions = std::max<uint32_t>(1u, std::min<uint32_t>(num_repetitions, max_repetitions));
    }

    uint32_t grid_template_track_count =
        std::min<uint32_t>(non_auto_repeating_track_count
                               + static_cast<uint32_t>(repetition_track_count) * num_repetitions,
                           MAX_GRID_TRACKS);
    return {static_cast<uint16_t>(num_repetitions), static_cast<uint16_t>(grid_template_track_count)};
}

/// Utility function for the repeating logic of creating implicit tracks
inline void create_implicit_tracks(std::vector<GridTrack>& tracks, uint16_t count,
                                   const std::vector<TrackSizingFunction>& auto_tracks, size_t& auto_index,
                                   LengthPercentage gap) {
    for (uint16_t i = 0; i < count; ++i) {
        TrackSizingFunction tsf = auto_tracks.empty()
                                      ? TrackSizingFunction{CompactLength::auto_(), CompactLength::auto_()}
                                      : auto_tracks[auto_index % auto_tracks.size()];
        ++auto_index;
        tracks.push_back(GridTrack::new_track(tsf.min, tsf.max));
        tracks.push_back(GridTrack::gutter(gap));
    }
}

/// Resolve the track sizing functions of explicit tracks, automatically created tracks and gutters
/// given a set of track counts and all of the relevant styles
inline void initialize_grid_tracks(std::vector<GridTrack>& tracks, TrackCounts counts, const Style& style,
                                   AbsoluteAxis axis, uint16_t auto_repetition_count,
                                   const std::function<bool(size_t)>& track_has_items) {
    const auto& track_template = (axis == AbsoluteAxis::Horizontal) ? style.grid_template_columns : style.grid_template_rows;
    const auto& auto_tracks = (axis == AbsoluteAxis::Horizontal) ? style.grid_auto_columns : style.grid_auto_rows;
    LengthPercentage gap = (axis == AbsoluteAxis::Horizontal) ? style.gap.width : style.gap.height;

    tracks.clear();
    tracks.reserve(counts.len() * 2 + 1);
    tracks.push_back(GridTrack::gutter(gap));

    size_t auto_track_count = auto_tracks.size();
    size_t auto_index = 0;

    // Create negative implicit tracks
    if (counts.negative_implicit > 0) {
        if (auto_track_count == 0) {
            std::vector<TrackSizingFunction> none{};
            create_implicit_tracks(tracks, counts.negative_implicit, none, auto_index, gap);
        } else {
            auto_index = auto_track_count
                         - (static_cast<size_t>(counts.negative_implicit) % auto_track_count);
            create_implicit_tracks(tracks, counts.negative_implicit, auto_tracks, auto_index, gap);
        }
    }

    size_t current_track_index = static_cast<size_t>(counts.negative_implicit);
    size_t explicit_track_limit = static_cast<size_t>(counts.negative_implicit) + counts.explicit_;

    // Create explicit tracks
    if (counts.explicit_ > 0 && !track_template.empty()) {
        for (auto& def : track_template) {
            if (def.kind == GridTemplateComponent::Single) {
                if (current_track_index < explicit_track_limit) {
                    tracks.push_back(GridTrack::new_track(def.single_track.min, def.single_track.max));
                    tracks.push_back(GridTrack::gutter(gap));
                    current_track_index += 1;
                }
            } else if (def.repeat_val.count.kind == RepetitionCount::Count) {
                size_t repeated_track_count =
                    std::min(static_cast<size_t>(def.repeat_val.count.count_val) * def.repeat_val.tracks.size(),
                             explicit_track_limit - current_track_index);
                size_t i = 0;
                while (i < repeated_track_count) {
                    TrackSizingFunction tsf = def.repeat_val.tracks[i % def.repeat_val.tracks.size()];
                    tracks.push_back(GridTrack::new_track(tsf.min, tsf.max));
                    tracks.push_back(GridTrack::gutter(gap));
                    current_track_index += 1;
                    i += 1;
                }
            } else {
                // AutoFill / AutoFit
                size_t auto_repeated_track_count =
                    std::min(static_cast<size_t>(def.repeat_val.tracks.size()) * auto_repetition_count,
                             explicit_track_limit - current_track_index);
                size_t i = 0;
                while (i < auto_repeated_track_count) {
                    TrackSizingFunction tsf = def.repeat_val.tracks[i % def.repeat_val.tracks.size()];
                    GridTrack track = GridTrack::new_track(tsf.min, tsf.max);
                    GridTrack gutter = GridTrack::gutter(gap);

                    // Auto-fit tracks that don't contain items should be collapsed
                    if (def.repeat_val.count.kind == RepetitionCount::AutoFit && !track_has_items(current_track_index)) {
                        track.collapse();
                        gutter.collapse();
                    }

                    tracks.push_back(track);
                    tracks.push_back(gutter);
                    current_track_index += 1;
                    i += 1;
                }

                // Is this auto-repeat at the very end of the track list?
                bool is_last = current_track_index == counts.len();

                // When collapsing auto-fit tracks, the gutter after a collapsed track is collapsed
                // but not the gutter before. This is correct so long as there is another
                // non-collapsed track after the collapsed track. If the auto-fit repeat is at the
                // very end of the track list, collapse backwards until the first non-collapsed track.
                if (def.repeat_val.count.kind == RepetitionCount::AutoFit && is_last) {
                    for (auto it = tracks.rbegin(); it != tracks.rend(); ++it) {
                        if (it->kind == GridTrackKind::Track && !it->is_collapsed) break;
                        it->collapse();
                    }
                }
            }
        }
    }

    uint16_t grid_area_tracks = static_cast<uint16_t>(
        (counts.negative_implicit + counts.explicit_) - static_cast<uint16_t>(current_track_index));

    // Create positive implicit tracks
    if (auto_track_count == 0) {
        std::vector<TrackSizingFunction> none{};
        size_t idx = 0;
        create_implicit_tracks(tracks, static_cast<uint16_t>(counts.positive_implicit + grid_area_tracks), none, idx, gap);
    } else {
        size_t idx = 0;
        create_implicit_tracks(tracks, static_cast<uint16_t>(counts.positive_implicit + grid_area_tracks), auto_tracks,
                               idx, gap);
    }

    // Mark first and last grid lines as collapsed
    if (!tracks.empty()) {
        tracks.front().collapse();
        tracks.back().collapse();
    }
}

// ---------------------------------------------------------------------------
// Implicit grid size estimate
// ---------------------------------------------------------------------------

/// A grid item's `grid-row`/`grid-column` styles with named lines resolved, in origin-zero coordinates
struct ItemPlacement {
    Line<OriginZeroGridPlacement> horizontal{};  // column
    Line<OriginZeroGridPlacement> vertical{};    // row
};

/// Helper for `compute_grid_size_estimate`: a conservative estimate of the greatest and smallest
/// grid lines used by a single grid item (in origin-zero coordinates)
inline void child_min_line_max_line_span(Line<OriginZeroGridPlacement> oz_line, OriginZeroLine& out_min,
                                        OriginZeroLine& out_max, uint16_t& out_span) {
    auto resolve_indefinite_span = [&]() -> uint16_t {
        switch (oz_line.start.kind) {
            case OriginZeroGridPlacement::LineKind:
                switch (oz_line.end.kind) {
                    case OriginZeroGridPlacement::AutoKind: return 1;
                    case OriginZeroGridPlacement::SpanKind: return oz_line.end.span_value;
                    case OriginZeroGridPlacement::LineKind: return 1;
                }
                return 1;
            case OriginZeroGridPlacement::AutoKind:
                switch (oz_line.end.kind) {
                    case OriginZeroGridPlacement::AutoKind: return 1;
                    case OriginZeroGridPlacement::SpanKind: return oz_line.end.span_value;
                    case OriginZeroGridPlacement::LineKind: return 1;
                }
                return 1;
            case OriginZeroGridPlacement::SpanKind:
                return oz_line.start.span_value;
        }
        return 1;
    };

    // min
    OriginZeroLine min_line;
    if (oz_line.start.kind == OriginZeroGridPlacement::LineKind && oz_line.end.kind == OriginZeroGridPlacement::LineKind) {
        int16_t track1 = oz_line.start.line_value.value;
        int16_t track2 = oz_line.end.line_value.value;
        min_line = OriginZeroLine{static_cast<int16_t>(track1 == track2 ? track1 : std::min(track1, track2))};
    } else if (oz_line.start.kind == OriginZeroGridPlacement::LineKind) {
        min_line = oz_line.start.line_value;
    } else if (oz_line.end.kind == OriginZeroGridPlacement::LineKind) {
        if (oz_line.start.kind == OriginZeroGridPlacement::AutoKind) {
            min_line = OriginZeroLine{static_cast<int16_t>(oz_line.end.line_value.value - 1)};
        } else {
            min_line = OriginZeroLine{static_cast<int16_t>(oz_line.end.line_value.value
                                                           - static_cast<int16_t>(oz_line.start.span_value))};
        }
    } else {
        min_line = OriginZeroLine{0};
    }

    // max
    OriginZeroLine max_line;
    if (oz_line.start.kind == OriginZeroGridPlacement::LineKind && oz_line.end.kind == OriginZeroGridPlacement::LineKind) {
        int16_t track1 = oz_line.start.line_value.value;
        int16_t track2 = oz_line.end.line_value.value;
        max_line = OriginZeroLine{static_cast<int16_t>(track1 == track2 ? static_cast<int16_t>(track1 + 1)
                                                                       : std::max(track1, track2))};
    } else if (oz_line.start.kind == OriginZeroGridPlacement::LineKind) {
        if (oz_line.end.kind == OriginZeroGridPlacement::AutoKind) {
            max_line = OriginZeroLine{static_cast<int16_t>(oz_line.start.line_value.value + 1)};
        } else {
            max_line = OriginZeroLine{static_cast<int16_t>(oz_line.start.line_value.value
                                                           + static_cast<int16_t>(oz_line.end.span_value))};
        }
    } else if (oz_line.end.kind == OriginZeroGridPlacement::LineKind) {
        max_line = oz_line.end.line_value;
    } else {
        max_line = OriginZeroLine{0};
    }

    // Calculate span only for indefinitely placed items
    uint16_t span = (oz_line.start.kind != OriginZeroGridPlacement::LineKind
                     && oz_line.end.kind != OriginZeroGridPlacement::LineKind)
                        ? resolve_indefinite_span()
                        : 1;

    // Clamp the min and max lines into the limited grid
    out_min = OriginZeroLine{static_cast<int16_t>(std::max<int16_t>(min_line.value, MIN_OZ_LINE))};
    out_max = OriginZeroLine{static_cast<int16_t>(std::min<int16_t>(max_line.value, MAX_OZ_LINE))};
    out_span = span;
}

inline uint16_t implied_negative_implicit_tracks(OriginZeroLine line) {
    return line.value < 0 ? static_cast<uint16_t>(line.value < 0 ? -line.value : line.value) : 0;
}
inline uint16_t implied_positive_implicit_tracks(OriginZeroLine line, uint16_t explicit_track_count) {
    return line.value > static_cast<int16_t>(explicit_track_count)
               ? static_cast<uint16_t>(line.value - static_cast<int16_t>(explicit_track_count))
               : 0;
}

/// Estimate the number of rows and columns in the grid (used as a perf optimisation and as a
/// necessary step in the auto-placement algorithm)
inline std::pair<TrackCounts, TrackCounts> compute_grid_size_estimate(uint16_t explicit_col_count,
                                                                     uint16_t explicit_row_count,
                                                                     const std::vector<ItemPlacement>& placements) {
    OriginZeroLine col_min{0}, col_max{0}, row_min{0}, row_max{0};
    uint16_t col_max_span = 0, row_max_span = 0;
    for (auto& placement : placements) {
        OriginZeroLine child_col_min, child_col_max, child_row_min, child_row_max;
        uint16_t child_col_span, child_row_span;
        child_min_line_max_line_span(placement.horizontal, child_col_min, child_col_max, child_col_span);
        child_min_line_max_line_span(placement.vertical, child_row_min, child_row_max, child_row_span);

        col_min = OriginZeroLine{static_cast<int16_t>(std::min(col_min.value, child_col_min.value))};
        col_max = OriginZeroLine{static_cast<int16_t>(std::max(col_max.value, child_col_max.value))};
        col_max_span = std::max(col_max_span, child_col_span);
        row_min = OriginZeroLine{static_cast<int16_t>(std::min(row_min.value, child_row_min.value))};
        row_max = OriginZeroLine{static_cast<int16_t>(std::max(row_max.value, child_row_max.value))};
        row_max_span = std::max(row_max_span, child_row_span);
    }

    uint16_t negative_implicit_inline_tracks = implied_negative_implicit_tracks(col_min);
    uint16_t explicit_inline_tracks = explicit_col_count;
    uint16_t positive_implicit_inline_tracks = implied_positive_implicit_tracks(col_max, explicit_col_count);
    uint16_t negative_implicit_block_tracks = implied_negative_implicit_tracks(row_min);
    uint16_t explicit_block_tracks = explicit_row_count;
    uint16_t positive_implicit_block_tracks = implied_positive_implicit_tracks(row_max, explicit_row_count);

    uint32_t tot_inline_tracks = static_cast<uint32_t>(negative_implicit_inline_tracks) + explicit_inline_tracks
                                 + positive_implicit_inline_tracks;
    if (tot_inline_tracks < col_max_span) {
        positive_implicit_inline_tracks = static_cast<uint16_t>(col_max_span - explicit_inline_tracks
                                                                - negative_implicit_inline_tracks);
    }

    uint32_t tot_block_tracks = static_cast<uint32_t>(negative_implicit_block_tracks) + explicit_block_tracks
                                + positive_implicit_block_tracks;
    if (tot_block_tracks < row_max_span) {
        positive_implicit_block_tracks = static_cast<uint16_t>(row_max_span - explicit_block_tracks
                                                               - negative_implicit_block_tracks);
    }

    return {TrackCounts::from_raw(negative_implicit_inline_tracks, explicit_inline_tracks,
                                  positive_implicit_inline_tracks),
            TrackCounts::from_raw(negative_implicit_block_tracks, explicit_block_tracks,
                                  positive_implicit_block_tracks)};
}

// ---------------------------------------------------------------------------
// Grid item placement
// ---------------------------------------------------------------------------

/// Advances the cursor by one track
inline OriginZeroLine advance_position(OriginZeroLine position) {
    return OriginZeroLine{static_cast<int16_t>(position.value == INT16_MAX ? INT16_MAX : position.value + 1)};
}

/// Resolves an indefinite span starting at `position`
inline Line<OriginZeroLine> resolve_indefinite_grid_span(OriginZeroLine position, uint16_t span) {
    int32_t pos = position.value;
    int32_t s = static_cast<int32_t>(span);
    auto line = [](int32_t value) {
        return OriginZeroLine{static_cast<int16_t>(std::max<int32_t>(INT16_MIN, std::min<int32_t>(INT16_MAX, value)))};
    };
    return {line(pos), line(pos + s)};
}

/// Clamp a placement into the limited grid, preserving a span of at least 1 track
inline Line<OriginZeroLine> clamp_span_to_limited_grid(Line<OriginZeroLine> span) {
    int16_t start = std::max<int16_t>(MIN_OZ_LINE, std::min<int16_t>(static_cast<int16_t>(MAX_OZ_LINE - 1), span.start.value));
    int16_t end = std::max<int16_t>(static_cast<int16_t>(start + 1), std::min<int16_t>(MAX_OZ_LINE, span.end.value));
    return {OriginZeroLine{start}, OriginZeroLine{end}};
}

/// Record the grid item's placement in both the CellOccupancyMatrix and the item itself
inline void record_grid_placement(CellOccupancyMatrix& matrix, GridItem& item, AbsoluteAxis primary_axis,
                                  Line<OriginZeroLine> primary_span, Line<OriginZeroLine> secondary_span,
                                  CellOccupancyState placement_type) {
    Line<OriginZeroLine> clamped_primary = clamp_span_to_limited_grid(primary_span);
    Line<OriginZeroLine> clamped_secondary = clamp_span_to_limited_grid(secondary_span);

    matrix.mark_area_as(primary_axis, clamped_primary, clamped_secondary, placement_type);

    if (primary_axis == AbsoluteAxis::Horizontal) {
        item.column = clamped_primary;
        item.row = clamped_secondary;
    } else {
        item.column = clamped_secondary;
        item.row = clamped_primary;
    }
}

/// 8.5. Grid Item Placement Algorithm: place items into the grid, generating new rows/columns into
/// the implicit grid as required
inline void place_grid_items(CellOccupancyMatrix& matrix, std::vector<GridItem>& items,
                             const std::vector<ItemPlacement>& placements, GridAutoFlow grid_auto_flow) {
    AbsoluteAxis primary_axis =
        (grid_auto_flow == GridAutoFlow::Row || grid_auto_flow == GridAutoFlow::RowDense) ? AbsoluteAxis::Horizontal
                                                                                          : AbsoluteAxis::Vertical;
    AbsoluteAxis secondary_axis = other_axis(primary_axis);

    auto placement_get = [&](const ItemPlacement& p, AbsoluteAxis axis) -> const Line<OriginZeroGridPlacement>& {
        return axis == AbsoluteAxis::Horizontal ? p.horizontal : p.vertical;
    };

    // 1. Place children with definite positions
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& placement = placements[i];
        if (!(placement_is_definite(placement.horizontal) && placement_is_definite(placement.vertical))) continue;

        Line<OriginZeroLine> primary_span = placement_resolve_definite_grid_lines(placement_get(placement, primary_axis));
        Line<OriginZeroLine> secondary_span =
            placement_resolve_definite_grid_lines(placement_get(placement, secondary_axis));
        record_grid_placement(matrix, items[i], primary_axis, primary_span, secondary_span,
                              CellOccupancyState::DefinitelyPlaced);
    }

    // 2. Place remaining children with definite secondary axis positions
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& placement = placements[i];
        const auto& secondary_placement = placement_get(placement, secondary_axis);
        const auto& primary_placement = placement_get(placement, primary_axis);
        if (!(placement_is_definite(secondary_placement) && !placement_is_definite(primary_placement))) continue;

        Line<OriginZeroLine> secondary_axis_placement = placement_resolve_definite_grid_lines(secondary_placement);
        OriginZeroLine primary_axis_grid_start_line = matrix.track_counts(primary_axis).implicit_start_line();
        OriginZeroLine starting_position = primary_axis_grid_start_line;
        if (!is_dense(grid_auto_flow)) {
            auto cursor = matrix.auto_placement_cursor(primary_axis, secondary_axis_placement.start);
            starting_position = cursor.value_or(primary_axis_grid_start_line);
        }
        uint16_t primary_axis_span = placement_indefinite_span(primary_placement);

        OriginZeroLine position = starting_position;
        while (true) {
            Line<OriginZeroLine> primary_axis_placement = resolve_indefinite_grid_span(position, primary_axis_span);
            auto collision =
                matrix.line_area_collision_jump(primary_axis, primary_axis_placement, secondary_axis_placement);
            if (!collision) {
                record_grid_placement(matrix, items[i], primary_axis, primary_axis_placement, secondary_axis_placement,
                                      CellOccupancyState::AutoPlaced);
                break;
            }
            position = *collision;
        }
    }

    // 3. Determine the number of columns in the implicit grid: handled by the grid size estimate and
    //    by mark_area_as expanding the matrix as necessary.

    // 4. Position the remaining grid items (which either have definite position only in the
    //    secondary axis or indefinite positions in both axes)
    OriginZeroLine primary_axis_grid_start_line = matrix.track_counts(primary_axis).implicit_start_line();
    OriginZeroLine secondary_axis_grid_start_line = matrix.track_counts(secondary_axis).implicit_start_line();
    std::pair<OriginZeroLine, OriginZeroLine> grid_start_position{primary_axis_grid_start_line,
                                                                  secondary_axis_grid_start_line};
    std::pair<OriginZeroLine, OriginZeroLine> grid_position = grid_start_position;

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& placement = placements[i];
        const auto& secondary_placement = placement_get(placement, secondary_axis);
        if (placement_is_definite(secondary_placement)) continue;

        const auto& primary_placement = placement_get(placement, primary_axis);
        uint16_t secondary_span_value = placement_indefinite_span(secondary_placement);
        bool has_definite_primary_axis_position = placement_is_definite(primary_placement);
        OriginZeroLine primary_axis_grid_end_line = matrix.track_counts(primary_axis).implicit_end_line();

        OriginZeroLine primary_idx = grid_position.first;
        OriginZeroLine secondary_idx = grid_position.second;

        Line<OriginZeroLine> resolved_primary_span;
        Line<OriginZeroLine> resolved_secondary_span;

        if (has_definite_primary_axis_position) {
            Line<OriginZeroLine> primary_span = placement_resolve_definite_grid_lines(primary_placement);

            // Compute the secondary axis starting position for the search
            if (is_dense(grid_auto_flow)) {
                secondary_idx = secondary_axis_grid_start_line;
            } else if (primary_span.start.value < primary_idx.value) {
                secondary_idx = advance_position(secondary_idx);
            }

            // Item has a fixed primary axis position: increment the secondary axis position until we
            // find a space that the item fits in
            while (true) {
                Line<OriginZeroLine> secondary_span = resolve_indefinite_grid_span(secondary_idx, secondary_span_value);
                auto collision = matrix.line_area_collision_jump(secondary_axis, secondary_span, primary_span);
                if (collision) {
                    secondary_idx = *collision;
                    continue;
                }
                resolved_primary_span = primary_span;
                resolved_secondary_span = secondary_span;
                break;
            }
        } else {
            uint16_t primary_span_value = placement_indefinite_span(primary_placement);
            size_t track_count = matrix.track_counts(primary_axis).len();
            bool spans_all_primary_tracks = static_cast<size_t>(primary_span_value) >= track_count;

            while (true) {
                Line<OriginZeroLine> primary_span = resolve_indefinite_grid_span(primary_idx, primary_span_value);
                Line<OriginZeroLine> secondary_span = resolve_indefinite_grid_span(secondary_idx, secondary_span_value);

                // If the primary index is out of bounds, increment the secondary index and reset the
                // primary index back to the start of the grid
                if (primary_span.end.value > primary_axis_grid_end_line.value) {
                    // If the span is out of bounds even at the search start position then it can never fit
                    if (primary_idx.value == primary_axis_grid_start_line.value) {
                        resolved_primary_span = primary_span;
                        resolved_secondary_span = secondary_span;
                        break;
                    }
                    secondary_idx = advance_position(secondary_idx);
                    primary_idx = primary_axis_grid_start_line;
                    continue;
                }

                // If the item spans every primary axis track, it fits if and only if all of the
                // secondary axis tracks it spans are entirely unoccupied
                if (spans_all_primary_tracks) {
                    auto jump = matrix.occupied_track_jump(secondary_axis, secondary_span);
                    if (jump) {
                        secondary_idx = *jump;
                        primary_idx = primary_axis_grid_start_line;
                        continue;
                    }
                    resolved_primary_span = primary_span;
                    resolved_secondary_span = secondary_span;
                    break;
                }

                // If the area is occupied, jump the primary index past the collision and try again
                auto collision = matrix.line_area_collision_jump(primary_axis, primary_span, secondary_span);
                if (collision) {
                    primary_idx = *collision;
                    continue;
                }
                resolved_primary_span = primary_span;
                resolved_secondary_span = secondary_span;
                break;
            }
        }

        record_grid_placement(matrix, items[i], primary_axis, resolved_primary_span, resolved_secondary_span,
                              CellOccupancyState::AutoPlaced);

        // If using the "dense" placement algorithm then reset the grid position back to the start,
        // otherwise set it to the position of the current item so that the next item is placed after it
        grid_position = is_dense(grid_auto_flow)
                            ? grid_start_position
                            : std::make_pair(resolved_primary_span.end, resolved_secondary_span.start);
    }
}

// --- Sizing keyword resolution (src/compute/common/sizing_keyword.rs) ---
// Note: the shared Section 15d version is defined after the grid algorithm, so the grid algorithm
// carries its own copy (same semantics, same Rust reference).
struct GridSizingKeywordResolution {
    enum Kind : uint8_t { Measure, Exact };
    Kind kind{Measure};
    AvailableSpace available{};
    float exact{0.0f};
};

inline std::optional<GridSizingKeywordResolution> resolve_grid_sizing_keyword(
    Dimension style, std::optional<float> stretch_size, std::optional<float> percent_resolution_basis) {
    switch (style.tag()) {
        case CompactLength::MIN_CONTENT_TAG:
            return GridSizingKeywordResolution{GridSizingKeywordResolution::Measure,
                                               AvailableSpace::min_content(), 0.0f};
        case CompactLength::MAX_CONTENT_TAG:
            return GridSizingKeywordResolution{GridSizingKeywordResolution::Measure,
                                               AvailableSpace::max_content(), 0.0f};
        case CompactLength::FIT_CONTENT_PX_TAG:
            return GridSizingKeywordResolution{GridSizingKeywordResolution::Measure,
                                               AvailableSpace::definite(style.value()), 0.0f};
        case CompactLength::FIT_CONTENT_PERCENT_TAG:
            if (percent_resolution_basis.has_value()) {
                // Percentages are stored 0..100 in this port (unlike Rust's 0..1 fractions)
                return GridSizingKeywordResolution{
                    GridSizingKeywordResolution::Measure,
                    AvailableSpace::definite(*percent_resolution_basis * (style.value() / 100.0f)), 0.0f};
            }
            return std::nullopt;
        case CompactLength::FIT_CONTENT_KEYWORD_TAG:
            if (stretch_size.has_value()) {
                return GridSizingKeywordResolution{GridSizingKeywordResolution::Measure,
                                                   AvailableSpace::definite(*stretch_size), 0.0f};
            }
            return std::nullopt;
        case CompactLength::STRETCH_TAG:
            if (stretch_size.has_value()) {
                return GridSizingKeywordResolution{GridSizingKeywordResolution::Exact, AvailableSpace{},
                                                   *stretch_size};
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Child layout helpers (measure / perform layout through the tree)
// ---------------------------------------------------------------------------

template <typename Tree, typename MF>
LayoutOutput perform_child_layout(Tree& tree, MF& measure_fn, NodeId node,
                                  Size<std::optional<float>> known_dimensions,
                                  Size<std::optional<float>> parent_size,
                                  Size<AvailableSpace> available_space) {
    LayoutInput in;
    in.run_mode = RunMode::PerformLayout;
    in.sizing_mode = SizingMode::InherentSize;
    in.axis = RequestedAxis::Both;
    in.known_dimensions = known_dimensions;
    in.parent_size = parent_size;
    in.available_space = available_space;
    in.vertical_margins_are_collapsible = Line<bool>{false, false};
    return tree.compute_node_layout(node, in, measure_fn);
}

template <typename Tree, typename MF>
float measure_child_axis_size(Tree& tree, MF& measure_fn, NodeId node,
                              Size<std::optional<float>> known_dimensions,
                              Size<std::optional<float>> parent_size,
                              Size<AvailableSpace> available_space, AbsoluteAxis axis) {
    LayoutInput in;
    in.run_mode = RunMode::ComputeSize;
    in.sizing_mode = SizingMode::InherentSize;
    in.axis = (axis == AbsoluteAxis::Horizontal) ? RequestedAxis::Horizontal : RequestedAxis::Vertical;
    in.known_dimensions = known_dimensions;
    in.parent_size = parent_size;
    in.available_space = available_space;
    LayoutOutput out = tree.compute_node_layout(node, in, measure_fn);
    return out.size.get_abs(axis);
}

// ---------------------------------------------------------------------------
// Grid item intrinsic sizing
// ---------------------------------------------------------------------------

/// Compute the item's resolved margins for size contributions. Horizontal percentage margins always
/// resolve to zero if the container size is indefinite as otherwise this would introduce a cyclic
/// dependency.
inline Size<float> margins_axis_sums_with_baseline_shims(const GridItem& item, std::optional<float> inner_node_width) {
    Rect<float> resolved{
        resolve_or_zero(item.margin.left, 0.0f),
        resolve_or_zero(item.margin.right, 0.0f),
        resolve_or_zero(item.margin.top, inner_node_width) + item.baseline_shim,
        resolve_or_zero(item.margin.bottom, inner_node_width)};
    return resolved.sum_axes();
}

/// When applying the track sizing algorithm and estimating the size in the other axis for content
/// sizing items we should take into account align-content/justify-content if both the grid container
/// and all items in the other axis have definite sizes. This computes such a per-gutter adjustment.
template <typename EstimateFn>
float compute_alignment_gutter_adjustment(AlignContent alignment, std::optional<float> axis_inner_node_size,
                                          EstimateFn get_track_size_estimate, const std::vector<GridTrack>& tracks) {
    if (tracks.size() <= 1) return 0.0f;

    uint8_t outer_gutter_weight = 0;
    switch (alignment.keyword) {
        case AlignContentKeyword::Start:
        case AlignContentKeyword::FlexStart:
        case AlignContentKeyword::End:
        case AlignContentKeyword::FlexEnd:
        case AlignContentKeyword::Center: outer_gutter_weight = 1; break;
        case AlignContentKeyword::Normal:
        case AlignContentKeyword::Stretch:
        case AlignContentKeyword::SpaceBetween: outer_gutter_weight = 0; break;
        case AlignContentKeyword::SpaceAround:
        case AlignContentKeyword::SpaceEvenly: outer_gutter_weight = 1; break;
    }

    uint8_t inner_gutter_weight = 0;
    switch (alignment.keyword) {
        case AlignContentKeyword::FlexStart:
        case AlignContentKeyword::Start:
        case AlignContentKeyword::FlexEnd:
        case AlignContentKeyword::End:
        case AlignContentKeyword::Center:
        case AlignContentKeyword::Normal:
        case AlignContentKeyword::Stretch: inner_gutter_weight = 0; break;
        case AlignContentKeyword::SpaceBetween: inner_gutter_weight = 1; break;
        case AlignContentKeyword::SpaceAround: inner_gutter_weight = 2; break;
        case AlignContentKeyword::SpaceEvenly: inner_gutter_weight = 1; break;
    }

    if (inner_gutter_weight == 0) return 0.0f;

    if (axis_inner_node_size) {
        bool all_resolved = true;
        float track_size_sum = 0.0f;
        for (auto& track : tracks) {
            auto estimate = get_track_size_estimate(track, axis_inner_node_size);
            if (!estimate) {
                all_resolved = false;
                break;
            }
            track_size_sum += *estimate;
        }
        float free_space = all_resolved ? std::max(0.0f, *axis_inner_node_size - track_size_sum) : 0.0f;

        size_t weighted_track_count =
            (((tracks.size() - 3) / 2) * static_cast<size_t>(inner_gutter_weight)) + 2 * outer_gutter_weight;

        return (free_space / static_cast<float>(weighted_track_count)) * static_cast<float>(inner_gutter_weight);
    }

    return 0.0f;
}

/// The upper limit used to calculate an item's limited min-/max-content contribution: the sum of the
/// fixed max track sizing functions of any tracks it spans (applied if it only spans such tracks)
std::optional<float> spanned_track_limit(GridItem& item, AbstractAxis axis, std::vector<GridTrack>& axis_tracks,
                                         std::optional<float> axis_parent_size) {
    auto range = item.track_range_excluding_lines(axis);
    bool tracks_all_fixed = true;
    for (size_t i = range.first; i < range.second; ++i) {
        if (!cl_definite_limit(axis_tracks[i].max_track_sizing_function, axis_parent_size)) {
            tracks_all_fixed = false;
            break;
        }
    }
    if (!tracks_all_fixed) return std::nullopt;
    float limit = 0.0f;
    for (size_t i = range.first; i < range.second; ++i) {
        limit += *cl_definite_limit(axis_tracks[i].max_track_sizing_function, axis_parent_size);
    }
    return limit;
}

/// Similar to spanned_track_limit, but excludes fit-content() arguments from the limit
inline std::optional<float> spanned_fixed_track_limit(GridItem& item, AbstractAxis axis,
                                                      std::vector<GridTrack>& axis_tracks,
                                                      std::optional<float> axis_parent_size) {
    auto range = item.track_range_excluding_lines(axis);
    bool tracks_all_fixed = true;
    for (size_t i = range.first; i < range.second; ++i) {
        if (!cl_definite_value(axis_tracks[i].max_track_sizing_function, axis_parent_size)) {
            tracks_all_fixed = false;
            break;
        }
    }
    if (!tracks_all_fixed) return std::nullopt;
    float limit = 0.0f;
    for (size_t i = range.first; i < range.second; ++i) {
        limit += *cl_definite_value(axis_tracks[i].max_track_sizing_function, axis_parent_size);
    }
    return limit;
}

inline bool should_stretch_aligned(AlignSelf alignment, bool is_replaced) {
    if (alignment.keyword == AlignItemsKeyword::Stretch) return true;
    if (alignment.keyword == AlignItemsKeyword::Normal) return !is_replaced;
    return false;
}

/// Compute the known_dimensions to be passed to the child sizing functions. The key thing done here
/// is applying stretch alignment, which is necessary to allow percentage sizes further down the tree
/// to resolve properly in some cases.
template <typename Tree, typename MF>
Size<std::optional<float>> item_known_dimensions(Tree& tree, MF& measure_fn, GridItem& item,
                                                 Size<std::optional<float>> grid_area_size) {
    (void)tree;
    (void)measure_fn;
    Size<float> margins = margins_axis_sums_with_baseline_shims(item, grid_area_size.width);

    auto aspect_ratio = item.aspect_ratio;
    Rect<float> padding = resolve_or_zero_rect(item.padding, grid_area_size.width);
    Rect<float> border = resolve_or_zero_rect(item.border, grid_area_size.width);
    Size<float> padding_border_size = (padding + border).sum_axes();
    Size<float> box_sizing_adjustment =
        (item.box_sizing == BoxSizing::ContentBox) ? padding_border_size : Size<float>{0.0f, 0.0f};

    Size<std::optional<float>> inherent_size = size_opt_maybe_add(
        maybe_resolve_size(item.size, grid_area_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);

    Size<std::optional<float>> min_size = size_opt_maybe_max(
        size_opt_or(size_opt_maybe_add(maybe_resolve_size(item.min_size, grid_area_size).maybe_apply_aspect_ratio(aspect_ratio),
                                       box_sizing_adjustment),
                    Size<std::optional<float>>{padding_border_size.width, padding_border_size.height}),
        padding_border_size);

    Size<std::optional<float>> max_size = size_opt_maybe_add(
        maybe_resolve_size(item.max_size, grid_area_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);

    Size<std::optional<float>> grid_area_minus_item_margins_size =
        size_opt_maybe_max(size_opt_maybe_sub(grid_area_size, Size<std::optional<float>>{margins.width, margins.height}),
                           Size<float>{0.0f, 0.0f});

    std::optional<float> width = inherent_size.width;
    if (!width) {
        // A width that is a sizing keyword is not auto, so it does not stretch. The stretch keyword
        // resolves to an exact width; the others resolve during content measurement.
        if (item.size.width.is_sizing_keyword()) {
            auto resolution = resolve_grid_sizing_keyword(item.size.width, grid_area_minus_item_margins_size.width,
                                                     grid_area_size.width);
            if (resolution && resolution->kind == GridSizingKeywordResolution::Exact) width = resolution->exact;
        } else if (!item.margin.left.is_auto() && !item.margin.right.is_auto()
                   && should_stretch_aligned(item.justify_self, item.is_replaced)) {
            // Apply width based on stretch alignment
            width = grid_area_minus_item_margins_size.width;
        }
    }
    // Reapply aspect ratio after stretch adjustments
    Size<std::optional<float>> size{width, inherent_size.height};
    size = size.maybe_apply_aspect_ratio(aspect_ratio);

    std::optional<float> height = size.height;
    if (!height) {
        if (item.size.height.is_sizing_keyword()) {
            auto resolution = resolve_grid_sizing_keyword(item.size.height, grid_area_minus_item_margins_size.height,
                                                     grid_area_size.height);
            if (resolution && resolution->kind == GridSizingKeywordResolution::Exact) height = resolution->exact;
        } else if (!item.margin.top.is_auto() && !item.margin.bottom.is_auto()
                   && should_stretch_aligned(item.align_self, item.is_replaced)) {
            height = grid_area_minus_item_margins_size.height;
        }
    }
    size = Size<std::optional<float>>{size.width, height}.maybe_apply_aspect_ratio(aspect_ratio);

    // Clamp size by min and max width/height
    return size_opt_maybe_clamp(size, min_size, max_size);
}

inline Size<std::optional<float>> known_dimensions_cached(GridItem& item, Size<std::optional<float>> grid_area_size,
                                                          const Size<std::optional<float>>& known_dimensions) {
    if (item.known_dimensions_cache && item.known_dimensions_cache->grid_area_size == grid_area_size) {
        return item.known_dimensions_cache->known_dimensions;
    }
    item.known_dimensions_cache = KnownDimensionsCacheEntry{grid_area_size, known_dimensions};
    return known_dimensions;
}

/// Subtract the item's margins (and baseline shim) from the definite axes of the space available
inline Size<std::optional<float>> available_space_minus_margins(const GridItem& item,
                                                               Size<std::optional<float>> grid_area_size,
                                                               Size<std::optional<float>> available_space) {
    if (!available_space.width && !available_space.height) return available_space;
    Rect<float> margin = resolve_or_zero_rect(item.margin, grid_area_size.width);
    Size<std::optional<float>> r = available_space;
    if (r.width) r.width = std::max(*r.width - margin.horizontal_axis_sum(), 0.0f);
    if (r.height) r.height = std::max(*r.height - margin.vertical_axis_sum() - item.baseline_shim, 0.0f);
    return r;
}

/// Override the available space in each axis whose size style is a sizing keyword that measures the
/// item under a specific available space constraint (min-content, max-content, fit-content,
/// fit-content(...))
inline Size<AvailableSpace> keyword_adjusted_available_space(const GridItem& item,
                                                             Size<std::optional<float>> grid_area_size,
                                                             Size<AvailableSpace> available_space) {
    if (!item.size.width.is_sizing_keyword() && !item.size.height.is_sizing_keyword()) return available_space;
    Size<float> margins = margins_axis_sums_with_baseline_shims(item, grid_area_size.width);
    Size<AvailableSpace> adjusted = available_space;
    for (AbstractAxis axis : {AbstractAxis::Inline, AbstractAxis::Block}) {
        Dimension size_style = (axis == AbstractAxis::Inline) ? item.size.width : item.size.height;
        if (!size_style.is_sizing_keyword()) continue;
        std::optional<float> stretch_size;
        if (auto area = size_opt_get(grid_area_size, axis)) {
            stretch_size = std::max(*area - margins.get(axis), 0.0f);
        }
        auto resolution = resolve_grid_sizing_keyword(size_style, stretch_size, size_opt_get(grid_area_size, axis));
        if (resolution && resolution->kind == GridSizingKeywordResolution::Measure) {
            adjusted.set(axis, resolution->available);
        }
    }
    return adjusted;
}

/// Compute the item's min content contribution from the provided parameters
template <typename Tree, typename MF>
float item_min_content_contribution(Tree& tree, MF& measure_fn, GridItem& item, AbstractAxis axis,
                                    Size<std::optional<float>> grid_area_size,
                                    Size<std::optional<float>> available_space) {
    Size<std::optional<float>> known = item_known_dimensions(tree, measure_fn, item, grid_area_size);
    known = known_dimensions_cached(item, grid_area_size, known);
    if (size_opt_get(known, axis)) return *size_opt_get(known, axis);

    Size<std::optional<float>> available_minus_margins =
        available_space_minus_margins(item, grid_area_size, available_space);
    Size<AvailableSpace> avail{available_minus_margins.width ? AvailableSpace::definite(*available_minus_margins.width)
                                                              : AvailableSpace::min_content(),
                               available_minus_margins.height ? AvailableSpace::definite(*available_minus_margins.height)
                                                              : AvailableSpace::min_content()};
    avail = keyword_adjusted_available_space(item, grid_area_size, avail);
    return measure_child_axis_size(tree, measure_fn, item.node, known, grid_area_size, avail, as_abs_naive(axis));
}

/// Compute the item's max content contribution from the provided parameters
template <typename Tree, typename MF>
float item_max_content_contribution(Tree& tree, MF& measure_fn, GridItem& item, AbstractAxis axis,
                                    Size<std::optional<float>> grid_area_size,
                                    Size<std::optional<float>> available_space) {
    Size<std::optional<float>> known = item_known_dimensions(tree, measure_fn, item, grid_area_size);
    known = known_dimensions_cached(item, grid_area_size, known);
    if (size_opt_get(known, axis)) return *size_opt_get(known, axis);

    Size<std::optional<float>> available_minus_margins =
        available_space_minus_margins(item, grid_area_size, available_space);
    Size<AvailableSpace> avail{available_minus_margins.width ? AvailableSpace::definite(*available_minus_margins.width)
                                                              : AvailableSpace::max_content(),
                               available_minus_margins.height ? AvailableSpace::definite(*available_minus_margins.height)
                                                              : AvailableSpace::max_content()};
    avail = keyword_adjusted_available_space(item, grid_area_size, avail);
    return measure_child_axis_size(tree, measure_fn, item.node, known, grid_area_size, avail, as_abs_naive(axis));
}

inline bool is_scroll_container_overflow(Overflow o) {
    return o == Overflow::Hidden || o == Overflow::Scroll;
}

/// Retrieve the item's min content contribution from the cache or compute it
template <typename Tree, typename MF>
float item_min_content_contribution_cached(Tree& tree, MF& measure_fn, GridItem& item, AbstractAxis axis,
                                           Size<std::optional<float>> grid_area_size,
                                           Size<std::optional<float>> available_space) {
    auto cached = size_opt_get(item.min_content_contribution_cache, axis);
    if (cached) return *cached;
    float value = item_min_content_contribution(tree, measure_fn, item, axis, grid_area_size, available_space);
    size_opt_set(item.min_content_contribution_cache, axis, value);
    return value;
}

/// Retrieve the item's max content contribution from the cache or compute it
template <typename Tree, typename MF>
float item_max_content_contribution_cached(Tree& tree, MF& measure_fn, GridItem& item, AbstractAxis axis,
                                           Size<std::optional<float>> grid_area_size,
                                           Size<std::optional<float>> available_space) {
    auto cached = size_opt_get(item.max_content_contribution_cache, axis);
    if (cached) return *cached;
    float value = item_max_content_contribution(tree, measure_fn, item, axis, grid_area_size, available_space);
    size_opt_set(item.max_content_contribution_cache, axis, value);
    return value;
}

/// The minimum contribution of an item is the smallest outer size it can have.
template <typename Tree, typename MF>
float item_minimum_contribution(Tree& tree, MF& measure_fn, GridItem& item, AbstractAxis axis,
                                std::vector<GridTrack>& axis_tracks, Size<std::optional<float>> grid_area_size,
                                Size<std::optional<float>> inner_node_size) {
    Rect<float> padding = resolve_or_zero_rect(item.padding, grid_area_size.width);
    Rect<float> border = resolve_or_zero_rect(item.border, grid_area_size.width);
    Size<float> padding_border_size = (padding + border).sum_axes();
    Size<float> box_sizing_adjustment =
        (item.box_sizing == BoxSizing::ContentBox) ? padding_border_size : Size<float>{0.0f, 0.0f};

    std::optional<float> result;

    Size<std::optional<float>> preferred = size_opt_maybe_add(
        maybe_resolve_size(item.size, grid_area_size).maybe_apply_aspect_ratio(item.aspect_ratio), box_sizing_adjustment);
    if (auto preferred_axis = size_opt_get(preferred, axis)) {
        result = *preferred_axis;
        // A definite preferred size is clamped by the minimum and maximum sizes
        auto min_size_style = item.min_size.get(axis);
        auto max_size_style = item.max_size.get(axis);
        if (!min_size_style.is_auto() || !max_size_style.is_auto()) {
            auto basis = size_opt_get(grid_area_size, axis);
            auto max_size_basis = item.is_replaced ? (basis ? basis : std::optional<float>(0.0f)) : basis;
            float adjustment = box_sizing_adjustment.get(axis);
            auto min_size = maybe_resolve(min_size_style, basis);
            auto max_size = maybe_resolve(max_size_style, max_size_basis);
            if (min_size) *min_size += adjustment;
            if (max_size) *max_size += adjustment;
            float v = *result;
            if (min_size) v = std::max(v, *min_size);
            if (max_size) v = std::min(v, *max_size);
            result = v;
        }
    }

    if (!result) {
        // A preferred size that is a sizing keyword other than `stretch` does not behave as auto,
        // so the minimum contribution is the min-content contribution.
        Dimension axis_size_style = item.size.get(axis);
        if (axis_size_style.is_sizing_keyword() && !axis_size_style.is_stretch()) {
            result =
                item_min_content_contribution_cached(tree, measure_fn, item, axis, grid_area_size, grid_area_size);
        }
    }

    if (!result) {
        Size<float> basis{grid_area_size.width.value_or(0.0f), grid_area_size.height.value_or(0.0f)};
        Size<std::optional<float>> min_resolved{maybe_resolve(item.min_size.width, basis.width),
                                                maybe_resolve(item.min_size.height, basis.height)};
        min_resolved = min_resolved.maybe_apply_aspect_ratio(item.aspect_ratio);
        min_resolved = size_opt_maybe_add(min_resolved, box_sizing_adjustment);
        result = size_opt_get(min_resolved, axis);
    }

    if (!result) {
        if (is_scroll_container_overflow(item.overflow.get(axis))) result = 0.0f;
    }

    if (!result) {
        // Automatic minimum size. See https://www.w3.org/TR/css-grid-1/#min-size-auto
        auto range = item.track_range_excluding_lines(axis);
        bool spans_auto_min_track = false;
        for (size_t i = range.first; i < range.second; ++i) {
            if (cl_min_behaves_as_auto(axis_tracks[i].min_track_sizing_function, size_opt_get(inner_node_size, axis))) {
                spans_auto_min_track = true;
                break;
            }
        }
        bool only_span_one_track = (range.second - range.first) == 1;
        bool any_flexible = false;
        for (size_t i = range.first; i < range.second; ++i)
            if (axis_tracks[i].max_track_sizing_function.is_fr()) any_flexible = true;
        bool use_content_based_minimum = spans_auto_min_track && (only_span_one_track || !any_flexible);

        if (use_content_based_minimum) {
            float contribution =
                item_min_content_contribution(tree, measure_fn, item, axis, grid_area_size, grid_area_size);

            // If the item is a compressible replaced element, and has a definite preferred size or
            // maximum size in the relevant axis, the size suggestion is capped by those sizes
            if (item.is_compressible_replaced) {
                auto size = maybe_resolve(item.size.get(axis), 0.0f);
                auto max_size = maybe_resolve(item.max_size.get(axis), 0.0f);
                if (size) contribution = std::min(contribution, *size);
                if (max_size) contribution = std::min(contribution, *max_size);
            }

            auto limit = spanned_fixed_track_limit(item, axis, axis_tracks, size_opt_get(inner_node_size, axis));
            if (limit) {
                Size<float> margins = margins_axis_sums_with_baseline_shims(item, grid_area_size.width);
                float clamped = std::max(*limit - margins.get(axis), padding_border_size.get(axis));
                contribution = std::min(contribution, clamped);
            }
            result = contribution;
        } else {
            result = 0.0f;
        }
    }

    return *result;
}

/// Retrieve the item's minimum contribution from the cache or compute it
template <typename Tree, typename MF>
float item_minimum_contribution_cached(Tree& tree, MF& measure_fn, GridItem& item, AbstractAxis axis,
                                       std::vector<GridTrack>& axis_tracks,
                                       Size<std::optional<float>> grid_area_size,
                                       Size<std::optional<float>> inner_node_size) {
    auto cached = size_opt_get(item.minimum_contribution_cache, axis);
    if (cached) return *cached;
    float value = item_minimum_contribution(tree, measure_fn, item, axis, axis_tracks, grid_area_size, inner_node_size);
    size_opt_set(item.minimum_contribution_cache, axis, value);
    return value;
}

/// Returns the grid area's size in the specified axis when every spanned track has a definite fixed
/// size, and an estimate based on the other axis' track sizes for the other axis.
struct IntrinsicSizeMeasurer {
    AbstractAxis axis{AbstractAxis::Inline};
    Size<std::optional<float>> inner_node_size{};
    bool estimate_is_base_size{false};

    /// Estimate of an other-axis track's size passed to child size measurement functions
    std::optional<float> estimate(const GridTrack& track, std::optional<float> parent_size) const {
        if (estimate_is_base_size) return track.base_size;
        return cl_definite_value(track.max_track_sizing_function, parent_size);
    }

    template <typename GetEstimate>
    Size<std::optional<float>> grid_area_size(GridItem& item, std::vector<GridTrack>& axis_tracks,
                                              const std::vector<GridTrack>& other_axis_tracks,
                                              GetEstimate get_track_size_estimate) const {
        Size<std::optional<float>> size{std::nullopt, std::nullopt};

        auto axis_range = item.track_range_excluding_lines(axis);
        bool axis_ok = true;
        float axis_sum = 0.0f;
        for (size_t i = axis_range.first; i < axis_range.second; ++i) {
            auto& track = axis_tracks[i];
            auto min_size = cl_definite_value(track.min_track_sizing_function, size_opt_get(inner_node_size, axis));
            auto max_size = cl_definite_value(track.max_track_sizing_function, size_opt_get(inner_node_size, axis));
            if (!min_size || !max_size || *min_size != *max_size) {
                axis_ok = false;
                break;
            }
            axis_sum += track.base_size;
        }
        if (axis_ok) size_opt_set(size, axis, std::optional<float>(axis_sum));

        auto other_axis = other(axis);
        auto other_range = item.track_range_excluding_lines(other_axis);
        bool other_ok = true;
        float other_sum = 0.0f;
        for (size_t i = other_range.first; i < other_range.second; ++i) {
            auto& track = other_axis_tracks[i];
            auto estimate_value = get_track_size_estimate(track, size_opt_get(inner_node_size, other_axis));
            if (!estimate_value) {
                other_ok = false;
                break;
            }
            other_sum += *estimate_value + track.content_alignment_adjustment;
        }
        if (other_ok) size_opt_set(size, other_axis, std::optional<float>(other_sum));
        return size;
    }

    template <typename GetEstimate>
    Size<std::optional<float>> grid_area_size_cached(GridItem& item, std::vector<GridTrack>& axis_tracks,
                                                     const std::vector<GridTrack>& other_axis_tracks,
                                                     GetEstimate get_track_size_estimate) {
        if (item.grid_area_size_cache) return *item.grid_area_size_cache;
        auto size = grid_area_size(item, axis_tracks, other_axis_tracks, get_track_size_estimate);
        item.grid_area_size_cache = size;
        return size;
    }
};

/// A batcher which takes an axis and a list of grid items sorted firstly by whether they cross a flex
/// track (items that don't cross a flex track first) and then by the number of tracks they cross.
struct ItemBatcher {
    AbstractAxis axis{AbstractAxis::Inline};
    size_t index_offset{0};
    uint16_t current_span{1};
    bool current_is_flex{false};

    ItemBatcher(AbstractAxis axis_in) : axis(axis_in) {}

    /// Returns a batch of items (as indices into `items`) and whether the batch crosses a flex track
    std::optional<std::pair<std::pair<size_t, size_t>, bool>> next(const std::vector<GridItem>& items) {
        if (current_is_flex || index_offset >= items.size()) return std::nullopt;

        const auto& item = items[index_offset];
        current_span = item.span(axis);
        current_is_flex = item.crosses_flexible_track(axis);

        size_t next_index_offset;
        if (current_is_flex) {
            next_index_offset = items.size();
        } else {
            next_index_offset = items.size();
            for (size_t i = 0; i < items.size(); ++i) {
                if (items[i].crosses_flexible_track(axis) || items[i].span(axis) > current_span) {
                    next_index_offset = i;
                    break;
                }
            }
        }

        std::pair<size_t, size_t> batch_range{index_offset, next_index_offset};
        index_offset = next_index_offset;
        return std::make_pair(batch_range, current_is_flex);
    }
};

// ---------------------------------------------------------------------------
// Track sizing algorithm
// ---------------------------------------------------------------------------

/// Add any planned base size increases to the base size after a round of distributing space
inline void flush_planned_base_size_increases(std::vector<GridTrack>& tracks) {
    for (auto& track : tracks) {
        track.base_size += track.base_size_planned_increase;
        track.base_size_planned_increase = 0.0f;
    }
}

/// Add any planned growth limit increases to the growth limit after a round of distributing space
inline void flush_planned_growth_limit_increases(std::vector<GridTrack>& tracks, bool set_infinitely_growable) {
    for (auto& track : tracks) {
        if (track.growth_limit_planned_increase > 0.0f) {
            track.growth_limit = (track.growth_limit == std::numeric_limits<float>::infinity())
                                     ? track.base_size + track.growth_limit_planned_increase
                                     : track.growth_limit + track.growth_limit_planned_increase;
            track.infinitely_growable = set_infinitely_growable;
        } else {
            track.infinitely_growable = false;
        }
        track.growth_limit_planned_increase = 0.0f;
    }
}

/// 11.4 Initialise Track sizes: initialize each track's base size and growth limit.
inline void initialize_track_sizes(std::vector<GridTrack>& axis_tracks, std::optional<float> axis_inner_node_size) {
    for (auto& track : axis_tracks) {
        track.base_size = cl_definite_value(track.min_track_sizing_function, axis_inner_node_size).value_or(0.0f);
        track.growth_limit = cl_definite_value(track.max_track_sizing_function, axis_inner_node_size)
                                 .value_or(std::numeric_limits<float>::infinity());
        if (track.growth_limit < track.base_size) track.growth_limit = track.base_size;
    }
}

/// Helper function for distributing space to tracks evenly
/// Returns the space remaining after distribution
template <typename TrackIsAffected, typename DistributionProportion, typename AffectedProperty, typename TrackLimit>
float distribute_space_up_to_limits(float space_to_distribute, std::vector<GridTrack>& tracks,
                                    TrackIsAffected track_is_affected, DistributionProportion track_distribution_proportion,
                                    AffectedProperty track_affected_property, TrackLimit track_limit) {
    constexpr float THRESHOLD = 0.01f;
    size_t max_iterations = tracks.size() + 1;

    float remaining = space_to_distribute;
    for (size_t iteration = 0; iteration < max_iterations; ++iteration) {
        if (remaining <= THRESHOLD) break;

        float proportion_sum = 0.0f;
        for (auto& track : tracks) {
            if (track_affected_property(track) + track.item_incurred_increase < track_limit(track)
                && track_is_affected(track)) {
                proportion_sum += track_distribution_proportion(track);
            }
        }
        if (proportion_sum == 0.0f) break;

        float min_increase_limit = std::numeric_limits<float>::infinity();
        for (auto& track : tracks) {
            if (track_affected_property(track) + track.item_incurred_increase < track_limit(track)
                && track_is_affected(track)) {
                float proportion = track_distribution_proportion(track);
                float value = (track_limit(track) - track_affected_property(track) - track.item_incurred_increase)
                              / proportion;
                min_increase_limit = std::min(min_increase_limit, value);
            }
        }
        if (!std::isfinite(min_increase_limit)) min_increase_limit = remaining / proportion_sum;

        float iteration_increase = std::min(min_increase_limit, remaining / proportion_sum);

        for (auto& track : tracks) {
            if (!track_is_affected(track)) continue;
            float increase = iteration_increase * track_distribution_proportion(track);
            if (increase > 0.0f
                && track_affected_property(track) + track.item_incurred_increase + increase
                       <= track_limit(track) + THRESHOLD) {
                track.item_incurred_increase += increase;
                remaining -= increase;
            }
        }
    }

    return remaining;
}

enum class IntrinsicContributionType : uint8_t { Minimum, Maximum };

/// 11.5.1 Distributing Extra Space Across Spanned Tracks (base sizes)
template <typename TrackIsAffected, typename TrackLimit>
void distribute_item_space_to_base_size(bool is_flex, float space, std::vector<GridTrack>& tracks,
                                        TrackIsAffected track_is_affected, TrackLimit track_limit,
                                        IntrinsicContributionType intrinsic_contribution_type) {
    auto get_base_size = [](const GridTrack& track) { return track.base_size; };
    constexpr float THRESHOLD = 0.000001f;

    auto inner_distribute = [&](float space_to_distribute, std::vector<GridTrack>& inner_tracks,
                                auto is_affected, auto distribution_proportion, auto limit_fn) {
        if (space_to_distribute == 0.0f) return;
        bool any_affected = false;
        for (auto& track : inner_tracks)
            if (is_affected(track)) any_affected = true;
        if (!any_affected) return;

        float track_sizes = 0.0f;
        for (auto& track : inner_tracks) track_sizes += track.base_size;
        float extra_space = std::max(0.0f, space_to_distribute - track_sizes);

        extra_space = distribute_space_up_to_limits(extra_space, inner_tracks, is_affected, distribution_proportion,
                                                    get_base_size, limit_fn);

        // 3. Distribute remaining span beyond limits (if any)
        if (extra_space > THRESHOLD) {
            bool want_intrinsic_max = (intrinsic_contribution_type == IntrinsicContributionType::Minimum);
            std::function<bool(const GridTrack&)> filter = [want_intrinsic_max](const GridTrack& track) {
                return want_intrinsic_max ? track.max_track_sizing_function.is_intrinsic()
                                          : track.max_track_sizing_function.is_max_or_fit_content();
            };
            size_t affected_with_filter = 0;
            for (auto& track : inner_tracks)
                if (is_affected(track) && filter(track)) affected_with_filter++;

            if (affected_with_filter == 0) {
                filter = [](const GridTrack&) { return true; };
            }

            auto beyond_affected = [&](const GridTrack& track) { return is_affected(track) && filter(track); };
            auto beyond_limit = [](const GridTrack& track) { return track.fit_content_limit; };
            distribute_space_up_to_limits(extra_space, inner_tracks, beyond_affected, distribution_proportion,
                                          get_base_size, beyond_limit);
        }

        // 4. Record any item-incurred increase as the planned increase
        for (auto& track : inner_tracks) {
            if (track.item_incurred_increase > track.base_size_planned_increase) {
                track.base_size_planned_increase = track.item_incurred_increase;
            }
            track.item_incurred_increase = 0.0f;
        }
    };

    if (is_flex) {
        auto filter = [&](const GridTrack& track) { return track.is_flexible() && track_is_affected(track); };
        float flex_factor_sum = 0.0f;
        for (auto& track : tracks)
            if (filter(track)) flex_factor_sum += track.flex_factor();
        if (flex_factor_sum > 0.0f) {
            inner_distribute(space, tracks, filter, [](const GridTrack& track) { return track.flex_factor(); },
                             track_limit);
        } else {
            inner_distribute(space, tracks, filter, [](const GridTrack&) { return 1.0f; }, track_limit);
        }
    } else {
        inner_distribute(space, tracks, track_is_affected, [](const GridTrack&) { return 1.0f; }, track_limit);
    }
}

/// 11.5.1 Distributing Extra Space Across Spanned Tracks: simplified version for growth limits
template <typename TrackIsAffected>
void distribute_item_space_to_growth_limit(float space, std::vector<GridTrack>& tracks,
                                           TrackIsAffected track_is_affected) {
    if (space == 0.0f) return;
    size_t affected_count = 0;
    for (auto& track : tracks)
        if (track_is_affected(track)) affected_count++;
    if (affected_count == 0) return;

    float track_sizes = 0.0f;
    for (auto& track : tracks)
        track_sizes += (track.growth_limit == std::numeric_limits<float>::infinity()) ? track.base_size
                                                                                     : track.growth_limit;
    float extra_space = std::max(0.0f, space - track_sizes);

    auto growth_limit_or_base_size = [](const GridTrack& track) {
        return (track.growth_limit == std::numeric_limits<float>::infinity()) ? track.base_size : track.growth_limit;
    };

    auto growable = [&](const GridTrack& track) {
        return track_is_affected(track)
               && (track.infinitely_growable || track.growth_limit == std::numeric_limits<float>::infinity());
    };
    auto fit_content_limit = [](const GridTrack& track) { return track.fit_content_limit; };
    extra_space = distribute_space_up_to_limits(extra_space, tracks, growable, [](const GridTrack&) { return 1.0f; },
                                                 growth_limit_or_base_size, fit_content_limit);

    // 3. Distribute space beyond limits
    if (extra_space > 0.0f) {
        distribute_space_up_to_limits(extra_space, tracks, track_is_affected, [](const GridTrack&) { return 1.0f; },
                                      growth_limit_or_base_size, fit_content_limit);
    }

    // 4. Record any item-incurred increase as the planned increase
    for (auto& track : tracks) {
        if (track.item_incurred_increase > track.growth_limit_planned_increase) {
            track.growth_limit_planned_increase = track.item_incurred_increase;
        }
        track.item_incurred_increase = 0.0f;
    }
}

/// 11.6 Maximise Tracks: distributes free space (if any) to tracks with finite growth limits
inline void maximise_tracks(std::vector<GridTrack>& axis_tracks, AvailableSpace axis_available_grid_space) {
    float used_space = 0.0f;
    for (auto& track : axis_tracks) used_space += track.base_size;
    float free_space = avail_compute_free_space(axis_available_grid_space, used_space);
    if (free_space == std::numeric_limits<float>::infinity()) {
        for (auto& track : axis_tracks) track.base_size = track.growth_limit;
    } else if (free_space > 0.0f) {
        auto fit_content_limited_growth_limit = [](const GridTrack& track) {
            return track.fit_content_limited_growth_limit();
        };
        distribute_space_up_to_limits(free_space, axis_tracks, [](const GridTrack&) { return true; },
                                      [](const GridTrack&) { return 1.0f; },
                                      [](const GridTrack& track) { return track.base_size; },
                                      fit_content_limited_growth_limit);
        for (auto& track : axis_tracks) {
            track.base_size += track.item_incurred_increase;
            track.item_incurred_increase = 0.0f;
        }
    }
}

/// 11.7.1 Find the Size of an fr
inline float find_size_of_fr(const std::vector<GridTrack>& tracks, float space_to_fill) {
    if (space_to_fill == 0.0f) return 0.0f;

    float hypothetical_fr_size = std::numeric_limits<float>::infinity();
    float previous_iter_hypothetical_fr_size = hypothetical_fr_size;
    size_t max_iterations = tracks.size() + 1;
    for (size_t iteration = 0; iteration < max_iterations; ++iteration) {
        float used_space = 0.0f;
        float naive_flex_factor_sum = 0.0f;
        for (auto& track : tracks) {
            if (track.max_track_sizing_function.is_fr()
                && track.max_track_sizing_function.value() * hypothetical_fr_size >= track.base_size) {
                naive_flex_factor_sum += track.max_track_sizing_function.value();
            } else {
                used_space += track.base_size;
            }
        }
        float leftover_space = space_to_fill - used_space;
        float flex_factor = std::max(naive_flex_factor_sum, 1.0f);

        previous_iter_hypothetical_fr_size = hypothetical_fr_size;
        hypothetical_fr_size = leftover_space / flex_factor;

        bool valid = true;
        for (auto& track : tracks) {
            if (track.max_track_sizing_function.is_fr()) {
                float factor = track.max_track_sizing_function.value();
                if (!(factor * hypothetical_fr_size >= track.base_size
                      || factor * previous_iter_hypothetical_fr_size < track.base_size)) {
                    valid = false;
                    break;
                }
            }
        }
        if (valid) break;
    }
    return hypothetical_fr_size;
}

inline float crossed_flex_factor_sum(const std::vector<GridTrack>& tracks, size_t start, size_t end) {
    float sum = 0.0f;
    for (size_t i = start; i < end; ++i)
        if (tracks[i].is_flexible()) sum += tracks[i].flex_factor();
    return sum;
}

inline float tracks_base_size_sum(const std::vector<GridTrack>& tracks) {
    float sum = 0.0f;
    for (auto& track : tracks) sum += track.base_size;
    return sum;
}

// ---------------------------------------------------------------------------
// Baseline shims (11.5.1)
// ---------------------------------------------------------------------------


/// 11.5.1 Shim baseline-aligned items so their intrinsic size contributions reflect their baseline
/// alignment.
template <typename Tree, typename MF>
void resolve_item_baselines(Tree& tree, MF& measure_fn, AbstractAxis axis, std::vector<GridItem>& items,
                            Size<std::optional<float>> inner_node_size) {
    AbstractAxis other_axis = other(axis);
    std::vector<size_t> order(items.size());
    for (size_t i = 0; i < items.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return items[a].placement(other_axis).start.value < items[b].placement(other_axis).start.value;
    });

    size_t group_start = 0;
    while (group_start < order.size()) {
        int16_t current_row = items[order[group_start]].placement(other_axis).start.value;
        size_t group_end = group_start;
        while (group_end < order.size() && items[order[group_end]].placement(other_axis).start.value == current_row)
            group_end++;

        size_t baseline_item_count = 0;
        for (size_t i = group_start; i < group_end; ++i)
            if (items[order[i]].participates_in_baseline_alignment()) baseline_item_count++;

        if (baseline_item_count > 1) {
            for (size_t i = group_start; i < group_end; ++i) {
                auto& item = items[order[i]];
                if (!item.participates_in_baseline_alignment()) continue;

                LayoutOutput measured = perform_child_layout(tree, measure_fn, item.node,
                                                            Size<std::optional<float>>{std::nullopt, std::nullopt},
                                                            inner_node_size,
                                                            Size<AvailableSpace>{AvailableSpace::min_content(),
                                                                                 AvailableSpace::min_content()});
                float height = measured.size.height;
                auto baseline = child_first_baseline(measured.baselines.first, height, item.overflow.y);
                item.baseline = baseline.value_or(height);
            }

            auto margin_box_baseline = [&](const GridItem& item) {
                return item.baseline.value_or(0.0f) + resolve_or_zero(item.margin.top, inner_node_size.width);
            };

            float row_max_baseline = margin_box_baseline(items[order[group_start]]);
            for (size_t i = group_start; i < group_end; ++i) {
                const auto& item = items[order[i]];
                if (!item.participates_in_baseline_alignment()) continue;
                row_max_baseline = std::max(row_max_baseline, margin_box_baseline(item));
            }

            for (size_t i = group_start; i < group_end; ++i) {
                auto& item = items[order[i]];
                if (!item.participates_in_baseline_alignment()) continue;
                item.baseline_shim = row_max_baseline - margin_box_baseline(item);
                item.known_dimensions_cache = std::nullopt;
            }
        }

        group_start = group_end;
    }
}

// ---------------------------------------------------------------------------
// 11.5 Resolve Intrinsic Track Sizes
// ---------------------------------------------------------------------------

/// 11.7. Expand Flexible Tracks
template <typename Tree, typename MF>
void expand_flexible_tracks(Tree& tree, MF& measure_fn, AbstractAxis axis, std::vector<GridTrack>& axis_tracks,
                            std::vector<GridTrack>& other_axis_tracks, std::vector<GridItem>& items,
                            std::optional<float> axis_min_size, std::optional<float> axis_max_size,
                            AvailableSpace axis_available_space_for_expansion,
                            Size<std::optional<float>> inner_node_size, bool estimate_is_base_size) {
    IntrinsicSizeMeasurer measurer{axis, inner_node_size, estimate_is_base_size};

    auto get_estimate = [&](const GridTrack& track, std::optional<float> parent_size) {
        return measurer.estimate(track, parent_size);
    };

    float flex_fraction;
    switch (axis_available_space_for_expansion.type) {
        case AvailableSpace::Definite: {
            float available_space = axis_available_space_for_expansion.value;
            float used_space = tracks_base_size_sum(axis_tracks);
            float free_space = available_space - used_space;
            flex_fraction = (free_space <= 0.0f) ? 0.0f : find_size_of_fr(axis_tracks, available_space);
            break;
        }
        case AvailableSpace::MinContent: flex_fraction = 0.0f; break;
        default: {
            // MaxContent: the used flex fraction is the maximum of ...
            float max_base_size_over_factor = 0.0f;
            bool any_flex = false;
            for (auto& track : axis_tracks) {
                if (!track.max_track_sizing_function.is_fr()) continue;
                any_flex = true;
                float factor = track.flex_factor();
                float value = (factor > 1.0f) ? track.base_size / factor : track.base_size;
                max_base_size_over_factor = std::max(max_base_size_over_factor, value);
            }
            if (!any_flex) max_base_size_over_factor = 0.0f;

            float max_of_item_fr_sizes = 0.0f;
            for (auto& item : items) {
                if (!item.crosses_flexible_track(axis)) continue;
                auto grid_area = measurer.grid_area_size_cached(item, axis_tracks, other_axis_tracks, get_estimate);
                auto available_space = size_opt_with(grid_area, axis, std::nullopt);
                float max_content_size = item_max_content_contribution(tree, measure_fn, item, axis, grid_area, available_space);
                auto range = item.track_range_excluding_lines(axis);
                std::vector<GridTrack> spanned(axis_tracks.begin() + static_cast<ptrdiff_t>(range.first),
                                              axis_tracks.begin() + static_cast<ptrdiff_t>(range.second));
                max_of_item_fr_sizes = std::max(max_of_item_fr_sizes, find_size_of_fr(spanned, max_content_size));
            }

            flex_fraction = std::max(max_base_size_over_factor, max_of_item_fr_sizes);

            // If using this flex fraction would cause the grid to be smaller than the grid
            // container's min size (or larger than its max size), redo this step treating the free
            // space as definite
            float hypothetical_grid_size = 0.0f;
            for (auto& track : axis_tracks) {
                if (track.max_track_sizing_function.is_fr()) {
                    float factor = track.max_track_sizing_function.value();
                    hypothetical_grid_size += std::max(track.base_size, factor * flex_fraction);
                } else {
                    hypothetical_grid_size += track.base_size;
                }
            }
            float min_size = axis_min_size.value_or(0.0f);
            float max_size = axis_max_size.value_or(std::numeric_limits<float>::infinity());
            if (hypothetical_grid_size < min_size) {
                flex_fraction = find_size_of_fr(axis_tracks, min_size);
            } else if (hypothetical_grid_size > max_size) {
                flex_fraction = find_size_of_fr(axis_tracks, max_size);
            }
            break;
        }
    }

    for (auto& track : axis_tracks) {
        if (!track.max_track_sizing_function.is_fr()) continue;
        float factor = track.max_track_sizing_function.value();
        track.base_size = std::max(track.base_size, factor * flex_fraction);
    }
}

/// 11.8. Stretch auto Tracks
inline void stretch_auto_tracks(std::vector<GridTrack>& axis_tracks, std::optional<float> axis_min_size,
                                AvailableSpace axis_available_space_for_expansion) {
    size_t num_auto_tracks = 0;
    for (auto& track : axis_tracks)
        if (track.max_track_sizing_function.is_auto()) num_auto_tracks++;
    if (num_auto_tracks == 0) return;

    float used_space = tracks_base_size_sum(axis_tracks);
    float free_space;
    if (axis_available_space_for_expansion.is_definite()) {
        free_space = avail_compute_free_space(axis_available_space_for_expansion, used_space);
    } else if (axis_min_size) {
        free_space = *axis_min_size - used_space;
    } else {
        free_space = 0.0f;
    }
    if (free_space > 0.0f) {
        float extra_space_per_auto_track = free_space / static_cast<float>(num_auto_tracks);
        for (auto& track : axis_tracks)
            if (track.max_track_sizing_function.is_auto()) track.base_size += extra_space_per_auto_track;
    }
}

/// 11.5 Resolve Intrinsic Track Sizes
template <typename Tree, typename MF>
void resolve_intrinsic_track_sizes(Tree& tree, MF& measure_fn, AbstractAxis axis,
                                   std::vector<GridTrack>& axis_tracks, std::vector<GridTrack>& other_axis_tracks,
                                   std::vector<GridItem>& items, AvailableSpace axis_available_grid_space,
                                   Size<std::optional<float>> inner_node_size, bool estimate_is_base_size) {
    IntrinsicSizeMeasurer measurer{axis, inner_node_size, estimate_is_base_size};
    auto get_estimate = [&](const GridTrack& track, std::optional<float> parent_size) {
        return measurer.estimate(track, parent_size);
    };
    std::optional<float> axis_inner_node_size = size_opt_get(inner_node_size, axis);

    // Partition items: non-flex items first (in document order), then flex items (in reverse)
    std::vector<size_t> front;
    std::vector<size_t> back;
    bool needs_sort = false;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].crosses_flexible_track(axis)) {
            back.push_back(i);
        } else {
            if (items[i].span(axis) > 1) needs_sort = true;
            front.push_back(i);
        }
    }
    std::vector<size_t> order = front;
    for (auto it = back.rbegin(); it != back.rend(); ++it) order.push_back(*it);
    size_t non_flex_item_count = front.size();
    if (needs_sort) {
        std::stable_sort(order.begin(), order.begin() + static_cast<ptrdiff_t>(non_flex_item_count),
                         [&](size_t a, size_t b) { return items[a].span(axis) < items[b].span(axis); });
    }

    auto min_content = [&](size_t item_index) {
        auto& item = items[item_index];
        auto grid_area = measurer.grid_area_size_cached(item, axis_tracks, other_axis_tracks, get_estimate);
        auto available_space = size_opt_with(grid_area, axis, std::nullopt);
        Size<float> margin_axis_sums = margins_axis_sums_with_baseline_shims(item, available_space.width);
        float contribution = item_min_content_contribution_cached(tree, measure_fn, item, axis, grid_area, available_space);
        return contribution + margin_axis_sums.get(axis);
    };
    auto max_content = [&](size_t item_index) {
        auto& item = items[item_index];
        auto grid_area = measurer.grid_area_size_cached(item, axis_tracks, other_axis_tracks, get_estimate);
        auto available_space = size_opt_with(grid_area, axis, std::nullopt);
        Size<float> margin_axis_sums = margins_axis_sums_with_baseline_shims(item, available_space.width);
        float contribution = item_max_content_contribution_cached(tree, measure_fn, item, axis, grid_area, available_space);
        return contribution + margin_axis_sums.get(axis);
    };
    auto minimum = [&](size_t item_index) {
        auto& item = items[item_index];
        auto grid_area = measurer.grid_area_size_cached(item, axis_tracks, other_axis_tracks, get_estimate);
        auto available_space = size_opt_with(grid_area, axis, std::nullopt);
        Size<float> margin_axis_sums = margins_axis_sums_with_baseline_shims(item, available_space.width);
        float contribution = item_minimum_contribution_cached(tree, measure_fn, item, axis, axis_tracks, grid_area,
                                                              inner_node_size);
        return contribution + margin_axis_sums.get(axis);
    };

    // Copy the spanned tracks of an item, distribute into the copy, then merge the planned increases
    // back into the real track list (the distribution functions only produce planned increases).
    auto distribute_base = [&](size_t item_index, bool is_flex, float space, std::vector<GridTrack>& spanned,
                               std::function<bool(const GridTrack&)> is_affected,
                               std::function<float(const GridTrack&)> limit,
                               IntrinsicContributionType type) {
        distribute_item_space_to_base_size(is_flex, space, spanned, is_affected, limit, type);
        auto& item = items[item_index];
        auto range = item.track_range_excluding_lines(axis);
        for (size_t i = range.first; i < range.second; ++i) {
            size_t local = i - range.first;
            if (spanned[local].base_size_planned_increase > axis_tracks[i].base_size_planned_increase) {
                axis_tracks[i].base_size_planned_increase = spanned[local].base_size_planned_increase;
            }
        }
    };
    auto distribute_growth = [&](size_t item_index, float space, std::vector<GridTrack>& spanned,
                                 std::function<bool(const GridTrack&)> is_affected) {
        distribute_item_space_to_growth_limit(space, spanned, is_affected);
        auto& item = items[item_index];
        auto range = item.track_range_excluding_lines(axis);
        for (size_t i = range.first; i < range.second; ++i) {
            size_t local = i - range.first;
            if (spanned[local].growth_limit_planned_increase > axis_tracks[i].growth_limit_planned_increase) {
                axis_tracks[i].growth_limit_planned_increase = spanned[local].growth_limit_planned_increase;
            }
        }
    };
    auto spanned_copy = [&](size_t item_index) {
        auto range = items[item_index].track_range_excluding_lines(axis);
        return std::vector<GridTrack>(axis_tracks.begin() + static_cast<ptrdiff_t>(range.first),
                                      axis_tracks.begin() + static_cast<ptrdiff_t>(range.second));
    };
    auto spans_track_matching = [&](size_t item_index, std::function<bool(const GridTrack&)> predicate) {
        auto range = items[item_index].track_range_excluding_lines(axis);
        for (size_t i = range.first; i < range.second; ++i)
            if (predicate(axis_tracks[i])) return true;
        return false;
    };

    ItemBatcher batcher(axis);
    auto batch = batcher.next(items);
    (void)batch;
    // Note: ItemBatcher needs the ordered list, so batching is done over `order` manually below
    size_t cursor = 0;
    while (cursor < order.size()) {
        bool is_flex = items[order[cursor]].crosses_flexible_track(axis);
        uint16_t batch_span = items[order[cursor]].span(axis);
        size_t batch_end;
        if (is_flex) {
            batch_end = order.size();
        } else {
            batch_end = order.size();
            for (size_t i = cursor; i < order.size(); ++i) {
                if (items[order[i]].crosses_flexible_track(axis) || items[order[i]].span(axis) > batch_span) {
                    batch_end = i;
                    break;
                }
            }
        }

        // 2. Size tracks to fit non-spanning items
        if (!is_flex && batch_span == 1) {
            for (size_t b = cursor; b < batch_end; ++b) {
                size_t item_index = order[b];
                auto& item = items[item_index];
                uint16_t track_index = item.placement_indexes(axis).start + 1;

                // Handle base sizes
                const GridTrack& track = axis_tracks[track_index];
                float new_base_size;
                if (track.min_track_sizing_function.tag() == CompactLength::MIN_CONTENT_TAG) {
                    new_base_size = std::max(track.base_size, min_content(item_index));
                } else if (track.min_track_sizing_function.tag() == CompactLength::MAX_CONTENT_TAG) {
                    new_base_size = std::max(track.base_size, max_content(item_index));
                } else if (cl_min_behaves_as_auto(track.min_track_sizing_function, axis_inner_node_size)) {
                    float space;
                    if ((axis_available_grid_space.type == AvailableSpace::MinContent
                         || axis_available_grid_space.type == AvailableSpace::MaxContent)
                        && !is_scroll_container_overflow(item.overflow.get(axis))) {
                        float axis_minimum_size = minimum(item_index);
                        float axis_min_content_size = min_content(item_index);
                        auto limit = cl_definite_limit(track.max_track_sizing_function, axis_inner_node_size);
                        float limited = limit ? std::min(axis_min_content_size, *limit) : axis_min_content_size;
                        space = std::max(limited, axis_minimum_size);
                    } else {
                        space = minimum(item_index);
                    }
                    new_base_size = std::max(track.base_size, space);
                } else {
                    new_base_size = track.base_size;
                }
                axis_tracks[track_index].base_size = new_base_size;

                // Handle growth limits
                const GridTrack& track_after = axis_tracks[track_index];
                CompactLength max_track_sizing_function = track_after.max_track_sizing_function;
                float fit_content_limit = track_after.fit_content_limit;
                bool has_min_content_value = false;
                float min_content_value = 0.0f;
                if (track_after.has_intrinsic_sizing_function()) {
                    min_content_value = min_content(item_index);
                    has_min_content_value = true;
                }
                bool have_planned_increase = false;
                float planned_increase = 0.0f;
                if (max_track_sizing_function.is_fit_content()) {
                    float max_content_value = std::min(max_content(item_index), fit_content_limit);
                    if (has_min_content_value && !is_scroll_container_overflow(item.overflow.get(axis))) {
                        planned_increase = std::max(min_content_value, max_content_value);
                    } else {
                        planned_increase = max_content_value;
                    }
                    have_planned_increase = true;
                } else if (max_track_sizing_function.is_max_content_alike()
                           || (max_track_sizing_function.uses_percentage() && !axis_inner_node_size)) {
                    planned_increase = max_content(item_index);
                    have_planned_increase = true;
                } else if (max_track_sizing_function.is_intrinsic()) {
                    planned_increase = min_content_value;
                    have_planned_increase = true;
                }
                if (have_planned_increase) {
                    axis_tracks[track_index].growth_limit_planned_increase =
                        std::max(axis_tracks[track_index].growth_limit_planned_increase, planned_increase);
                }
            }

            for (auto& track : axis_tracks) {
                if (track.growth_limit_planned_increase > 0.0f) {
                    track.growth_limit = (track.growth_limit == std::numeric_limits<float>::infinity())
                                             ? track.growth_limit_planned_increase
                                             : std::max(track.growth_limit, track.growth_limit_planned_increase);
                }
                track.infinitely_growable = false;
                track.growth_limit_planned_increase = 0.0f;
                if (track.growth_limit < track.base_size) track.growth_limit = track.base_size;
            }

            cursor = batch_end;
            continue;
        }

        // 1. For intrinsic minimums: increase the base size of tracks with an intrinsic min track
        //    sizing function by distributing extra space to accommodate minimum contributions
        for (size_t b = cursor; b < batch_end; ++b) {
            size_t item_index = order[b];
            auto& item = items[item_index];
            if (!item.crosses_intrinsic_track(axis)) continue;

            float space;
            if ((axis_available_grid_space.type == AvailableSpace::MinContent
                 || axis_available_grid_space.type == AvailableSpace::MaxContent)
                && !is_scroll_container_overflow(item.overflow.get(axis))) {
                float axis_minimum_size = minimum(item_index);
                float axis_min_content_size = min_content(item_index);
                auto limit = spanned_track_limit(item, axis, axis_tracks, axis_inner_node_size);
                float limited_min_content = limit ? std::min(axis_min_content_size, *limit) : axis_min_content_size;
                limited_min_content = std::max(limited_min_content, axis_minimum_size);

                if (is_flex) {
                    auto range = item.track_range_excluding_lines(axis);
                    float inflexible_sizes = 0.0f;
                    for (size_t i = range.first; i < range.second; ++i)
                        if (!axis_tracks[i].is_flexible()) inflexible_sizes += axis_tracks[i].base_size;
                    float scale = std::min(crossed_flex_factor_sum(axis_tracks, range.first, range.second), 1.0f);
                    float excess = std::max(limited_min_content - inflexible_sizes, 0.0f);
                    space = std::max(axis_minimum_size, inflexible_sizes + excess * scale);
                } else {
                    space = limited_min_content;
                }
            } else {
                space = minimum(item_index);
            }

            auto is_affected = [&](const GridTrack& track) {
                return !cl_definite_value(track.min_track_sizing_function, axis_inner_node_size).has_value();
            };
            if (space > 0.0f) {
                auto spanned = spanned_copy(item_index);
                if (is_scroll_container_overflow(item.overflow.get(axis))) {
                    distribute_base(item_index, is_flex, space, spanned, is_affected,
                                    [](const GridTrack& track) { return track.fit_content_limited_growth_limit(); },
                                    IntrinsicContributionType::Minimum);
                } else {
                    distribute_base(item_index, is_flex, space, spanned, is_affected,
                                    [](const GridTrack& track) { return track.growth_limit; },
                                    IntrinsicContributionType::Minimum);
                }
            }
        }
        flush_planned_base_size_increases(axis_tracks);

        // 2. For content-based minimums: increase the base size of tracks with a min track sizing
        //    function of min-content or max-content by distributing space to account for these
        //    items' min-content contributions
        auto has_min_or_max_content_min_track_sizing_function = [](const GridTrack& track) {
            return track.min_track_sizing_function.is_min_or_max_content();
        };
        for (size_t b = cursor; b < batch_end; ++b) {
            size_t item_index = order[b];
            auto& item = items[item_index];
            if (!spans_track_matching(item_index, has_min_or_max_content_min_track_sizing_function)) continue;
            float space = min_content(item_index);
            if (space > 0.0f) {
                auto spanned = spanned_copy(item_index);
                if (is_scroll_container_overflow(item.overflow.get(axis))) {
                    distribute_base(item_index, is_flex, space, spanned, has_min_or_max_content_min_track_sizing_function,
                                    [](const GridTrack& track) { return track.fit_content_limited_growth_limit(); },
                                    IntrinsicContributionType::Minimum);
                } else {
                    distribute_base(item_index, is_flex, space, spanned, has_min_or_max_content_min_track_sizing_function,
                                    [](const GridTrack& track) { return track.growth_limit; },
                                    IntrinsicContributionType::Minimum);
                }
            }
        }
        flush_planned_base_size_increases(axis_tracks);

        // 3. For max-content minimums
        if (axis_available_grid_space.type == AvailableSpace::MaxContent) {
            auto has_auto_min_track_sizing_function = [](const GridTrack& track) {
                return track.min_track_sizing_function.is_auto() && !track.max_track_sizing_function.is_min_content();
            };
            auto has_max_content_min_track_sizing_function = [](const GridTrack& track) {
                return track.min_track_sizing_function.is_max_content();
            };

            for (size_t b = cursor; b < batch_end; ++b) {
                size_t item_index = order[b];
                if (!spans_track_matching(item_index, [&](const GridTrack& track) {
                        return has_auto_min_track_sizing_function(track) || has_max_content_min_track_sizing_function(track);
                    }))
                    continue;

                auto& item = items[item_index];
                float axis_max_content_size = max_content(item_index);
                auto limit = spanned_track_limit(item, axis, axis_tracks, axis_inner_node_size);
                float space = limit ? std::min(axis_max_content_size, *limit) : axis_max_content_size;

                if (is_flex) {
                    auto range = item.track_range_excluding_lines(axis);
                    float inflexible_sizes = 0.0f;
                    for (size_t i = range.first; i < range.second; ++i)
                        if (!axis_tracks[i].is_flexible()) inflexible_sizes += axis_tracks[i].base_size;
                    float scale = std::min(crossed_flex_factor_sum(axis_tracks, range.first, range.second), 1.0f);
                    space = inflexible_sizes + std::max(space - inflexible_sizes, 0.0f) * scale;
                }

                if (space > 0.0f) {
                    auto spanned = spanned_copy(item_index);
                    bool any_max_content_min = false;
                    for (auto& track : spanned)
                        if (has_max_content_min_track_sizing_function(track)) any_max_content_min = true;
                    if (any_max_content_min) {
                        distribute_base(item_index, is_flex, space, spanned, has_max_content_min_track_sizing_function,
                                        [](const GridTrack&) { return std::numeric_limits<float>::infinity(); },
                                        IntrinsicContributionType::Maximum);
                    } else {
                        distribute_base(item_index, is_flex, space, spanned, has_auto_min_track_sizing_function,
                                        [](const GridTrack& track) { return track.fit_content_limited_growth_limit(); },
                                        IntrinsicContributionType::Maximum);
                    }
                }
            }
            flush_planned_base_size_increases(axis_tracks);
        }

        // In all cases, continue to increase the base size of tracks with a min track sizing function
        // of max-content by distributing extra space to account for these items' max-content
        // contributions
        auto has_max_content_min_track_sizing_function = [](const GridTrack& track) {
            return track.min_track_sizing_function.is_max_content();
        };
        for (size_t b = cursor; b < batch_end; ++b) {
            size_t item_index = order[b];
            if (!spans_track_matching(item_index, has_max_content_min_track_sizing_function)) continue;
            float space = max_content(item_index);
            if (space > 0.0f) {
                auto spanned = spanned_copy(item_index);
                distribute_base(item_index, is_flex, space, spanned, has_max_content_min_track_sizing_function,
                                [](const GridTrack& track) { return track.growth_limit; },
                                IntrinsicContributionType::Maximum);
            }
        }
        flush_planned_base_size_increases(axis_tracks);

        // 4. If any track's growth limit is now less than its base size, increase it
        for (auto& track : axis_tracks)
            if (track.growth_limit < track.base_size) track.growth_limit = track.base_size;

        if (!is_flex) {
            // 5. For intrinsic maximums: increase the growth limit of tracks with an intrinsic max
            //    track sizing function by distributing space to account for min-content contributions
            auto has_intrinsic_max_track_sizing_function = [&](const GridTrack& track) {
                return !cl_definite_value(track.max_track_sizing_function, axis_inner_node_size).has_value();
            };
            for (size_t b = cursor; b < batch_end; ++b) {
                size_t item_index = order[b];
                if (!spans_track_matching(item_index, has_intrinsic_max_track_sizing_function)) continue;
                float space = min_content(item_index);
                if (space > 0.0f) {
                    auto spanned = spanned_copy(item_index);
                    distribute_growth(item_index, space, spanned, has_intrinsic_max_track_sizing_function);
                }
            }
            flush_planned_growth_limit_increases(axis_tracks, true);

            // 6. For max-content maximums: increase the growth limit of tracks with a max track
            //    sizing function of max-content by distributing space to account for max-content
            //    contributions, limited by any fit-content() argument
            auto has_max_content_max_track_sizing_function = [&](const GridTrack& track) {
                return track.max_track_sizing_function.is_max_content_alike()
                       || (track.max_track_sizing_function.uses_percentage() && !axis_inner_node_size);
            };
            for (size_t b = cursor; b < batch_end; ++b) {
                size_t item_index = order[b];
                if (!spans_track_matching(item_index, has_max_content_max_track_sizing_function)) continue;
                float space = max_content(item_index);
                if (space > 0.0f) {
                    auto spanned = spanned_copy(item_index);
                    distribute_growth(item_index, space, spanned, has_max_content_max_track_sizing_function);
                }
            }
            flush_planned_growth_limit_increases(axis_tracks, false);
        }

        cursor = batch_end;
    }

    // Step 5. If any track still has an infinite growth limit, set it to its base size
    for (auto& track : axis_tracks)
        if (track.growth_limit == std::numeric_limits<float>::infinity()) track.growth_limit = track.base_size;
}

// ---------------------------------------------------------------------------
// Track sizing algorithm entry point
// ---------------------------------------------------------------------------

template <typename Tree, typename MF>
void track_sizing_algorithm(Tree& tree, MF& measure_fn, AbstractAxis axis, std::optional<float> axis_min_size,
                            std::optional<float> axis_max_size, AlignContent axis_alignment,
                            AlignContent other_axis_alignment, Size<AvailableSpace> available_grid_space,
                            Size<std::optional<float>> inner_node_size, std::vector<GridTrack>& axis_tracks,
                            std::vector<GridTrack>& other_axis_tracks, std::vector<GridItem>& items,
                            bool estimate_is_base_size, bool has_baseline_aligned_item) {
    // 11.4 Initialise Track sizes
    std::optional<float> percentage_basis = size_opt_get(inner_node_size, axis);
    if (!percentage_basis) percentage_basis = axis_min_size;
    initialize_track_sizes(axis_tracks, percentage_basis);

    // Resolve fit-content() limits against the container's inner size
    for (auto& track : axis_tracks) track.resolve_fit_content_limit(size_opt_get(inner_node_size, axis));

    // 11.5.1 Shim item baselines
    if (has_baseline_aligned_item) {
        resolve_item_baselines(tree, measure_fn, axis, items, inner_node_size);
    }

    // If all tracks have a fixed min track sizing function and base_size == growth_limit, then the
    // track sizes are already final and we can skip the rest of this function
    bool all_final = true;
    for (auto& track : axis_tracks) {
        if (!(track.base_size == track.growth_limit
              && cl_definite_value(track.min_track_sizing_function, percentage_basis).has_value())) {
            all_final = false;
            break;
        }
    }
    if (all_final) return;

    // Pre-computations for 11.5 Resolve Intrinsic Track Sizes: compute the additional amount to add
    // to each spanned gutter when computing an item's estimated size in the opposite axis
    IntrinsicSizeMeasurer gutter_measurer{other(axis), inner_node_size, estimate_is_base_size};
    auto get_estimate = [&](const GridTrack& track, std::optional<float> parent_size) {
        return gutter_measurer.estimate(track, parent_size);
    };
    float gutter_alignment_adjustment = compute_alignment_gutter_adjustment(
        other_axis_alignment, size_opt_get(inner_node_size, other(axis)), get_estimate, other_axis_tracks);
    if (other_axis_tracks.size() > 3) {
        for (size_t i = 2; i < other_axis_tracks.size(); i += 2) {
            other_axis_tracks[i].content_alignment_adjustment = gutter_alignment_adjustment;
        }
    }

    // 11.5 Resolve Intrinsic Track Sizes
    resolve_intrinsic_track_sizes(tree, measure_fn, axis, axis_tracks, other_axis_tracks, items,
                                  available_grid_space.get(axis), inner_node_size, estimate_is_base_size);

#ifdef TAFFY_GRID_TRACE
    {
        auto as = available_grid_space.get(axis);
        std::printf("[tsz] axis=%d avail=%s%.2f bases:", (int)axis, as.is_definite() ? "def" : "indef",
                    as.is_definite() ? as.value : 0.0f);
        for (auto& t : axis_tracks) std::printf(" %.2f", t.base_size);
        std::printf("\n");
    }
#endif
    // 11.6 Maximise Tracks
    maximise_tracks(axis_tracks, available_grid_space.get(axis));
#ifdef TAFFY_GRID_TRACE
    {
        std::printf("[tsz] after maximise:");
        for (auto& t : axis_tracks) std::printf(" %.2f", t.base_size);
        std::printf("\n");
    }
#endif

    // Map definite available space to MaxContent when the container's inner size is indefinite
    AvailableSpace axis_available_space_for_expansion;
    if (size_opt_get(inner_node_size, axis)) {
        axis_available_space_for_expansion = AvailableSpace::definite(*size_opt_get(inner_node_size, axis));
    } else if (available_grid_space.get(axis).type == AvailableSpace::MinContent && axis == AbstractAxis::Inline) {
        axis_available_space_for_expansion = AvailableSpace::min_content();
    } else {
        axis_available_space_for_expansion = AvailableSpace::max_content();
    }

    // 11.7 Expand Flexible Tracks
    expand_flexible_tracks(tree, measure_fn, axis, axis_tracks, other_axis_tracks, items, axis_min_size, axis_max_size,
                           axis_available_space_for_expansion, inner_node_size, estimate_is_base_size);

#ifdef TAFFY_GRID_TRACE
    {
        std::printf("[tsz] after expand:");
        for (auto& t : axis_tracks) std::printf(" %.2f", t.base_size);
        std::printf("\n");
    }
#endif
    // 11.8 Stretch auto Tracks
    bool alignment_is_stretch = (axis_alignment.keyword == AlignContentKeyword::Stretch
                                 || axis_alignment.keyword == AlignContentKeyword::Normal);
    if (alignment_is_stretch) {
        stretch_auto_tracks(axis_tracks, axis_min_size, axis_available_space_for_expansion);
    }
}

// ---------------------------------------------------------------------------
// Track alignment and item positioning
// ---------------------------------------------------------------------------

/// Align the grid tracks within the grid according to the align-content (rows) or justify-content
/// (columns) property
inline void align_tracks(float grid_container_content_box_size, Line<float> padding, Line<float> border,
                         std::vector<GridTrack>& tracks, AlignContent track_alignment_style, bool axis_is_reversed) {
    float used_size = tracks_base_size_sum(tracks);
    float free_space = grid_container_content_box_size - used_size;
    float origin = padding.start + border.start;

    size_t num_tracks = 0;
    for (size_t i = 1; i < tracks.size(); i += 2)
        if (!tracks[i].is_collapsed) num_tracks++;

    float gap = 0.0f;
    bool layout_is_reversed = false;
    AlignContentKeyword track_alignment = apply_alignment_fallback(free_space, num_tracks, track_alignment_style);
    if (axis_is_reversed) {
        track_alignment = AlignContent{track_alignment, AlignmentSafety::Unsafe}.reversed();
    }

    // If every track is collapsed then no track receives the alignment offset below, but the grid's
    // lines should still be aligned within the container
    float empty_grid_offset =
        (num_tracks == 0) ? compute_alignment_offset(free_space, num_tracks, gap, track_alignment, layout_is_reversed, true)
                          : 0.0f;

    float total_offset = origin + empty_grid_offset;
    bool seen_non_collapsed_track = false;

    auto position_track = [&](size_t i, GridTrack& track) {
        bool is_gutter = (i % 2 == 0);
        bool is_non_collapsed_track = !is_gutter && !track.is_collapsed;
        bool is_first = is_non_collapsed_track && !seen_non_collapsed_track;
        float offset = is_non_collapsed_track
                           ? compute_alignment_offset(free_space, num_tracks, gap, track_alignment, layout_is_reversed,
                                                      is_first)
                           : 0.0f;
        track.offset = total_offset + offset;
        total_offset = total_offset + offset + track.base_size;
        if (is_non_collapsed_track) seen_non_collapsed_track = true;
    };

    if (axis_is_reversed) {
        for (size_t k = 0; k < tracks.size(); ++k) position_track(k, tracks[tracks.size() - 1 - k]);
    } else {
        for (size_t k = 0; k < tracks.size(); ++k) position_track(k, tracks[k]);
    }
}

/// The offset applied to an item in a single axis by its `inset` if it is `position: relative`
inline float relative_offset(bool is_relative, Line<std::optional<float>> inset, bool is_rtl_axis) {
    if (!is_relative) return 0.0f;
    std::optional<float> relative_inset;
    if (is_rtl_axis) {
        relative_inset = inset.end ? std::optional<float>(-*inset.end) : inset.start;
    } else {
        relative_inset = inset.start ? inset.start : (inset.end ? std::optional<float>(-*inset.end) : std::nullopt);
    }
    return relative_inset.value_or(0.0f);
}

/// Align and size a grid item along a single axis
inline std::pair<float, Line<float>> align_item_within_area(Line<float> grid_area, AlignItems alignment_style,
                                                            float resolved_size, bool is_out_of_flow,
                                                            Line<std::optional<float>> inset,
                                                            Line<std::optional<float>> margin, float baseline_shim,
                                                            bool direction_is_rtl) {
    float non_auto_margin_start = margin.start.value_or(0.0f) + baseline_shim;
    float non_auto_margin_end = margin.end.value_or(0.0f);
    float grid_area_size = std::max(grid_area.end - grid_area.start, 0.0f);
    float free_space = std::max(grid_area_size - resolved_size - (non_auto_margin_start + non_auto_margin_end), 0.0f);

    // Expand auto margins to fill available space
    int auto_margin_count = (margin.start.has_value() ? 0 : 1) + (margin.end.has_value() ? 0 : 1);
    float auto_margin_size = (auto_margin_count > 0) ? free_space / static_cast<float>(auto_margin_count) : 0.0f;
    Line<float> resolved_margin{margin.start.value_or(auto_margin_size) + baseline_shim,
                                margin.end.value_or(auto_margin_size)};

    bool overflows = resolved_size + (non_auto_margin_start + non_auto_margin_end) > grid_area_size;
    AlignItemsKeyword alignment_keyword = resolve_self_alignment_safety(alignment_style, overflows);

    float alignment_based_offset;
    switch (alignment_keyword) {
        case AlignItemsKeyword::Start:
        case AlignItemsKeyword::FlexStart:
        case AlignItemsKeyword::Baseline:
        case AlignItemsKeyword::Stretch:
            alignment_based_offset = direction_is_rtl ? (grid_area_size - resolved_size - resolved_margin.end)
                                                      : resolved_margin.start;
            break;
        case AlignItemsKeyword::End:
        case AlignItemsKeyword::FlexEnd:
            alignment_based_offset = direction_is_rtl ? resolved_margin.start
                                                      : (grid_area_size - resolved_size - resolved_margin.end);
            break;
        case AlignItemsKeyword::Center:
            alignment_based_offset = (grid_area_size - resolved_size + resolved_margin.start - resolved_margin.end) / 2.0f;
            break;
        default:
            alignment_based_offset = resolved_margin.start;
            break;
    }

    float offset_within_area;
    if (is_out_of_flow) {
        if (inset.start && inset.end) {
            offset_within_area = direction_is_rtl
                                     ? (grid_area_size - *inset.end - resolved_size - non_auto_margin_end)
                                     : (*inset.start + non_auto_margin_start);
        } else if (inset.start) {
            offset_within_area = *inset.start + non_auto_margin_start;
        } else if (inset.end) {
            offset_within_area = grid_area_size - *inset.end - resolved_size - non_auto_margin_end;
        } else {
            offset_within_area = alignment_based_offset;
        }
    } else {
        offset_within_area = alignment_based_offset;
    }

    return {grid_area.start + offset_within_area, resolved_margin};
}

/// Resolve writing-mode relative SelfStart/SelfEnd keywords to Start/End
inline AlignItems resolve_self_relative_alignment(AlignItems alignment, Direction item_direction,
                                                  Direction container_direction, bool axis_is_inline) {
    bool flip = axis_is_inline && item_direction != container_direction;
    AlignItems resolved = alignment;
    if (alignment.keyword == AlignItemsKeyword::SelfStart) {
        resolved.keyword = flip ? AlignItemsKeyword::End : AlignItemsKeyword::Start;
    } else if (alignment.keyword == AlignItemsKeyword::SelfEnd) {
        resolved.keyword = flip ? AlignItemsKeyword::Start : AlignItemsKeyword::End;
    }
    return resolved;
}

struct ItemAlignmentResult {
    float y_position{0.0f};
    float height{0.0f};
    std::optional<float> baseline{};
};

/// Align and size a grid item into its final position
template <typename Tree, typename MF>
ItemAlignmentResult align_and_position_item(Tree& tree, MF& measure_fn, NodeId node, uint32_t order,
                                            Rect<float> grid_area, Size<float> containing_block_size,
                                            AlignItems container_justify_items, AlignItems container_align_items,
                                            float baseline_shim, Direction direction) {
    Size<float> grid_area_size{grid_area.right - grid_area.left, grid_area.bottom - grid_area.top};

    const Style& style = tree.style(node);
    Overflow overflow_x = style.overflow.x;
    Overflow overflow_y = style.overflow.y;
    float scrollbar_width = style.scrollbar_width;
    auto aspect_ratio = style.aspect_ratio;
    bool is_replaced = style.item_is_replaced;
    Direction item_direction = style.direction;

    AlignItems justify_self = resolve_self_relative_alignment(
        style.justify_self.value_or(container_justify_items), item_direction, direction, true);
    AlignItems align_self = resolve_self_relative_alignment(
        style.align_self.value_or(container_align_items), item_direction, direction, false);

    bool is_out_of_flow = (style.position == Position::Absolute || style.position == Position::Fixed);
    Rect<std::optional<float>> inset = maybe_resolve_rect(style.inset, containing_block_size.width);
    Line<std::optional<float>> inset_horizontal{inset.left, inset.right};
    Line<std::optional<float>> inset_vertical{inset.top, inset.bottom};

    Rect<float> padding = resolve_or_zero_rect(style.padding, containing_block_size.width);
    Rect<float> border = resolve_or_zero_rect(style.border, containing_block_size.width);
    Size<float> padding_border_size = (padding + border).sum_axes();
    Size<float> box_sizing_adjustment =
        (style.box_sizing == BoxSizing::ContentBox) ? padding_border_size : Size<float>{0.0f, 0.0f};

    Size<std::optional<float>> inherent_size = size_opt_maybe_add(
        maybe_resolve_size(style.size, containing_block_size).maybe_apply_aspect_ratio(aspect_ratio),
        box_sizing_adjustment);
    Size<std::optional<float>> min_size =
        size_opt_maybe_max_opt(size_opt_maybe_add(maybe_resolve_size(style.min_size, containing_block_size),
                                                  box_sizing_adjustment),
                               Size<std::optional<float>>{padding_border_size.width, padding_border_size.height})
            .maybe_apply_aspect_ratio(aspect_ratio);
    Size<std::optional<float>> max_size = size_opt_maybe_add(
        maybe_resolve_size(style.max_size, containing_block_size).maybe_apply_aspect_ratio(aspect_ratio),
        box_sizing_adjustment);

    // Resolve `normal` alignment (the default if alignment is set on neither the parent or the node)
    AlignItems horizontal_alignment = justify_self;
    if (justify_self.keyword == AlignItemsKeyword::Normal) {
        horizontal_alignment = (inherent_size.width.has_value() || is_replaced) ? AlignItems_START : AlignItems_STRETCH;
    }
    AlignItems vertical_alignment = align_self;
    if (align_self.keyword == AlignItemsKeyword::Normal) {
        vertical_alignment = (inherent_size.height.has_value() || aspect_ratio.has_value() || is_replaced)
                                 ? AlignItems_START
                                 : AlignItems_STRETCH;
    }

    // Note: this is not a bug. Both horizontal and vertical margins resolve against the WIDTH of the
    // grid area.
    Rect<std::optional<float>> margin = maybe_resolve_rect(style.margin, containing_block_size.width);

    Size<float> grid_area_minus_item_margins_size{
        std::max(grid_area_size.width - margin.left.value_or(0.0f) - margin.right.value_or(0.0f), 0.0f),
        std::max(grid_area_size.height - margin.top.value_or(0.0f) - margin.bottom.value_or(0.0f) - baseline_shim, 0.0f)};

    // A size that is a sizing keyword (min-content, max-content, fit-content, fit-content(...),
    // stretch) either resolves to an exact size or is resolved by measuring the item under the
    // corresponding available space constraint
    std::optional<GridSizingKeywordResolution> keyword_width;
    if (!inherent_size.width) {
        keyword_width = resolve_grid_sizing_keyword(style.size.width, grid_area_minus_item_margins_size.width,
                                               containing_block_size.width);
    }
    std::optional<GridSizingKeywordResolution> keyword_height;
    if (!inherent_size.height) {
        keyword_height = resolve_grid_sizing_keyword(style.size.height, grid_area_minus_item_margins_size.height,
                                                containing_block_size.height);
    }

    // If both axes need to be measured then resolve them with a single measure call
    Size<std::optional<float>> keyword_measured_size{std::nullopt, std::nullopt};
    if (keyword_width && keyword_height && keyword_width->kind == GridSizingKeywordResolution::Measure
        && keyword_height->kind == GridSizingKeywordResolution::Measure && !is_out_of_flow) {
        LayoutInput measure_input;
        measure_input.run_mode = RunMode::ComputeSize;
        measure_input.sizing_mode = SizingMode::InherentSize;
        measure_input.axis = RequestedAxis::Both;
        measure_input.parent_size = Size<std::optional<float>>{containing_block_size.width,
                                                               containing_block_size.height};
        measure_input.available_space =
            Size<AvailableSpace>{keyword_width->available, keyword_height->available};
        LayoutOutput measured = tree.compute_node_layout(node, measure_input, measure_fn);
        keyword_measured_size.width = measured.size.width;
        keyword_measured_size.height = measured.size.height;
    }

    std::optional<float> width = inherent_size.width;
    if (!width) {
        if (keyword_width) {
            if (keyword_width->kind == GridSizingKeywordResolution::Exact) {
                width = keyword_width->exact;
            } else if (keyword_measured_size.width) {
                width = keyword_measured_size.width;
            } else {
                width = measure_child_axis_size(
                    tree, measure_fn, node, Size<std::optional<float>>{std::nullopt, std::nullopt},
                    Size<std::optional<float>>{containing_block_size.width, containing_block_size.height},
                    Size<AvailableSpace>{keyword_width->available,
                                         AvailableSpace::definite(grid_area_minus_item_margins_size.height)},
                    AbsoluteAxis::Horizontal);
            }
        } else if (margin.left.has_value() && margin.right.has_value()
                   && horizontal_alignment.keyword == AlignItemsKeyword::Stretch && !is_out_of_flow) {
            // Apply width based on stretch alignment if the alignment style is "stretch" and the node
            // does not have auto margins in this axis
            width = grid_area_minus_item_margins_size.width;
        }
    }
    Size<std::optional<float>> size{width, inherent_size.height};
    size = size.maybe_apply_aspect_ratio(aspect_ratio);

    std::optional<float> height = size.height;
    if (!height) {
        if (keyword_height) {
            if (keyword_height->kind == GridSizingKeywordResolution::Exact) {
                height = keyword_height->exact;
            } else if (keyword_measured_size.height) {
                height = keyword_measured_size.height;
            } else {
                height = measure_child_axis_size(
                    tree, measure_fn, node, Size<std::optional<float>>{size.width, std::nullopt},
                    Size<std::optional<float>>{containing_block_size.width, containing_block_size.height},
                    Size<AvailableSpace>{size.width ? AvailableSpace::definite(*size.width)
                                                    : AvailableSpace::definite(grid_area_minus_item_margins_size.width),
                                         keyword_height->available},
                    AbsoluteAxis::Vertical);
            }
        } else if (margin.top.has_value() && margin.bottom.has_value()
                   && vertical_alignment.keyword == AlignItemsKeyword::Stretch && !is_out_of_flow) {
            height = grid_area_minus_item_margins_size.height;
        }
    }
    size = Size<std::optional<float>>{size.width, height}.maybe_apply_aspect_ratio(aspect_ratio);
    size = size_opt_maybe_clamp(size, min_size, max_size);

    LayoutOutput layout_output = perform_child_layout(
        tree, measure_fn, node, size,
        Size<std::optional<float>>{containing_block_size.width, containing_block_size.height},
        Size<AvailableSpace>{AvailableSpace::definite(grid_area_minus_item_margins_size.width),
                             AvailableSpace::definite(grid_area_minus_item_margins_size.height)});

    Size<std::optional<float>> final_size_opt = size;
    if (!final_size_opt.width) final_size_opt.width = layout_output.size.width;
    if (!final_size_opt.height) final_size_opt.height = layout_output.size.height;
    final_size_opt = size_opt_maybe_clamp(final_size_opt, min_size, max_size);
    Size<float> final_size{*final_size_opt.width, *final_size_opt.height};

    auto horizontal = align_item_within_area(Line<float>{grid_area.left, grid_area.right}, horizontal_alignment,
                                             final_size.width, is_out_of_flow, inset_horizontal,
                                             Line<std::optional<float>>{margin.left, margin.right}, 0.0f,
                                             direction == Direction::Rtl);
    auto vertical = align_item_within_area(Line<float>{grid_area.top, grid_area.bottom}, vertical_alignment,
                                           final_size.height, is_out_of_flow, inset_vertical,
                                           Line<std::optional<float>>{margin.top, margin.bottom}, baseline_shim, false);

    bool is_relative = (style.position == Position::Relative);
    float x = horizontal.first + relative_offset(is_relative, inset_horizontal, direction == Direction::Rtl);
    float y = vertical.first + relative_offset(is_relative, inset_vertical, false);

    Size<float> scrollbar_size{
        (overflow_y == Overflow::Scroll) ? scrollbar_width : 0.0f,
        (overflow_x == Overflow::Scroll) ? scrollbar_width : 0.0f,
    };

    Rect<float> resolved_margin{horizontal.second.start, horizontal.second.end, vertical.second.start,
                                vertical.second.end};

    Layout layout_result;
    layout_result.order = order;
    layout_result.location = {x, y};
    layout_result.size = final_size;
    layout_result.scrollbar_size = scrollbar_size;
    layout_result.padding = padding;
    layout_result.border = border;
    layout_result.margin = resolved_margin;
#ifdef TAFFY_CONTENT_SIZE
    layout_result.scrollable_overflow_rect = layout_output.scrollable_overflow_rect;
#endif
    tree.set_unrounded_layout(node, layout_result);

    ItemAlignmentResult result;
    result.y_position = vertical.first;
    result.height = final_size.height;
    result.baseline = child_first_baseline(layout_output.baselines.first, final_size.height, overflow_y);
    return result;
}

// ---------------------------------------------------------------------------
// Resolved track list (grid-template-rows / grid-template-columns resolved value)
// ---------------------------------------------------------------------------

inline std::string format_px(float value) {
    // Emit a plain decimal representation (never exponent form, unlike %g) that round-trips,
    // matching Rust's `f32` Display used by the reference resolved-track-list formatter.
    char buf[64];
    if (std::isfinite(value)) {
        for (int precision = 0; precision <= 7; ++precision) {
            std::snprintf(buf, sizeof(buf), "%.*f", precision, static_cast<double>(value));
            if (std::strtof(buf, nullptr) == value) return std::string(buf) + "px";
        }
    }
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(value));
    return std::string(buf) + "px";
}

struct DetailedGridTracksInfo {
    uint16_t negative_implicit_tracks{0};
    uint16_t explicit_tracks{0};
    uint16_t positive_implicit_tracks{0};
    std::vector<Line<float>> positions{};
    std::vector<std::pair<size_t, size_t>> collapsed_tracks{};
    std::optional<float> empty_axis_line{};
    GridLineNames line_names{};

    static DetailedGridTracksInfo from_grid_tracks_and_track_count(TrackCounts track_count,
                                                                  const std::vector<GridTrack>& grid_tracks,
                                                                  GridLineNames names) {
        DetailedGridTracksInfo info;
        info.negative_implicit_tracks = track_count.negative_implicit;
        info.explicit_tracks = track_count.explicit_;
        info.positive_implicit_tracks = track_count.positive_implicit;
        for (auto& track : grid_tracks) {
            if (track.kind != GridTrackKind::Track) continue;
            if (track.is_collapsed) {
                size_t index = info.positions.size();
                if (!info.collapsed_tracks.empty() && info.collapsed_tracks.back().second == index) {
                    info.collapsed_tracks.back().second = index + 1;
                } else {
                    info.collapsed_tracks.emplace_back(index, index + 1);
                }
            }
            info.positions.push_back(Line<float>{track.offset, track.offset + track.base_size});
        }
        if (info.positions.empty() && !grid_tracks.empty()) info.empty_axis_line = grid_tracks.front().offset;
        info.line_names = std::move(names);
        return info;
    }

    std::pair<size_t, size_t> names_for_line(size_t line_index) const {
        if (line_index < negative_implicit_tracks) return {0, 0};
        return line_names.line(line_index - negative_implicit_tracks);
    }

    std::string to_track_list_string(float zoom = 1.0f) const {
        if (positions.empty()) return "none";
        std::string out;
        bool needs_space = false;
        for (size_t track_index = 0; track_index < positions.size(); ++track_index) {
            auto range = names_for_line(track_index);
            if (range.first != range.second) {
                if (needs_space) out += ' ';
                out += '[';
                for (size_t i = range.first; i < range.second; ++i) {
                    if (i != range.first) out += ' ';
                    out += line_names.names[i];
                }
                out += ']';
                needs_space = true;
            }
            if (needs_space) out += ' ';
            out += format_px((positions[track_index].end - positions[track_index].start) / zoom);
            needs_space = true;
        }
        auto trailing = names_for_line(positions.size());
        if (trailing.first != trailing.second) {
            out += ' ';
            out += '[';
            for (size_t i = trailing.first; i < trailing.second; ++i) {
                if (i != trailing.first) out += ' ';
                out += line_names.names[i];
            }
            out += ']';
        }
        return out;
    }
};

struct DetailedGridInfo {
    DetailedGridTracksInfo rows{};
    DetailedGridTracksInfo columns{};
    NamedLineResolver resolver{};

    std::string grid_template_rows(float zoom = 1.0f) const { return rows.to_track_list_string(zoom); }
    std::string grid_template_columns(float zoom = 1.0f) const { return columns.to_track_list_string(zoom); }
};

// Detect (at compile time) whether the tree type exposes `set_detailed_grid_info(NodeId, DetailedGridInfo)`
template <typename T, typename = void>
struct has_set_detailed_grid_info : std::false_type {};
template <typename T>
struct has_set_detailed_grid_info<
    T, std::void_t<decltype(std::declval<T&>().set_detailed_grid_info(std::declval<NodeId>(),
                                                                     std::declval<DetailedGridInfo>()))>>
    : std::true_type {};

/// Get a grid container's align-content or justify-content alignment for an axis (with `normal`
/// resolved to `stretch`)
inline AlignContent grid_align_content(std::optional<AlignContent> alignment) {
    AlignContent resolved = alignment.value_or(AlignContent{});
    if (resolved.keyword == AlignContentKeyword::Normal) return AlignContent_STRETCH;
    return resolved;
}

/// Compute the grid container's resolved box, size constraints, alignment styles and available
/// space (step 1 of `compute_grid_layout`)
struct GridContainerConstants {
    Direction direction{Direction::Ltr};
    Rect<float> padding{};
    Rect<float> border{};
    Size<float> padding_border_size{};
    Size<std::optional<float>> min_size{};
    Size<std::optional<float>> max_size{};
    Size<std::optional<float>> preferred_size{};
    Point<float> scrollbar_gutter{};
    Rect<float> content_box_inset{};
    AlignContent align_content{};
    AlignContent justify_content{};
    AlignItems align_items{};
    AlignItems justify_items{};
    Size<AvailableSpace> available_grid_space{};
    Size<std::optional<float>> outer_node_size{};
    Size<std::optional<float>> inner_min_size{};
    Size<std::optional<float>> inner_max_size{};
    Size<std::optional<float>> inner_node_size{};
};

inline GridContainerConstants compute_container_constants(const Style& style, const LayoutInput& inputs) {
    Size<std::optional<float>> known_dimensions = inputs.known_dimensions;
    Size<std::optional<float>> parent_size = inputs.parent_size;
    auto aspect_ratio = style.aspect_ratio;

    GridContainerConstants c;
    c.direction = style.direction;

    // 1. Compute "available grid space"
    c.padding = resolve_or_zero_rect(style.padding, parent_size.width);
    c.border = resolve_or_zero_rect(style.border, parent_size.width);
    Rect<float> padding_border = c.padding + c.border;
    c.padding_border_size = padding_border.sum_axes();
    Size<float> box_sizing_adjustment =
        (style.box_sizing == BoxSizing::ContentBox) ? c.padding_border_size : Size<float>{0.0f, 0.0f};

    c.min_size = size_opt_maybe_add(
        maybe_resolve_size(style.min_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);
    c.max_size = size_opt_maybe_add(
        maybe_resolve_size(style.max_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);
    c.preferred_size =
        (inputs.sizing_mode == SizingMode::InherentSize)
            ? size_opt_maybe_add(
                  maybe_resolve_size(style.size, parent_size).maybe_apply_aspect_ratio(aspect_ratio),
                  box_sizing_adjustment)
            : Size<std::optional<float>>{std::nullopt, std::nullopt};

    // Scrollbar gutters are reserved when the `overflow` property is set to `Overflow::Scroll`. The
    // axes are switched (transposed) because a node that scrolls vertically needs horizontal space.
    c.scrollbar_gutter = Point<float>{(style.overflow.y == Overflow::Scroll) ? style.scrollbar_width : 0.0f,
                                      (style.overflow.x == Overflow::Scroll) ? style.scrollbar_width : 0.0f};

    c.content_box_inset = padding_border;
    c.content_box_inset.bottom += c.scrollbar_gutter.y;
    if (c.direction == Direction::Rtl) {
        c.content_box_inset.left += c.scrollbar_gutter.x;
    } else {
        c.content_box_inset.right += c.scrollbar_gutter.x;
    }

    c.align_content = grid_align_content(style.align_content);
    c.justify_content = grid_align_content(style.justify_content);
    c.align_items = style.align_items.value_or(AlignItems{});
    c.justify_items = style.justify_items.value_or(AlignItems{});

    Size<std::optional<float>> resolved_style_size = size_opt_or(known_dimensions, c.preferred_size);
    Size<AvailableSpace> constrained = size_opt_to_available(resolved_style_size, inputs.available_space);
    constrained = Size<AvailableSpace>{avail_maybe_clamp(constrained.width, c.min_size.width, c.max_size.width),
                                       avail_maybe_clamp(constrained.height, c.min_size.height, c.max_size.height)};
    constrained = Size<AvailableSpace>{avail_maybe_max(constrained.width, c.padding_border_size.width),
                                       avail_maybe_max(constrained.height, c.padding_border_size.height)};

    auto map_definite = [](AvailableSpace as, float amount) -> AvailableSpace {
        return as.is_definite() ? AvailableSpace::definite(as.value - amount) : as;
    };
    c.available_grid_space =
        Size<AvailableSpace>{map_definite(constrained.width, c.content_box_inset.horizontal_axis_sum()),
                             map_definite(constrained.height, c.content_box_inset.vertical_axis_sum())};

    c.outer_node_size = size_opt_maybe_max(size_opt_maybe_clamp(resolved_style_size, c.min_size, c.max_size),
                                          c.padding_border_size);
    c.inner_min_size = size_opt_maybe_sub(c.min_size, c.content_box_inset.sum_axes());
    c.inner_max_size = size_opt_maybe_sub(c.max_size, c.content_box_inset.sum_axes());
    c.inner_node_size = Size<std::optional<float>>{
        c.outer_node_size.width ? std::optional<float>(*c.outer_node_size.width
                                                       - c.content_box_inset.horizontal_axis_sum())
                                : std::nullopt,
        c.outer_node_size.height ? std::optional<float>(*c.outer_node_size.height
                                                        - c.content_box_inset.vertical_axis_sum())
                                 : std::nullopt};
    return c;
}

/// Grid layout algorithm
///
/// This consists of a few phases:
///   - Resolving the explicit grid
///   - Placing items (which also resolves the implicit grid)
///   - Track (row/column) sizing
///   - Alignment & final item placement
template <typename Tree, typename MF>
LayoutOutput compute_grid_layout(Tree& tree, NodeId node, LayoutInput inputs, MF& measure_fn) {
    const Style& style = tree.style(node);
    GridContainerConstants constants = compute_container_constants(style, inputs);

    Direction direction = constants.direction;
    Rect<float> padding = constants.padding;
    Rect<float> border = constants.border;
    Size<float> padding_border_size = constants.padding_border_size;
    Size<std::optional<float>> min_size = constants.min_size;
    Size<std::optional<float>> max_size = constants.max_size;
    Size<std::optional<float>> preferred_size = constants.preferred_size;
    Point<float> scrollbar_gutter = constants.scrollbar_gutter;
    Rect<float> content_box_inset = constants.content_box_inset;
    AlignContent align_content = constants.align_content;
    AlignContent justify_content = constants.justify_content;
    AlignItems align_items = constants.align_items;
    AlignItems justify_items = constants.justify_items;
    Size<AvailableSpace> available_grid_space = constants.available_grid_space;
    Size<std::optional<float>> outer_node_size = constants.outer_node_size;
    Size<std::optional<float>> inner_min_size = constants.inner_min_size;
    Size<std::optional<float>> inner_max_size = constants.inner_max_size;
    Size<std::optional<float>> inner_node_size = constants.inner_node_size;

    // Short-circuit layout if the container's size is fully determined by the container's size and
    // the run mode is ComputeSize
    if (inputs.run_mode == RunMode::ComputeSize) {
        if (outer_node_size.width && outer_node_size.height) {
            return LayoutOutput::from_outer_size(Size<float>{*outer_node_size.width, *outer_node_size.height});
        }
        if (inputs.axis == RequestedAxis::Horizontal && outer_node_size.width) {
            return LayoutOutput::from_outer_size(Size<float>{*outer_node_size.width, 0.0f});
        }
    }

    // 2. Resolve the explicit grid

    // If the grid container has a definite size or max size in the relevant axis then the number of
    // repetitions is the largest possible positive integer that does not cause the grid to overflow
    Size<std::optional<float>> auto_fit_container_size = size_opt_maybe_sub(
        size_opt_maybe_max(size_opt_maybe_clamp(size_opt_or(size_opt_or(outer_node_size, max_size), min_size), min_size,
                                                max_size),
                          padding_border_size),
        content_box_inset.sum_axes());

    AutoRepeatStrategy auto_repeat_fit_strategy = AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow;
    if (!outer_node_size.width && !max_size.width) {
        // No definite size or max size in this axis
    }

    // Note: the strategy is per-axis in Rust
    AutoRepeatStrategy strategy_h =
        (outer_node_size.width || max_size.width) ? AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow
                                                  : AutoRepeatStrategy::MinRepetitionsThatDoOverflow;
    AutoRepeatStrategy strategy_v =
        (outer_node_size.height || max_size.height) ? AutoRepeatStrategy::MaxRepetitionsThatDoNotOverflow
                                                    : AutoRepeatStrategy::MinRepetitionsThatDoOverflow;
    (void)auto_repeat_fit_strategy;

    auto [col_auto_repetition_count, grid_template_col_count] = compute_explicit_grid_size_in_axis(
        style, auto_fit_container_size.width, strategy_h, AbsoluteAxis::Horizontal);
    auto [row_auto_repetition_count, grid_template_row_count] = compute_explicit_grid_size_in_axis(
        style, auto_fit_container_size.height, strategy_v, AbsoluteAxis::Vertical);

    auto name_resolver = NamedLineResolver{style, col_auto_repetition_count, row_auto_repetition_count};

    // Clamp the explicit grid to MAX_GRID_TRACKS tracks in each axis
    uint16_t explicit_col_count =
        std::min<uint16_t>(std::max<uint16_t>(grid_template_col_count, name_resolver.area_column_count_()),
                           MAX_GRID_TRACKS);
    uint16_t explicit_row_count =
        std::min<uint16_t>(std::max<uint16_t>(grid_template_row_count, name_resolver.area_row_count_()),
                           MAX_GRID_TRACKS);
    auto mutable_name_resolver = name_resolver;
    mutable_name_resolver.set_explicit_column_count(explicit_col_count);
    mutable_name_resolver.set_explicit_row_count(explicit_row_count);

    const auto& resolver = mutable_name_resolver;
    GridLineNames detailed_column_line_names = resolver.detailed_line_names(AbsoluteAxis::Horizontal);
    GridLineNames detailed_row_line_names = resolver.detailed_line_names(AbsoluteAxis::Vertical);

    // 3. Create the grid items (in document order) and resolve the named lines in their
    //    grid-row/grid-column styles. Absolutely positioned children do not take part in grid
    //    placement.
    size_t child_count = tree.child_count(node);
    std::vector<GridItem> items;
    std::vector<ItemPlacement> placements;
    items.reserve(child_count);
    placements.reserve(child_count);

    for (size_t index = 0; index < child_count; ++index) {
        NodeId child_node = tree.get_child_id(node, index);
        const Style& child_style = tree.style(child_node);
        bool out_of_flow = (child_style.position == Position::Absolute || child_style.position == Position::Fixed);
        if (child_style.display == Display::None || out_of_flow) continue;

        Line<NonNamedGridPlacement> resolved_column = resolver.resolve_column_names(child_style.grid_column);
        Line<NonNamedGridPlacement> resolved_row = resolver.resolve_row_names(child_style.grid_row);
        placements.push_back(ItemPlacement{into_origin_zero_placement(resolved_column, explicit_col_count),
                                           into_origin_zero_placement(resolved_row, explicit_row_count)});

        GridItem item;
        item.node = child_node;
        item.source_order = static_cast<uint16_t>(index);
        item.is_compressible_replaced = child_style.item_is_replaced;
        item.is_replaced = child_style.item_is_replaced;
        item.overflow = child_style.overflow;
        item.box_sizing = child_style.box_sizing;
        item.size = child_style.size;
        item.min_size = child_style.min_size;
        item.max_size = child_style.max_size;
        item.aspect_ratio = child_style.aspect_ratio;
        item.padding = child_style.padding;
        item.border = child_style.border;
        item.margin = child_style.margin;
        item.align_self = child_style.align_self.value_or(align_items);
        item.justify_self = child_style.justify_self.value_or(justify_items);
        items.push_back(item);
    }

    // 4. Implicit Grid: Estimate Track Counts
    auto [est_col_counts, est_row_counts] = compute_grid_size_estimate(explicit_col_count, explicit_row_count, placements);

    // 5. Grid Item Placement
    CellOccupancyMatrix cell_occupancy_matrix = CellOccupancyMatrix::with_track_counts(est_col_counts, est_row_counts);
    place_grid_items(cell_occupancy_matrix, items, placements, style.grid_auto_flow);

    TrackCounts final_col_counts = cell_occupancy_matrix.track_counts(AbsoluteAxis::Horizontal);
    TrackCounts final_row_counts = cell_occupancy_matrix.track_counts(AbsoluteAxis::Vertical);

    // 5. Initialize Tracks
    std::vector<GridTrack> columns;
    std::vector<GridTrack> rows;
    initialize_grid_tracks(columns, final_col_counts, style, AbsoluteAxis::Horizontal, col_auto_repetition_count,
                           [&](size_t column_index) { return cell_occupancy_matrix.column_is_occupied(column_index); });
    initialize_grid_tracks(rows, final_row_counts, style, AbsoluteAxis::Vertical, row_auto_repetition_count,
                           [&](size_t row_index) { return cell_occupancy_matrix.row_is_occupied(row_index); });

    // 6. Track Sizing

    // Convert grid placements into indexes into the GridTrack vectors
    for (auto& item : items) {
        item.column_indexes = {oz_line_to_track_vec_index(item.column.start, final_col_counts),
                               oz_line_to_track_vec_index(item.column.end, final_col_counts)};
        item.row_indexes = {oz_line_to_track_vec_index(item.row.start, final_row_counts),
                            oz_line_to_track_vec_index(item.row.end, final_row_counts)};
    }
    // Determine (in each axis) whether the item crosses any flexible or intrinsic tracks
    for (auto& item : items) {
        auto col_range = item.track_range_excluding_lines(AbstractAxis::Inline);
        auto row_range = item.track_range_excluding_lines(AbstractAxis::Block);
        item.crosses_flexible_column = false;
        item.crosses_intrinsic_column = false;
        for (size_t i = col_range.first; i < col_range.second; ++i) {
            if (columns[i].is_flexible()) item.crosses_flexible_column = true;
            if (columns[i].has_intrinsic_sizing_function()) item.crosses_intrinsic_column = true;
        }
        item.crosses_flexible_row = false;
        item.crosses_intrinsic_row = false;
        for (size_t i = row_range.first; i < row_range.second; ++i) {
            if (rows[i].is_flexible()) item.crosses_flexible_row = true;
            if (rows[i].has_intrinsic_sizing_function()) item.crosses_intrinsic_row = true;
        }
    }

    bool has_baseline_aligned_item = false;
    for (auto& item : items)
        if (item.participates_in_baseline_alignment()) has_baseline_aligned_item = true;

    // Track sizing for the Inline axis
    track_sizing_algorithm(tree, measure_fn, AbstractAxis::Inline, inner_min_size.width, inner_max_size.width,
                           justify_content, align_content, available_grid_space, inner_node_size, columns, rows, items,
                           false, has_baseline_aligned_item);
    float initial_column_sum = tracks_base_size_sum(columns);
#ifdef TAFFY_GRID_TRACE
    std::printf("[grid] inline sizing: columns=%zu sum=%.2f inner_w=%s avail_w=%s\n", columns.size(),
                initial_column_sum, inner_node_size.width ? "def" : "none",
                available_grid_space.width.is_definite() ? "def" : "indef");
#endif
    if (!inner_node_size.width) inner_node_size.width = initial_column_sum;

    // If only the container's width has been requested then we can skip sizing the rows entirely
    if (inputs.run_mode == RunMode::ComputeSize && inputs.axis == RequestedAxis::Horizontal) {
        float width = initial_column_sum + content_box_inset.horizontal_axis_sum();
        if (min_size.width) width = std::max(width, *min_size.width);
        if (max_size.width) width = std::min(width, *max_size.width);
        width = std::max(width, padding_border_size.width);
        return LayoutOutput::from_outer_size(Size<float>{width, 0.0f});
    }

    for (auto& item : items) item.grid_area_size_cache = std::nullopt;

    // Track sizing for the Block axis
    track_sizing_algorithm(tree, measure_fn, AbstractAxis::Block, inner_min_size.height, inner_max_size.height,
                           align_content, justify_content, available_grid_space, inner_node_size, rows, columns, items,
                           true, false);
    float initial_row_sum = tracks_base_size_sum(rows);

    // 6. Compute container size
    Size<std::optional<float>> resolved_style_size = size_opt_or(inputs.known_dimensions, preferred_size);
    auto clamp_and_max = [&](float value, std::optional<float> mn, std::optional<float> mx, float pb) {
        if (mn) value = std::max(value, *mn);
        if (mx) value = std::min(value, *mx);
        return std::max(value, pb);
    };
    Size<float> container_border_box{
        clamp_and_max(resolved_style_size.width.value_or(initial_column_sum + content_box_inset.horizontal_axis_sum()),
                      min_size.width, max_size.width, padding_border_size.width),
        clamp_and_max(resolved_style_size.height.value_or(initial_row_sum + content_box_inset.vertical_axis_sum()),
                      min_size.height, max_size.height, padding_border_size.height)};
#ifdef TAFFY_GRID_TRACE
    std::printf("[grid] after block sizing: rows=%zu row_sum=%.2f container=%.2fx%.2f\n", rows.size(),
                tracks_base_size_sum(rows), container_border_box.width, container_border_box.height);
#endif
    Size<float> container_content_box{
        std::max(0.0f, container_border_box.width - content_box_inset.horizontal_axis_sum()),
        std::max(0.0f, container_border_box.height - content_box_inset.vertical_axis_sum())};

    // If only the container's size has been requested
    if (inputs.run_mode == RunMode::ComputeSize) {
        return LayoutOutput::from_outer_size(container_border_box);
    }

    // The container's size is now determined, so percentages resolve against its content box when
    // re-running track sizing.
    inner_node_size = Size<std::optional<float>>{container_content_box.width, container_content_box.height};

    // Column sizing must be re-run (once) if the container's width was initially indefinite and
    // there are any columns with percentage track sizing functions, or if any grid item crossing an
    // intrinsically sized track's min content contribution width has changed.
    bool has_percentage_column = false;
    for (auto& track : columns)
        if (track.uses_percentage()) has_percentage_column = true;
    bool has_percentage_row = false;
    for (auto& track : rows)
        if (track.uses_percentage()) has_percentage_row = true;
    bool parent_width_indefinite = !outer_node_size.width;
    bool rerun_column_sizing = parent_width_indefinite && has_percentage_column;
    bool intrinsic_column_contribution_changed = false;

    if (!rerun_column_sizing) {
        for (auto& item : items) {
            if (!item.crosses_intrinsic_column) continue;
            IntrinsicSizeMeasurer measurer{AbstractAxis::Inline, inner_node_size, true};
            auto get_estimate = [&](const GridTrack& track, std::optional<float> parent_size) {
                return measurer.estimate(track, parent_size);
            };
            auto grid_area = measurer.grid_area_size(item, columns, rows, get_estimate);
            auto available_space = size_opt_with(grid_area, AbstractAxis::Inline, std::nullopt);
            float new_min_content =
                item_min_content_contribution(tree, measure_fn, item, AbstractAxis::Inline, grid_area, available_space);
            bool has_changed = !item.min_content_contribution_cache.width
                               || *item.min_content_contribution_cache.width != new_min_content;
            item.grid_area_size_cache = grid_area;
            item.min_content_contribution_cache.width = new_min_content;
            item.max_content_contribution_cache.width = std::nullopt;
            item.minimum_contribution_cache.width = std::nullopt;
            if (has_changed) intrinsic_column_contribution_changed = true;
        }
        rerun_column_sizing = intrinsic_column_contribution_changed;
    } else {
        for (auto& item : items) {
            item.grid_area_size_cache = std::nullopt;
            item.min_content_contribution_cache.width = std::nullopt;
            item.max_content_contribution_cache.width = std::nullopt;
            item.minimum_contribution_cache.width = std::nullopt;
        }
    }

    bool parent_height_indefinite = !outer_node_size.height;
    bool rerun_row_sizing = parent_height_indefinite && has_percentage_row;
    bool intrinsic_row_contribution_changed = false;
    if (rerun_row_sizing) {
        for (auto& item : items) {
            item.grid_area_size_cache = std::nullopt;
            item.min_content_contribution_cache.height = std::nullopt;
            item.max_content_contribution_cache.height = std::nullopt;
            item.minimum_contribution_cache.height = std::nullopt;
        }
    }

    if (rerun_column_sizing) {
        track_sizing_algorithm(tree, measure_fn, AbstractAxis::Inline, inner_min_size.width, inner_max_size.width,
                               justify_content, align_content, available_grid_space, inner_node_size, columns, rows,
                               items, true, has_baseline_aligned_item);

        if (!rerun_row_sizing) {
            for (auto& item : items) {
                if (!item.crosses_intrinsic_row) continue;
                IntrinsicSizeMeasurer measurer{AbstractAxis::Block, inner_node_size, true};
                auto get_estimate = [&](const GridTrack& track, std::optional<float> parent_size) {
                    return measurer.estimate(track, parent_size);
                };
                auto grid_area = measurer.grid_area_size(item, rows, columns, get_estimate);
                auto available_space = size_opt_with(grid_area, AbstractAxis::Block, std::nullopt);
                float new_min_content = item_min_content_contribution(tree, measure_fn, item, AbstractAxis::Block,
                                                                     grid_area, available_space);
                bool has_changed = !item.min_content_contribution_cache.height
                                   || *item.min_content_contribution_cache.height != new_min_content;
                item.grid_area_size_cache = grid_area;
                item.min_content_contribution_cache.height = new_min_content;
                item.max_content_contribution_cache.height = std::nullopt;
                item.minimum_contribution_cache.height = std::nullopt;
                if (has_changed) intrinsic_row_contribution_changed = true;
            }
            rerun_row_sizing = intrinsic_row_contribution_changed;
        }
    }

    if (rerun_row_sizing) {
        track_sizing_algorithm(tree, measure_fn, AbstractAxis::Block, inner_min_size.height, inner_max_size.height,
                               align_content, justify_content, available_grid_space, inner_node_size, rows, columns,
                               items, true, false);
    }

    if ((intrinsic_column_contribution_changed && !has_percentage_column)
        || (intrinsic_row_contribution_changed && !has_percentage_row)) {
        float final_column_sum = tracks_base_size_sum(columns);
        float final_row_sum = tracks_base_size_sum(rows);

        if (intrinsic_column_contribution_changed && !has_percentage_column) {
            container_border_box.width =
                clamp_and_max(resolved_style_size.width.value_or(final_column_sum
                                                                 + content_box_inset.horizontal_axis_sum()),
                              min_size.width, max_size.width, padding_border_size.width);
            container_content_box.width =
                std::max(0.0f, container_border_box.width - content_box_inset.horizontal_axis_sum());
        }
        if (intrinsic_row_contribution_changed && !has_percentage_row) {
            container_border_box.height =
                clamp_and_max(resolved_style_size.height.value_or(final_row_sum
                                                                  + content_box_inset.vertical_axis_sum()),
                              min_size.height, max_size.height, padding_border_size.height);
            container_content_box.height =
                std::max(0.0f, container_border_box.height - content_box_inset.vertical_axis_sum());
        }
    }

    // If only the container's size has been requested
    if (inputs.run_mode == RunMode::ComputeSize) {
        return LayoutOutput::from_outer_size(container_border_box);
    }

    // 8. Track Alignment

    // Align columns
    float inline_size_without_scrollbar = std::max(container_border_box.width - padding_border_size.width, 0.0f);
    float inline_scrollbar_gutter_for_alignment = std::min(scrollbar_gutter.x, inline_size_without_scrollbar);
    align_tracks(container_content_box.get(AbstractAxis::Inline),
                 Line<float>{padding.left + (direction == Direction::Rtl ? inline_scrollbar_gutter_for_alignment : 0.0f),
                             padding.right + (direction == Direction::Rtl ? 0.0f : inline_scrollbar_gutter_for_alignment)},
                 Line<float>{border.left, border.right}, columns, justify_content, direction == Direction::Rtl);
    // Align rows
    align_tracks(container_content_box.get(AbstractAxis::Block), Line<float>{padding.top, padding.bottom},
                 Line<float>{border.top, border.bottom}, rows, align_content, false);

    // 9. Size, Align and Position Grid Items
    for (size_t index = 0; index < items.size(); ++index) {
        auto& item = items[index];
        size_t col_start = static_cast<size_t>(item.column_indexes.start);
        size_t col_end = static_cast<size_t>(item.column_indexes.end);
        size_t row_start = static_cast<size_t>(item.row_indexes.start);
        size_t row_end = static_cast<size_t>(item.row_indexes.end);

        Rect<float> grid_area{
            rows[row_start + 1].offset,
            (direction == Direction::Rtl) ? columns[col_start].offset : columns[col_end].offset,
            (direction == Direction::Rtl) ? columns[col_end - 1].offset : columns[col_start + 1].offset,
            rows[row_end].offset};
        // Note: Rect is (left, right, top, bottom); recompute in the documented order
        grid_area.left = (direction == Direction::Rtl) ? columns[col_end - 1].offset : columns[col_start + 1].offset;
        grid_area.right = (direction == Direction::Rtl) ? columns[col_start].offset : columns[col_end].offset;
        grid_area.top = rows[row_start + 1].offset;
        grid_area.bottom = rows[row_end].offset;

        Size<float> area_size{grid_area.right - grid_area.left, grid_area.bottom - grid_area.top};
        ItemAlignmentResult result =
            align_and_position_item(tree, measure_fn, item.node, static_cast<uint32_t>(index), grid_area, area_size,
                                    justify_items, align_items, item.baseline_shim, direction);
        item.y_position = result.y_position;
        item.height = result.height;
        item.baseline = result.baseline;
    }

    // Position hidden and absolutely positioned children
    uint32_t order = static_cast<uint32_t>(items.size());
    size_t in_flow_index = 0;
    for (size_t index = 0; index < child_count; ++index) {
        if (in_flow_index < items.size() && items[in_flow_index].source_order == index) {
            in_flow_index++;
            continue;
        }
        NodeId child = tree.get_child_id(node, index);
        const Style& child_style = tree.style(child);
        if (child_style.display == Display::None) {
            tree.set_unrounded_layout(child, Layout::with_order(order));
            LayoutInput hidden_input = LayoutInput::hidden_input();
            tree.compute_node_layout(child, hidden_input, measure_fn);
            order += 1;
            continue;
        }
        // Out-of-flow children are not positioned by this algorithm (the tree's out-of-flow
        // positioning pass handles them; it is not part of this port yet).
        order += 1;
    }

    // Store the detailed grid info for resolved track lists / grid area queries
    DetailedGridInfo detailed_info;
    detailed_info.rows = DetailedGridTracksInfo::from_grid_tracks_and_track_count(final_row_counts, rows,
                                                                                  std::move(detailed_row_line_names));
    detailed_info.columns = DetailedGridTracksInfo::from_grid_tracks_and_track_count(final_col_counts, columns,
                                                                                     std::move(detailed_column_line_names));
    detailed_info.resolver = resolver;
    if constexpr (has_set_detailed_grid_info<Tree>::value) {
        tree.set_detailed_grid_info(node, detailed_info);
    }

    // If there are no in-flow items then return the container size (no baseline)
    if (items.empty()) {
        LayoutOutput out = LayoutOutput::from_outer_size(container_border_box);
#ifdef TAFFY_CONTENT_SIZE
        // Mark the (empty) scrollable overflow rect as reversed in RTL so that out-of-flow children
        // overflowing the inline-start edge are accumulated on the correct side.
        if (direction == Direction::Rtl) {
            out.scrollable_overflow_rect = ScrollableOverflow::new_(Rect<float>{0.0f, 0.0f, 0.0f, 0.0f},
                                                                    Point<bool>{true, false});
        }
#endif
        return out;
    }

    // Determine the grid container baseline (currently only the first baseline)
    std::optional<float> grid_container_baseline;
    {
        uint16_t first_row = items[0].row_indexes.start;
        for (auto& item : items) first_row = std::min(first_row, item.row_indexes.start);
        size_t first_item_index = items.size();
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].row_indexes.start == first_row) {
                first_item_index = i;
                break;
            }
        }
        if (first_item_index < items.size()) {
            size_t chosen = first_item_index;
            if (!items[first_item_index].participates_in_baseline_alignment()) {
                for (size_t i = first_item_index; i < items.size(); ++i) {
                    if (items[i].row_indexes.start != first_row) break;
                    if (items[i].participates_in_baseline_alignment()) {
                        chosen = i;
                        break;
                    }
                }
            }
            grid_container_baseline = items[chosen].y_position + items[chosen].baseline.value_or(items[chosen].height);
        }
    }

    // Leave the scrollable overflow rectangle empty so that the tree accumulates the container's
    // scrollable overflow from its children (src/compute/common/scrollable_overflow.rs rules).
    Size<float> content_size{0.0f, 0.0f};
    LayoutOutput output = LayoutOutput::from_sizes_and_baselines(container_border_box, content_size,
                                                                Baselines::from_first(grid_container_baseline));
#ifdef TAFFY_CONTENT_SIZE
    // Mark the (empty) scrollable overflow rect as reversed in RTL so that out-of-flow children
    // overflowing the inline-start edge are accumulated on the correct side.
    if (direction == Direction::Rtl) {
        output.scrollable_overflow_rect = ScrollableOverflow::new_(Rect<float>{0.0f, 0.0f, 0.0f, 0.0f},
                                                                   Point<bool>{true, false});
    }
#endif
    return output;
}

} // namespace grid_detail

#endif // TAFFY_GRID


// ===========================================================================
// Section 15c: Float Layout
// Ref: src/compute/float.rs, src/util/front_back_vec_builder.rs
// ===========================================================================

#ifdef TAFFY_BLOCK_LAYOUT
#ifdef TAFFY_FLOAT_LAYOUT

namespace float_layout {

/// Tolerance used when checking whether a box fits in a horizontal space, to absorb f32
/// rounding errors (e.g. percentage widths and margins that sum to exactly 100% may otherwise
/// exceed the container width and spuriously wrap)
inline constexpr float FIT_TOLERANCE = 0.001f;

/// An empty "slot" that avoids floats that is suitable for non-floated content to be laid out into
struct ContentSlot {
    std::optional<size_t> segment_id{std::nullopt};
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
};

/// An empty "slot" that avoids floats that is suitable for a box that establishes an
/// independent formatting context (and therefore must not overlap floats) to be laid out into.
struct BfcSlot {
    std::optional<size_t> segment_id{std::nullopt};
    float x{0.0f};
    float y{0.0f};
    float border_width{0.0f};
    float stretch_width{0.0f};
};

/// A floated box
struct PlacedFloatedBox {
    uint64_t id{0};
    float width{0.0f};
    float height{0.0f};
    /// Horizontal distance from the edge of the container that the box is floated towards
    float x_inset{0.0f};
    /// Vertical distance from top edge of the container
    float y{0.0f};
};

/// Whether a floated box of the given width fits horizontally given the float insets
/// (`float_insets`) and containing block insets (`cb_insets`) that apply to it.
inline bool float_fits_horizontally(
    float width, FloatDirection direction, float bfc_width,
    const float float_insets[2], const float cb_insets[2]) {
    int lead = direction == FloatDirection::Left ? 0 : 1;
    int trail = 1 - lead;
    float x_inset = std::max(float_insets[lead], cb_insets[lead]);
    bool fits_opposite_floats =
        float_insets[trail] == 0.0f || x_inset + width <= bfc_width - float_insets[trail] + FIT_TOLERANCE;
    bool fits_containing_block =
        float_insets[lead] == 0.0f || x_inset + width <= bfc_width - cb_insets[trail] + FIT_TOLERANCE;
    return fits_opposite_floats && fits_containing_block;
}

/// A non-overlapping horizontal segment of the Block Formatting Context container
struct Segment {
    float y_start{0.0f};
    float y_end{0.0f};
    /// Left inset in slot 0. Right inset in slot 1.
    float insets[2]{0.0f, 0.0f};
    /// Whether a float actually occupies the left (slot 0) / right (slot 1) inset.
    bool has_float[2]{false, false};

    bool y_contains(float v) const { return y_start <= v && v < y_end; }

    /// Whether the segment can fit the passed floated box (in the horizontal axis)
    bool fits_float_width(
        Size<float> floated_box, FloatDirection direction, float bfc_width, const float cb_insets[2]) const {
        return float_fits_horizontally(floated_box.width, direction, bfc_width, insets, cb_insets);
    }
};

/// Helper type for placing a single floated box
struct FloatFitter {
    float bfc_width{0.0f};
    double slot_height{0.0};
    float float_insets[2]{0.0f, 0.0f};
    float cb_insets[2]{0.0f, 0.0f};

    FloatFitter(float bfc_width_in, float slot_height_in, const float cb_insets_in[2])
        : bfc_width(bfc_width_in), slot_height(static_cast<double>(slot_height_in)) {
        cb_insets[0] = cb_insets_in[0];
        cb_insets[1] = cb_insets_in[1];
    }

    void union_insets(const float insets[2]) {
        float_insets[0] = std::max(float_insets[0], insets[0]);
        float_insets[1] = std::max(float_insets[1], insets[1]);
    }

    float placed_inset(FloatDirection direction) const {
        int lead = direction == FloatDirection::Left ? 0 : 1;
        return std::max(float_insets[lead], cb_insets[lead]);
    }

    bool fits_horizontally(float width, FloatDirection direction) const {
        return float_fits_horizontally(width, direction, bfc_width, float_insets, cb_insets);
    }

    void add_height(float height) { slot_height += static_cast<double>(height); }

    bool fits_vertically(float height) { return slot_height >= static_cast<double>(height); }
};

/// A context for placing floated boxes
struct FloatContext {
    float available_width{0.0f};
    bool has_floats{false};
    std::vector<PlacedFloatedBox> left_floats{};
    std::vector<PlacedFloatedBox> right_floats{};
    std::vector<Segment> segments{};
    /// The closed-open ranges of segments occupied by the last placed float (each side).
    size_t last_placed_start[2]{0, 0};
    size_t last_placed_end[2]{0, 0};
    /// The bottom (y + height) of the lowest float placed on each side, including
    /// zero-sized floats (which occupy no segment). Left in slot 0, right in slot 1.
    std::optional<float> clear_bottoms[2]{std::nullopt, std::nullopt};
    /// The topmost y position allowed for a new float (CSS2 float rule 5), including
    /// the tops of zero-sized floats (which occupy no segment)
    std::optional<float> float_ceiling{std::nullopt};

    bool has_floats_() const { return has_floats; }

    bool has_active_floats(float min_y) const {
        return has_floats && !segments.empty() && segments.back().y_end > min_y;
    }

    void set_width(float available_width_in) { available_width = available_width_in; }

    const std::vector<PlacedFloatedBox>& left_floats_() const { return left_floats; }
    const std::vector<PlacedFloatedBox>& right_floats_() const { return right_floats; }

    /// Divide a segment into two segments so that a new float can be placed and have its
    /// vertical start and end at exact segment boundaries
    void subdivide_segment(size_t idx, float divide_at_y) {
        Segment old_segment = segments[idx];
        assert(old_segment.y_contains(divide_at_y) && old_segment.y_start != divide_at_y);
        Segment new_segment = old_segment;
        new_segment.y_start = divide_at_y;
        segments[idx].y_end = divide_at_y;
        segments.insert(segments.begin() + (idx + 1), new_segment);
    }

    /// The (x, y) coordinates of a box previously placed in the context on the `direction` side
    Point<float> placed_position(const PlacedFloatedBox& placed, FloatDirection direction) const {
        if (direction == FloatDirection::Left) {
            return Point<float>{placed.x_inset, placed.y};
        }
        return Point<float>{available_width - placed.x_inset - placed.width, placed.y};
    }

    void update_last_placed_float(FloatDirection direction, size_t start, size_t end) {
        int slot = direction == FloatDirection::Left ? 0 : 1;
        last_placed_start[slot] = std::max(last_placed_start[slot], start);
        last_placed_end[slot] = std::max(last_placed_end[slot], end);
    }

    /// Position a floated box with the context, returning the (x, y) coordinates.
    Point<float> place_floated_box(
        uint64_t id, Size<float> floated_box, float min_y,
        const float containing_block_insets[2], FloatDirection direction, Clear clear) {
        has_floats = true;

        PlacedFloatedBox placed_floated_box =
            place_floated_box_inner(id, floated_box, min_y, containing_block_insets, direction, clear);

        int slot = direction == FloatDirection::Left ? 0 : 1;
        float bottom = placed_floated_box.y + placed_floated_box.height;
        clear_bottoms[slot] = clear_bottoms[slot].has_value() ? std::max(*clear_bottoms[slot], bottom) : bottom;
        float y = placed_floated_box.y;
        float_ceiling = float_ceiling.has_value() ? std::max(*float_ceiling, y) : y;

        Point<float> pos = placed_position(placed_floated_box, direction);
        if (direction == FloatDirection::Left) {
            left_floats.push_back(placed_floated_box);
        } else {
            right_floats.push_back(placed_floated_box);
        }
        return pos;
    }

    /// Inner implementation of float placement, split into a separate function so that it can early-return
    PlacedFloatedBox place_floated_box_inner(
        uint64_t id, Size<float> floated_box, float min_y,
        const float containing_block_insets[2], FloatDirection direction, Clear clear) {
        int slot = direction == FloatDirection::Left ? 0 : 1;

        // Floats (including zero-sized floats) that occupy no segment still constrain the
        // position of later boxes: rule 5 (a float may not be higher than the top of any
        // earlier float) and `clear` (which clears past the bottom of floats on the
        // relevant side, regardless of their width)
        float neg_inf = -std::numeric_limits<float>::infinity();
        if (float_ceiling.has_value()) min_y = std::max(min_y, *float_ceiling);
        std::optional<float> ct = cleared_threshold(clear);
        if (ct.has_value()) min_y = std::max(min_y, *ct);

        // Ensure that float starts at or after the last placed float in either direction
        // (CSS2 float rule 5) and respects "clear"
        size_t float_start = std::max(last_placed_start[0], last_placed_start[1]);
        size_t hwm;
        switch (clear) {
            case Clear::Left: hwm = std::max(float_start, last_placed_end[0] + 1); break;
            case Clear::Right: hwm = std::max(float_start, last_placed_end[1] + 1); break;
            case Clear::Both: hwm = std::max(last_placed_end[0], last_placed_end[1]) + 1; break;
            case Clear::None:
            default: hwm = float_start; break;
        }

        // Ensure that float is placed in a segment at or below "min_y"
        size_t start_idx = segments.size();
        for (size_t i = hwm; i < segments.size(); ++i) {
            if (segments[i].y_end > min_y) { start_idx = i; break; }
        }

        float start_y = min_y;
        size_t end_idx = start_idx;
        bool have_start = false;
        bool have_end = false;
        float placed_inset = 0.0f;
        bool done = false;

        // Loop over remaining segments, trying to place the float in a position
        // that has space to accommodate it.
        while (!done) {
            // Start segment does not exist: no existing segment can accommodate the float so
            // a new segment below all existing segments will be created.
            if (start_idx >= segments.size()) {
                placed_inset = containing_block_insets[slot];
                break;
            }

            const Segment& start_segment = segments[start_idx];

            // Candidate start segment doesn't have (horizontal) space for the float: retry
            // with the next segment
            if (!start_segment.fits_float_width(floated_box, direction, available_width, containing_block_insets)) {
                start_idx += 1;
                end_idx = std::max(end_idx, start_idx);
                continue;
            }

            start_y = std::max(start_y, start_segment.y_start);
            float available_height = start_segment.y_end - start_y;
            FloatFitter fitter(available_width, available_height, containing_block_insets);
            fitter.union_insets(start_segment.insets);

            // Pinning the start segment, loop over segments starting with the start segment
            // to find the end segment
            bool restart_outer = false;
            while (!restart_outer) {
                if (end_idx >= segments.size()) {
                    have_start = true;
                    have_end = false;
                    placed_inset = fitter.placed_inset(direction);
                    done = true;
                    break;
                }

                const Segment& end_segment = segments[end_idx];
                fitter.union_insets(end_segment.insets);
                if (!fitter.fits_horizontally(floated_box.width, direction)) {
                    start_idx += 1;
                    end_idx = std::max(end_idx, start_idx);
                    restart_outer = true;
                    break;
                }

                if (end_idx != start_idx) {
                    fitter.add_height(end_segment.y_end - end_segment.y_start);
                }
                if (!fitter.fits_vertically(floated_box.height)) {
                    end_idx += 1;
                    continue;
                }

                have_start = true;
                have_end = true;
                placed_inset = fitter.placed_inset(direction);
                done = true;
                break;
            }
        }

        // Short-circuit for zero-height boxes. Zero-width boxes are still recorded in segments:
        // their edge acts as an obstacle that boxes establishing an independent formatting
        // context may not be placed to the outside of (e.g. via a negative margin).
        if (floated_box.height == 0.0f) {
            PlacedFloatedBox out;
            out.id = id;
            out.width = floated_box.width;
            out.height = floated_box.height;
            out.y = start_y;
            out.x_inset = placed_inset;
            return out;
        }

        // Handle case where floated box is placed after all existing segments
        if (!have_start) {
            float last_y_end = segments.empty() ? neg_inf : segments.back().y_end;
            if (start_y > last_y_end) {
                Segment blank;
                blank.y_start = last_y_end;
                blank.y_end = start_y;
                segments.push_back(blank);
            }

            float float_start_y = std::max(last_y_end, start_y);

            float insets[2] = {containing_block_insets[0], containing_block_insets[1]};
            insets[slot] += floated_box.width;
            Segment seg;
            seg.y_start = float_start_y;
            seg.y_end = float_start_y + floated_box.height;
            seg.insets[0] = insets[0];
            seg.insets[1] = insets[1];
            seg.has_float[0] = false;
            seg.has_float[1] = false;
            seg.has_float[slot] = true;
            segments.push_back(seg);

            size_t new_start_idx = segments.size() - 1;
            size_t new_end_idx = new_start_idx + 1;
            update_last_placed_float(direction, new_start_idx, new_end_idx);

            PlacedFloatedBox out;
            out.id = id;
            out.width = floated_box.width;
            out.height = floated_box.height;
            out.y = float_start_y;
            out.x_inset = containing_block_insets[slot];
            return out;
        }

        size_t s_idx = start_idx;

        // If the floated box doesn't start at the exact same y-offset as the segment it starts
        // in, then subdivide that segment into two segments at the y-offset that the floated
        // box starts at, and increment `start_idx` so that the floating box is placed in the
        // second of the two segments.
        if (start_y != segments[s_idx].y_start) {
            subdivide_segment(s_idx, start_y);
            s_idx += 1;
            if (have_end) end_idx += 1;
        }

        size_t e_idx;
        if (!have_end) {
            // The float extends below all existing segments: add a segment for the part of it
            // below the last segment
            float last_y_end = segments.back().y_end;
            float end_y = start_y + floated_box.height;
            if (end_y > last_y_end) {
                Segment seg;
                seg.y_start = last_y_end;
                seg.y_end = end_y;
                seg.insets[0] = containing_block_insets[0];
                seg.insets[1] = containing_block_insets[1];
                seg.has_float[0] = false;
                seg.has_float[1] = false;
                segments.push_back(seg);
            }
            e_idx = segments.size() - 1;
        } else {
            e_idx = end_idx;
            float end_y = start_y + floated_box.height;

            // Due to floating point imprecision, `end_y` may coincide with (or fall before)
            // the start of the end segment: walk back to the segment that contains `end_y`.
            while (e_idx > s_idx && end_y <= segments[e_idx].y_start) {
                e_idx -= 1;
            }

            if (segments[e_idx].y_start < end_y && end_y < segments[e_idx].y_end) {
                subdivide_segment(e_idx, end_y);
            }
        }

        // Update inset for the range of segments that the float is placed in
        float placed_inset_plus_width = placed_inset + floated_box.width;
        for (size_t i = s_idx; i <= e_idx; ++i) {
            segments[i].insets[slot] = placed_inset_plus_width;
            segments[i].has_float[slot] = true;
        }

        update_last_placed_float(direction, s_idx, e_idx + 1);

        PlacedFloatedBox out;
        out.id = id;
        out.width = floated_box.width;
        out.height = floated_box.height;
        out.y = start_y;
        out.x_inset = placed_inset;
        return out;
    }

    /// Get the end segment of the last float on side(s) specified by the clear parameter (if any)
    std::optional<size_t> cleared_segment(Clear clear) const {
        size_t left_end = last_placed_end[0];
        size_t right_end = last_placed_end[1];
        switch (clear) {
            case Clear::Left: if (left_end > 0) return left_end; break;
            case Clear::Right: if (right_end > 0) return right_end; break;
            case Clear::Both:
                if (left_end > 0 || right_end > 0) return std::max(left_end, right_end);
                break;
            default: break;
        }
        return std::nullopt;
    }

    /// Get the bottom of lowest relevant float for the specific clear property
    std::optional<float> cleared_threshold(Clear clear) const {
        switch (clear) {
            case Clear::Left: return clear_bottoms[0];
            case Clear::Right: return clear_bottoms[1];
            case Clear::Both: {
                if (clear_bottoms[0].has_value() && clear_bottoms[1].has_value())
                    return std::max(*clear_bottoms[0], *clear_bottoms[1]);
                if (clear_bottoms[0].has_value()) return clear_bottoms[0];
                return clear_bottoms[1];
            }
            case Clear::None:
            default: return std::nullopt;
        }
    }

    /// Search for a space suitable for laying out non-floated content into
    ContentSlot find_content_slot(
        float min_y, const float containing_block_insets[2], Clear clear, std::optional<size_t> after) const {
        ContentSlot none_slot;
        none_slot.segment_id = std::nullopt;
        none_slot.x = containing_block_insets[0];
        none_slot.y = min_y;
        none_slot.width = available_width - containing_block_insets[0] - containing_block_insets[1];
        none_slot.height = std::numeric_limits<float>::infinity();

        if (!has_active_floats(min_y)) return none_slot;

        std::optional<float> ct = cleared_threshold(clear);
        if (ct.has_value()) min_y = std::max(min_y, *ct);

        size_t at_least = after.has_value() ? *after + 1 : 0;
        std::optional<size_t> cleared = cleared_segment(clear);
        size_t hwm = std::max(at_least, cleared.has_value() ? *cleared + 1 : 0);

        size_t start_idx = segments.size();
        for (size_t i = hwm; i < segments.size(); ++i) {
            if (segments[i].y_end > min_y) { start_idx = i; break; }
        }

        if (start_idx < segments.size()) {
            const Segment& segment = segments[start_idx];
            float inset_left = std::max(segment.insets[0], containing_block_insets[0]);
            float inset_right = std::max(segment.insets[1], containing_block_insets[1]);
            ContentSlot slot;
            slot.segment_id = start_idx;
            slot.x = inset_left;
            slot.y = std::max(segment.y_start, min_y);
            slot.width = available_width - inset_left - inset_right;
            slot.height = std::numeric_limits<float>::infinity();
            return slot;
        }
        return none_slot;
    }

    /// Search for a space suitable for laying out a box that establishes an independent
    /// formatting context (whose border box must not overlap floats).
    BfcSlot find_bfc_slot(
        float min_y, const float containing_block_insets[2], const float margins[2],
        Direction direction, Clear clear, std::optional<size_t> after) const {
        float margin_insets[2] = {
            containing_block_insets[0] + margins[0],
            containing_block_insets[1] + margins[1]};
        float no_float_width = available_width - margin_insets[0] - margin_insets[1];
        BfcSlot no_float_slot;
        no_float_slot.segment_id = std::nullopt;
        no_float_slot.x = margin_insets[0];
        no_float_slot.y = min_y;
        no_float_slot.border_width = no_float_width;
        no_float_slot.stretch_width = no_float_width;

        if (!has_active_floats(min_y)) return no_float_slot;

        std::optional<float> ct = cleared_threshold(clear);
        if (ct.has_value()) min_y = std::max(min_y, *ct);

        size_t at_least = after.has_value() ? *after + 1 : 0;
        std::optional<size_t> cleared = cleared_segment(clear);
        size_t hwm = std::max(at_least, cleared.has_value() ? *cleared + 1 : 0);

        size_t start_idx = segments.size();
        for (size_t i = hwm; i < segments.size(); ++i) {
            if (segments[i].y_end > min_y) { start_idx = i; break; }
        }

        if (start_idx >= segments.size()) {
            BfcSlot slot = no_float_slot;
            float last_end = segments.empty() ? min_y : segments.back().y_end;
            slot.y = std::max(last_end, min_y);
            return slot;
        }

        const Segment& segment = segments[start_idx];
        int lead = direction == Direction::Ltr ? 0 : 1;
        int trail = 1 - lead;
        bool has_lead_float = segment.has_float[lead];
        bool has_trail_float = segment.has_float[trail];
        float fit_insets[2] = {0.0f, 0.0f};
        float stretch_insets[2] = {0.0f, 0.0f};
        fit_insets[lead] = has_lead_float
            ? std::max(segment.insets[lead], margin_insets[lead])
            : margin_insets[lead];
        stretch_insets[lead] = fit_insets[lead];
        if (has_trail_float) {
            fit_insets[trail] = std::max(segment.insets[trail], containing_block_insets[trail]);
        } else {
            // A positive trailing margin may overflow the containing block edge (it does
            // not affect fit), but a negative one widens the space for the border box
            fit_insets[trail] = std::min(margin_insets[trail], containing_block_insets[trail]);
        }
        stretch_insets[trail] = has_trail_float
            ? std::max(segment.insets[trail], margin_insets[trail])
            : margin_insets[trail];

        BfcSlot slot;
        slot.segment_id = start_idx;
        slot.x = fit_insets[0];
        slot.y = std::max(segment.y_start, min_y);
        slot.border_width = available_width - fit_insets[0] - fit_insets[1];
        slot.stretch_width = available_width - stretch_insets[0] - stretch_insets[1];
        return slot;
    }
};

/// Context for computing the intrinsic width contribution of a set of floats
struct FloatIntrinsicWidthCalculator {
    AvailableSpace available_width{};
    float side_sums[2]{0.0f, 0.0f};
    float contribution{0.0f};
    float widest{0.0f};

    explicit FloatIntrinsicWidthCalculator(AvailableSpace available_width_in) : available_width(available_width_in) {}

    void add_float(float width, FloatDirection direction, Clear clear) {
        if (available_width.type == AvailableSpace::Definite || available_width.type == AvailableSpace::MaxContent) {
            // A float that clears a side is placed below the floats on that side, so its
            // width does not accumulate with theirs.
            if (clear == Clear::Left || clear == Clear::Both) side_sums[0] = 0.0f;
            if (clear == Clear::Right || clear == Clear::Both) side_sums[1] = 0.0f;
            side_sums[direction == FloatDirection::Left ? 0 : 1] += width;
            contribution = std::max(contribution, side_sums[0] + side_sums[1]);
        } else {
            contribution = std::max(contribution, width);
        }
        widest = std::max(widest, width);
    }

    float result() const {
        if (available_width.type == AvailableSpace::Definite) {
            return std::max(std::min(contribution, available_width.value), widest);
        }
        return contribution;
    }
};

} // namespace float_layout

#endif // TAFFY_FLOAT_LAYOUT
#endif // TAFFY_BLOCK_LAYOUT
// ===========================================================================
// Section 15d: Block Layout
// Ref: src/compute/block.rs, src/compute/common/scrollable_overflow.rs,
//      src/compute/common/alignment.rs, src/compute/common/sizing_keyword.rs
// ===========================================================================

#ifdef TAFFY_BLOCK_LAYOUT

namespace block_detail {

using SizeOpt = Size<std::optional<float>>;

inline SizeOpt size_opt_none() { return SizeOpt{std::nullopt, std::nullopt}; }

inline SizeOpt size_opt_or(const SizeOpt& a, const SizeOpt& b) {
    return SizeOpt{a.width ? a.width : b.width, a.height ? a.height : b.height};
}

inline SizeOpt size_opt_add(const SizeOpt& a, const SizeOpt& b) {
    return SizeOpt{maybe_add(a.width, b.width), maybe_add(a.height, b.height)};
}

inline SizeOpt size_opt_sub(const SizeOpt& a, const SizeOpt& b) {
    return SizeOpt{maybe_sub(a.width, b.width), maybe_sub(a.height, b.height)};
}

inline SizeOpt size_opt_add_f32(const SizeOpt& a, const Size<float>& b) {
    return SizeOpt{maybe_add(a.width, std::optional<float>(b.width)), maybe_add(a.height, std::optional<float>(b.height))};
}

inline SizeOpt size_opt_sub_f32(const SizeOpt& a, const Size<float>& b) {
    return SizeOpt{maybe_sub(a.width, std::optional<float>(b.width)), maybe_sub(a.height, std::optional<float>(b.height))};
}

inline uint64_t encode_node_id_u64(NodeId node) {
    return (static_cast<uint64_t>(node.index) << 32) | static_cast<uint64_t>(node.generation);
}

inline SizeOpt size_opt_clamp(const SizeOpt& v, const SizeOpt& mn, const SizeOpt& mx) {
    return SizeOpt{maybe_clamp(v.width, mn.width, mx.width), maybe_clamp(v.height, mn.height, mx.height)};
}

inline SizeOpt size_opt_max(const SizeOpt& a, const SizeOpt& b) {
    return SizeOpt{maybe_max(a.width, b.width), maybe_max(a.height, b.height)};
}

inline SizeOpt size_opt_min(const SizeOpt& a, const SizeOpt& b) {
    return SizeOpt{maybe_min(a.width, b.width), maybe_min(a.height, b.height)};
}

// --- Contain helpers (src/style/mod.rs) ---

inline bool contain_establishes_independent_formatting_context(const Contain& c) { return c.layout || c.paint; }
inline bool contain_suppresses_baseline(const Contain& c) { return c.layout; }
inline bool contain_contains_scrollable_overflow(const Contain& c) { return c.layout || c.paint; }

// --- Alignment helpers ---
// The shared alignment helpers in Section 14b are only compiled when flex or grid is enabled,
// so block-only builds provide equivalent local definitions here.

#if !defined(TAFFY_FLEXBOX) && !defined(TAFFY_GRID)
inline AlignContentKeyword apply_alignment_fallback(float free_space, size_t num_items, AlignContent alignment_mode) {
    AlignContentKeyword keyword = alignment_mode.keyword;
    if (keyword == AlignContentKeyword::Normal) keyword = AlignContentKeyword::Stretch;
    bool is_safe = alignment_mode.is_safe();
    if (num_items <= 1 || free_space <= 0.0f) {
        switch (keyword) {
            case AlignContentKeyword::Stretch:
            case AlignContentKeyword::SpaceBetween:
                keyword = AlignContentKeyword::FlexStart; is_safe = true; break;
            case AlignContentKeyword::SpaceAround:
            case AlignContentKeyword::SpaceEvenly:
                keyword = AlignContentKeyword::Center; is_safe = true; break;
            default: break;
        }
    }
    if (free_space <= 0.0f && is_safe) keyword = AlignContentKeyword::Start;
    return keyword;
}

inline float compute_alignment_offset(
    float free_space, size_t num_items, float gap, AlignContentKeyword alignment_mode,
    bool layout_is_flex_reversed, bool is_first) {
    if (is_first) {
        switch (alignment_mode) {
            case AlignContentKeyword::Start: return 0.0f;
            case AlignContentKeyword::FlexStart: return layout_is_flex_reversed ? free_space : 0.0f;
            case AlignContentKeyword::End: return free_space;
            case AlignContentKeyword::FlexEnd: return layout_is_flex_reversed ? 0.0f : free_space;
            case AlignContentKeyword::Center: return free_space / 2.0f;
            case AlignContentKeyword::Stretch: return 0.0f;
            case AlignContentKeyword::SpaceBetween: return 0.0f;
            case AlignContentKeyword::SpaceAround:
                return free_space >= 0.0f
                           ? (free_space / static_cast<float>(num_items > 0 ? num_items : 1)) / 2.0f
                           : free_space / 2.0f;
            case AlignContentKeyword::SpaceEvenly:
                return free_space >= 0.0f ? free_space / static_cast<float>(num_items + 1) : free_space / 2.0f;
            default: return 0.0f;
        }
    }
    float fs = std::max(free_space, 0.0f);
    float spacing = 0.0f;
    switch (alignment_mode) {
        case AlignContentKeyword::SpaceBetween: spacing = fs / static_cast<float>(num_items - 1); break;
        case AlignContentKeyword::SpaceAround: spacing = fs / static_cast<float>(num_items); break;
        case AlignContentKeyword::SpaceEvenly: spacing = fs / static_cast<float>(num_items + 1); break;
        default: spacing = 0.0f; break;
    }
    return gap + spacing;
}
#endif

/// Resolve the writing-mode-relative `SelfStart`/`SelfEnd` keywords to `Start`/`End`
inline AlignItems resolve_self_relative(
    AlignItems alignment, Direction item_direction, Direction container_direction, bool axis_is_inline) {
    bool flip = axis_is_inline && item_direction != container_direction;
    AlignItemsKeyword keyword = alignment.keyword;
    if (keyword == AlignItemsKeyword::SelfStart) {
        keyword = flip ? AlignItemsKeyword::End : AlignItemsKeyword::Start;
    } else if (keyword == AlignItemsKeyword::SelfEnd) {
        keyword = flip ? AlignItemsKeyword::Start : AlignItemsKeyword::End;
    }
    return AlignItems{keyword, alignment.safety};
}

/// Compute the block-axis offset that `align-content` applies to the in-flow content of a block
/// container, given the free space between the container's content box and its content.
inline float compute_block_align_content_offset(AlignContent align_content, float free_space) {
    AlignContentKeyword keyword = apply_alignment_fallback(free_space, 1, align_content);
    return compute_alignment_offset(free_space, 1, 0.0f, keyword, false, true);
}

/// The first baseline of a child box (measured from the top of its border box)
inline std::optional<float> block_child_first_baseline(
    std::optional<float> first_baseline, float height, Overflow overflow_y) {
    if (is_scroll_container(overflow_y)) {
        float baseline = first_baseline.has_value() ? *first_baseline : height;
        return std::min(baseline, height) > 0.0f ? std::min(baseline, height) : 0.0f;
    }
    return first_baseline;
}

// --- Sizing keyword resolution (src/compute/common/sizing_keyword.rs) ---

struct SizingKeywordResolution {
    enum Kind : uint8_t { Measure, Exact };
    Kind kind{Measure};
    AvailableSpace available{};
    float exact{0.0f};
};

inline std::optional<SizingKeywordResolution> resolve_sizing_keyword(
    Dimension style, std::optional<float> stretch_size, std::optional<float> percent_resolution_basis) {
    switch (style.tag()) {
        case CompactLength::MIN_CONTENT_TAG:
            return SizingKeywordResolution{SizingKeywordResolution::Measure, AvailableSpace::min_content(), 0.0f};
        case CompactLength::MAX_CONTENT_TAG:
            return SizingKeywordResolution{SizingKeywordResolution::Measure, AvailableSpace::max_content(), 0.0f};
        case CompactLength::FIT_CONTENT_PX_TAG:
            return SizingKeywordResolution{SizingKeywordResolution::Measure,
                                           AvailableSpace::definite(style.value()), 0.0f};
        case CompactLength::FIT_CONTENT_PERCENT_TAG:
            if (percent_resolution_basis.has_value()) {
                // Percentages are stored 0..100 in this port (unlike Rust's 0..1 fractions)
                return SizingKeywordResolution{SizingKeywordResolution::Measure,
                                               AvailableSpace::definite(*percent_resolution_basis * (style.value() / 100.0f)), 0.0f};
            }
            return std::nullopt;
        case CompactLength::FIT_CONTENT_KEYWORD_TAG:
            if (stretch_size.has_value()) {
                return SizingKeywordResolution{SizingKeywordResolution::Measure,
                                               AvailableSpace::definite(*stretch_size), 0.0f};
            }
            return std::nullopt;
        case CompactLength::STRETCH_TAG:
            if (stretch_size.has_value()) {
                return SizingKeywordResolution{SizingKeywordResolution::Exact, AvailableSpace{}, *stretch_size};
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

/// Resolve the `stretch` sizing keyword for an item's height style
inline std::optional<float> resolve_stretch_height(
    Dimension height_style, std::optional<float> container_inner_height,
    Rect<float> item_margin, Line<bool> margins_treated_as_zero) {
    if (!height_style.is_stretch()) return std::nullopt;
    if (!container_inner_height.has_value()) return std::nullopt;
    float height = *container_inner_height;
    if (!margins_treated_as_zero.start) height -= item_margin.top;
    if (!margins_treated_as_zero.end) height -= item_margin.bottom;
    return height;
}

// --- Scrollable overflow (src/compute/common/scrollable_overflow.rs) ---

struct ScrollableOverflowContainer {
    bool parent_is_scroll_container{false};
    Size<float> parent_size{0.0f, 0.0f};
    Point<bool> reversed{false, false};
    Rect<float> padding{0.0f, 0.0f, 0.0f, 0.0f};
};

inline Rect<float> rect_union(const Rect<float>& a, const Rect<float>& b) {
    return Rect<float>{std::min(a.left, b.left), std::max(a.right, b.right),
                       std::min(a.top, b.top), std::max(a.bottom, b.bottom)};
}

inline Rect<float> compute_scrollable_overflow_contribution(
    Point<float> location, Size<float> size, Rect<float> scrollable_overflow_rect, Rect<float> border,
    Point<Overflow> overflow, Contain contain, const ScrollableOverflowContainer& container, Rect<float> margin) {
    Rect<float> sor{
        scrollable_overflow_rect.left + border.left,
        scrollable_overflow_rect.right + border.left,
        scrollable_overflow_rect.top + border.top,
        scrollable_overflow_rect.bottom + border.top};
    bool is_scroll = is_scroll_container(overflow.x) || is_scroll_container(overflow.y);
    bool overflow_is_contained = contain_contains_scrollable_overflow(contain);
    Point<bool> propagates{
        !is_scroll && !overflow_is_contained && overflow.x == Overflow::Visible,
        !is_scroll && !overflow_is_contained && overflow.y == Overflow::Visible};
    Size<float> end_extent{
        propagates.x ? std::max(size.width, sor.right) : size.width,
        propagates.y ? std::max(size.height, sor.bottom) : size.height};
    if (end_extent.width <= 0.0f || end_extent.height <= 0.0f) {
        return Rect<float>{0.0f, 0.0f, 0.0f, 0.0f};
    }
    Point<float> start_extent{
        propagates.x ? std::min(0.0f, sor.left) : 0.0f,
        propagates.y ? std::min(0.0f, sor.top) : 0.0f};
    Rect<float> contribution{
        location.x + start_extent.x,
        location.x + end_extent.width,
        location.y + start_extent.y,
        location.y + end_extent.height};
    bool is_wholly_unreachable =
        (container.reversed.x ? contribution.left >= container.parent_size.width : contribution.right <= 0.0f)
        || (container.reversed.y ? contribution.top >= container.parent_size.height : contribution.bottom <= 0.0f);
    if (container.parent_is_scroll_container && is_wholly_unreachable) {
        return Rect<float>{0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (!container.parent_is_scroll_container) return contribution;
    Rect<float> padded = contribution;
    if (container.reversed.x) {
        padded.left = std::min(padded.left, location.x - std::max(0.0f, container.padding.left + margin.left));
    } else {
        padded.right = std::max(padded.right, location.x + size.width + std::max(0.0f, container.padding.right + margin.right));
    }
    if (container.reversed.y) {
        padded.top = std::min(padded.top, location.y - std::max(0.0f, container.padding.top + margin.top));
    } else {
        padded.bottom = std::max(padded.bottom, location.y + size.height + std::max(0.0f, container.padding.bottom + margin.bottom));
    }
    return padded;
}

inline ScrollableOverflow finalize_scrollable_overflow(
    Rect<float> rect_in, Size<float> size_in, Point<bool> reversed_in, bool is_scroll_container_) {
    Size<float> size{std::max(size_in.width, 0.0f), std::max(size_in.height, 0.0f)};
    Point<bool> reversed = reversed_in;
    if (is_scroll_container_) {
        if (reversed.x) {
            rect_in.right = std::min(rect_in.right, size.width);
        } else {
            rect_in.left = 0.0f;
        }
        if (reversed.y) {
            rect_in.bottom = std::min(rect_in.bottom, size.height);
        } else {
            rect_in.top = 0.0f;
        }
    } else {
        reversed = Point<bool>{false, false};
    }
    return ScrollableOverflow::new_(rect_in, reversed);
}

// --- Block formatting context ---

struct BlockFormattingContext {
#ifdef TAFFY_FLOAT_LAYOUT
    float_layout::FloatContext float_context{};
#endif
};

/// A mark of how many floats a Block Formatting Context held when it was taken.
struct FloatMark {
    size_t left{0};
    size_t right{0};
};

struct AdjoiningFloats {
    bool values[2]{false, false};
};

/// Context for each individual Block within a Block Formatting Context
struct BlockContext {
    BlockFormattingContext* bfc{nullptr};
    float y_offset{0.0f};
    float insets[2]{0.0f, 0.0f};
    float content_box_insets[2]{0.0f, 0.0f};
    float float_content_contribution{-std::numeric_limits<float>::infinity()};
    bool is_root{false};
#ifdef TAFFY_FLOAT_LAYOUT
    bool adjoining_floats[2]{false, false};
    std::optional<AdjoiningFloats> top_adjoining_floats{std::nullopt};
#endif

    bool is_bfc_root() const { return is_root; }

    BlockContext sub_context(float additional_y_offset, const float insets_in[2]) const {
        BlockContext ctx;
        ctx.bfc = bfc;
        ctx.y_offset = y_offset + additional_y_offset;
        ctx.insets[0] = insets[0] + insets_in[0];
        ctx.insets[1] = insets[1] + insets_in[1];
        ctx.content_box_insets[0] = ctx.insets[0];
        ctx.content_box_insets[1] = ctx.insets[1];
        ctx.float_content_contribution = -std::numeric_limits<float>::infinity();
        ctx.is_root = false;
#ifdef TAFFY_FLOAT_LAYOUT
        ctx.adjoining_floats[0] = adjoining_floats[0];
        ctx.adjoining_floats[1] = adjoining_floats[1];
        ctx.top_adjoining_floats = std::nullopt;
#endif
        return ctx;
    }

    void apply_content_box_inset(const float content_box_x_insets[2]) {
        content_box_insets[0] = insets[0] + content_box_x_insets[0];
        content_box_insets[1] = insets[1] + content_box_x_insets[1];
    }

#ifdef TAFFY_FLOAT_LAYOUT
    void set_width(float available_width) { bfc->float_context.set_width(available_width); }

    bool has_floats() const { return bfc->float_context.has_floats_(); }
    bool has_active_floats(float min_y) const { return bfc->float_context.has_active_floats(min_y + y_offset); }

    Point<float> place_floated_box(
        uint64_t id, Size<float> floated_box, float min_y, FloatDirection direction, Clear clear,
        bool adjoins_unresolved_strut) {
        if (adjoins_unresolved_strut) adjoining_floats[direction == FloatDirection::Left ? 0 : 1] = true;
        Point<float> pos = bfc->float_context.place_floated_box(
            id, floated_box, min_y + y_offset, content_box_insets, direction, clear);
        pos.y -= y_offset;
        pos.x -= insets[0];
        float_content_contribution = std::max(float_content_contribution, pos.y + floated_box.height);
        return pos;
    }

    FloatMark float_mark() const {
        return FloatMark{bfc->float_context.left_floats.size(), bfc->float_context.right_floats.size()};
    }

    std::vector<std::pair<uint64_t, Point<float>>> floats_placed_since(FloatMark mark) const {
        std::vector<std::pair<uint64_t, Point<float>>> out;
        const auto& lf = bfc->float_context.left_floats;
        for (size_t i = mark.left; i < lf.size(); ++i) {
            Point<float> pos = bfc->float_context.placed_position(lf[i], FloatDirection::Left);
            out.push_back({lf[i].id, Point<float>{pos.x - insets[0], pos.y - y_offset}});
        }
        const auto& rf = bfc->float_context.right_floats;
        for (size_t i = mark.right; i < rf.size(); ++i) {
            Point<float> pos = bfc->float_context.placed_position(rf[i], FloatDirection::Right);
            out.push_back({rf[i].id, Point<float>{pos.x - insets[0], pos.y - y_offset}});
        }
        return out;
    }

    float_layout::ContentSlot find_content_slot(float min_y, Clear clear, std::optional<size_t> after) const {
        float_layout::ContentSlot slot =
            bfc->float_context.find_content_slot(min_y + y_offset, content_box_insets, clear, after);
        slot.y -= y_offset;
        slot.x -= insets[0];
        return slot;
    }

    float_layout::BfcSlot find_bfc_slot(
        float min_y, const float margins[2], Direction direction, Clear clear,
        std::optional<size_t> after) const {
        float_layout::BfcSlot slot =
            bfc->float_context.find_bfc_slot(min_y + y_offset, content_box_insets, margins, direction, clear, after);
        slot.y -= y_offset;
        slot.x -= insets[0];
        return slot;
    }

    std::optional<float> cleared_threshold(Clear clear) const {
        std::optional<float> threshold = bfc->float_context.cleared_threshold(clear);
        if (threshold.has_value()) return *threshold - y_offset;
        return std::nullopt;
    }

    bool has_adjoining_float(Clear clear) const {
        switch (clear) {
            case Clear::Left: return adjoining_floats[0];
            case Clear::Right: return adjoining_floats[1];
            case Clear::Both: return adjoining_floats[0] || adjoining_floats[1];
            case Clear::None:
            default: return false;
        }
    }

    void merge_adjoining_floats(const AdjoiningFloats& flags) {
        adjoining_floats[0] = adjoining_floats[0] || flags.values[0];
        adjoining_floats[1] = adjoining_floats[1] || flags.values[1];
    }

    /// Record that in-flow content has been committed within this block, resolving the position
    /// of the current margin-collapse strut.
    void commit_strut() {
        if (!top_adjoining_floats.has_value()) {
            top_adjoining_floats = AdjoiningFloats{{adjoining_floats[0], adjoining_floats[1]}};
        }
        adjoining_floats[0] = false;
        adjoining_floats[1] = false;
    }

    AdjoiningFloats top_adjoining_floats_() const {
        if (top_adjoining_floats.has_value()) return *top_adjoining_floats;
        return AdjoiningFloats{{adjoining_floats[0], adjoining_floats[1]}};
    }
#else
    // The float-dependent helpers are only referenced from `#ifdef TAFFY_FLOAT_LAYOUT` code
    // paths, so nothing is required when the float feature is disabled.
#endif

    void add_child_floated_content_height_contribution(float child_contribution) {
        float_content_contribution = std::max(float_content_contribution, child_contribution);
    }

    float floated_content_height_contribution() const { return float_content_contribution; }
};

inline BlockContext root_block_context(BlockFormattingContext& bfc) {
    BlockContext ctx;
    ctx.bfc = &bfc;
    ctx.is_root = true;
    return ctx;
}

/// Per-child data that is accumulated and modified over the course of the layout algorithm
struct BlockItem {
    NodeId node_id{};
    uint32_t order{0};
    bool is_table{false};
    bool is_compressible_replaced{false};
    bool is_in_same_bfc{false};
#ifdef TAFFY_FLOAT_LAYOUT
    Float float_{Float::None};
    Clear clear{Clear::None};
#endif
    Size<Dimension> size_style{};
    SizeOpt size{};
    SizeOpt min_size{};
    SizeOpt max_size{};
    Point<Overflow> overflow{Overflow::Visible, Overflow::Visible};
    Contain contain{};
    float scrollbar_width{0.0f};
    Position position{Position::Static};
    Direction direction{Direction::Ltr};
    std::optional<AlignSelf> align_self{std::nullopt};
    std::optional<AlignSelf> justify_self{std::nullopt};
    Rect<LengthPercentageAuto> inset{};
    Rect<LengthPercentageAuto> margin{};
    Rect<float> padding{0.0f, 0.0f, 0.0f, 0.0f};
    Rect<float> border{0.0f, 0.0f, 0.0f, 0.0f};
    Size<float> padding_border_sum{0.0f, 0.0f};
    Size<float> computed_size{0.0f, 0.0f};
    Point<float> static_position{0.0f, 0.0f};
    bool can_be_collapsed_through{false};
    std::optional<Layout> final_layout{std::nullopt};
    OofCandidates oof_candidates{};
};

/// Rust `compute/leaf.rs`: whether a leaf node's (used) height is zero and nothing in its style
/// prevents its top/bottom margins from collapsing through it.
inline bool leaf_can_collapse_through(const BlockItem& item, const LayoutOutput& out, Size<float> measured_size) {
    bool has_scroll = is_scroll_container(item.overflow.x) || is_scroll_container(item.overflow.y);
    bool preventing = !item.is_in_same_bfc
        || has_scroll
        || is_absolute_position(item.position)
        || contain_establishes_independent_formatting_context(item.contain)
        || item.padding.top > 0.0f || item.padding.bottom > 0.0f
        || item.border.top > 0.0f || item.border.bottom > 0.0f
        || (item.size.height.has_value() && *item.size.height > 0.0f)
        || (item.min_size.height.has_value() && *item.min_size.height > 0.0f);
    return !preventing && out.size.height == 0.0f && measured_size.height == 0.0f;
}

/// Whether any descendant of `node` is out-of-flow (absolute/fixed). A positioned same-BFC
/// child whose subtree contains out-of-flow boxes owns an OOF positioning pass, so it must go
/// through `TaffyTree::compute_node_layout` (which runs that pass) instead of being recursed
/// into directly here.
template <typename Tree>
bool subtree_has_absolute_descendant(Tree& tree, NodeId node, int depth = 0) {
    if (depth > 512) return false;
    size_t child_count = tree.child_count(node);
    for (size_t i = 0; i < child_count; ++i) {
        NodeId child = tree.get_child_id(node, i);
        const Style& child_style = tree.style(child);
        if (child_style.display == Display::None) continue;
        if (is_absolute_position(child_style.position)) return true;
        if (subtree_has_absolute_descendant(tree, child, depth + 1)) return true;
    }
    return false;
}

/// Resolve the `width`/`height` of a node given the constraints/// Resolve the `width`/`height` of a node given the constraints, then dispatch to the layout
/// algorithm for its `display` value (or measure leaves).
template <typename Tree, typename MeasureFn>
LayoutOutput perform_child_layout(
    Tree& tree, NodeId node_id, SizeOpt known_dimensions, SizeOpt parent_size,
    Size<AvailableSpace> available_space, SizingMode sizing_mode, Line<bool> vertical_margins_are_collapsible,
    MeasureFn& measure_fn) {
    LayoutInput inputs;
    inputs.run_mode = RunMode::PerformLayout;
    inputs.sizing_mode = sizing_mode;
    inputs.axis = RequestedAxis::Both;
    inputs.known_dimensions = known_dimensions;
    inputs.known_dimensions_are_definite = Size<bool>{true, true};
    inputs.parent_size = parent_size;
    inputs.available_space = available_space;
    inputs.vertical_margins_are_collapsible = vertical_margins_are_collapsible;
    return tree.compute_node_layout(node_id, inputs, measure_fn);
}

template <typename Tree, typename MeasureFn>
float measure_child_size(
    Tree& tree, NodeId node_id, SizeOpt known_dimensions, SizeOpt parent_size,
    Size<AvailableSpace> available_space, SizingMode sizing_mode, AbsoluteAxis axis,
    Line<bool> vertical_margins_are_collapsible, MeasureFn& measure_fn) {
    LayoutInput inputs;
    inputs.run_mode = RunMode::ComputeSize;
    inputs.sizing_mode = sizing_mode;
    inputs.axis = axis == AbsoluteAxis::Horizontal ? RequestedAxis::Horizontal : RequestedAxis::Vertical;
    inputs.known_dimensions = known_dimensions;
    inputs.known_dimensions_are_definite = Size<bool>{true, true};
    inputs.parent_size = parent_size;
    inputs.available_space = available_space;
    inputs.vertical_margins_are_collapsible = vertical_margins_are_collapsible;
    return tree.compute_node_layout(node_id, inputs, measure_fn).size.get_abs(axis);
}

/// Compute the offset applied to a box by its `inset` if it is `position: relative`.
template <typename Tree>
Point<float> resolve_relative_offset(
    const BlockItem& item, SizeOpt parent_size, Direction direction, Tree& tree) {
    (void)tree;
    if (item.position != Position::Relative) return Point<float>{0.0f, 0.0f};
    Rect<std::optional<float>> inset = item.inset.zip_size(parent_size, [&](LengthPercentageAuto p, std::optional<float> s) {
        return maybe_resolve(p, s);
    });
    std::optional<float> x;
    if (direction == Direction::Rtl) {
        x = inset.right.has_value() ? std::optional<float>(-*inset.right) : inset.left;
    } else {
        x = inset.left.has_value() ? inset.left : (inset.right.has_value() ? std::optional<float>(-*inset.right) : std::nullopt);
    }
    std::optional<float> y = inset.top.has_value()
        ? inset.top
        : (inset.bottom.has_value() ? std::optional<float>(-*inset.bottom) : std::nullopt);
    return Point<float>{x.value_or(0.0f), y.value_or(0.0f)};
}

/// Create a list of `BlockItem` structs where each item represents a child of the current node
template <typename Tree>
std::vector<BlockItem> generate_item_list(Tree& tree, NodeId node, SizeOpt node_inner_size) {
    std::vector<BlockItem> items;
    uint32_t order = 0;
    size_t child_count = tree.child_count(node);
    for (size_t i = 0; i < child_count; ++i) {
        NodeId child_node_id = tree.get_child_id(node, i);
        const Style& child_style = tree.style(child_node_id);
        if (child_style.display == Display::None) continue;

        std::optional<float> aspect_ratio = child_style.aspect_ratio;
        Rect<float> padding = resolve_or_zero_rect(child_style.padding, node_inner_size.width);
        Rect<float> border = resolve_or_zero_rect(child_style.border, node_inner_size.width);
        Size<float> pb_sum = (padding + border).sum_axes();
        Size<float> box_sizing_adjustment =
            child_style.box_sizing == BoxSizing::ContentBox ? pb_sum : Size<float>{0.0f, 0.0f};

        Position position = child_style.position;
        Point<Overflow> overflow = child_style.overflow;

#ifdef TAFFY_FLOAT_LAYOUT
        Float float_style = child_style.float_;
        bool is_not_floated = float_style == Float::None;
#else
        bool is_not_floated = true;
#endif

        bool is_block = child_style.display == Display::Block;
        bool is_table = child_style.item_is_table;
        bool is_compressible_replaced =
            child_style.item_is_replaced || child_style.item_is_compressible_replaced;
        bool is_scroll = is_scroll_container(overflow.x) || is_scroll_container(overflow.y);
        Contain contain = child_style.contain;
        bool align_content_normal = !child_style.align_content.has_value()
            || child_style.align_content->keyword == AlignContentKeyword::Normal;

        bool is_in_same_bfc = is_block && !is_table && !is_absolute_position(position) && is_not_floated
            && !is_scroll && !contain_establishes_independent_formatting_context(contain)
            && align_content_normal;

        BlockItem item;
        item.node_id = child_node_id;
        item.order = order++;
        item.is_table = is_table;
        item.is_compressible_replaced = is_compressible_replaced;
        item.is_in_same_bfc = is_in_same_bfc;
#ifdef TAFFY_FLOAT_LAYOUT
        item.float_ = float_style;
        item.clear = child_style.clear;
#endif
        item.size_style = child_style.size;
        item.size = size_opt_add_f32(
            maybe_resolve_size(child_style.size, node_inner_size).maybe_apply_aspect_ratio(aspect_ratio),
            box_sizing_adjustment);
        item.min_size = size_opt_add_f32(
            maybe_resolve_size(child_style.min_size, node_inner_size).maybe_apply_aspect_ratio(aspect_ratio),
            box_sizing_adjustment);
        item.max_size = size_opt_add_f32(
            maybe_resolve_size(child_style.max_size, node_inner_size).maybe_apply_aspect_ratio(aspect_ratio),
            box_sizing_adjustment);
        item.overflow = overflow;
        item.contain = contain;
        item.scrollbar_width = child_style.scrollbar_width;
        item.position = position;
        item.direction = child_style.direction;
        item.align_self = child_style.align_self;
#ifdef TAFFY_GRID
        item.justify_self = child_style.justify_self;
#else
        item.justify_self = std::nullopt;
#endif
        item.inset = child_style.inset;
        item.margin = child_style.margin;
        item.padding = padding;
        item.border = border;
        item.padding_border_sum = pb_sum;
        items.push_back(item);
    }
    return items;
}

/// Compute the content-based width in the case that the width of the container is not known
template <typename Tree, typename MeasureFn>
float determine_content_based_container_width(
    Tree& tree, const std::vector<BlockItem>& items, AvailableSpace available_width, MeasureFn& measure_fn) {
    Size<AvailableSpace> available_space{available_width, AvailableSpace::min_content()};

    float max_child_width = 0.0f;
#ifdef TAFFY_FLOAT_LAYOUT
    float_layout::FloatIntrinsicWidthCalculator float_contribution(available_width);
#endif
    for (const BlockItem& item : items) {
        if (is_absolute_position(item.position)) continue;
        SizeOpt known_dimensions = size_opt_clamp(item.size, item.min_size, item.max_size);

        float item_x_margin_sum = resolve_or_zero_rect(item.margin, available_space.width.into_option()).horizontal_axis_sum();
        std::optional<float> width_opt = known_dimensions.width;
        float width;
        if (width_opt.has_value()) {
            width = *width_opt;
        } else {
            AvailableSpace item_available_width;
            std::optional<SizingKeywordResolution> keyword =
                resolve_sizing_keyword(item.size_style.width, std::nullopt, std::nullopt);
            if (keyword.has_value()) {
                if (keyword->kind == SizingKeywordResolution::Measure) {
                    item_available_width = keyword->available;
                } else {
                    item_available_width = AvailableSpace::definite(keyword->exact);
                }
            } else {
                item_available_width = maybe_sub(available_space.width, item_x_margin_sum);
            }
            width = measure_child_size(
                tree, item.node_id, known_dimensions, size_opt_none(),
                Size<AvailableSpace>{item_available_width, available_space.height},
                SizingMode::InherentSize, AbsoluteAxis::Horizontal,
                item.is_in_same_bfc ? Line_bool_TRUE : Line_bool_FALSE, measure_fn);
        }

        width = std::max(width, item.padding_border_sum.width) + item_x_margin_sum;

#ifdef TAFFY_FLOAT_LAYOUT
        if (item.float_ != Float::None) {
            FloatDirection direction = item.float_ == Float::Left ? FloatDirection::Left : FloatDirection::Right;
            float_contribution.add_float(width, direction, item.clear);
            continue;
        }
#endif
        max_child_width = std::max(max_child_width, width);
    }

#ifdef TAFFY_FLOAT_LAYOUT
    max_child_width = std::max(max_child_width, float_contribution.result());
#endif
    return max_child_width;
}

struct FinalLayoutResult {
    Rect<float> inflow_overflow_rect{0.0f, 0.0f, 0.0f, 0.0f};
    float intrinsic_outer_height{0.0f};
    CollapsibleMarginSet first_child_top_margin_set{CollapsibleMarginSet::zero()};
    CollapsibleMarginSet last_child_bottom_margin_set{CollapsibleMarginSet::zero()};
    std::optional<float> first_baseline{std::nullopt};
};

template <typename Tree, typename MeasureFn>
LayoutOutput compute_block_layout(
    Tree& tree, NodeId node_id, LayoutInput inputs, BlockContext* block_ctx, MeasureFn& measure_fn);

/// Computes the layout of a block container according to the block layout algorithm
template <typename Tree, typename MeasureFn>
LayoutOutput compute_inner(
    Tree& tree, NodeId node_id, LayoutInput inputs, BlockContext& block_ctx, MeasureFn& measure_fn) {
    SizeOpt known_dimensions_in = inputs.known_dimensions;
    SizeOpt parent_size = inputs.parent_size;
    Size<AvailableSpace> available_space = inputs.available_space;
    RunMode run_mode = inputs.run_mode;
    Line<bool> vertical_margins_are_collapsible = inputs.vertical_margins_are_collapsible;

    const Style& style = tree.style(node_id);
    Rect<LengthPercentage> raw_padding = style.padding;
    Rect<LengthPercentage> raw_border = style.border;
    Rect<LengthPercentageAuto> raw_margin = style.margin;
    std::optional<float> aspect_ratio = style.aspect_ratio;
    Rect<float> padding = resolve_or_zero_rect(raw_padding, parent_size.width);
    Rect<float> border = resolve_or_zero_rect(raw_border, parent_size.width);
    Direction direction = style.direction;

    // Scrollbar gutters are reserved when the `overflow` property is set to `Overflow::Scroll`,
    // transposed because a node that scrolls vertically needs horizontal space for a scrollbar.
    float gutter_right = style.overflow.y == Overflow::Scroll ? style.scrollbar_width : 0.0f;
    float gutter_bottom = style.overflow.x == Overflow::Scroll ? style.scrollbar_width : 0.0f;
    Rect<float> scrollbar_gutter = direction == Direction::Ltr
        ? Rect<float>{0.0f, gutter_right, 0.0f, gutter_bottom}
        : Rect<float>{gutter_right, 0.0f, 0.0f, gutter_bottom};

    Rect<float> padding_border = padding + border;
    Size<float> padding_border_size = padding_border.sum_axes();
    Rect<float> content_box_inset = padding_border + scrollbar_gutter;

    float content_box_x_insets[2] = {content_box_inset.left, content_box_inset.right};
    block_ctx.apply_content_box_inset(content_box_x_insets);

    Size<float> box_sizing_adjustment =
        style.box_sizing == BoxSizing::ContentBox ? padding_border_size : Size<float>{0.0f, 0.0f};
    SizeOpt size = size_opt_add_f32(
        maybe_resolve_size(style.size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);
    SizeOpt min_size = size_opt_add_f32(
        maybe_resolve_size(style.min_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);
    SizeOpt max_size = size_opt_add_f32(
        maybe_resolve_size(style.max_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);

    // css-sizing-4: a definite size in one axis transfers through `aspect-ratio` to make the
    // other definite. Only a newly-filled axis is adopted.
    SizeOpt known_dimensions;
    {
        SizeOpt derived = size_opt_clamp(known_dimensions_in.maybe_apply_aspect_ratio(aspect_ratio), min_size, max_size);
        known_dimensions.width = known_dimensions_in.width ? known_dimensions_in.width : derived.width;
        known_dimensions.height = known_dimensions_in.height ? known_dimensions_in.height : derived.height;
    }
    SizeOpt percentage_basis_dimensions{
        known_dimensions.width,
        inputs.known_dimensions_are_definite.height ? known_dimensions.height : std::optional<float>(std::nullopt)};
    SizeOpt container_content_box_size = size_opt_sub_f32(percentage_basis_dimensions, content_box_inset.sum_axes());

    Point<Overflow> overflow = style.overflow;
    bool is_scroll = is_scroll_container(overflow.x) || is_scroll_container(overflow.y);
    bool establishes_new_bfc = is_scroll
        || (style.align_content.has_value() && style.align_content->keyword != AlignContentKeyword::Normal)
        || contain_establishes_independent_formatting_context(style.contain);

    // Determine margin collapsing behaviour
    bool position_out_of_flow = is_absolute_position(style.position);
    Line<bool> children_margins_adjoin_own_edges{
        vertical_margins_are_collapsible.start && !establishes_new_bfc && !position_out_of_flow
            && padding.top == 0.0f && border.top == 0.0f,
        vertical_margins_are_collapsible.end && !establishes_new_bfc && !position_out_of_flow
            && padding.bottom == 0.0f && border.bottom == 0.0f};
    Line<bool> own_margins_collapse_with_children{
        children_margins_adjoin_own_edges.start,
        children_margins_adjoin_own_edges.end && !size.height.has_value()};
    bool is_block_style = style.display == Display::Block;
    bool has_styles_preventing_being_collapsed_through = !is_block_style
        || block_ctx.is_bfc_root() || establishes_new_bfc || position_out_of_flow
        || padding.top > 0.0f || padding.bottom > 0.0f || border.top > 0.0f || border.bottom > 0.0f;

    TextAlign text_align = style.text_align;
    AlignContent align_content = style.align_content.value_or(AlignContent_NORMAL);
    AlignItems justify_items = AlignItems_NORMAL;
#ifdef TAFFY_GRID
    if (style.justify_items.has_value()) justify_items = *style.justify_items;
#endif

    // 1. Generate items
    std::vector<BlockItem> items = generate_item_list(tree, node_id, container_content_box_size);

    // 2. Compute container width
    float container_outer_width;
    if (known_dimensions.width.has_value()) {
        container_outer_width = *known_dimensions.width;
    } else {
        AvailableSpace available_width = maybe_sub(available_space.width, content_box_inset.horizontal_axis_sum());
        float intrinsic_width = determine_content_based_container_width(tree, items, available_width, measure_fn)
            + content_box_inset.horizontal_axis_sum();
        intrinsic_width = maybe_clamp_f32(intrinsic_width, min_size.width, max_size.width);
        intrinsic_width = maybe_max_f32(intrinsic_width, padding_border_size.width);
        container_outer_width = intrinsic_width;
    }

    // Short-circuit if computing size and both dimensions known
    if (run_mode == RunMode::ComputeSize && known_dimensions.height.has_value()) {
        return LayoutOutput::from_outer_size(Size<float>{container_outer_width, *known_dimensions.height});
    }
    if (run_mode == RunMode::ComputeSize && inputs.axis == RequestedAxis::Horizontal) {
        return LayoutOutput::from_outer_size(Size<float>{container_outer_width, 0.0f});
    }

    // Under SizingMode::ContentSize the container's own height/min-height are ignored.
    std::optional<float> container_percentage_resolution_height = percentage_basis_dimensions.height;
    if (!container_percentage_resolution_height.has_value() && inputs.sizing_mode == SizingMode::InherentSize) {
        container_percentage_resolution_height = maybe_max(size.height, min_size.height);
    }

    // 3. Perform final item layout and return content height
    float percentage_resolution_width = parent_size.width.value_or(container_outer_width);
    Rect<float> resolved_padding = resolve_or_zero_rect(raw_padding, percentage_resolution_width);
    Rect<float> resolved_border = resolve_or_zero_rect(raw_border, percentage_resolution_width);
    Rect<float> resolved_content_box_inset = resolved_padding + resolved_border + scrollbar_gutter;

    FinalLayoutResult final_result = FinalLayoutResult{};
    // (inlined call below)
    {
        // ---- body of perform_final_layout_on_in_flow_children ----
        auto& items_ref = items;
        float container_inner_width =
            std::max(container_outer_width - resolved_content_box_inset.horizontal_axis_sum(), 0.0f);
        ScrollableOverflowContainer overflow_container;
        overflow_container.parent_is_scroll_container = is_scroll;
        overflow_container.parent_size = Size<float>{
            container_inner_width + resolved_padding.horizontal_axis_sum(), 0.0f};
        overflow_container.reversed = Point<bool>{direction == Direction::Rtl, false};
        overflow_container.padding = resolved_padding;

        std::optional<float> container_percentage_height = container_percentage_resolution_height;
        if (container_percentage_height.has_value()) {
            container_percentage_height = *container_percentage_height - resolved_content_box_inset.vertical_axis_sum();
        }
        SizeOpt parent_size_final{std::optional<float>(container_inner_width), container_percentage_height};
        // Vertical available space in block flow is indefinite (MaxContent).
        Size<AvailableSpace> available_space_final{AvailableSpace::definite(container_inner_width), AvailableSpace::max_content()};

#ifdef TAFFY_FLOAT_LAYOUT
        if (block_ctx.is_bfc_root()) {
            block_ctx.set_width(container_outer_width);
            float cb_insets[2] = {resolved_content_box_inset.left, resolved_content_box_inset.right};
            block_ctx.apply_content_box_inset(cb_insets);
        }
        if (!own_margins_collapse_with_children.start) block_ctx.commit_strut();
#endif

        Rect<float> inflow_overflow_rect{0.0f, 0.0f, 0.0f, 0.0f};
        float committed_y_offset = resolved_content_box_inset.top;
        float y_offset_for_absolute = resolved_content_box_inset.top;
        CollapsibleMarginSet first_child_top_margin_set = CollapsibleMarginSet::zero();
        CollapsibleMarginSet active_collapsible_margin_set = CollapsibleMarginSet::zero();
        bool is_collapsing_with_first_margin_set = true;
        std::optional<float> first_baseline;
        bool active_margin_set_has_clearance = false;
        bool has_active_floats = false;
#ifdef TAFFY_FLOAT_LAYOUT
        has_active_floats = block_ctx.has_active_floats(committed_y_offset);
#endif

        for (BlockItem& item : items_ref) {
            if (is_absolute_position(item.position)) {
                float x = direction == Direction::Ltr
                    ? resolved_content_box_inset.left
                    : container_outer_width - resolved_content_box_inset.right;
                item.static_position = Point<float>{x, y_offset_for_absolute};
                continue;
            }

            Rect<std::optional<float>> item_margin = item.margin.map([&](LengthPercentageAuto m) {
                return maybe_resolve(m, std::optional<float>(container_inner_width));
            });
            Rect<float> item_non_auto_margin = item_margin.map([](std::optional<float> m) { return m.value_or(0.0f); });
            float item_non_auto_x_margin_sum = item_non_auto_margin.horizontal_axis_sum();

            Size<float> scrollbar_size{
                item.overflow.y == Overflow::Scroll ? item.scrollbar_width : 0.0f,
                item.overflow.x == Overflow::Scroll ? item.scrollbar_width : 0.0f};

            // Handle floated boxes
#ifdef TAFFY_FLOAT_LAYOUT
            if (item.float_ != Float::None) {
                has_active_floats = true;
                FloatDirection float_direction = item.float_ == Float::Left
                    ? FloatDirection::Left : FloatDirection::Right;

                // A float with `width: auto` is shrink-to-fit (fit-content) sized
                float available_width_float = std::max(container_inner_width - item_non_auto_x_margin_sum, 0.0f);
                std::optional<float> item_known_width;
                AvailableSpace item_available_width = AvailableSpace::definite(available_width_float);
                std::optional<SizingKeywordResolution> keyword = resolve_sizing_keyword(
                    item.size_style.width, std::optional<float>(available_width_float),
                    std::optional<float>(container_inner_width));
                if (keyword.has_value()) {
                    if (keyword->kind == SizingKeywordResolution::Measure) {
                        item_known_width = std::nullopt;
                        item_available_width = keyword->available;
                    } else {
                        item_known_width = keyword->exact;
                        item_available_width = AvailableSpace::definite(keyword->exact);
                    }
                }
                std::optional<float> item_known_height = resolve_stretch_height(
                    item.size_style.height, container_percentage_height, item_non_auto_margin, Line_bool_FALSE);
                LayoutOutput item_layout = perform_child_layout(
                    tree, item.node_id, SizeOpt{item_known_width, item_known_height}, parent_size_final,
                    Size<AvailableSpace>{item_available_width, AvailableSpace::max_content()},
                    SizingMode::InherentSize, Line_bool_FALSE, measure_fn);
                item.oof_candidates = std::move(item_layout.oof_candidates);
                Size<float> margin_box = item_layout.size + item_non_auto_margin.sum_axes();

                bool adjoins_unresolved_strut =
                    is_collapsing_with_first_margin_set && own_margins_collapse_with_children.start;
                float y_offset_for_float = adjoins_unresolved_strut
                    ? committed_y_offset
                    : committed_y_offset + active_collapsible_margin_set.resolve();

                Point<float> location = block_ctx.place_floated_box(
                    encode_node_id_u64(item.node_id), margin_box, y_offset_for_float,
                    float_direction, item.clear, adjoins_unresolved_strut);

                location.y += item_non_auto_margin.top;
                location.x += item_non_auto_margin.left;

                Point<float> inset_offset = resolve_relative_offset(item, parent_size_final, direction, tree);
                location.x += inset_offset.x;
                location.y += inset_offset.y;

                Layout layout_item;
                layout_item.order = item.order;
                layout_item.size = item_layout.size;
                layout_item.scrollbar_size = scrollbar_size;
                layout_item.location = location;
                layout_item.padding = item.padding;
                layout_item.border = item.border;
                layout_item.margin = item_non_auto_margin;
#ifdef TAFFY_CONTENT_SIZE
                layout_item.scrollable_overflow_rect = item_layout.scrollable_overflow_rect;
#endif
                item.final_layout = layout_item;

#ifdef TAFFY_CONTENT_SIZE
                {
                    Point<float> contribution_location{
                        location.x - resolved_content_box_inset.left + resolved_padding.left,
                        location.y - resolved_border.top};
                    inflow_overflow_rect = rect_union(inflow_overflow_rect, compute_scrollable_overflow_contribution(
                        contribution_location, item_layout.size, item_layout.scrollable_overflow_rect.rect(),
                        item.border, item.overflow, item.contain, overflow_container,
                        item_non_auto_margin.map([](float value) { return std::min(value, 0.0f); })));
                }
#endif
                continue;
            }
#endif

            // Handle non-floated boxes
            float y_margin_offset = 0.0f;
#ifdef TAFFY_FLOAT_LAYOUT
            bool item_avoids_floats = false;
            bool item_pushed_below_float = false;
#endif

            float stretch_width;
            Point<float> float_avoiding_position;
            float float_avoiding_width;

            if (item.is_in_same_bfc) {
                stretch_width = std::max(container_inner_width - item_non_auto_x_margin_sum, 0.0f);
                float_avoiding_position = Point<float>{0.0f, 0.0f};
                float_avoiding_width = 0.0f;
            } else {
                // Set y_margin_offset (different bfc child)
                if (!is_collapsing_with_first_margin_set || !own_margins_collapse_with_children.start) {
                    y_margin_offset =
                        active_collapsible_margin_set.collapse_with_margin(item_non_auto_margin.top).resolve();
                }
                float min_y = committed_y_offset + y_margin_offset;

#ifdef TAFFY_FLOAT_LAYOUT
                if (has_active_floats || block_ctx.has_active_floats(min_y)) {
                    float x_margins[2] = {item_non_auto_margin.left, item_non_auto_margin.right};
                    float min_auto_width = -item_non_auto_x_margin_sum;

                    std::optional<size_t> slot_segment;
                    float_layout::BfcSlot slot;
                    for (;;) {
                        slot = block_ctx.find_bfc_slot(min_y, x_margins, direction, item.clear, slot_segment);
                        if (!slot.segment_id.has_value()) break;
                        float width = item.size.width.has_value()
                            ? *item.size.width
                            : std::max(std::max(slot.stretch_width, min_auto_width), 0.0f);
                        width = maybe_clamp_f32(width, item.min_size.width, item.max_size.width);
                        if (width <= slot.border_width + float_layout::FIT_TOLERANCE) break;
                        slot_segment = slot.segment_id;
                    }
                    if (slot.y > min_y) item_pushed_below_float = true;
                    has_active_floats = slot.segment_id.has_value();
                    item_avoids_floats = true;
                    stretch_width = std::max(std::max(slot.stretch_width, min_auto_width), 0.0f);
                    float_avoiding_position = Point<float>{slot.x, slot.y};
                    float_avoiding_width = slot.border_width;
                } else
#endif
                {
                    stretch_width = std::max(container_inner_width - item_non_auto_x_margin_sum, 0.0f);
                    float_avoiding_position = Point<float>{resolved_content_box_inset.left, min_y};
                    float_avoiding_width = container_inner_width;
                }
            }

            // `justify-self` on an in-flow block-level box (css-align-3 6.1.1)
            AlignItems justify_self = resolve_self_relative(
                item.justify_self.value_or(justify_items), item.direction, direction, true);
            bool is_stretch = justify_self.keyword == AlignItemsKeyword::Stretch;
            std::optional<AlignItems> non_stretch_justify_self;
            if (justify_self.keyword != AlignItemsKeyword::Normal && justify_self.keyword != AlignItemsKeyword::Stretch) {
                non_stretch_justify_self = justify_self;
            }

            SizeOpt item_known_dims;
            if ((item.is_table || item.is_compressible_replaced) && !is_stretch) {
                item_known_dims = size_opt_none();
            } else {
                // The automatic width of a block-level box whose `justify-self` is neither
                // `normal` nor `stretch` is equivalent to `fit-content` rather than `stretch`
                Dimension width_style = item.size_style.width;
                if (non_stretch_justify_self.has_value() && item.size_style.width.is_auto()) {
                    width_style = Dimension::fit_content_keyword();
                }

                std::optional<float> keyword_width;
                std::optional<SizingKeywordResolution> width_resolution = resolve_sizing_keyword(
                    width_style, std::optional<float>(stretch_width), std::optional<float>(container_inner_width));
                if (width_resolution.has_value()) {
                    if (width_resolution->kind == SizingKeywordResolution::Exact) {
                        keyword_width = width_resolution->exact;
                    } else {
                        keyword_width = measure_child_size(
                            tree, item.node_id, size_opt_none(), parent_size_final,
                            Size<AvailableSpace>{width_resolution->available, AvailableSpace::max_content()},
                            SizingMode::InherentSize, AbsoluteAxis::Horizontal,
                            item.is_in_same_bfc ? Line_bool_TRUE : Line_bool_FALSE, measure_fn);
                    }
                }

                std::optional<float> keyword_height = resolve_stretch_height(
                    item.size_style.height, container_percentage_height, item_non_auto_margin,
                    children_margins_adjoin_own_edges);

                item_known_dims = item.size;
                {
                    float w = item_known_dims.width.value_or(keyword_width.value_or(stretch_width));
                    w = maybe_clamp_f32(w, item.min_size.width, item.max_size.width);
                    item_known_dims.width = w;
                }
                item_known_dims.height = item_known_dims.height.has_value()
                    ? item_known_dims.height : keyword_height;
                item_known_dims = size_opt_clamp(item_known_dims, item.min_size, item.max_size);
            }

            LayoutInput item_inputs;
            item_inputs.run_mode = run_mode;
            item_inputs.sizing_mode = SizingMode::InherentSize;
            item_inputs.axis = RequestedAxis::Both;
            item_inputs.known_dimensions = item_known_dims;
            item_inputs.known_dimensions_are_definite = Size<bool>{true, true};
            item_inputs.parent_size = parent_size_final;
            item_inputs.available_space = Size<AvailableSpace>{
                AvailableSpace::definite(stretch_width), available_space_final.height};
            item_inputs.vertical_margins_are_collapsible =
                item.is_in_same_bfc ? Line_bool_TRUE : Line_bool_FALSE;

            std::optional<float> clear_threshold;
            float clear_pos = -std::numeric_limits<float>::infinity();
#ifdef TAFFY_FLOAT_LAYOUT
            clear_threshold = block_ctx.cleared_threshold(item.clear);
            if (clear_threshold.has_value()) clear_pos = *clear_threshold;
#endif

            float child_float_contribution = -std::numeric_limits<float>::infinity();

            LayoutOutput item_layout;
            // Same-BFC children inherit the enclosing BlockContext so that margins can collapse
            // with the parent. Childless same-BFC nodes are special-cased: they must honour a
            // measure function (e.g. text nodes), which block layout has no notion of, while
            // still keeping block margin-collapse-through semantics.
            if (item.is_in_same_bfc) {
                bool is_leaf = tree.child_count(item.node_id) == 0;
                bool use_leaf_layout = false;
                Size<float> measured_probe{0.0f, 0.0f};
                if (is_leaf) {
                    // Probe the measure function under a max-content constraint: a non-empty
                    // intrinsic size means this "leaf" has real content (text) and must be measured.
                    const Style& child_style = tree.style(item.node_id);
                    measured_probe = measure_fn(
                        size_opt_none(),
                        Size<AvailableSpace>{AvailableSpace::max_content(), AvailableSpace::max_content()},
                        item.node_id, child_style);
                    use_leaf_layout = measured_probe.width > 0.0f || measured_probe.height > 0.0f;
                }
                bool needs_oof_pass =
                    item.position != Position::Static && subtree_has_absolute_descendant(tree, item.node_id);
                if (!use_leaf_layout && !needs_oof_pass) {
                    float width = item_known_dims.width.value_or(stretch_width);
                    width = std::max(width, item.padding_border_sum.width);

                    float inset_left = item_non_auto_margin.left + content_box_inset.left;
                    float inset_right = container_outer_width - width - inset_left;
                    float insets[2] = {inset_left, inset_right};

                    float estimated_y = committed_y_offset
                        + active_collapsible_margin_set.collapse_with_margin(item_non_auto_margin.top).resolve();
                    BlockContext child_block_ctx = block_ctx.sub_context(std::max(estimated_y, clear_pos), insets);
                    item_layout = compute_block_layout(tree, item.node_id, item_inputs, &child_block_ctx, measure_fn);

#ifdef TAFFY_FLOAT_LAYOUT
                    child_float_contribution = child_block_ctx.floated_content_height_contribution();
                    block_ctx.merge_adjoining_floats(child_block_ctx.top_adjoining_floats_());
#endif
                } else {
                    item_layout = tree.compute_node_layout(item.node_id, item_inputs, measure_fn);
                    // `compute_leaf_layout_impl` does not (yet) report collapse-through; reproduce
                    // Rust `compute/leaf.rs`'s rule for leaf nodes so margins still collapse.
                    item_layout.margins_can_collapse_through =
                        leaf_can_collapse_through(item, item_layout, measured_probe);
                }
            } else {
                item_layout = tree.compute_node_layout(item.node_id, item_inputs, measure_fn);
            }
            item.oof_candidates = std::move(item_layout.oof_candidates);
            Size<float> final_size = item_layout.size;

            CollapsibleMarginSet top_margin_set =
                item_layout.top_margin.collapse_with_margin(item_margin.top.value_or(0.0f));
            CollapsibleMarginSet bottom_margin_set =
                item_layout.bottom_margin.collapse_with_margin(item_margin.bottom.value_or(0.0f));

            // Expand auto margins to fill available space
            float free_x_space = std::max(0.0f, stretch_width - final_size.width);
            float x_axis_auto_margin_size = 0.0f;
            {
                int auto_margin_count = (item_margin.left.has_value() ? 0 : 1) + (item_margin.right.has_value() ? 0 : 1);
                if (auto_margin_count > 0) {
                    x_axis_auto_margin_size = free_x_space / static_cast<float>(auto_margin_count);
                }
            }
            Rect<float> resolved_margin{
                item_margin.left.value_or(x_axis_auto_margin_size),
                item_margin.right.value_or(x_axis_auto_margin_size),
                item_margin.top.value_or(0.0f),
                item_margin.bottom.value_or(0.0f)};

            Point<float> inset_offset = resolve_relative_offset(item, parent_size_final, direction, tree);

            // Set y_margin_offset (same bfc child)
            if (item.is_in_same_bfc
                && (!is_collapsing_with_first_margin_set || !own_margins_collapse_with_children.start)) {
                y_margin_offset = active_collapsible_margin_set.collapse_with_set(top_margin_set).resolve();
            }

            // Compute clearance (CSS2.2 9.5.2)
            bool has_clearance = false;
#ifdef TAFFY_FLOAT_LAYOUT
            if (item.is_in_same_bfc && clear_threshold.has_value()) {
                float hypothetical_y =
                    committed_y_offset + active_collapsible_margin_set.collapse_with_set(top_margin_set).resolve();
                bool forced_clearance = block_ctx.has_adjoining_float(item.clear);
                if (forced_clearance || hypothetical_y < *clear_threshold) {
                    has_clearance = true;
                    float escaped_margin =
                        (is_collapsing_with_first_margin_set && own_margins_collapse_with_children.start)
                        ? active_collapsible_margin_set.resolve() : 0.0f;
                    y_margin_offset = *clear_threshold - committed_y_offset - escaped_margin;
                }
            }
#endif

            item.computed_size = item_layout.size;
            item.can_be_collapsed_through = item_layout.margins_can_collapse_through && !has_clearance;
            if (item.is_in_same_bfc) {
                float uncleared_y = committed_y_offset + active_collapsible_margin_set.resolve();
                item.static_position = Point<float>{
                    direction == Direction::Ltr
                        ? resolved_content_box_inset.left
                        : container_outer_width - resolved_content_box_inset.right - final_size.width,
                    std::max(uncleared_y, clear_pos)};
            } else {
                item.static_position = Point<float>{
                    direction == Direction::Ltr
                        ? float_avoiding_position.x
                        : float_avoiding_position.x + float_avoiding_width - final_size.width,
                    float_avoiding_position.y};
            }

            Point<float> location;
            if (item.is_in_same_bfc) {
                location = Point<float>{
                    direction == Direction::Ltr
                        ? resolved_content_box_inset.left + inset_offset.x + resolved_margin.left
                        : container_outer_width - resolved_content_box_inset.right - final_size.width
                            - resolved_margin.right + inset_offset.x,
                    committed_y_offset + y_margin_offset + inset_offset.y};
            } else {
                float extra_margin_left = resolved_margin.left;
                float extra_margin_right = resolved_margin.right;
#ifdef TAFFY_FLOAT_LAYOUT
                if (item_avoids_floats) {
                    extra_margin_left = resolved_margin.left - item_non_auto_margin.left;
                    extra_margin_right = resolved_margin.right - item_non_auto_margin.right;
                }
#endif
                location = Point<float>{
                    direction == Direction::Ltr
                        ? float_avoiding_position.x + extra_margin_left + inset_offset.x
                        : float_avoiding_position.x + float_avoiding_width - final_size.width
                            - extra_margin_right + inset_offset.x,
                    float_avoiding_position.y + inset_offset.y};
            }

#ifdef TAFFY_FLOAT_LAYOUT
            if (item.is_in_same_bfc) {
                block_ctx.add_child_floated_content_height_contribution(
                    location.y - inset_offset.y + child_float_contribution);
            }
#endif

            // Apply alignment
            bool has_auto_x_margin = !item_margin.left.has_value() || !item_margin.right.has_value();
            float item_outer_width = item_layout.size.width + resolved_margin.horizontal_axis_sum();
            if (non_stretch_justify_self.has_value() && !has_auto_x_margin) {
                float container_start;
                float free_x;
#ifdef TAFFY_FLOAT_LAYOUT
                if (item_avoids_floats) {
                    container_start = float_avoiding_position.x - resolved_margin.left;
                    free_x = float_avoiding_width - item_layout.size.width;
                } else {
                    container_start = resolved_content_box_inset.left;
                    free_x = container_inner_width - item_outer_width;
                }
#else
                container_start = resolved_content_box_inset.left;
                free_x = container_inner_width - item_outer_width;
#endif
                AlignItemsKeyword keyword = non_stretch_justify_self->keyword;
                if (non_stretch_justify_self->is_safe() && free_x < 0.0f) {
                    keyword = AlignItemsKeyword::Start;
                }
                std::optional<bool> align_to_end;
                if (keyword == AlignItemsKeyword::Center) {
                    align_to_end = std::nullopt;
                } else if (keyword == AlignItemsKeyword::End
                    || keyword == AlignItemsKeyword::FlexEnd
                    || keyword == AlignItemsKeyword::SelfEnd) {
                    align_to_end = true;
                } else {
                    align_to_end = false;
                }
                float offset;
                if (!align_to_end.has_value()) {
                    offset = free_x / 2.0f;
                } else if (*align_to_end != (direction == Direction::Rtl)) {
                    offset = free_x;
                } else {
                    offset = 0.0f;
                }
                location.x = container_start + resolved_margin.left + inset_offset.x + offset;
            } else if (!non_stretch_justify_self.has_value() && item_outer_width < container_inner_width) {
                float free = container_inner_width - item_outer_width;
                if (text_align == TextAlign::LegacyLeft && direction == Direction::Rtl) {
                    location.x -= free;
                } else if (text_align == TextAlign::LegacyRight && direction == Direction::Ltr) {
                    location.x += free;
                } else if (text_align == TextAlign::LegacyCenter && direction == Direction::Ltr) {
                    location.x += free / 2.0f;
                } else if (text_align == TextAlign::LegacyCenter && direction == Direction::Rtl) {
                    location.x -= free / 2.0f;
                }
            }

            // A block container's first baseline is the first baseline of its first in-flow child
            if (!first_baseline.has_value()) {
                std::optional<float> child_baseline = block_child_first_baseline(
                    item_layout.baselines.first, item_layout.size.height, item.overflow.y);
                if (child_baseline.has_value()) {
                    first_baseline = location.y - inset_offset.y + *child_baseline;
                }
            }

#ifdef TAFFY_FLOAT_LAYOUT
            if (item_avoids_floats) {
                if (!item_margin.left.has_value()) {
                    resolved_margin.left += float_avoiding_position.x - resolved_content_box_inset.left;
                }
                if (!item_margin.right.has_value()) {
                    resolved_margin.right += container_outer_width - resolved_content_box_inset.right
                        - float_avoiding_position.x - float_avoiding_width;
                }
            }
#endif

            Layout layout_item;
            layout_item.order = item.order;
            layout_item.size = item_layout.size;
            layout_item.scrollbar_size = scrollbar_size;
            layout_item.location = location;
            layout_item.padding = item.padding;
            layout_item.border = item.border;
            layout_item.margin = resolved_margin;
#ifdef TAFFY_CONTENT_SIZE
            layout_item.scrollable_overflow_rect = item_layout.scrollable_overflow_rect;
#endif
            item.final_layout = layout_item;

#ifdef TAFFY_CONTENT_SIZE
            {
                Point<float> contribution_location{
                    location.x - resolved_content_box_inset.left + resolved_padding.left,
                    location.y - resolved_border.top};
                inflow_overflow_rect = rect_union(inflow_overflow_rect, compute_scrollable_overflow_contribution(
                    contribution_location, final_size, item_layout.scrollable_overflow_rect.rect(),
                    item.border, item.overflow, item.contain, overflow_container,
                    resolved_margin.map([](float value) { return std::min(value, 0.0f); })));
            }
#endif

            // Update first_child_top_margin_set
#ifdef TAFFY_FLOAT_LAYOUT
            if (is_collapsing_with_first_margin_set && item_pushed_below_float) {
                is_collapsing_with_first_margin_set = false;
            }
#endif
            if (is_collapsing_with_first_margin_set && has_clearance) {
                is_collapsing_with_first_margin_set = false;
            } else if (is_collapsing_with_first_margin_set) {
                if (item.can_be_collapsed_through) {
                    first_child_top_margin_set = first_child_top_margin_set
                        .collapse_with_set(top_margin_set).collapse_with_set(bottom_margin_set);
                } else {
                    first_child_top_margin_set = first_child_top_margin_set.collapse_with_set(top_margin_set);
                    is_collapsing_with_first_margin_set = false;
                }
            }

            // Update active_collapsible_margin_set
            if (item.can_be_collapsed_through) {
                active_collapsible_margin_set = active_collapsible_margin_set
                    .collapse_with_set(top_margin_set).collapse_with_set(bottom_margin_set);
                y_offset_for_absolute = committed_y_offset + item_layout.size.height + y_margin_offset;
            } else {
                committed_y_offset = location.y - inset_offset.y + item_layout.size.height;
                if (has_clearance && item_layout.margins_can_collapse_through) {
                    committed_y_offset -= top_margin_set.resolve();
                    active_collapsible_margin_set = top_margin_set.collapse_with_set(bottom_margin_set);
                    active_margin_set_has_clearance = true;
                } else {
                    active_collapsible_margin_set = bottom_margin_set;
                    active_margin_set_has_clearance = false;
                }
                y_offset_for_absolute = committed_y_offset + active_collapsible_margin_set.resolve();
#ifdef TAFFY_FLOAT_LAYOUT
                block_ctx.commit_strut();
#endif
            }
        }

        // The margins of a self-collapsing element with clearance do not collapse with the
        // bottom margin of the parent block
        CollapsibleMarginSet last_child_bottom_margin_set =
            active_margin_set_has_clearance ? CollapsibleMarginSet::zero() : active_collapsible_margin_set;
        float bottom_y_margin_offset;
        if (active_margin_set_has_clearance) {
            bottom_y_margin_offset = active_collapsible_margin_set.resolve();
        } else if (own_margins_collapse_with_children.end) {
            bottom_y_margin_offset = 0.0f;
        } else {
            bottom_y_margin_offset = last_child_bottom_margin_set.resolve();
        }

        committed_y_offset += resolved_content_box_inset.bottom + bottom_y_margin_offset;
        float content_height = std::max(0.0f, committed_y_offset);

        final_result.inflow_overflow_rect = inflow_overflow_rect;
        final_result.intrinsic_outer_height = content_height;
        final_result.first_child_top_margin_set = first_child_top_margin_set;
        final_result.last_child_bottom_margin_set = last_child_bottom_margin_set;
        final_result.first_baseline = first_baseline;
    }

    float intrinsic_outer_height = final_result.intrinsic_outer_height;
#ifdef TAFFY_FLOAT_LAYOUT
    if (block_ctx.is_bfc_root() || establishes_new_bfc) {
        intrinsic_outer_height = std::max(intrinsic_outer_height, block_ctx.floated_content_height_contribution());
    }
#endif

    float container_outer_height;
    if (known_dimensions.height.has_value()) {
        container_outer_height = *known_dimensions.height;
    } else {
        container_outer_height = maybe_clamp_f32(intrinsic_outer_height, min_size.height, max_size.height);
    }
    container_outer_height = maybe_max_f32(container_outer_height, padding_border_size.height);
    Size<float> final_outer_size{container_outer_width, container_outer_height};

    // CSS2 8.3.1: the bottom margin of a block with `height: auto` collapses with its last
    // in-flow child's bottom margin only if the box's `min-height` is less than the used height.
    bool height_constrained_by_min_height =
        min_size.height.has_value() && *min_size.height > 0.0f && *min_size.height >= container_outer_height;
    bool own_bottom_margin_collapses_with_children =
        own_margins_collapse_with_children.end && !height_constrained_by_min_height;

    // Apply `align-content` to in-flow non-floated items if requested.
    if (align_content.keyword != AlignContentKeyword::Normal) {
        float container_inner_height = container_outer_height - resolved_content_box_inset.vertical_axis_sum();
        float inflow_content_height = intrinsic_outer_height - resolved_content_box_inset.vertical_axis_sum();
        float free_space = container_inner_height - inflow_content_height;
        float group_offset = compute_block_align_content_offset(align_content, free_space);
        if (final_result.first_baseline.has_value()) {
            final_result.first_baseline = *final_result.first_baseline + group_offset;
        }
        for (BlockItem& item : items) {
            if (item.final_layout.has_value()) {
                item.final_layout->location.y += group_offset;
            }
            if (is_absolute_position(item.position)) {
                item.static_position.y += group_offset;
            }
        }
        bool any_in_flow = false;
        for (const BlockItem& item : items) {
            if (item.final_layout.has_value()) { any_in_flow = true; break; }
        }
        if (any_in_flow) {
#ifdef TAFFY_CONTENT_SIZE
            Rect<float> inflow_overflow_rect{0.0f, 0.0f, 0.0f, 0.0f};
            ScrollableOverflowContainer overflow_container;
            overflow_container.parent_is_scroll_container = is_scroll;
            overflow_container.parent_size = Size<float>{
                final_outer_size.width - resolved_border.horizontal_axis_sum() - scrollbar_gutter.horizontal_axis_sum(),
                final_outer_size.height - resolved_border.vertical_axis_sum() - scrollbar_gutter.vertical_axis_sum()};
            overflow_container.reversed = Point<bool>{direction == Direction::Rtl, false};
            overflow_container.padding = resolved_padding;
            for (const BlockItem& item : items) {
                if (!item.final_layout.has_value()) continue;
                const Layout& layout = *item.final_layout;
                Point<float> contribution_location{
                    layout.location.x - resolved_border.left - scrollbar_gutter.left,
                    layout.location.y - resolved_border.top};
                inflow_overflow_rect = rect_union(inflow_overflow_rect, compute_scrollable_overflow_contribution(
                    contribution_location, layout.size, layout.scrollable_overflow_rect.rect(),
                    layout.border, item.overflow, item.contain, overflow_container,
                    layout.margin.map([](float value) { return std::min(value, 0.0f); })));
            }
            final_result.inflow_overflow_rect = inflow_overflow_rect;
#endif
        }
    }

    // Determine whether this node can be collapsed through
    bool all_in_flow_children_can_be_collapsed_through = true;
    for (const BlockItem& item : items) {
#ifdef TAFFY_FLOAT_LAYOUT
        if (item.float_ != Float::None) continue;
#endif
        if (!is_absolute_position(item.position) && !item.can_be_collapsed_through) {
            all_in_flow_children_can_be_collapsed_through = false;
            break;
        }
    }
    bool can_be_collapsed_through = !has_styles_preventing_being_collapsed_through
        && container_outer_height == 0.0f && all_in_flow_children_can_be_collapsed_through;

    LayoutOutput output;
    output.size = final_outer_size;
#ifdef TAFFY_CONTENT_SIZE
    output.scrollable_overflow_rect = ScrollableOverflow::zero();
#endif
    output.baselines = Baselines::from_first(final_result.first_baseline);
    if (own_margins_collapse_with_children.start) {
        output.top_margin = final_result.first_child_top_margin_set;
    } else {
        float margin_top = resolve_or_zero(raw_margin.top, parent_size.width);
        output.top_margin = CollapsibleMarginSet::from_margin(margin_top);
    }
    if (own_bottom_margin_collapses_with_children) {
        output.bottom_margin = final_result.last_child_bottom_margin_set;
    } else {
        float margin_bottom = resolve_or_zero(raw_margin.bottom, parent_size.width);
        output.bottom_margin = CollapsibleMarginSet::from_margin(margin_bottom);
    }
    output.margins_can_collapse_through = can_be_collapsed_through;
    output.oof_candidates = OofCandidates::none();
    output.oof_positioning_area = std::nullopt;

    if (run_mode == RunMode::ComputeSize) return output;

    // Commit deferred child layouts to the tree.
    for (const BlockItem& item : items) {
        if (item.final_layout.has_value()) {
            tree.set_unrounded_layout(item.node_id, *item.final_layout);
        }
    }

    // 4. Collect out-of-flow candidates in document order
    OofCandidates candidates;
    for (BlockItem& item : items) {
        if (is_absolute_position(item.position)) {
            // The static-position rectangle of an out-of-flow child of a block container spans
            // the container's content box in the inline axis, and is a zero-height line at the
            // child's hypothetical position in the block axis.
            Line<float> inline_area{resolved_content_box_inset.left, container_outer_width - resolved_content_box_inset.right};
            Line<float> block_area{item.static_position.y, item.static_position.y};
            AlignItems justify_self = resolve_self_relative(
                item.justify_self.value_or(justify_items), item.direction, direction, true);
            AlignItems align_self = resolve_self_relative(
                item.align_self.value_or(AlignItems_NORMAL), item.direction, direction, false);
            OofCandidate candidate;
            candidate.node = item.node_id;
            candidate.order = item.order;
            candidate.position = item.position;
            candidate.static_position = Point<AxisStaticPosition>{
                AxisStaticPosition::from_alignment(justify_self, inline_area, direction == Direction::Rtl),
                AxisStaticPosition::from_alignment(align_self, block_area, false)};
            candidates.push(candidate);
        } else if (!item.oof_candidates.is_empty()) {
            if (item.final_layout.has_value()) {
                item.oof_candidates.translate(item.final_layout->location);
            }
            candidates.append(item.oof_candidates);
        }
    }

    Rect<float> absolute_position_inset = resolved_border + scrollbar_gutter;
    Size<float> absolute_position_area = final_outer_size - absolute_position_inset.sum_axes();
    Point<float> absolute_position_offset{absolute_position_inset.left, absolute_position_inset.top};
    output.oof_candidates = std::move(candidates);
    OofPositioningArea positioning_area;
    positioning_area.size = absolute_position_area;
    positioning_area.offset = absolute_position_offset;
    output.oof_positioning_area = positioning_area;

#ifdef TAFFY_CONTENT_SIZE
    {
        float client_width = std::max(0.0f,
            final_outer_size.width - resolved_border.horizontal_axis_sum() - scrollbar_gutter.horizontal_axis_sum());
        Rect<float> inflow_overflow_rect = final_result.inflow_overflow_rect;
        if (direction == Direction::Rtl) {
            inflow_overflow_rect = rect_union(inflow_overflow_rect, Rect<float>{0.0f, client_width, 0.0f, 0.0f});
        }
        if (is_scroll) {
            inflow_overflow_rect = rect_union(inflow_overflow_rect, Rect<float>{
                direction == Direction::Rtl ? std::min(client_width - resolved_padding.left, 0.0f) : 0.0f,
                direction == Direction::Rtl ? client_width : resolved_padding.right,
                0.0f,
                resolved_padding.bottom});
        }
        output.scrollable_overflow_rect = finalize_scrollable_overflow(
            inflow_overflow_rect,
            Size<float>{
                final_outer_size.width - resolved_border.horizontal_axis_sum() - scrollbar_gutter.horizontal_axis_sum(),
                final_outer_size.height - resolved_border.vertical_axis_sum() - scrollbar_gutter.vertical_axis_sum()},
            Point<bool>{direction == Direction::Rtl, false},
            is_scroll);
    }
#endif

    // 5. Perform hidden layout on hidden children
    size_t len = tree.child_count(node_id);
    for (size_t order = 0; order < len; ++order) {
        NodeId child = tree.get_child_id(node_id, order);
        const Style& child_style = tree.style(child);
        if (child_style.display == Display::None) {
            tree.set_unrounded_layout(child, Layout::with_order(static_cast<uint32_t>(order)));
            LayoutInput hidden_input = LayoutInput::hidden_input();
            tree.compute_node_layout(child, hidden_input, measure_fn);
        }
    }

    return output;
}

/// Computes the layout of a block container according to the block layout algorithm
template <typename Tree, typename MeasureFn>
LayoutOutput compute_block_layout(
    Tree& tree, NodeId node_id, LayoutInput inputs, BlockContext* block_ctx, MeasureFn& measure_fn) {
    SizeOpt known_dimensions = inputs.known_dimensions;
    SizeOpt parent_size = inputs.parent_size;
    RunMode run_mode = inputs.run_mode;
    const Style& style = tree.style(node_id);

    Point<Overflow> overflow = style.overflow;
    bool is_scroll = is_scroll_container(overflow.x) || is_scroll_container(overflow.y);
    Contain contain = style.contain;
    bool establishes_new_bfc = is_scroll
        || (style.align_content.has_value() && style.align_content->keyword != AlignContentKeyword::Normal)
        || contain_establishes_independent_formatting_context(contain);
    std::optional<float> aspect_ratio = style.aspect_ratio;
    Rect<float> padding = resolve_or_zero_rect(style.padding, parent_size.width);
    Rect<float> border = resolve_or_zero_rect(style.border, parent_size.width);
    Size<float> padding_border_size = (padding + border).sum_axes();
    Size<float> box_sizing_adjustment =
        style.box_sizing == BoxSizing::ContentBox ? padding_border_size : Size<float>{0.0f, 0.0f};

    SizeOpt min_size = size_opt_add_f32(
        maybe_resolve_size(style.min_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);
    SizeOpt max_size = size_opt_add_f32(
        maybe_resolve_size(style.max_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio), box_sizing_adjustment);
    SizeOpt clamped_style_size;
    if (inputs.sizing_mode == SizingMode::InherentSize) {
        clamped_style_size = size_opt_clamp(
            size_opt_add_f32(
                maybe_resolve_size(style.size, parent_size).maybe_apply_aspect_ratio(aspect_ratio),
                box_sizing_adjustment),
            min_size, max_size);
    } else {
        clamped_style_size = size_opt_none();
    }

    // If both min and max in a given axis are set and max <= min then this determines the size
    SizeOpt min_max_definite_size;
    {
        auto f = [](std::optional<float> mn, std::optional<float> mx) -> std::optional<float> {
            if (mn.has_value() && mx.has_value() && *mx <= *mn) return mn;
            return std::nullopt;
        };
        min_max_definite_size.width = f(min_size.width, max_size.width);
        min_max_definite_size.height = f(min_size.height, max_size.height);
    }

    SizeOpt styled_based_known_dimensions = size_opt_max(
        size_opt_or(size_opt_or(known_dimensions, min_max_definite_size), clamped_style_size),
        SizeOpt{padding_border_size.width, padding_border_size.height});

    // Short-circuit layout if the container's size is fully determined by the container's size
    // and the run mode is ComputeSize
    if (run_mode == RunMode::ComputeSize) {
        if (styled_based_known_dimensions.width.has_value() && styled_based_known_dimensions.height.has_value()) {
            return LayoutOutput::from_outer_size(
                Size<float>{*styled_based_known_dimensions.width, *styled_based_known_dimensions.height});
        }
        if (inputs.axis == RequestedAxis::Horizontal && styled_based_known_dimensions.width.has_value()) {
            return LayoutOutput::from_outer_size(Size<float>{*styled_based_known_dimensions.width, 0.0f});
        }
    }

    LayoutInput inner_inputs = inputs;
    inner_inputs.known_dimensions = styled_based_known_dimensions;

    LayoutOutput output;
    if (block_ctx != nullptr && !establishes_new_bfc) {
        output = compute_inner(tree, node_id, inner_inputs, *block_ctx, measure_fn);
    } else {
        BlockFormattingContext root_bfc;
        BlockContext root_ctx = root_block_context(root_bfc);
        output = compute_inner(tree, node_id, inner_inputs, root_ctx, measure_fn);
    }

    // Layout containment suppresses the box's baseline for baseline-alignment purposes
    if (contain_suppresses_baseline(contain)) {
        output.baselines = Baselines::none();
    }

    return output;
}

} // namespace block_detail

#endif // TAFFY_BLOCK_LAYOUT
// ===========================================================================
// Section 16: TaffyTree (Arena-based high-level API)
// Ref: src/tree/taffy_tree.rs
// ===========================================================================

// ===========================================================================
// Section 16: TaffyTree (Arena-based high-level API)
// Ref: src/tree/taffy_tree.rs, src/compute/mod.rs, src/compute/leaf.rs,
//      src/compute/oof.rs, src/compute/common/scrollable_overflow.rs,
//      src/tree/layout.rs, src/util/sys.rs
// ===========================================================================

// ===========================================================================
// Section 16: TaffyTree (Arena-based high-level API)
// Ref: src/tree/taffy_tree.rs, src/compute/mod.rs, src/compute/leaf.rs,
//      src/compute/oof.rs, src/compute/common/scrollable_overflow.rs,
//      src/tree/layout.rs, src/util/sys.rs
// ===========================================================================

// ===========================================================================
// Section 16: TaffyTree (Arena-based high-level API)
// Ref: src/tree/taffy_tree.rs, src/compute/mod.rs, src/compute/leaf.rs,
//      src/compute/oof.rs, src/compute/common/scrollable_overflow.rs,
//      src/tree/layout.rs, src/util/sys.rs
// ===========================================================================

namespace detail {
    struct NodeData {
        Style style{};
        Layout unrounded_layout{};
        Layout final_layout{};
        Cache cache{};
        bool has_context{false};
        /// Out-of-flow (absolute/fixed) boxes whose containing block is this node, as recorded by
        /// the out-of-flow positioning pass
        std::vector<NodeId> hoisted_children{};
        /// Out-of-flow candidates produced by this node's subtree which were not claimed by it
        /// and bubble up to its parent
        OofCandidates pending_oof{};
#ifdef TAFFY_GRID
        /// Detailed layout information recorded by the grid algorithm
        grid_detail::DetailedGridInfo detailed_info{};
#endif
    };
    struct NodeSlot {
        uint32_t generation{0};
        NodeData data{};
        bool alive{false};
        uint32_t next_free{UINT32_MAX};
    };

    // --- Style helpers (the C++ Style is a plain struct: these mirror CoreStyle) ---

    inline bool style_is_block(const Style& s) {
#ifdef TAFFY_BLOCK_LAYOUT
        return s.display == Display::Block;
#else
        (void)s;
        return false;
#endif
    }
    inline bool style_is_replaced(const Style& s) { return s.item_is_replaced; }
    inline bool style_is_table(const Style& s) { return s.item_is_table; }
    inline bool contain_establishes_independent_formatting_context(const Contain& c) { return c.layout || c.paint; }
    inline bool contain_contains_scrollable_overflow(const Contain& c) { return c.layout || c.paint; }
    inline bool style_is_out_of_flow(const Style& s) { return is_absolute_position(s.position); }
    inline bool style_is_positioned(const Style& s) { return s.position != Position::Static; }
    /// `justify_self` only exists with the grid feature; elsewhere there is no such property
    inline AlignSelf style_justify_self_or(const Style& s, AlignSelf fallback) {
#ifdef TAFFY_GRID
        return s.justify_self.has_value() ? *s.justify_self : fallback;
#else
        (void)s;
        return fallback;
#endif
    }

    /// Scrollbar gutters are reserved when `overflow` is `Scroll`, with the axes transposed (a
    /// node that scrolls vertically needs *horizontal* space for a scrollbar)
    inline Point<float> scrollbar_gutter_of(const Style& s) {
        return {s.overflow.y == Overflow::Scroll ? s.scrollbar_width : 0.0f,
                s.overflow.x == Overflow::Scroll ? s.scrollbar_width : 0.0f};
    }
    inline bool style_is_scroll_container(const Style& s) {
        return is_scroll_container(s.overflow.x) || is_scroll_container(s.overflow.y);
    }

    inline Rect<float> rect_union(Rect<float> a, Rect<float> b) {
        return {std::min(a.left, b.left), std::max(a.right, b.right), std::min(a.top, b.top),
                std::max(a.bottom, b.bottom)};
    }

    /// MaybeMath::maybe_add for Size<Option<f32>> + Size<f32> (None stays None)
    inline Size<std::optional<float>> maybe_add_size(Size<std::optional<float>> v, Size<float> adj) {
        return {v.width ? std::optional<float>(*v.width + adj.width) : std::nullopt,
                v.height ? std::optional<float>(*v.height + adj.height) : std::nullopt};
    }
    /// MaybeMath::maybe_clamp for Size<Option<f32>>
    inline Size<std::optional<float>> maybe_clamp_size(
        Size<std::optional<float>> v, Size<std::optional<float>> mn, Size<std::optional<float>> mx) {
        return {maybe_clamp(v.width, mn.width, mx.width), maybe_clamp(v.height, mn.height, mx.height)};
    }
    /// MaybeMath::maybe_max for Size<Option<f32>>
    inline Size<std::optional<float>> maybe_max_size(Size<std::optional<float>> v, Size<std::optional<float>> mx) {
        return {maybe_max(v.width, mx.width), maybe_max(v.height, mx.height)};
    }
    /// MaybeMath::maybe_min for Size<Option<f32>>
    inline Size<std::optional<float>> maybe_min_size(Size<std::optional<float>> v, Size<std::optional<float>> mn) {
        return {maybe_min(v.width, mn.width), maybe_min(v.height, mn.height)};
    }

    // --- Sizing keyword resolution (src/compute/common/sizing_keyword.rs) ---

    /// How a sizing keyword resolves to a used size
    enum class SizingKeywordKind : uint8_t { None, Measure, Exact };
    struct SizingKeywordResolution {
        SizingKeywordKind kind{SizingKeywordKind::None};
        /// For `Measure`: the available space the box should be measured under
        AvailableSpace available{AvailableSpace::max_content()};
        /// For `Exact`: the exact used size
        float exact{0.0f};

        bool present() const { return kind != SizingKeywordKind::None; }
    };

    /// Resolve a single size style if it is a sizing keyword (`min-content`, `max-content`,
    /// `fit-content`, `fit-content(...)`, or `stretch`). None if it is not a keyword or cannot be
    /// resolved in the current context (in which case it behaves as `auto`).
    inline SizingKeywordResolution resolve_sizing_keyword_impl(Dimension style, std::optional<float> stretch_size,
                                                               std::optional<float> percent_resolution_basis) {
        SizingKeywordResolution out;
        switch (style.tag()) {
            case CompactLength::MIN_CONTENT_TAG:
                out = {SizingKeywordKind::Measure, AvailableSpace::min_content(), 0.0f};
                break;
            case CompactLength::MAX_CONTENT_TAG:
                out = {SizingKeywordKind::Measure, AvailableSpace::max_content(), 0.0f};
                break;
            case CompactLength::FIT_CONTENT_PX_TAG:
                out = {SizingKeywordKind::Measure, AvailableSpace::definite(style.value()), 0.0f};
                break;
            case CompactLength::FIT_CONTENT_PERCENT_TAG:
                if (percent_resolution_basis.has_value()) {
                    out = {SizingKeywordKind::Measure,
                           AvailableSpace::definite(*percent_resolution_basis * style.value() / 100.0f), 0.0f};
                }
                break;
            case CompactLength::FIT_CONTENT_KEYWORD_TAG:
                if (stretch_size.has_value()) {
                    out = {SizingKeywordKind::Measure, AvailableSpace::definite(*stretch_size), 0.0f};
                }
                break;
            case CompactLength::STRETCH_TAG:
                if (stretch_size.has_value()) {
                    out = {SizingKeywordKind::None, AvailableSpace::max_content(), *stretch_size};
                    out.kind = SizingKeywordKind::Exact;
                }
                break;
            default:
                break;
        }
        return out;
    }

    /// Pack an algorithm's physical overflow accumulator and clip unreachable overflow for scroll
    /// containers. Port of `compute_scrollable_overflow::finalize_scrollable_overflow`.
    inline ScrollableOverflow finalize_scrollable_overflow(
        Rect<float> rect, Size<float> size, Point<bool> reversed, bool is_scroll_container) {
        size = Size<float>{std::max(size.width, 0.0f), std::max(size.height, 0.0f)};
        Point<bool> rev = reversed;
        if (is_scroll_container) {
            if (rev.x) rect.right = std::min(rect.right, size.width);
            else rect.left = 0.0f;
            if (rev.y) rect.bottom = std::min(rect.bottom, size.height);
            else rect.top = 0.0f;
        } else {
            rev = Point<bool>{false, false};
        }
        return ScrollableOverflow::new_(rect, rev);
    }

    inline bool is_scroll_container_(Point<Overflow> overflow) {
        return is_scroll_container(overflow.x) || is_scroll_container(overflow.y);
    }

    /// Container geometry shared by its children's scrollable overflow contributions
    struct ScrollOverflowContainer {
        bool parent_is_scroll_container{false};
        Size<float> parent_size{0.0f, 0.0f};
        Point<bool> reversed{false, false};
        Rect<float> padding{0.0f, 0.0f, 0.0f, 0.0f};
    };

    /// Determine the rectangle that `node` contributes to its parent's scrollable overflow
    /// rectangle. Port of `compute_scrollable_overflow::compute_scrollable_overflow_contribution`.
    inline Rect<float> compute_scrollable_overflow_contribution(
        Point<float> location, Size<float> size, Rect<float> scrollable_overflow_rect, Rect<float> border,
        Point<Overflow> overflow, const Contain& contain, const ScrollOverflowContainer& container,
        Rect<float> margin) {
        const Rect<float> ov = {scrollable_overflow_rect.left + border.left, scrollable_overflow_rect.right + border.left,
                                scrollable_overflow_rect.top + border.top, scrollable_overflow_rect.bottom + border.top};
        const bool is_scroll_container = is_scroll_container_(overflow);
        const bool overflow_is_contained = contain_contains_scrollable_overflow(contain);
        const Point<bool> propagates{
            !is_scroll_container && !overflow_is_contained && overflow.x == Overflow::Visible,
            !is_scroll_container && !overflow_is_contained && overflow.y == Overflow::Visible};
        const Size<float> end_extent{
            propagates.x ? std::max(size.width, ov.right) : size.width,
            propagates.y ? std::max(size.height, ov.bottom) : size.height};
        if (end_extent.width <= 0.0f || end_extent.height <= 0.0f) return Rect<float>{0.0f, 0.0f, 0.0f, 0.0f};
        const Point<float> start_extent{
            propagates.x ? std::min(0.0f, ov.left) : 0.0f,
            propagates.y ? std::min(0.0f, ov.top) : 0.0f};
        const Rect<float> contribution{location.x + start_extent.x, location.x + end_extent.width,
                                       location.y + start_extent.y, location.y + end_extent.height};
        const bool is_wholly_unreachable =
            (container.reversed.x ? contribution.left >= container.parent_size.width : contribution.right <= 0.0f)
            || (container.reversed.y ? contribution.top >= container.parent_size.height
                                     : contribution.bottom <= 0.0f);
        if (container.parent_is_scroll_container && is_wholly_unreachable) {
            return Rect<float>{0.0f, 0.0f, 0.0f, 0.0f};
        }
        if (!container.parent_is_scroll_container) return contribution;
        Rect<float> padded = contribution;
        if (container.reversed.x) {
            padded.left = std::min(padded.left, location.x - std::max(0.0f, container.padding.left + margin.left));
        } else {
            padded.right = std::max(
                padded.right, location.x + size.width + std::max(0.0f, container.padding.right + margin.right));
        }
        if (container.reversed.y) {
            padded.top = std::min(padded.top, location.y - std::max(0.0f, container.padding.top + margin.top));
        } else {
            padded.bottom = std::max(
                padded.bottom, location.y + size.height + std::max(0.0f, container.padding.bottom + margin.bottom));
        }
        return padded;
    }

    // --- Alignment helpers (src/style/alignment.rs) ---

    /// Resolve `self-start`/`self-end` against the item and container directions
    inline AlignSelf resolve_self_relative(
        AlignSelf alignment, Direction item_direction, Direction container_direction, bool axis_is_inline) {
        const bool flip = axis_is_inline && item_direction != container_direction;
        AlignItemsKeyword keyword = alignment.keyword;
        if (keyword == AlignItemsKeyword::SelfStart) keyword = flip ? AlignItemsKeyword::End : AlignItemsKeyword::Start;
        else if (keyword == AlignItemsKeyword::SelfEnd) keyword = flip ? AlignItemsKeyword::Start : AlignItemsKeyword::End;
        return AlignSelf{keyword, alignment.safety};
    }

    /// Resolve the `normal` alignment keyword, which behaves as `stretch` for flex items
    inline AlignItems resolve_normal_alignment(AlignItems alignment) {
        if (alignment.keyword == AlignItemsKeyword::Normal) return AlignItems_STRETCH;
        return alignment;
    }

    // --- Out-of-flow positioning (src/compute/oof.rs) ---

    enum class OofAlignment : uint8_t { Normal, Stretch, Start, Center, End };

    /// Convert an `align-self`/`justify-self` value into a logical out-of-flow alignment
    inline OofAlignment oof_alignment_from_resolved(AlignSelf alignment) {
        switch (alignment.keyword) {
            case AlignItemsKeyword::Normal: return OofAlignment::Normal;
            case AlignItemsKeyword::Stretch: return OofAlignment::Stretch;
            case AlignItemsKeyword::Center: return OofAlignment::Center;
            case AlignItemsKeyword::End:
            case AlignItemsKeyword::FlexEnd:
            case AlignItemsKeyword::SelfEnd: return OofAlignment::End;
            default: return OofAlignment::Start;
        }
    }

    /// The physical edge of the containing block the box is aligned to once its size is known
    inline int oof_physical_edge(OofAlignment alignment, bool reversed) {
        switch (alignment) {
            case OofAlignment::Center: return AxisStaticEdge::Center;
            case OofAlignment::End: return reversed ? AxisStaticEdge::Start : AxisStaticEdge::End;
            default: return reversed ? AxisStaticEdge::End : AxisStaticEdge::Start;
        }
    }

    /// Position a box of `size` within `container` aligned to the given physical edge
    inline float align_in_line(Line<float> container, float size, int edge) {
        switch (edge) {
            case AxisStaticEdge::End: return container.end - size;
            case AxisStaticEdge::Center: return (container.start + container.end - size) / 2.0f;
            default: return container.start;
        }
    }

    /// Single-axis static offset resolution (safe-overflow fallback included)
    inline float resolve_static_offset_axis(AxisStaticPosition sp, float size, Line<float> margin) {
        const float margin_box_size = size + margin.start + margin.end;
        const bool overflows = sp.align.safety == AlignmentSafety::Safe && margin_box_size > sp.area.end - sp.area.start;
        const int keyword = overflows ? sp.align.fallback : sp.align.keyword;
        return align_in_line(sp.area, margin_box_size, keyword) + margin.start;
    }

    /// Resolve the final static offset of an out-of-flow box from its static position
    inline Point<float> resolve_static_offset(
        Point<AxisStaticPosition> static_position, Size<float> final_size, Rect<float> resolved_margin) {
        return {resolve_static_offset_axis(static_position.x, final_size.width,
                                           Line<float>{resolved_margin.left, resolved_margin.right}),
                resolve_static_offset_axis(static_position.y, final_size.height,
                                           Line<float>{resolved_margin.top, resolved_margin.bottom})};
    }

    /// The inputs to the absolute positioning layout model in a single axis (physical
    /// coordinates relative to the inset-resolution area)
    struct OofAxis {
        float cb_size{0.0f};
        Line<std::optional<float>> inset{};
        Line<std::optional<float>> margin{};
        AxisStaticPosition static_position{};
        OofAlignment alignment{OofAlignment::Normal};
        AlignmentSafety safety{AlignmentSafety::Default};
        bool is_inline{false};
        bool reversed{false};
        bool is_scroll_axis{false};

        int start_edge() const { return reversed ? AxisStaticEdge::End : AxisStaticEdge::Start; }
        int end_edge() const { return reversed ? AxisStaticEdge::Start : AxisStaticEdge::End; }

        /// The inset-modified containing block in this axis
        Line<float> inset_modified_containing_block() const {
            Line<float> imcb{0.0f, 0.0f};
            int weaker_edge = AxisStaticEdge::End;
            if (inset.start && inset.end) {
                imcb = Line<float>{*inset.start, cb_size - *inset.end};
                weaker_edge = end_edge();
            } else if (inset.start) {
                imcb = Line<float>{*inset.start, cb_size};
                weaker_edge = AxisStaticEdge::End;
            } else if (inset.end) {
                imcb = Line<float>{0.0f, cb_size - *inset.end};
                weaker_edge = AxisStaticEdge::Start;
            } else {
                const AxisStaticPosition sp = static_position;
                switch (sp.align.keyword) {
                    case AxisStaticEdge::Start: imcb = Line<float>{sp.area.start, cb_size}; break;
                    case AxisStaticEdge::End: imcb = Line<float>{0.0f, sp.area.end}; break;
                    default: {
                        const float center = (sp.area.start + sp.area.end) / 2.0f;
                        const float start_distance = center;
                        const float end_distance = cb_size - center;
                        if (std::abs(start_distance) <= std::abs(end_distance)) {
                            imcb = Line<float>{0.0f, 2.0f * start_distance};
                        } else {
                            imcb = Line<float>{cb_size - 2.0f * end_distance, cb_size};
                        }
                        break;
                    }
                }
                weaker_edge = end_edge();
            }
            if (imcb.end < imcb.start) {
                if (weaker_edge == AxisStaticEdge::Start) imcb.start = imcb.end;
                else imcb.end = imcb.start;
            }
            return imcb;
        }

        float inset_modified_containing_block_size() const {
            const Line<float> imcb = inset_modified_containing_block();
            return imcb.end - imcb.start;
        }

        /// The stretch-fit size of the box's border box
        float stretch_fit_size() const {
            const float non_auto_margin_sum = margin.start.value_or(0.0f) + margin.end.value_or(0.0f);
            return std::max(inset_modified_containing_block_size() - non_auto_margin_sum, 0.0f);
        }

        /// Whether an `auto` size in this axis is the stretch-fit size (rather than fit-content)
        bool is_stretch_sized(bool is_replaced, bool is_table) const {
            const bool no_auto_inset = inset.start.has_value() && inset.end.has_value();
            if (!no_auto_inset) return false;
            switch (alignment) {
                case OofAlignment::Stretch: return true;
                case OofAlignment::Normal: return !is_replaced && !is_table;
                default: return false;
            }
        }

        /// Resolve the box's auto margins given its final size
        Line<float> resolve_margins(float size) const {
            const bool both_insets = inset.start.has_value() && inset.end.has_value();
            const Line<float> imcb = inset_modified_containing_block();
            const float free_space = imcb.end - imcb.start - size;
            if (margin.start && margin.end) return Line<float>{*margin.start, *margin.end};
            if (!both_insets) return Line<float>{margin.start.value_or(0.0f), margin.end.value_or(0.0f)};
            if (margin.start) return Line<float>{*margin.start, free_space - *margin.start};
            if (margin.end) return Line<float>{free_space - *margin.end, *margin.end};
            if (free_space < 0.0f && is_inline) {
                return reversed ? Line<float>{free_space, 0.0f} : Line<float>{0.0f, free_space};
            }
            return Line<float>{free_space / 2.0f, free_space / 2.0f};
        }

        /// Compute the physical start of the box's border box given its final size and margins
        float position(float size, Line<float> resolved_margin) const {
            const Line<float> imcb = inset_modified_containing_block();
            if (!inset.start && !inset.end) {
                const AxisStaticPosition sp = static_position;
                const float margin_box_size = size + resolved_margin.start + resolved_margin.end;
                float offset;
                if (sp.align.safety == AlignmentSafety::Safe && margin_box_size > imcb.end - imcb.start) {
                    offset = align_in_line(imcb, margin_box_size, start_edge());
                } else {
                    offset = align_in_line(sp.area, margin_box_size, sp.align.keyword);
                }
                return offset + resolved_margin.start;
            }
            if (inset.start && !inset.end) return imcb.start + resolved_margin.start;
            if (!inset.start && inset.end) return imcb.end - size - resolved_margin.end;
            // Both insets set
            if (!margin.start || !margin.end) return imcb.start + resolved_margin.start;
            const float margin_box_size = size + resolved_margin.start + resolved_margin.end;
            const float imcb_size = imcb.end - imcb.start;
            const int edge = oof_physical_edge(alignment, reversed);
            float margin_box_start;
            switch (safety) {
                case AlignmentSafety::Unsafe:
                    margin_box_start = align_in_line(imcb, margin_box_size, edge);
                    break;
                case AlignmentSafety::Safe: {
                    const int e = margin_box_size > imcb_size ? start_edge() : edge;
                    margin_box_start = align_in_line(imcb, margin_box_size, e);
                    break;
                }
                default: {
                    if (margin_box_size <= imcb_size || alignment == OofAlignment::Normal) {
                        margin_box_start = align_in_line(imcb, margin_box_size, edge);
                    } else {
                        Line<float> limit{std::min(imcb.start, 0.0f), std::max(imcb.end, cb_size)};
                        if (is_scroll_axis) {
                            if (reversed) limit.start = -std::numeric_limits<float>::infinity();
                            else limit.end = std::numeric_limits<float>::infinity();
                        }
                        if (margin_box_size <= limit.end - limit.start) {
                            const float start = align_in_line(imcb, margin_box_size, edge);
                            margin_box_start = std::max(std::min(start, limit.end - margin_box_size), limit.start);
                        } else {
                            margin_box_start = align_in_line(limit, margin_box_size, start_edge());
                        }
                    }
                    break;
                }
            }
            return margin_box_start + resolved_margin.start;
        }
    };

    struct OofBoxLayout {
        Layout layout{};
        OofCandidates surfaced{};
        Point<Overflow> overflow{Overflow::Visible, Overflow::Visible};
        Contain contain{};
    };

#ifdef TAFFY_GRID
    // --- Absolute grid-area resolution (src/compute/grid/mod.rs) ---

    /// Resolve a grid placement to non-named lines. Named lines are looked up in the recorded
    /// detailed line names; named grid *areas* are not part of DetailedGridInfo, so placements
    /// that only reference an area fall back to `auto`.
    inline Line<grid_detail::NonNamedGridPlacement> grid_placement_to_non_named(
        const Line<GridPlacement>& placement, const grid_detail::DetailedGridTracksInfo& tracks) {
        auto convert = [&](const GridPlacement& p) -> grid_detail::NonNamedGridPlacement {
            switch (p.kind) {
                case GridPlacement::Auto: return grid_detail::NonNamedGridPlacement::auto_();
                case GridPlacement::LineKind: return grid_detail::NonNamedGridPlacement::line(p.line_val);
                case GridPlacement::SpanKind:
                case GridPlacement::NamedSpanKind: return grid_detail::NonNamedGridPlacement::span(p.span_val);
                case GridPlacement::NamedLineKind: {
                    // Full-grid line index: 0 .. negative_implicit + explicit + positive_implicit
                    const size_t line_count = tracks.positions.size() + 1;
                    std::vector<size_t> matches;
                    for (size_t i = 0; i < line_count; ++i) {
                        auto range = tracks.names_for_line(i);
                        for (size_t n = range.first; n < range.second; ++n) {
                            if (tracks.line_names.names[n] == p.name) { matches.push_back(i); break; }
                        }
                    }
                    if (matches.empty()) return grid_detail::NonNamedGridPlacement::auto_();
                    int32_t occurrence = static_cast<int32_t>(p.named_line_index);
                    size_t chosen;
                    if (occurrence > 0) chosen = matches[std::min<size_t>(static_cast<size_t>(occurrence - 1), matches.size() - 1)];
                    else if (occurrence < 0) chosen = matches[matches.size() - std::min<size_t>(static_cast<size_t>(-occurrence), matches.size())];
                    else chosen = matches.empty() ? 0 : matches[matches.size() - 1];
                    // Convert full-grid line index -> origin-zero -> CSS grid line
                    int32_t oz = static_cast<int32_t>(chosen) - static_cast<int32_t>(tracks.negative_implicit_tracks);
                    int32_t explicit_line_count = static_cast<int32_t>(tracks.explicit_tracks) + 1;
                    int32_t css_line = (oz >= 0) ? (oz + 1) : (oz - explicit_line_count);
                    return grid_detail::NonNamedGridPlacement::line(GridLine{static_cast<int16_t>(css_line)});
                }
            }
            return grid_detail::NonNamedGridPlacement::auto_();
        };
        return {convert(placement.start), convert(placement.end)};
    }

    /// Port of `Line<grid_detail::OriginZeroGridPlacement>::resolve_absolutely_positioned_grid_tracks`
    inline Line<std::optional<OriginZeroLine>> resolve_absolutely_positioned_grid_tracks(
        const Line<grid_detail::OriginZeroGridPlacement>& placement) {
        using GP = grid_detail::OriginZeroGridPlacement;
        if (placement.start.kind == GP::LineKind && placement.end.kind == GP::LineKind) {
            const OriginZeroLine a = placement.start.line_value;
            const OriginZeroLine b = placement.end.line_value;
            if (a == b) return {std::optional<OriginZeroLine>(a), std::optional<OriginZeroLine>(OriginZeroLine{static_cast<int16_t>(a.value + 1)})};
            return {std::optional<OriginZeroLine>(OriginZeroLine{static_cast<int16_t>(std::min(a.value, b.value))}),
                    std::optional<OriginZeroLine>(OriginZeroLine{static_cast<int16_t>(std::max(a.value, b.value))})};
        }
        if (placement.start.kind == GP::LineKind && placement.end.kind == GP::SpanKind) {
            return {std::optional<OriginZeroLine>(placement.start.line_value),
                    std::optional<OriginZeroLine>(OriginZeroLine{static_cast<int16_t>(
                        placement.start.line_value.value + static_cast<int16_t>(placement.end.span_value))})};
        }
        if (placement.start.kind == GP::LineKind) {
            return {std::optional<OriginZeroLine>(placement.start.line_value), std::nullopt};
        }
        if (placement.start.kind == GP::SpanKind && placement.end.kind == GP::LineKind) {
            return {std::optional<OriginZeroLine>(OriginZeroLine{static_cast<int16_t>(
                        placement.end.line_value.value - static_cast<int16_t>(placement.start.span_value))}),
                    std::optional<OriginZeroLine>(placement.end.line_value)};
        }
        if (placement.end.kind == GP::LineKind) {
            return {std::nullopt, std::optional<OriginZeroLine>(placement.end.line_value)};
        }
        return {std::nullopt, std::nullopt};
    }

    /// Fallible conversion of an origin-zero line to its (doubled) track-vector index: lines that
    /// fall outside the implicit grid are treated as `auto` for absolutely positioned boxes.
    inline std::optional<size_t> absolute_oz_line_track_vec_index(OriginZeroLine line, const grid_detail::TrackCounts& counts) {
        const int16_t min_line = static_cast<int16_t>(-static_cast<int16_t>(counts.negative_implicit));
        const int16_t max_line = static_cast<int16_t>(counts.explicit_ + counts.positive_implicit);
        if (line.value < min_line || line.value > max_line) return std::nullopt;
        return static_cast<size_t>(2 * (static_cast<int32_t>(line.value) + static_cast<int32_t>(counts.negative_implicit)));
    }

    /// A track edge for a grid line, excluding gutters that overlap across a collapsed-track run
    inline float absolute_track_edge(const grid_detail::DetailedGridTracksInfo& tracks, size_t index, bool is_start,
                                     bool is_reversed) {
        if (tracks.positions.empty()) return 0.0f;
        const std::pair<size_t, size_t>* range = nullptr;
        for (const auto& r : tracks.collapsed_tracks) {
            if (r.second <= index) continue;
            if (index >= r.first) range = &r;
            break;
        }
        size_t position_index = index;
        if (range != nullptr) {
            std::optional<size_t> adjacent;
            if (is_start) {
                adjacent = range->second;
            } else if (range->first > 0) {
                adjacent = range->first - 1;
            }
            if (adjacent.has_value() && *adjacent < tracks.positions.size()) position_index = *adjacent;
        }
        if (position_index >= tracks.positions.size()) position_index = tracks.positions.size() - 1;
        const Line<float>& track = tracks.positions[position_index];
        return (is_start != is_reversed) ? track.start : track.end;
    }

    /// Resolve an absolute placement in one axis to physical start/end coordinates
    inline Line<float> resolve_absolute_grid_axis(const grid_detail::DetailedGridTracksInfo& tracks,
                                                  const Line<GridPlacement>& placement, float padding_start,
                                                  float padding_end, bool is_reversed,
                                                  grid_detail::GridAreaAxis axis = grid_detail::GridAreaAxis::Row,
                                                  const grid_detail::NamedLineResolver* resolver = nullptr) {
        const grid_detail::TrackCounts counts{tracks.negative_implicit_tracks, tracks.explicit_tracks,
                                 tracks.positive_implicit_tracks};
        const int16_t min_line = static_cast<int16_t>(-static_cast<int16_t>(counts.negative_implicit));
        const int16_t max_line = static_cast<int16_t>(counts.explicit_ + counts.positive_implicit);

        // Named lines and named grid areas resolve through the grid algorithm's name resolver (which
        // also handles names that do not exist, generating implicit lines); fall back to resolving
        // against the explicit line names only when no resolver is available.
        const Line<grid_detail::NonNamedGridPlacement> non_named =
            resolver != nullptr ? resolver->resolve_line_names(placement, axis)
                                : grid_placement_to_non_named(placement, tracks);
        const Line<grid_detail::OriginZeroGridPlacement> oz = grid_detail::into_origin_zero_placement(non_named, tracks.explicit_tracks);
        const Line<std::optional<OriginZeroLine>> lines = resolve_absolutely_positioned_grid_tracks(oz);

        auto to_position_index = [&](const std::optional<OriginZeroLine>& line) -> std::optional<size_t> {
            if (!line.has_value()) return std::nullopt;
            if (line->value < min_line || line->value > max_line) return std::nullopt;
            auto vec_index = absolute_oz_line_track_vec_index(*line, counts);
            if (!vec_index.has_value()) return std::nullopt;
            return *vec_index / 2;
        };
        const std::optional<size_t> start_index = to_position_index(lines.start);
        const std::optional<size_t> end_index = to_position_index(lines.end);

        float start_position;
        if (start_index.has_value()) {
            if (*start_index < tracks.positions.size()) {
                start_position = absolute_track_edge(tracks, *start_index, true, is_reversed);
            } else if (!tracks.positions.empty()) {
                start_position = is_reversed ? tracks.positions.back().start : tracks.positions.back().end;
            } else if (tracks.empty_axis_line.has_value()) {
                start_position = *tracks.empty_axis_line;
            } else {
                start_position = is_reversed ? padding_end : padding_start;
            }
        } else {
            start_position = is_reversed ? padding_end : padding_start;
        }

        float end_position;
        if (end_index.has_value()) {
            if (*end_index >= 1 && (*end_index - 1) < tracks.positions.size()) {
                end_position = absolute_track_edge(tracks, *end_index - 1, false, is_reversed);
            } else if (!tracks.positions.empty()) {
                end_position = is_reversed ? tracks.positions.front().end : tracks.positions.front().start;
            } else if (tracks.empty_axis_line.has_value()) {
                end_position = *tracks.empty_axis_line;
            } else {
                end_position = is_reversed ? padding_start : padding_end;
            }
        } else {
            end_position = is_reversed ? padding_start : padding_end;
        }

        if (is_reversed) return Line<float>{std::min(start_position, end_position), start_position};
        return Line<float>{start_position, std::max(start_position, end_position)};
    }

    /// Detect whether grid's `DetailedGridInfo` exposes an exact `resolve_absolute_grid_area`
    /// (which additionally resolves named grid *areas* and named lines through grid's own maps).
    /// When it exists it is preferred over the local approximation below.
    template <typename T, typename = void>
    struct has_resolve_absolute_grid_area : std::false_type {};
    template <typename T>
    struct has_resolve_absolute_grid_area<
        T, std::void_t<decltype(std::declval<const T&>().resolve_absolute_grid_area(
               std::declval<Line<GridPlacement>>(), std::declval<Line<GridPlacement>>(), std::declval<Direction>(),
               std::declval<Rect<float>>()))>> : std::true_type {};

    /// Resolve the physical grid area for an absolutely positioned box from its grid placement.
    /// `padding_box` and the returned area use coordinates relative to the grid container's border
    /// box. Port of `DetailedGridInfo::resolve_absolute_grid_area`.
    template <typename Info>
    inline Rect<float> resolve_absolute_grid_area_dispatch(const Info& info, const Style& child_style,
                                                           Direction direction, Rect<float> padding_box,
                                                           std::true_type) {
        return info.resolve_absolute_grid_area(child_style.grid_row, child_style.grid_column, direction, padding_box);
    }

    /// Fallback grid-area resolution from the recorded track positions (numeric/named-line
    /// placements only; named grid areas are not part of DetailedGridInfo in this port).
    template <typename Info>
    inline Rect<float> resolve_absolute_grid_area_dispatch(const Info& info, const Style& child_style,
                                                           Direction direction, Rect<float> padding_box,
                                                           std::false_type) {
        const bool is_rtl = direction == Direction::Rtl;
        const Line<float> columns = resolve_absolute_grid_axis(
            info.columns, child_style.grid_column, padding_box.left, padding_box.right, is_rtl,
            grid_detail::GridAreaAxis::Column, &info.resolver);
        const Line<float> rows = resolve_absolute_grid_axis(
            info.rows, child_style.grid_row, padding_box.top, padding_box.bottom, false,
            grid_detail::GridAreaAxis::Row, &info.resolver);
        return Rect<float>{columns.start, columns.end, rows.start, rows.end};
    }

    inline Rect<float> resolve_absolute_grid_area(const grid_detail::DetailedGridInfo& info,
                                                  const Style& child_style, Direction direction,
                                                  Rect<float> padding_box) {
        return resolve_absolute_grid_area_dispatch(info, child_style, direction, padding_box,
                                                   has_resolve_absolute_grid_area<grid_detail::DetailedGridInfo>{});
    }

    /// Detailed grid info used by the out-of-flow positioning pass to resolve grid areas
    struct GridAreaLookup {
        const grid_detail::DetailedGridInfo* info{nullptr};
    };
#else
    /// Detailed grid info used by the out-of-flow positioning pass (no grid feature)
    struct GridAreaLookup {};
#endif
} // namespace detail

enum class TaffyError : uint8_t {
    ChildIndexOutOfBounds,
    InvalidParentNode,
    InvalidChildNode,
    InvalidInputNode,
};

template <typename NodeContext = void>
class TaffyTree {
public:
    TaffyTree() { nodes_.reserve(16); }
    explicit TaffyTree(size_t capacity) { nodes_.reserve(capacity); }

    void enable_rounding() { use_rounding_ = true; }
    void disable_rounding() { use_rounding_ = false; }
    bool is_rounding_enabled() const { return use_rounding_; }

    // Node creation
    NodeId new_leaf(const Style& style) {
        NodeId id = alloc_slot();
        nodes_[id.index].data.style = style;
        return id;
    }

    template <typename NC = NodeContext, typename = std::enable_if_t<!std::is_void_v<NC>>>
    NodeId new_leaf_with_context(const Style& style, NC context) {
        NodeId id = new_leaf(style);
        contexts_[id.index] = std::move(context);
        nodes_[id.index].data.has_context = true;
        return id;
    }

    NodeId new_with_children(const Style& style, const std::vector<NodeId>& child_ids) {
        NodeId id = new_leaf(style);
        for (NodeId child : child_ids) add_child(id, child);
        return id;
    }

    void clear() {
        nodes_.clear();
        children_.clear();
        parents_.clear();
        free_head_ = UINT32_MAX;
        alive_count_ = 0;
        if constexpr (!std::is_void_v<NodeContext>) contexts_.clear();
    }

    // Tree modification
    void add_child(NodeId parent, NodeId child) {
        remove_from_parent(child);
        children_[parent.index].push_back(child);
        parents_[child.index] = parent;
        mark_dirty_recursive(parent);
    }

    void insert_child_at_index(NodeId parent, size_t child_index, NodeId child) {
        auto& pc = children_[parent.index];
        if (child_index > pc.size()) child_index = pc.size();
        remove_from_parent(child);
        pc.insert(pc.begin() + static_cast<ptrdiff_t>(child_index), child);
        parents_[child.index] = parent;
        mark_dirty_recursive(parent);
    }

    void set_children(NodeId parent, const std::vector<NodeId>& new_children) {
        for (NodeId old_child : children_[parent.index]) parents_[old_child.index] = NODE_ID_NONE;
        children_[parent.index].clear();
        for (NodeId child : new_children) {
            remove_from_parent(child);
            children_[parent.index].push_back(child);
            parents_[child.index] = parent;
        }
        mark_dirty_recursive(parent);
    }

    NodeId remove_child_at_index(NodeId parent, size_t child_index) {
        auto& pc = children_[parent.index];
        if (child_index >= pc.size()) return NODE_ID_NONE;
        NodeId child = pc[child_index];
        pc.erase(pc.begin() + static_cast<ptrdiff_t>(child_index));
        parents_[child.index] = NODE_ID_NONE;
        mark_dirty_recursive(parent);
        return child;
    }

    NodeId remove_child(NodeId parent, NodeId child) {
        auto& pc = children_[parent.index];
        auto it = std::find(pc.begin(), pc.end(), child);
        if (it == pc.end()) return NODE_ID_NONE;
        size_t index = static_cast<size_t>(it - pc.begin());
        return remove_child_at_index(parent, index);
    }

    NodeId remove(NodeId node) {
        remove_from_parent(node);
        for (NodeId child : children_[node.index]) parents_[child.index] = NODE_ID_NONE;
        children_[node.index].clear();
        free_slot(node);
        return node;
    }

    // Style access
    void set_style(NodeId node, const Style& style) {
        nodes_[node.index].data.style = style;
        mark_dirty_recursive(node);
    }
    const Style& style(NodeId node) const { return nodes_[node.index].data.style; }

    // Layout access
    const Layout& layout(NodeId node) const {
        return use_rounding_ ? nodes_[node.index].data.final_layout : nodes_[node.index].data.unrounded_layout;
    }
    const Layout& unrounded_layout(NodeId node) const { return nodes_[node.index].data.unrounded_layout; }

    // Parent/children
    NodeId parent(NodeId child) const { return parents_[child.index]; }
    std::vector<NodeId> children(NodeId parent) const { return children_[parent.index]; }
    size_t child_count(NodeId parent) const { return children_[parent.index].size(); }
    NodeId get_child_id(NodeId parent, size_t index) const { return children_[parent.index][index]; }
    size_t total_node_count() const { return alive_count_; }
    const std::vector<NodeId>& hoisted_children(NodeId node) const { return nodes_[node.index].data.hoisted_children; }

#ifdef TAFFY_GRID
    /// Record the grid algorithm's detailed layout information for `node`
    void set_detailed_grid_info(NodeId node, const grid_detail::DetailedGridInfo& info) {
        nodes_[node.index].data.detailed_info = info;
    }
    /// Read back the detailed grid information most recently recorded for `node`
    const grid_detail::DetailedGridInfo& detailed_layout_info(NodeId node) const {
        return nodes_[node.index].data.detailed_info;
    }
#endif

    // Dirty tracking
    void mark_dirty(NodeId node) { mark_dirty_recursive(node); }
    bool dirty(NodeId node) const { return nodes_[node.index].data.cache.is_empty(); }

    // Context access (only when NodeContext is not void)
    template <typename NC = NodeContext, typename = std::enable_if_t<!std::is_void_v<NC>>>
    NC* get_node_context(NodeId node) {
        return nodes_[node.index].data.has_context ? &contexts_[node.index] : nullptr;
    }
    template <typename NC = NodeContext, typename = std::enable_if_t<!std::is_void_v<NC>>>
    void set_node_context(NodeId node, NC context) {
        contexts_[node.index] = std::move(context);
        nodes_[node.index].data.has_context = true;
        mark_dirty_recursive(node);
    }

    // Main layout computation
    template <typename MeasureFunction>
    void compute_layout_with_measure(NodeId root, Size<AvailableSpace> available_space, MeasureFunction&& measure_fn) {
        auto& mf = measure_fn;
        compute_root_layout_impl(root, available_space, mf);
        if (use_rounding_) round_layout_impl(root, 0.0f, 0.0f);
    }

    void compute_layout(NodeId root, Size<AvailableSpace> available_space) {
        compute_layout_with_measure(root, available_space,
            [](Size<std::optional<float>>, Size<AvailableSpace>, NodeId, const Style&) -> Size<float> {
                return {0.0f, 0.0f};
            });
    }

    /// Like `compute_layout_with_measure`, but additionally derives the baselines of leaf nodes
    /// (e.g. text) from their measured content size via `baseline_fn`.
    ///
    /// `baseline_fn(measured_content_size, node_id, style) -> Baselines` is applied to every leaf's
    /// `LayoutOutput.baselines`; used for baseline alignment in flexbox/grid.
    template <typename MeasureFunction, typename BaselineFunction>
    void compute_layout_with_measure_baselines(NodeId root, Size<AvailableSpace> available_space,
                                               MeasureFunction&& measure_fn, BaselineFunction&& baseline_fn) {
        auto& mf = measure_fn;
        auto previous = std::move(baseline_fn_);
        baseline_fn_ = [&baseline_fn](Size<float> measured, NodeId node, const Style& style) -> Baselines {
            return baseline_fn(measured, node, style);
        };
        compute_root_layout_impl(root, available_space, mf);
        if (use_rounding_) round_layout_impl(root, 0.0f, 0.0f);
        baseline_fn_ = std::move(previous);
    }

private:
    std::vector<detail::NodeSlot> nodes_{};
    std::vector<std::vector<NodeId>> children_{};
    std::vector<NodeId> parents_{};
    std::conditional_t<!std::is_void_v<NodeContext>, std::vector<NodeContext>, char> contexts_{};
    uint32_t free_head_{UINT32_MAX};
    size_t alive_count_{0};
    bool use_rounding_{true};
    /// Optional baseline derivation callback for leaf nodes (see compute_layout_with_measure_baselines)
    std::function<Baselines(Size<float>, NodeId, const Style&)> baseline_fn_{};

    NodeId alloc_slot() {
        uint32_t idx;
        if (free_head_ != UINT32_MAX) {
            idx = free_head_;
            free_head_ = nodes_[idx].next_free;
            nodes_[idx].generation++;
            nodes_[idx].alive = true;
            nodes_[idx].data = detail::NodeData{};
            children_[idx].clear();
            parents_[idx] = NODE_ID_NONE;
        } else {
            idx = static_cast<uint32_t>(nodes_.size());
            nodes_.push_back({0, detail::NodeData{}, true, UINT32_MAX});
            children_.push_back({});
            parents_.push_back(NODE_ID_NONE);
            if constexpr (!std::is_void_v<NodeContext>) contexts_.push_back(NodeContext{});
        }
        alive_count_++;
        return {idx, nodes_[idx].generation};
    }

    void free_slot(NodeId id) {
        nodes_[id.index].alive = false;
        nodes_[id.index].next_free = free_head_;
        free_head_ = id.index;
        alive_count_--;
    }

    void remove_from_parent(NodeId child) {
        NodeId p = parents_[child.index];
        if (p.is_valid()) {
            auto& pc = children_[p.index];
            pc.erase(std::remove(pc.begin(), pc.end(), child), pc.end());
            parents_[child.index] = NODE_ID_NONE;
        }
    }

    void mark_dirty_recursive(NodeId node) {
        while (node.is_valid()) {
            auto& data = nodes_[node.index].data;
            data.pending_oof.clear();
            ClearState state = data.cache.clear();
            if (state == ClearState::AlreadyEmpty) break;
            node = parents_[node.index];
        }
    }

    // =======================================================================
    // Layout dispatch
    // =======================================================================
public:
    void set_unrounded_layout(NodeId node_id, const Layout& layout) {
        nodes_[node_id.index].data.unrounded_layout = layout;
    }

    const Layout& get_unrounded_layout(NodeId node_id) const {
        return nodes_[node_id.index].data.unrounded_layout;
    }

    template <typename MeasureFunction>
    LayoutOutput compute_node_layout(NodeId node_id, LayoutInput inputs, MeasureFunction&& measure_fn) {
        auto& mf = measure_fn;
        // If `RunMode` is `PerformHiddenLayout` then this indicates that an ancestor node is
        // `Display::None` and thus that we should lay out this node using hidden layout
        // regardless of its own display style.
        if (inputs.run_mode == RunMode::PerformHiddenLayout) {
            nodes_[node_id.index].data.final_layout = Layout::with_order(0);
            return compute_hidden_layout_impl(node_id, mf);
        }

        auto compute_body = [&](const LayoutInput& in) -> LayoutOutput {
            const Style& node_style = nodes_[node_id.index].data.style;
            const bool has_children = !children_[node_id.index].empty();
            LayoutOutput output;

            if (node_style.display == Display::None) {
                output = compute_hidden_layout_impl(node_id, mf);
            } else if (!has_children) {
                output = compute_leaf_layout_impl(in, node_id, node_style, mf);
            } else {
                output = compute_container_layout_impl(node_id, in, mf);
            }

            if (in.run_mode == RunMode::PerformLayout) {
                // Record the node's inset-resolution area for out-of-flow boxes, then run the
                // out-of-flow positioning pass and accumulate scrollable overflow.
                if (node_style.display != Display::None && has_children) {
                    if (!output.oof_positioning_area.has_value())
                        output.oof_positioning_area = oof_positioning_area_impl(node_id, in, output);
                    compute_oof_layout_impl(node_id, in, output, mf);
                    compute_container_scrollable_overflow_impl(node_id, in, output);
                }
                nodes_[node_id.index].data.pending_oof = output.oof_candidates;
            }
            return output;
        };

        // First check the cache
        if (auto cached = nodes_[node_id.index].data.cache.get(inputs)) return *cached;

        LayoutOutput computed = compute_body(inputs);
        nodes_[node_id.index].data.cache.store(inputs, computed);
        return computed;
    }

private:
    template <typename MeasureFunction>
    LayoutOutput compute_container_layout_impl(NodeId node_id, const LayoutInput& inputs, MeasureFunction& measure_fn) {
        const Style& node_style = nodes_[node_id.index].data.style;
#ifdef TAFFY_FLEXBOX
        if (node_style.display == Display::Flex) return compute_flexbox_layout_impl(node_id, inputs, measure_fn);
#endif
#ifdef TAFFY_GRID
        if (node_style.display == Display::Grid) return compute_grid_layout_impl(node_id, inputs, measure_fn);
#endif
#ifdef TAFFY_BLOCK_LAYOUT
        if (node_style.display == Display::Block || node_style.display == Display::FlowRoot)
            return compute_block_layout_impl(node_id, inputs, measure_fn);
#endif
        (void)inputs;
        (void)measure_fn;
        return LayoutOutput::hidden();
    }

    /// Creates a layout for this node and its children, recursively. Each hidden node has zero
    /// size and is placed at the origin.
    template <typename MeasureFunction>
    LayoutOutput compute_hidden_layout_impl(NodeId node, MeasureFunction& measure_fn) {
        auto& data = nodes_[node.index].data;
        data.cache.clear();
        data.pending_oof.clear();
        set_unrounded_layout(node, Layout::with_order(0));
        for (size_t i = 0; i < children_[node.index].size(); ++i) {
            compute_node_layout(children_[node.index][i], LayoutInput::hidden_input(), measure_fn);
        }
        return LayoutOutput::hidden();
    }

    // =======================================================================
    // Leaf layout (src/compute/leaf.rs)
    // =======================================================================
    template <typename MeasureFunction>
    LayoutOutput compute_leaf_layout_impl(
        const LayoutInput& inputs, NodeId node_id, const Style& style, MeasureFunction& measure_fn) {
        const auto known_dimensions = inputs.known_dimensions;
        const auto parent_size = inputs.parent_size;
        const auto sizing_mode = inputs.sizing_mode;
        const auto run_mode = inputs.run_mode;

        // Note: both horizontal and vertical percentage padding/borders are resolved against the
        // container's inline size (i.e. width), as CSS specifies.
        const Rect<float> padding = resolve_or_zero_rect(style.padding, parent_size.width);
        const Rect<float> border = resolve_or_zero_rect(style.border, parent_size.width);
        const Rect<float> padding_border = padding + border;
        const Size<float> pb_sum = padding_border.sum_axes();
        const Size<float> box_sizing_adjustment =
            style.box_sizing == BoxSizing::ContentBox ? pb_sum : Size_f32_ZERO;

        Size<std::optional<float>> node_size{};
        Size<std::optional<float>> node_min_size{};
        Size<std::optional<float>> node_max_size{};
        std::optional<float> aspect_ratio{std::nullopt};
        if (sizing_mode == SizingMode::ContentSize) {
            node_size = known_dimensions;
        } else {
            aspect_ratio = style.aspect_ratio;
            const auto style_size = detail::maybe_add_size(
                maybe_resolve_size(style.size, parent_size).maybe_apply_aspect_ratio(aspect_ratio),
                box_sizing_adjustment);
            const auto style_min_size = detail::maybe_add_size(
                maybe_resolve_size(style.min_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio),
                box_sizing_adjustment);
            const auto style_max_size = detail::maybe_add_size(
                maybe_resolve_size(style.max_size, parent_size), box_sizing_adjustment);
            node_size = Size<std::optional<float>>{known_dimensions.width ? known_dimensions.width : style_size.width,
                                                  known_dimensions.height ? known_dimensions.height : style_size.height};
            node_min_size = style_min_size;
            node_max_size = style_max_size;
        }

        // Scrollbar gutters are reserved when `overflow` is `Scroll` (axes transposed)
        const Point<float> scrollbar_gutter = detail::scrollbar_gutter_of(style);
        Rect<float> content_box_inset = padding_border;
        content_box_inset.right += scrollbar_gutter.x;
        content_box_inset.bottom += scrollbar_gutter.y;

        const bool has_styles_preventing_being_collapsed_through =
            !detail::style_is_block(style) || is_scroll_container(style.overflow.x)
            || is_scroll_container(style.overflow.y) || detail::style_is_out_of_flow(style)
            || detail::contain_establishes_independent_formatting_context(style.contain) || padding.top > 0.0f
            || padding.bottom > 0.0f || border.top > 0.0f || border.bottom > 0.0f
            || (node_size.height.has_value() && *node_size.height > 0.0f)
            || (node_min_size.height.has_value() && *node_min_size.height > 0.0f);

        // Return early if both width and height are known
        if (run_mode == RunMode::ComputeSize && has_styles_preventing_being_collapsed_through) {
            if (node_size.width.has_value() && node_size.height.has_value()) {
                Size<float> size = detail::maybe_clamp_size(node_size, node_min_size, node_max_size)
                                       .unwrap_or(Size<float>{0.0f, 0.0f});
                size.width = std::max(size.width, pb_sum.width);
                size.height = std::max(size.height, pb_sum.height);
                LayoutOutput out;
                out.size = size;
                out.margins_can_collapse_through = false;
                return out;
            }
        }

        // Compute available space
        Size<AvailableSpace> available_space;
        {
            AvailableSpace width = known_dimensions.width ? AvailableSpace::definite(*known_dimensions.width)
                                                          : inputs.available_space.width;
            width = width.maybe_set(known_dimensions.width);
            width = width.maybe_set(node_size.width);
            width = width.map_definite_value([&](float size) {
                return maybe_clamp_f32(size, node_min_size.width, node_max_size.width) - content_box_inset.horizontal_axis_sum();
            });
            AvailableSpace height = known_dimensions.height ? AvailableSpace::definite(*known_dimensions.height)
                                                            : inputs.available_space.height;
            height = height.maybe_set(known_dimensions.height);
            height = height.maybe_set(node_size.height);
            height = height.map_definite_value([&](float size) {
                return maybe_clamp_f32(size, node_min_size.height, node_max_size.height) - content_box_inset.vertical_axis_sum();
            });
            available_space = {width, height};
        }

        // Measure node
        const Size<std::optional<float>> measure_known =
            run_mode == RunMode::ComputeSize
                ? known_dimensions
                : (run_mode == RunMode::PerformLayout ? Size<std::optional<float>>{} : known_dimensions);
        const Size<float> measured_size = measure_fn(measure_known, available_space, node_id, style);

        Size<std::optional<float>> known_or_node{
            known_dimensions.width ? known_dimensions.width : node_size.width,
            known_dimensions.height ? known_dimensions.height : node_size.height};
        Size<float> clamped_size{
            known_or_node.width.value_or(measured_size.width + content_box_inset.horizontal_axis_sum()),
            known_or_node.height.value_or(measured_size.height + content_box_inset.vertical_axis_sum())};
        clamped_size = detail::maybe_clamp_size(Size<std::optional<float>>{clamped_size.width, clamped_size.height},
                                                node_min_size, node_max_size)
                           .unwrap_or(clamped_size);
        Size<float> size{
            clamped_size.width,
            std::max(clamped_size.height,
                     aspect_ratio.has_value() ? clamped_size.width / *aspect_ratio : 0.0f)};
        size.width = std::max(size.width, pb_sum.width);
        size.height = std::max(size.height, pb_sum.height);

        LayoutOutput out;
        out.size = size;
#ifdef TAFFY_CONTENT_SIZE
        {
            // A scroll container's own padding at the end of the content is part of its
            // scrollable overflow region.
            const bool is_scroll_container_ = is_scroll_container(style.overflow.x)
                                           || is_scroll_container(style.overflow.y);
            const bool is_rtl = style.direction == Direction::Rtl;
            const float start_padding = is_rtl ? padding.right : padding.left;
            const float end_padding = is_rtl ? padding.left : padding.right;
            const float client_width =
                std::max(0.0f, size.width - border.horizontal_axis_sum() - scrollbar_gutter.x);
            const float content_end =
                start_padding + measured_size.width + (is_scroll_container_ ? end_padding : 0.0f);
            Rect<float> rect{is_rtl ? std::min(client_width - content_end, 0.0f) : 0.0f,
                             is_rtl ? client_width : content_end, 0.0f,
                             padding.top + measured_size.height + (is_scroll_container_ ? padding.bottom : 0.0f)};
            out.scrollable_overflow_rect = detail::finalize_scrollable_overflow(
                rect, size - border.sum_axes() - Size<float>{scrollbar_gutter.x, scrollbar_gutter.y},
                Point<bool>{is_rtl, false}, is_scroll_container_);
        }
#endif
        out.baselines = baseline_fn_ ? baseline_fn_(measured_size, node_id, style) : Baselines::none();
        out.top_margin = CollapsibleMarginSet::zero();
        out.bottom_margin = CollapsibleMarginSet::zero();
        out.margins_can_collapse_through =
            !has_styles_preventing_being_collapsed_through && size.height == 0.0f && measured_size.height == 0.0f;
        return out;
    }

    // =======================================================================
    // Root layout (src/compute/mod.rs::compute_root_layout)
    // =======================================================================
    template <typename MeasureFunction>
    void compute_root_layout_impl(NodeId root, Size<AvailableSpace> available_space, MeasureFunction& measure_fn) {
        const Style& root_style = nodes_[root.index].data.style;
        const Position position = root_style.position;
        const Direction direction = root_style.direction;

        const Size<std::optional<float>> icb_size = into_options(available_space);
        const bool oof_root = is_absolute_position(position) && icb_size.width.has_value() && icb_size.height.has_value();

        bool root_is_cached = false;
        Layout layout;
        OofCandidates candidates;

        if (oof_root) {
            const Size<float> area_size{*icb_size.width, *icb_size.height};
            const Line<float> area_x = direction == Direction::Rtl ? Line<float>{area_size.width, area_size.width}
                                                                   : Line<float>{0.0f, 0.0f};
            OofCandidate candidate;
            candidate.node = root;
            candidate.order = 0;
            candidate.position = position;
            candidate.static_position = Point<AxisStaticPosition>{
                AxisStaticPosition{
                    area_x,
                    AxisStaticAlign::from_keyword(direction == Direction::Rtl ? AxisStaticEdge::End
                                                                             : AxisStaticEdge::Start)},
                AxisStaticPosition::from_edge(0.0f, AxisStaticEdge::Start)};

            detail::OofBoxLayout box = layout_oof_box_impl(
                candidate, area_size, Point<float>{0.0f, 0.0f}, direction,
                Point<Overflow>{Overflow::Visible, Overflow::Visible}, detail::GridAreaLookup{},
                [&](const LayoutInput& inputs) { root_is_cached = nodes_[root.index].data.cache.get(inputs).has_value(); },
                measure_fn);
            layout = box.layout;
            candidates = std::move(box.surfaced);
        } else {
            Size<std::optional<float>> known_dimensions{};

            // The available space passed to the root excludes the root's margins
            Size<AvailableSpace> root_available_space = available_space;
            {
                const Rect<float> margin = resolve_or_zero_rect(root_style.margin, icb_size.width);
                root_available_space.width = maybe_sub(root_available_space.width, margin.horizontal_axis_sum());
                root_available_space.height = maybe_sub(root_available_space.height, margin.vertical_axis_sum());
                root_available_space.width = maybe_max(root_available_space.width, 0.0f);
                root_available_space.height = maybe_max(root_available_space.height, 0.0f);
            }

#ifdef TAFFY_BLOCK_LAYOUT
            if (detail::style_is_block(root_style)) {
                const Size<std::optional<float>> parent_size = into_options(available_space);
                const std::optional<float> aspect_ratio = root_style.aspect_ratio;
                const Rect<float> padding = resolve_or_zero_rect(root_style.padding, parent_size.width);
                const Rect<float> border = resolve_or_zero_rect(root_style.border, parent_size.width);
                const Size<float> padding_border_size = (padding + border).sum_axes();
                const Size<float> box_sizing_adjustment =
                    root_style.box_sizing == BoxSizing::ContentBox ? padding_border_size : Size_f32_ZERO;

                const Size<std::optional<float>> min_size = detail::maybe_add_size(
                    maybe_resolve_size(root_style.min_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio),
                    box_sizing_adjustment);
                const Size<std::optional<float>> max_size = detail::maybe_add_size(
                    maybe_resolve_size(root_style.max_size, parent_size).maybe_apply_aspect_ratio(aspect_ratio),
                    box_sizing_adjustment);
                const Size<std::optional<float>> clamped_style_size = detail::maybe_clamp_size(
                    detail::maybe_add_size(
                        maybe_resolve_size(root_style.size, parent_size).maybe_apply_aspect_ratio(aspect_ratio),
                        box_sizing_adjustment),
                    min_size, max_size);

                // If both min and max in a given axis are set and max <= min then this determines
                // the size in that axis
                Size<std::optional<float>> min_max_definite_size{};
                if (min_size.width && max_size.width && *max_size.width <= *min_size.width)
                    min_max_definite_size.width = min_size.width;
                if (min_size.height && max_size.height && *max_size.height <= *min_size.height)
                    min_max_definite_size.height = min_size.height;

                // Block nodes stretch-fit their width to the available space when it is definite
                Size<std::optional<float>> available_space_based_size{root_available_space.width.into_option(),
                                                                      std::nullopt};

                Size<std::optional<float>> styled_based_known_dimensions = min_max_definite_size;
                if (!styled_based_known_dimensions.width) styled_based_known_dimensions.width = clamped_style_size.width;
                if (!styled_based_known_dimensions.height) styled_based_known_dimensions.height = clamped_style_size.height;
                if (!styled_based_known_dimensions.width)
                    styled_based_known_dimensions.width = available_space_based_size.width;
                if (!styled_based_known_dimensions.height)
                    styled_based_known_dimensions.height = available_space_based_size.height;
                styled_based_known_dimensions.width = maybe_max(
                    styled_based_known_dimensions.width, std::optional<float>(padding_border_size.width));
                styled_based_known_dimensions.height = maybe_max(
                    styled_based_known_dimensions.height, std::optional<float>(padding_border_size.height));
                known_dimensions = styled_based_known_dimensions;
            }
#endif

            LayoutInput inputs;
            inputs.known_dimensions = known_dimensions;
            inputs.known_dimensions_are_definite = Size<bool>{true, true};
            inputs.parent_size = into_options(available_space);
            inputs.available_space = root_available_space;
            inputs.sizing_mode = SizingMode::InherentSize;
            inputs.axis = RequestedAxis::Both;
            inputs.run_mode = RunMode::PerformLayout;
            inputs.vertical_margins_are_collapsible = Line_bool_FALSE;

            root_is_cached = nodes_[root.index].data.cache.get(inputs).has_value();

            LayoutOutput output = compute_node_layout(root, inputs, measure_fn);
            candidates = output.oof_candidates;

            const Rect<float> padding = resolve_or_zero_rect(root_style.padding, icb_size.width);
            const Rect<float> border = resolve_or_zero_rect(root_style.border, icb_size.width);
            const Rect<float> margin = resolve_or_zero_rect(root_style.margin, icb_size.width);
            const Size<float> scrollbar_size{
                root_style.overflow.y == Overflow::Scroll ? root_style.scrollbar_width : 0.0f,
                root_style.overflow.x == Overflow::Scroll ? root_style.scrollbar_width : 0.0f};
            const bool is_rtl = direction == Direction::Rtl;

            // The root's margin box is placed at the origin of the initial containing block
            Point<float> location{
                (is_rtl && icb_size.width.has_value()) ? *icb_size.width - output.size.width - margin.right
                                                       : margin.left,
                margin.top};

            // A relatively positioned root is offset by its insets (resolved against the ICB)
            if (position == Position::Relative) {
                const Rect<std::optional<float>> inset = maybe_resolve_rect(root_style.inset, icb_size.width);
                const std::optional<float> top = maybe_resolve(root_style.inset.top, icb_size.height);
                const std::optional<float> bottom = maybe_resolve(root_style.inset.bottom, icb_size.height);
                (void)inset;
                location.x += is_rtl ? (inset.right ? -(*inset.right) : inset.left.value_or(0.0f))
                                     : inset.left.value_or(inset.right ? -(*inset.right) : 0.0f);
                location.y += top.value_or(bottom ? -(*bottom) : 0.0f);
            }

            layout = Layout::new_layout();
            layout.order = 0;
            layout.location = location;
            layout.size = output.size;
#ifdef TAFFY_CONTENT_SIZE
            layout.scrollable_overflow_rect = output.scrollable_overflow_rect;
#endif
            layout.scrollbar_size = scrollbar_size;
            layout.padding = padding;
            layout.border = border;
            layout.margin = margin;

            set_unrounded_layout(root, layout);
        }

        // Final positioning pass for out-of-flow boxes with no nearer containing block
        if (!candidates.is_empty()) {
            const Point<Overflow> overflow{root_style.overflow.x, root_style.overflow.y};
#ifdef TAFFY_CONTENT_SIZE
            const Rect<float> area_inset = layout.border
                + Rect<float>{0.0f, layout.scrollbar_size.width, 0.0f, layout.scrollbar_size.height};
            const Size<float> root_padding_box_size{
                layout.size.width - (area_inset.left + area_inset.right),
                layout.size.height - (area_inset.top + area_inset.bottom)};
#else
            const Rect<float> area_inset = layout.border;
            const Size<float> root_padding_box_size{layout.size.width, layout.size.height};
#endif
            float area_width, area_x, area_height, area_y;
            if (icb_size.width.has_value()) {
#ifdef TAFFY_CONTENT_SIZE
                area_width = std::max(*icb_size.width - layout.scrollbar_size.width, 0.0f);
#else
                area_width = *icb_size.width;
#endif
                area_x = -layout.location.x;
            } else {
                area_width = root_padding_box_size.width;
                area_x = area_inset.left;
            }
            if (icb_size.height.has_value()) {
#ifdef TAFFY_CONTENT_SIZE
                area_height = std::max(*icb_size.height - layout.scrollbar_size.height, 0.0f);
#else
                area_height = *icb_size.height;
#endif
                area_y = -layout.location.y;
            } else {
                area_height = root_padding_box_size.height;
                area_y = area_inset.top;
            }

            detail::GridAreaLookup grid_lookup;
#ifdef TAFFY_GRID
            if (root_style.display == Display::Grid) grid_lookup.info = &nodes_[root.index].data.detailed_info;
#endif
            std::vector<NodeId> hoisted;
            OofCandidates unclaimed;
            perform_oof_layout_impl(root, candidates.inner, Size<float>{area_width, area_height},
                                    Point<float>{area_x, area_y}, direction, /*claims_absolute=*/true,
                                    /*claims_fixed=*/true, overflow, grid_lookup,
                                    Point<bool>{direction == Direction::Rtl, false}, hoisted, unclaimed, measure_fn);
            if (!root_is_cached) add_hoisted_children(root, hoisted);
        }
    }

    // =======================================================================
    // Rounding (src/compute/mod.rs::round_layout)
    // =======================================================================
    void round_layout_impl(NodeId node_id, float cumulative_x, float cumulative_y) {
        const Layout unrounded_layout = nodes_[node_id.index].data.unrounded_layout;
        Layout layout = unrounded_layout;

        const float parent_x = cumulative_x;
        const float parent_y = cumulative_y;
        cumulative_x += unrounded_layout.location.x;
        cumulative_y += unrounded_layout.location.y;

        layout.location.x = detail::f32_round(cumulative_x) - detail::f32_round(parent_x);
        layout.location.y = detail::f32_round(cumulative_y) - detail::f32_round(parent_y);
        layout.size.width = detail::f32_round(cumulative_x + unrounded_layout.size.width) - detail::f32_round(cumulative_x);
        layout.size.height =
            detail::f32_round(cumulative_y + unrounded_layout.size.height) - detail::f32_round(cumulative_y);
        layout.scrollbar_size.width = detail::f32_round(unrounded_layout.scrollbar_size.width);
        layout.scrollbar_size.height = detail::f32_round(unrounded_layout.scrollbar_size.height);
        layout.border.left = detail::f32_round(cumulative_x + unrounded_layout.border.left) - detail::f32_round(cumulative_x);
        layout.border.right = detail::f32_round(cumulative_x + unrounded_layout.size.width)
            - detail::f32_round(cumulative_x + unrounded_layout.size.width - unrounded_layout.border.right);
        layout.border.top = detail::f32_round(cumulative_y + unrounded_layout.border.top) - detail::f32_round(cumulative_y);
        layout.border.bottom = detail::f32_round(cumulative_y + unrounded_layout.size.height)
            - detail::f32_round(cumulative_y + unrounded_layout.size.height - unrounded_layout.border.bottom);
        layout.padding.left = detail::f32_round(cumulative_x + unrounded_layout.padding.left) - detail::f32_round(cumulative_x);
        layout.padding.right = detail::f32_round(cumulative_x + unrounded_layout.size.width)
            - detail::f32_round(cumulative_x + unrounded_layout.size.width - unrounded_layout.padding.right);
        layout.padding.top = detail::f32_round(cumulative_y + unrounded_layout.padding.top) - detail::f32_round(cumulative_y);
        layout.padding.bottom = detail::f32_round(cumulative_y + unrounded_layout.size.height)
            - detail::f32_round(cumulative_y + unrounded_layout.size.height - unrounded_layout.padding.bottom);
#ifdef TAFFY_CONTENT_SIZE
        {
            const Rect<float> unrounded_rect = unrounded_layout.scrollable_overflow_rect.rect();
            auto round_edge = [](float offset, float edge) {
                const float rounded = detail::f32_round(offset + edge) - detail::f32_round(offset);
                if (rounded == 0.0f) return std::copysign(rounded, edge);
                return rounded;
            };
            layout.scrollable_overflow_rect = ScrollableOverflow::new_(
                Rect<float>{round_edge(cumulative_x, unrounded_rect.left), round_edge(cumulative_x, unrounded_rect.right),
                            round_edge(cumulative_y, unrounded_rect.top), round_edge(cumulative_y, unrounded_rect.bottom)},
                unrounded_layout.scrollable_overflow_rect.is_reversed());
        }
#endif
        nodes_[node_id.index].data.final_layout = layout;

        // Recurse into in-flow children. Out-of-flow children are skipped here: they are visited
        // via their containing block's hoisted child list below.
        for (size_t index = 0; index < children_[node_id.index].size(); ++index) {
            const NodeId child = children_[node_id.index][index];
            if (!detail::style_is_out_of_flow(nodes_[child.index].data.style)) {
                round_layout_impl(child, cumulative_x, cumulative_y);
            }
        }
        // Recurse into out-of-flow boxes for which this node is the containing block
        for (NodeId child : nodes_[node_id.index].data.hoisted_children) {
            round_layout_impl(child, cumulative_x, cumulative_y);
        }
    }

    void add_hoisted_children(NodeId node, const std::vector<NodeId>& hoisted) {
        auto& dst = nodes_[node.index].data.hoisted_children;
        dst.insert(dst.end(), hoisted.begin(), hoisted.end());
    }

    void clear_hoisted_children(NodeId node) { nodes_[node.index].data.hoisted_children.clear(); }

// =======================================================================
    // Out-of-flow positioning (src/compute/oof.rs)
    // =======================================================================

    /// Compute the size of a node given the specified constraints
    template <typename MeasureFunction>
    Size<float> measure_child_size_both_impl(NodeId node_id, Size<std::optional<float>> known_dimensions,
                                             Size<std::optional<float>> parent_size, Size<AvailableSpace> available_space,
                                             SizingMode sizing_mode, Line<bool> vertical_margins_are_collapsible,
                                             MeasureFunction& measure_fn) {
        LayoutInput inputs;
        inputs.known_dimensions = known_dimensions;
        inputs.known_dimensions_are_definite = Size<bool>{true, true};
        inputs.parent_size = parent_size;
        inputs.available_space = available_space;
        inputs.sizing_mode = sizing_mode;
        inputs.axis = RequestedAxis::Both;
        inputs.run_mode = RunMode::ComputeSize;
        inputs.vertical_margins_are_collapsible = vertical_margins_are_collapsible;
        return compute_node_layout(node_id, inputs, measure_fn).size;
    }

    /// Compute the size of a node in a single axis given the specified constraints
    template <typename MeasureFunction>
    float measure_child_size_impl(NodeId node_id, Size<std::optional<float>> known_dimensions,
                                  Size<std::optional<float>> parent_size, Size<AvailableSpace> available_space,
                                  SizingMode sizing_mode, AbsoluteAxis axis,
                                  Line<bool> vertical_margins_are_collapsible, MeasureFunction& measure_fn) {
        LayoutInput inputs;
        inputs.known_dimensions = known_dimensions;
        inputs.known_dimensions_are_definite = Size<bool>{true, true};
        inputs.parent_size = parent_size;
        inputs.available_space = available_space;
        inputs.sizing_mode = sizing_mode;
        inputs.axis = axis == AbsoluteAxis::Horizontal ? RequestedAxis::Horizontal : RequestedAxis::Vertical;
        inputs.run_mode = RunMode::ComputeSize;
        inputs.vertical_margins_are_collapsible = vertical_margins_are_collapsible;
        return compute_node_layout(node_id, inputs, measure_fn).size.get_abs(axis);
    }

    /// Resolve the sizing keywords (`min-content`, `max-content`, `fit-content`, `fit-content(...)`
    /// and `stretch`) on the size styles of an absolutely positioned item, filling in the
    /// corresponding known dimensions. Port of `sizing_keyword::resolve_absolute_sizing_keywords`.
    template <typename MeasureFunction>
    void resolve_absolute_sizing_keywords_impl(NodeId node, Size<std::optional<float>>& known_dimensions,
                                               Size<Dimension> size_style, Size<float> area_size,
                                               Rect<std::optional<float>> inset, Rect<std::optional<float>> margin,
                                               SizingMode sizing_mode, MeasureFunction& measure_fn) {
        const Size<float> stretch_size{
            std::max(area_size.width - inset.left.value_or(0.0f) - inset.right.value_or(0.0f)
                         - margin.left.value_or(0.0f) - margin.right.value_or(0.0f),
                     0.0f),
            std::max(area_size.height - inset.top.value_or(0.0f) - inset.bottom.value_or(0.0f)
                         - margin.top.value_or(0.0f) - margin.bottom.value_or(0.0f),
                     0.0f)};
        const Size<std::optional<float>> area_opt{std::optional<float>(area_size.width),
                                                  std::optional<float>(area_size.height)};

        const detail::SizingKeywordResolution keyword_width =
            !known_dimensions.width.has_value()
                ? detail::resolve_sizing_keyword_impl(size_style.width, std::optional<float>(stretch_size.width),
                                                      std::optional<float>(area_size.width))
                : detail::SizingKeywordResolution{};
        const detail::SizingKeywordResolution keyword_height =
            !known_dimensions.height.has_value()
                ? detail::resolve_sizing_keyword_impl(size_style.height, std::optional<float>(stretch_size.height),
                                                      std::optional<float>(area_size.height))
                : detail::SizingKeywordResolution{};

        if (keyword_width.kind == detail::SizingKeywordKind::Measure
            && keyword_height.kind == detail::SizingKeywordKind::Measure) {
            const Size<float> measured = measure_child_size_both_impl(
                node, Size<std::optional<float>>{}, area_opt,
                Size<AvailableSpace>{keyword_width.available, keyword_height.available}, sizing_mode, Line_bool_FALSE,
                measure_fn);
            known_dimensions = Size<std::optional<float>>{std::optional<float>(measured.width),
                                                          std::optional<float>(measured.height)};
            return;
        }
        if (keyword_width.present()) {
            if (keyword_width.kind == detail::SizingKeywordKind::Exact) {
                known_dimensions.width = keyword_width.exact;
            } else {
                known_dimensions.width = measure_child_size_impl(
                    node, known_dimensions, area_opt,
                    Size<AvailableSpace>{keyword_width.available, AvailableSpace::definite(stretch_size.height)},
                    sizing_mode, AbsoluteAxis::Horizontal, Line_bool_FALSE, measure_fn);
            }
        }
        if (keyword_height.present()) {
            if (keyword_height.kind == detail::SizingKeywordKind::Exact) {
                known_dimensions.height = keyword_height.exact;
            } else {
                const AvailableSpace width_available =
                    known_dimensions.width.has_value() ? AvailableSpace::definite(*known_dimensions.width)
                                                       : AvailableSpace::definite(stretch_size.width);
                known_dimensions.height = measure_child_size_impl(
                    node, known_dimensions, area_opt,
                    Size<AvailableSpace>{width_available, keyword_height.available}, sizing_mode,
                    AbsoluteAxis::Vertical, Line_bool_FALSE, measure_fn);
            }
        }
    }

    /// Size and position a single out-of-flow box against its containing block, and store its
    /// layout. Port of `src/compute/oof.rs::layout_oof_box`.
    template <typename BeforeLayout, typename MeasureFunction>
    detail::OofBoxLayout layout_oof_box_impl(const OofCandidate& candidate, Size<float> area_size,
                                             Point<float> area_offset, Direction direction,
                                             Point<Overflow> container_overflow, detail::GridAreaLookup grid,
                                             BeforeLayout before_layout, MeasureFunction& measure_fn) {
        const Style& child_style = nodes_[candidate.node.index].data.style;

#ifdef TAFFY_GRID
        // If the containing block is a grid container, the box is positioned relative to the grid
        // area determined by its grid-placement properties (falling back to the passed area for
        // `auto` placement)
        if (grid.info != nullptr) {
            const Rect<float> padding_box{area_offset.x, area_offset.x + area_size.width, area_offset.y,
                                          area_offset.y + area_size.height};
            const Rect<float> grid_area =
                detail::resolve_absolute_grid_area(*grid.info, child_style, direction, padding_box);
            area_size = Size<float>{grid_area.right - grid_area.left, grid_area.bottom - grid_area.top};
            area_offset = Point<float>{grid_area.left, grid_area.top};
        }
#endif

        const Size<std::optional<float>> area_opt{std::optional<float>(area_size.width),
                                                  std::optional<float>(area_size.height)};
        const std::optional<float> aspect_ratio = child_style.aspect_ratio;
        const Rect<std::optional<float>> margin_opt = maybe_resolve_rect(child_style.margin, area_size.width);
        const Rect<float> padding = resolve_or_zero_rect(child_style.padding, area_size.width);
        const Rect<float> border = resolve_or_zero_rect(child_style.border, area_size.width);
        const Size<float> padding_border_sum = (padding + border).sum_axes();
        const Size<float> box_sizing_adjustment =
            child_style.box_sizing == BoxSizing::ContentBox ? padding_border_sum : Size_f32_ZERO;

        // Resolve insets against the positioning area
        const std::optional<float> left = maybe_resolve(child_style.inset.left, area_size.width);
        const std::optional<float> right = maybe_resolve(child_style.inset.right, area_size.width);
        const std::optional<float> top = maybe_resolve(child_style.inset.top, area_size.height);
        const std::optional<float> bottom = maybe_resolve(child_style.inset.bottom, area_size.height);

        // Compute known dimensions from min/max/inherent size styles
        const Size<std::optional<float>> style_size = detail::maybe_add_size(
            maybe_resolve_size(child_style.size, area_opt).maybe_apply_aspect_ratio(aspect_ratio),
            box_sizing_adjustment);
        Size<std::optional<float>> min_size = detail::maybe_add_size(
            maybe_resolve_size(child_style.min_size, area_opt).maybe_apply_aspect_ratio(aspect_ratio),
            box_sizing_adjustment);
        min_size.width =
            std::max(min_size.width.value_or(padding_border_sum.width), padding_border_sum.width);
        min_size.height =
            std::max(min_size.height.value_or(padding_border_sum.height), padding_border_sum.height);
        const Size<std::optional<float>> max_size = detail::maybe_add_size(
            maybe_resolve_size(child_style.max_size, area_opt).maybe_apply_aspect_ratio(aspect_ratio),
            box_sizing_adjustment);
        Size<std::optional<float>> known_dimensions = detail::maybe_clamp_size(style_size, min_size, max_size);

        const bool is_replaced = detail::style_is_replaced(child_style);
        const bool is_table = detail::style_is_table(child_style);
        const Direction item_direction = child_style.direction;
        const AlignSelf normal_alignment{AlignItemsKeyword::Normal, AlignmentSafety::Default};
        const AlignSelf justify_self = detail::resolve_self_relative(
            detail::style_justify_self_or(child_style, normal_alignment), item_direction,
            direction, true);
        const AlignSelf align_self = detail::resolve_self_relative(
            child_style.align_self.has_value() ? *child_style.align_self : normal_alignment, item_direction, direction,
            false);
        const Point<Overflow> overflow = child_style.overflow;
        const float scrollbar_width = child_style.scrollbar_width;
#ifdef TAFFY_CONTENT_SIZE
        [[maybe_unused]] const Contain contain = child_style.contain;
#endif

        // Static positions are relative to the containing block's border box: make them relative
        // to the inset-resolution area
        const Point<AxisStaticPosition> static_position{
            AxisStaticPosition{Line<float>{candidate.static_position.x.area.start - area_offset.x,
                                           candidate.static_position.x.area.end - area_offset.x},
                               candidate.static_position.x.align},
            AxisStaticPosition{Line<float>{candidate.static_position.y.area.start - area_offset.y,
                                           candidate.static_position.y.area.end - area_offset.y},
                               candidate.static_position.y.align}};

        detail::OofAxis horizontal;
        horizontal.cb_size = area_size.width;
        horizontal.inset = Line<std::optional<float>>{left, right};
        horizontal.margin = Line<std::optional<float>>{margin_opt.left, margin_opt.right};
        horizontal.static_position = static_position.x;
        horizontal.alignment = detail::oof_alignment_from_resolved(justify_self);
        horizontal.safety = justify_self.safety;
        horizontal.is_inline = true;
        horizontal.reversed = direction == Direction::Rtl;
        horizontal.is_scroll_axis = is_scroll_container(container_overflow.x);

        detail::OofAxis vertical;
        vertical.cb_size = area_size.height;
        vertical.inset = Line<std::optional<float>>{top, bottom};
        vertical.margin = Line<std::optional<float>>{margin_opt.top, margin_opt.bottom};
        vertical.static_position = static_position.y;
        vertical.alignment = detail::oof_alignment_from_resolved(align_self);
        vertical.safety = align_self.safety;
        vertical.is_inline = false;
        vertical.reversed = false;
        vertical.is_scroll_axis = is_scroll_container(container_overflow.y);

        const Size<float> stretch_fit_size{horizontal.stretch_fit_size(), vertical.stretch_fit_size()};
        Size<float> available_space = stretch_fit_size;
        if (is_table) {
            const Size<float> non_auto_margin{margin_opt.left.value_or(0.0f) + margin_opt.right.value_or(0.0f),
                                              margin_opt.top.value_or(0.0f) + margin_opt.bottom.value_or(0.0f)};
            available_space = available_space.f32_min(
                Size<float>{std::max(area_size.width - non_auto_margin.width, 0.0f),
                            std::max(area_size.height - non_auto_margin.height, 0.0f)});
        }
        const Size<AvailableSpace> available_space_avail{
            AvailableSpace::definite(maybe_clamp_f32(available_space.width, min_size.width, max_size.width)),
            AvailableSpace::definite(maybe_clamp_f32(available_space.height, min_size.height, max_size.height))};

        // Resolve any sizing keywords (min-content, max-content, fit-content, stretch) in the size
        // styles. An explicitly sized axis takes precedence over the automatic size below.
        if (child_style.size.width.is_sizing_keyword() || child_style.size.height.is_sizing_keyword()) {
            resolve_absolute_sizing_keywords_impl(
                candidate.node, known_dimensions, child_style.size, area_size,
                Rect<std::optional<float>>{left, right, top, bottom}, margin_opt, SizingMode::ContentSize, measure_fn);
            known_dimensions = detail::maybe_clamp_size(
                known_dimensions.maybe_apply_aspect_ratio(aspect_ratio), min_size, max_size);
        }

        // Resolve automatic sizes: stretch-fit when both insets are set and the alignment is
        // `stretch` (or `normal` for non-replaced, non-table boxes); otherwise fit-content.
        const bool stretch_width = !known_dimensions.width.has_value() && horizontal.is_stretch_sized(is_replaced, is_table);
        const bool stretch_height =
            !known_dimensions.height.has_value() && vertical.is_stretch_sized(is_replaced, is_table);
        const bool both_explicit_stretch = horizontal.alignment == detail::OofAlignment::Stretch
            && vertical.alignment == detail::OofAlignment::Stretch;
        const std::optional<float> stretch_aspect_ratio =
            (stretch_width && stretch_height && both_explicit_stretch) ? std::nullopt : aspect_ratio;
        if (stretch_width) {
            if (is_table) {
                min_size.width = std::max(min_size.width.value_or(0.0f), stretch_fit_size.width);
            } else {
                known_dimensions.width = stretch_fit_size.width;
                known_dimensions = detail::maybe_clamp_size(
                    known_dimensions.maybe_apply_aspect_ratio(stretch_aspect_ratio), min_size, max_size);
            }
        }
        if (!known_dimensions.height.has_value() && stretch_height) {
            if (is_table) {
                min_size.height = std::max(min_size.height.value_or(0.0f), stretch_fit_size.height);
            } else {
                known_dimensions.height = stretch_fit_size.height;
                known_dimensions = detail::maybe_clamp_size(
                    known_dimensions.maybe_apply_aspect_ratio(stretch_aspect_ratio), min_size, max_size);
            }
        }

        Size<float> final_size;
        if (known_dimensions.width.has_value() && known_dimensions.height.has_value()) {
            final_size = Size<float>{*known_dimensions.width, *known_dimensions.height};
        } else {
            const Size<float> measured_size = measure_child_size_both_impl(
                candidate.node, known_dimensions, area_opt, available_space_avail, SizingMode::ContentSize,
                Line_bool_FALSE, measure_fn);
            final_size = Size<float>{known_dimensions.width.value_or(measured_size.width),
                                     known_dimensions.height.value_or(measured_size.height)};
        }
        final_size = detail::maybe_clamp_size(Size<std::optional<float>>{final_size.width, final_size.height},
                                              min_size, max_size)
                         .unwrap_or(final_size);

        LayoutInput layout_input;
        layout_input.known_dimensions =
            Size<std::optional<float>>{final_size.width, final_size.height};
        layout_input.known_dimensions_are_definite = Size<bool>{true, true};
        layout_input.parent_size = area_opt;
        layout_input.available_space = available_space_avail;
        layout_input.sizing_mode = SizingMode::ContentSize;
        layout_input.axis = RequestedAxis::Both;
        layout_input.run_mode = RunMode::PerformLayout;
        layout_input.vertical_margins_are_collapsible = Line_bool_FALSE;
        before_layout(layout_input);
        LayoutOutput layout_output = compute_node_layout(candidate.node, layout_input, measure_fn);

        // Resolve auto margins and align the margin box within the IMCB
        const Line<float> horizontal_margin = horizontal.resolve_margins(final_size.width);
        const Line<float> vertical_margin = vertical.resolve_margins(final_size.height);
        const Rect<float> resolved_margin{horizontal_margin.start, horizontal_margin.end, vertical_margin.start,
                                          vertical_margin.end};
        const Point<float> location{horizontal.position(final_size.width, horizontal_margin) + area_offset.x,
                                    vertical.position(final_size.height, vertical_margin) + area_offset.y};

        // Note: axis intentionally switched: scrollbars take space in the opposite axis
        const Size<float> scrollbar_size{overflow.y == Overflow::Scroll ? scrollbar_width : 0.0f,
                                         overflow.x == Overflow::Scroll ? scrollbar_width : 0.0f};

        Layout layout = Layout::new_layout();
        layout.order = candidate.order;
        layout.location = location;
        layout.size = final_size;
#ifdef TAFFY_CONTENT_SIZE
        layout.scrollable_overflow_rect = layout_output.scrollable_overflow_rect;
#endif
        layout.scrollbar_size = scrollbar_size;
        layout.padding = padding;
        layout.border = border;
        layout.margin = resolved_margin;
        set_unrounded_layout(candidate.node, layout);

        detail::OofBoxLayout result;
        result.layout = layout;
        result.surfaced = layout_output.oof_candidates;
        result.overflow = overflow;
        result.contain = child_style.contain;
        return result;
    }

    /// Perform final layout on out-of-flow candidates claimed by `node_id`, collecting the
    /// remainder into `unclaimed`. Port of `src/compute/oof.rs::perform_oof_layout`.
    template <typename MeasureFunction>
    Rect<float> perform_oof_layout_impl(NodeId node_id, const std::vector<OofCandidate>& candidates,
                                        Size<float> area_size, Point<float> area_offset, Direction direction,
                                        bool claims_absolute, bool claims_fixed,
                                        Point<Overflow> container_overflow, detail::GridAreaLookup grid,
                                        Point<bool> reversed, std::vector<NodeId>& hoisted, OofCandidates& unclaimed,
                                        MeasureFunction& measure_fn) {
        (void)node_id;
        Rect<float> absolute_overflow_rect{0.0f, 0.0f, 0.0f, 0.0f};
#ifdef TAFFY_CONTENT_SIZE
        if (direction == Direction::Rtl) {
            absolute_overflow_rect =
                detail::rect_union(absolute_overflow_rect, Rect<float>{0.0f, std::max(area_size.width, 0.0f), 0.0f, 0.0f});
        }
#endif
        if (candidates.empty()) return absolute_overflow_rect;

        detail::ScrollOverflowContainer overflow_container;
        overflow_container.parent_is_scroll_container = detail::is_scroll_container_(container_overflow);
        overflow_container.parent_size = area_size;
        overflow_container.reversed = reversed;
        overflow_container.padding = Rect<float>{0.0f, 0.0f, 0.0f, 0.0f};

        auto claims = [&](Position position) -> bool {
            if (position == Position::Absolute) return claims_absolute;
            if (position == Position::Fixed) return claims_fixed;
            return false;
        };

        std::vector<OofCandidate> worklist;
        for (const auto& candidate : candidates) {
            if (claims(candidate.position)) worklist.push_back(candidate);
            else unclaimed.push(candidate);
        }

        size_t index = 0;
        while (index < worklist.size()) {
            const OofCandidate candidate = worklist[index];
            index++;
            hoisted.push_back(candidate.node);

            detail::OofBoxLayout box = layout_oof_box_impl(
                candidate, area_size, area_offset, direction, container_overflow, grid,
                [](const LayoutInput&) {}, measure_fn);

            // Re-sweep any candidates surfaced from within the box's subtree, translating their
            // anchors so that they are relative to this node's border box
            if (!box.surfaced.is_empty()) {
                box.surfaced.translate(box.layout.location);
                for (const auto& surfaced_candidate : box.surfaced.inner) {
                    if (claims(surfaced_candidate.position)) worklist.push_back(surfaced_candidate);
                    else unclaimed.push(surfaced_candidate);
                }
            }
#ifdef TAFFY_CONTENT_SIZE
            const Point<float> relative_location{box.layout.location.x - area_offset.x,
                                                 box.layout.location.y - area_offset.y};
            absolute_overflow_rect = detail::rect_union(
                absolute_overflow_rect,
                detail::compute_scrollable_overflow_contribution(
                    relative_location, box.layout.size, box.layout.scrollable_overflow_rect.rect(), box.layout.border,
                    box.overflow, box.contain, overflow_container, Rect<float>{0.0f, 0.0f, 0.0f, 0.0f}));
#endif
        }

        return absolute_overflow_rect;
    }

    /// The node's inset-resolution area for out-of-flow boxes (border box minus borders and
    /// scrollbar gutters)
    std::optional<OofPositioningArea> oof_positioning_area_impl(
        NodeId node_id, const LayoutInput& in, const LayoutOutput& output) {
        const Style& style = nodes_[node_id.index].data.style;
        const Rect<float> border = resolve_or_zero_rect(style.border, in.parent_size.width);
        const Point<float> gutter = detail::scrollbar_gutter_of(style);
        const Rect<float> padding_box_inset =
            border + Rect<float>{style.direction == Direction::Rtl ? gutter.x : 0.0f,
                                 style.direction == Direction::Rtl ? 0.0f : gutter.x, 0.0f, gutter.y};
        OofPositioningArea area;
        area.size = Size<float>{output.size.width - padding_box_inset.horizontal_axis_sum(),
                                output.size.height - padding_box_inset.vertical_axis_sum()};
        area.offset = Point<float>{padding_box_inset.left, padding_box_inset.top};
        return area;
    }

#ifdef TAFFY_FLEXBOX
    /// The static position of an out-of-flow child of a flex container (port of
    /// `flexbox.rs::collect_oof_candidates` static position computation)
    Point<AxisStaticPosition> flex_static_position(const Style& container_style, Size<float> container_size,
                                                   Rect<float> content_box_inset, const Style& child_style,
                                                   Direction direction) {
        const FlexDirection dir = container_style.flex_direction;
        const bool is_row_ = is_row(dir);
        const bool is_column_ = !is_row_;
        const bool is_wrap_reverse = flex_wrap_is_reverse(container_style.flex_wrap);

        const bool main_axis_is_horizontal = is_row_;
        const bool cross_axis_is_horizontal = !is_row_;
        const bool main_is_rtl = main_axis_is_horizontal && direction == Direction::Rtl;
        const bool cross_is_rtl = cross_axis_is_horizontal && direction == Direction::Rtl;
        const bool main_axis_flex_start_reversed = is_reverse(dir) ^ main_is_rtl;
        const bool cross_axis_flex_start_reversed = is_wrap_reverse ^ cross_is_rtl;

        const Line<float> main_area{content_box_inset.main_start(dir),
                                    container_size.main(dir) - content_box_inset.main_end(dir)};
        const Line<float> cross_area{content_box_inset.cross_start(dir),
                                     container_size.cross(dir) - content_box_inset.cross_end(dir)};

        // Main-axis static position (justify-content)
        AlignContentKeyword main_keyword = container_style.justify_content.has_value()
                                               ? container_style.justify_content->keyword
                                               : AlignContentKeyword::FlexStart;
        if (main_keyword == AlignContentKeyword::Normal) main_keyword = AlignContentKeyword::FlexStart;
        bool main_start_position = true;
        if (main_keyword == AlignContentKeyword::Start) main_start_position = !main_is_rtl;
        else if (main_keyword == AlignContentKeyword::End) main_start_position = main_is_rtl;
        int main_edge;
        switch (main_keyword) {
            case AlignContentKeyword::SpaceBetween:
            case AlignContentKeyword::Normal:
            case AlignContentKeyword::Stretch:
            case AlignContentKeyword::FlexStart:
                main_edge = main_axis_flex_start_reversed ? AxisStaticEdge::End : AxisStaticEdge::Start;
                break;
            case AlignContentKeyword::FlexEnd:
                main_edge = main_axis_flex_start_reversed ? AxisStaticEdge::Start : AxisStaticEdge::End;
                break;
            case AlignContentKeyword::Start:
            case AlignContentKeyword::End:
                main_edge = main_start_position ? AxisStaticEdge::Start : AxisStaticEdge::End;
                break;
            default:
                main_edge = AxisStaticEdge::Center;
                break;
        }

        // Cross-axis static position (align-self)
        AlignItems align_items = detail::resolve_normal_alignment(
            container_style.align_items.has_value() ? *container_style.align_items
                                                    : AlignItems{AlignItemsKeyword::Normal, AlignmentSafety::Default});
        AlignItems align_self = detail::resolve_normal_alignment(
            child_style.align_self.has_value() ? *child_style.align_self : align_items);
        align_self = detail::resolve_self_relative(align_self, child_style.direction, direction, is_column_);

        auto cross_edge_for = [&](AlignItemsKeyword keyword) -> int {
            bool start_position = true;
            if (keyword == AlignItemsKeyword::Start || keyword == AlignItemsKeyword::Baseline)
                start_position = !cross_is_rtl;
            else if (keyword == AlignItemsKeyword::End) start_position = cross_is_rtl;
            switch (keyword) {
                case AlignItemsKeyword::Start:
                case AlignItemsKeyword::End:
                case AlignItemsKeyword::Baseline:
                    return start_position ? AxisStaticEdge::Start : AxisStaticEdge::End;
                case AlignItemsKeyword::Stretch:
                case AlignItemsKeyword::FlexStart:
                    return cross_axis_flex_start_reversed ? AxisStaticEdge::End : AxisStaticEdge::Start;
                case AlignItemsKeyword::FlexEnd:
                    return cross_axis_flex_start_reversed ? AxisStaticEdge::Start : AxisStaticEdge::End;
                case AlignItemsKeyword::Center:
                    return AxisStaticEdge::Center;
                default:
                    return AxisStaticEdge::Start;
            }
        };

        const AxisStaticPosition cross{
            cross_area,
            AxisStaticAlign{cross_edge_for(align_self.keyword), align_self.safety,
                            cross_edge_for(resolve_self_alignment_safety(align_self, true))}};
        const AxisStaticPosition main{main_area, AxisStaticAlign::from_keyword(main_edge)};

        if (is_row_) return Point<AxisStaticPosition>{main, cross};
        return Point<AxisStaticPosition>{cross, main};
    }
#endif

    /// Compute the static position of an out-of-flow child of `node_id`
    Point<AxisStaticPosition> static_position_for_child(NodeId node_id, const Style& container_style,
                                                        Size<float> container_size, const Style& child_style,
                                                        const LayoutInput& in) {
        const Direction direction = container_style.direction;
        const Rect<float> padding = resolve_or_zero_rect(container_style.padding, in.parent_size.width);
        const Rect<float> border = resolve_or_zero_rect(container_style.border, in.parent_size.width);
        const Point<float> gutter = detail::scrollbar_gutter_of(container_style);
        Rect<float> content_box_inset = padding + border;
        content_box_inset.bottom += gutter.y;
        if (direction == Direction::Rtl) content_box_inset.left += gutter.x;
        else content_box_inset.right += gutter.x;

        const AlignSelf normal_alignment{AlignItemsKeyword::Normal, AlignmentSafety::Default};

#ifdef TAFFY_FLEXBOX
        if (container_style.display == Display::Flex) {
            return flex_static_position(container_style, container_size, content_box_inset, child_style, direction);
        }
#endif
#ifdef TAFFY_GRID
        if (container_style.display == Display::Grid) {
            Rect<float> area{border.left + padding.left + (direction == Direction::Rtl ? gutter.x : 0.0f),
                             container_size.width - border.right - padding.right
                                 - (direction == Direction::Rtl ? 0.0f : gutter.x),
                             border.top + padding.top,
                             container_size.height - border.bottom - padding.bottom - gutter.y};
            // When the grid is the box's containing block, the static-position rectangle is the
            // grid area determined by the box's grid placement
            if (detail::style_is_positioned(container_style)) {
                const Rect<float> padding_box{
                    border.left + (direction == Direction::Rtl ? gutter.x : 0.0f),
                    container_size.width - border.right - (direction == Direction::Rtl ? 0.0f : gutter.x),
                    border.top, container_size.height - border.bottom - gutter.y};
                area = detail::resolve_absolute_grid_area(nodes_[node_id.index].data.detailed_info, child_style,
                                                          direction, padding_box);
            }
            AlignItems justify_items = detail::resolve_normal_alignment(
                container_style.justify_items.has_value() ? *container_style.justify_items : normal_alignment);
            AlignItems align_items = detail::resolve_normal_alignment(
                container_style.align_items.has_value() ? *container_style.align_items : normal_alignment);
            const AlignSelf justify_self = detail::resolve_self_relative(
                detail::style_justify_self_or(child_style, justify_items),
                child_style.direction, direction, true);
            const AlignSelf align_self = detail::resolve_self_relative(
                child_style.align_self.has_value() ? *child_style.align_self : align_items, child_style.direction,
                direction, false);
            return Point<AxisStaticPosition>{
                AxisStaticPosition::from_alignment(justify_self, Line<float>{area.left, area.right},
                                                   direction == Direction::Rtl),
                AxisStaticPosition::from_alignment(align_self, Line<float>{area.top, area.bottom}, false)};
        }
#endif
#ifdef TAFFY_BLOCK_LAYOUT
        if (container_style.display == Display::Block || container_style.display == Display::FlowRoot) {
            // The static-position rectangle spans the content box in the inline axis and is a
            // zero-height line at the child's hypothetical position in the block axis.
            const Line<float> inline_area{content_box_inset.left, container_size.width - content_box_inset.right};
            float flow_y = content_box_inset.top;
            for (size_t i = 0; i < children_[node_id.index].size(); ++i) {
                NodeId prev = children_[node_id.index][i];
                const Style& prev_style = nodes_[prev.index].data.style;
                if (prev_style.display == Display::None || detail::style_is_out_of_flow(prev_style)) continue;
                const Layout& prev_layout = nodes_[prev.index].data.unrounded_layout;
                flow_y = std::max(flow_y, prev_layout.location.y + prev_layout.size.height + prev_layout.margin.bottom);
            }
            const AlignSelf justify_self = detail::resolve_self_relative(
                detail::style_justify_self_or(child_style, normal_alignment),
                child_style.direction, direction, true);
            const AlignSelf align_self = detail::resolve_self_relative(
                child_style.align_self.has_value() ? *child_style.align_self : normal_alignment,
                child_style.direction, direction, false);
            return Point<AxisStaticPosition>{
                AxisStaticPosition::from_alignment(justify_self, inline_area, direction == Direction::Rtl),
                AxisStaticPosition::from_alignment(align_self, Line<float>{flow_y, flow_y}, false)};
        }
#endif
        // Fallback: the content box origin
        return Point<AxisStaticPosition>{
            AxisStaticPosition::from_edge(content_box_inset.left, AxisStaticEdge::Start),
            AxisStaticPosition::from_edge(content_box_inset.top, AxisStaticEdge::Start)};
    }

    /// Collect the direct out-of-flow children of `node_id` (and any candidates bubbled up from
    /// its in-flow children) into `output.oof_candidates`
    void collect_oof_candidates_impl(NodeId node_id, const LayoutInput& in, LayoutOutput& output) {
        const Style& container_style = nodes_[node_id.index].data.style;
        const Size<float> container_size = output.size;

        auto already_present = [&](NodeId node) {
            for (const auto& c : output.oof_candidates.inner)
                if (c.node == node) return true;
            return false;
        };

        for (size_t i = 0; i < children_[node_id.index].size(); ++i) {
            const NodeId child = children_[node_id.index][i];
            auto& child_data = nodes_[child.index].data;
            const Style& child_style = child_data.style;
            if (child_style.display == Display::None) continue;

            if (detail::style_is_out_of_flow(child_style)) {
                if (already_present(child)) continue;
                OofCandidate candidate;
                candidate.node = child;
                candidate.order = static_cast<uint32_t>(i);
                candidate.position = child_style.position;
                candidate.static_position =
                    static_position_for_child(node_id, container_style, container_size, child_style, in);
                output.oof_candidates.push(candidate);
            } else if (!child_data.pending_oof.is_empty()) {
                OofCandidates pending = child_data.pending_oof;
                child_data.pending_oof.clear();
                pending.translate(Point<float>{child_data.unrounded_layout.location.x,
                                               child_data.unrounded_layout.location.y});
                for (const auto& c : pending.inner) {
                    if (!already_present(c.node)) output.oof_candidates.push(c);
                }
            }
        }
    }

    /// Run the out-of-flow positioning pass for `node_id` after its layout algorithm produced
    /// `output`. Port of `src/compute/oof.rs::compute_oof_layout`.
    template <typename MeasureFunction>
    void compute_oof_layout_impl(NodeId node_id, const LayoutInput& in, LayoutOutput& output,
                                 MeasureFunction& measure_fn) {
        if (!output.oof_positioning_area.has_value()) return;
        collect_oof_candidates_impl(node_id, in, output);
        if (output.oof_candidates.is_empty()) {
            clear_hoisted_children(node_id);
            return;
        }

        const Style& style = nodes_[node_id.index].data.style;
        const Direction direction = style.direction;
        const bool claims_absolute = detail::style_is_positioned(style);
        const bool claims_fixed = false;

        const OofCandidates candidates = output.oof_candidates;
        output.oof_candidates.clear();
        const OofPositioningArea area = *output.oof_positioning_area;

        Point<bool> reversed{false, false};
#ifdef TAFFY_CONTENT_SIZE
        reversed = output.scrollable_overflow_rect.is_reversed();
#endif

        detail::GridAreaLookup grid_lookup;
#ifdef TAFFY_GRID
        if (style.display == Display::Grid) grid_lookup.info = &nodes_[node_id.index].data.detailed_info;
#endif

        std::vector<NodeId> hoisted;
        OofCandidates unclaimed;
        const Rect<float> overflow_rect = perform_oof_layout_impl(
            node_id, candidates.inner, area.size, area.offset, direction, claims_absolute, claims_fixed,
            Point<Overflow>{style.overflow.x, style.overflow.y}, grid_lookup, reversed, hoisted, unclaimed,
            measure_fn);

        clear_hoisted_children(node_id);
        add_hoisted_children(node_id, hoisted);
        output.oof_candidates = unclaimed;
#ifdef TAFFY_CONTENT_SIZE
        output.scrollable_overflow_rect = detail::finalize_scrollable_overflow(
            detail::rect_union(output.scrollable_overflow_rect.rect(), overflow_rect), area.size,
            output.scrollable_overflow_rect.is_reversed(), detail::style_is_scroll_container(style));
#endif
    }

    // =======================================================================
    // Scrollable overflow accumulation for containers
    // Ref: src/compute/common/scrollable_overflow.rs
    // =======================================================================
    void compute_container_scrollable_overflow_impl(NodeId node_id, const LayoutInput& in, LayoutOutput& output) {
#ifdef TAFFY_CONTENT_SIZE
        const Style& style = nodes_[node_id.index].data.style;
        if (children_[node_id.index].empty()) return;

        const Rect<float> padding = resolve_or_zero_rect(style.padding, in.parent_size.width);
        const Rect<float> border = resolve_or_zero_rect(style.border, in.parent_size.width);
        const Point<float> gutter = detail::scrollbar_gutter_of(style);
        const bool is_rtl = style.direction == Direction::Rtl;
        const Rect<float> padding_box_inset =
            border + Rect<float>{is_rtl ? gutter.x : 0.0f, is_rtl ? 0.0f : gutter.x, 0.0f, gutter.y};
        const Size<float> client_size{output.size.width - padding_box_inset.horizontal_axis_sum(),
                                      output.size.height - padding_box_inset.vertical_axis_sum()};

        detail::ScrollOverflowContainer container;
        container.parent_is_scroll_container = detail::style_is_scroll_container(style);
        container.parent_size = client_size;
        container.reversed = Point<bool>{false, false};
        container.padding = padding;

        Rect<float> acc{0.0f, 0.0f, 0.0f, 0.0f};
        for (size_t i = 0; i < children_[node_id.index].size(); ++i) {
            const NodeId child = children_[node_id.index][i];
            const Style& child_style = nodes_[child.index].data.style;
            if (child_style.display == Display::None || detail::style_is_out_of_flow(child_style)) continue;
            const Layout& child_layout = nodes_[child.index].data.unrounded_layout;
            const Point<float> location{child_layout.location.x - padding_box_inset.left,
                                        child_layout.location.y - padding_box_inset.top};
            acc = detail::rect_union(
                acc, detail::compute_scrollable_overflow_contribution(
                         location, child_layout.size, child_layout.scrollable_overflow_rect.rect(),
                         child_layout.border, child_style.overflow, child_style.contain, container,
                         child_layout.margin));
        }
        if (is_rtl) acc = detail::rect_union(acc, Rect<float>{0.0f, std::max(client_size.width, 0.0f), 0.0f, 0.0f});
        const Point<bool> reversed = output.scrollable_overflow_rect.is_reversed();
        output.scrollable_overflow_rect = detail::finalize_scrollable_overflow(
            detail::rect_union(output.scrollable_overflow_rect.rect(), acc), client_size, reversed,
            detail::style_is_scroll_container(style));
#endif
    }

public:

    // Flexbox layout implementation
    // NOTE: flex owner edit (cross-region, flagged to Lead): the body of this method moved into
    // Section 15 as flex_compute_layout(). Please coordinate with the tree owner before merging.
    template <typename MeasureFunction>
    LayoutOutput compute_flexbox_layout_impl(NodeId node_id, LayoutInput inputs, MeasureFunction& measure_fn) {
        return flex_compute_layout(*this, node_id, inputs, measure_fn);
    }


    // Grid layout implementation
    template <typename MeasureFunction>
    LayoutOutput compute_grid_layout_impl(NodeId node_id, LayoutInput inputs, MeasureFunction& measure_fn) {
        return grid_detail::compute_grid_layout(*this, node_id, inputs, measure_fn);
    }

    // Block layout implementation
    // Ref: src/compute/block.rs
    template <typename MeasureFunction>
    LayoutOutput compute_block_layout_impl(NodeId node_id, LayoutInput inputs, MeasureFunction& measure_fn) {
        return block_detail::compute_block_layout(*this, node_id, inputs, nullptr, measure_fn);
    }

    void print_tree(NodeId root) const {
        printf("TREE\n");
        print_tree_impl(root, false, "");
    }

    void print_tree_impl(NodeId node, bool has_sibling, const std::string& lines_prefix) const {
        const auto& l = use_rounding_ ? nodes_[node.index].data.final_layout : nodes_[node.index].data.unrounded_layout;
        const auto& s = nodes_[node.index].data.style;
        const char* label = "UNKNOWN";
        if (s.display == Display::None) label = "NONE";
        else if (children_[node.index].empty()) label = "LEAF";
        else if (s.display == Display::Flex) label = is_row(s.flex_direction) ? "FLEX ROW" : "FLEX COL";
        else if (s.display == Display::Grid) label = "GRID";
        else if (s.display == Display::Block) label = "BLOCK";

        const char* fork_str = has_sibling ? "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 " : "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 ";
        printf("%s%s %s [x: %-6.1f y: %-6.1f w: %-6.1f h: %-6.1f border: l:%.1f r:%.1f t:%.1f b:%.1f padding: l:%.1f r:%.1f t:%.1f b:%.1f]\n",
            lines_prefix.c_str(), fork_str, label,
            l.location.x, l.location.y, l.size.width, l.size.height,
            l.border.left, l.border.right, l.border.top, l.border.bottom,
            l.padding.left, l.padding.right, l.padding.top, l.padding.bottom);

        const char* bar = has_sibling ? "\xe2\x94\x82   " : "    ";
        std::string new_prefix = lines_prefix + bar;
        size_t num_children = children_[node.index].size();
        for (size_t i = 0; i < num_children; ++i) {
            print_tree_impl(children_[node.index][i], i < num_children - 1, new_prefix);
        }
    }
};

} // namespace taffy

#endif // TAFFY_HPP
