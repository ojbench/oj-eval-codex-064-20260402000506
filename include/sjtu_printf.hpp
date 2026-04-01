#pragma once
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sjtu {

using sv_t = std::string_view;

struct format_error : std::exception {
public:
    constexpr format_error(const char *msg = "invalid format") noexcept : msg(msg) {}
    auto what() const noexcept -> const char * override { return msg; }

private:
    const char *msg;
};

// Forward declaration
template <typename Tp>
struct formatter;

struct format_info {
    inline static constexpr auto npos = static_cast<std::size_t>(-1);
    std::size_t position; // where is the specifier
    std::size_t consumed; // how many characters consumed (1 for s/d/u/_)
};

// Helpers for compile-time parsing
constexpr bool is_escaped_percent(sv_t fmt, std::size_t pos) {
    // Assumes fmt[pos] == '%'; treat "%%" as escaped
    return (pos + 1 < fmt.size() && fmt[pos + 1] == '%');
}

constexpr std::size_t find_next_unescaped_percent(sv_t fmt, std::size_t start) {
    for (std::size_t i = start; i < fmt.size(); ++i) {
        if (fmt[i] == '%') {
            if (is_escaped_percent(fmt, i)) {
                ++i; // skip the second '%'
                continue;
            }
            return i;
        }
    }
    return sv_t::npos;
}

// format_string with compile-time validation
template <typename... Args>
struct format_string {
public:
    consteval format_string(const char *fmt) : fmt_str(fmt), fmt_idx{} {
        compile_time_check<Args...>();
    }

    constexpr auto get_format() const -> std::string_view { return fmt_str; }
    constexpr auto get_index() const -> std::span<const format_info> { return fmt_idx; }

private:
    inline static constexpr auto Nm = sizeof...(Args);
    std::string_view fmt_str;
    std::array<format_info, Nm> fmt_idx;

    // Single-argument parser used in fold
    template <typename T>
    consteval void consume_next(std::size_t &cursor, std::size_t arg_index) {
        const auto pos = find_next_unescaped_percent(fmt_str, cursor);
        if (pos == sv_t::npos) {
            throw format_error{"missing specifier for argument"};
        }
        if (pos + 1 >= fmt_str.size()) {
            throw format_error{"missing specifier after '%'"};
        }
        const char spec = fmt_str[pos + 1];
        std::size_t consumed = 0;
        // Let per-type formatter validate the specifier
        sv_t tail(fmt_str.data() + pos + 1, fmt_str.size() - (pos + 1));
        consumed = formatter<std::decay_t<T>>::parse(tail);
        if (consumed == 0) {
            if (spec == '_') {
                consumed = 1; // default formatter
            } else {
                throw format_error{"invalid specifier"};
            }
        }
        fmt_idx[arg_index] = {pos, consumed};
        cursor = pos + 1 + consumed;
    }

    template <typename... Ts>
    consteval void compile_time_check() {
        std::size_t cursor = 0;
        std::size_t idx = 0;
        (consume_next<Ts>(cursor, idx++), ...);
        // Ensure no extra unescaped specifiers remain
        const auto extra = find_next_unescaped_percent(fmt_str, cursor);
        if (extra != sv_t::npos) {
            throw format_error{"too many specifiers"};
        }
    }
};

// String-like specializations
template <typename StrLike>
requires(
    std::same_as<StrLike, std::string> ||
    std::same_as<StrLike, std::string_view> ||
    std::same_as<StrLike, char *> ||
    std::same_as<StrLike, const char *>)
struct formatter<StrLike> {
    static constexpr auto parse(sv_t fmt) -> std::size_t { return fmt.starts_with("s") ? 1u : 0u; }
    static auto format_to(std::ostream &os, const StrLike &val, sv_t fmt = "s") -> void {
        if (!fmt.starts_with("s")) throw format_error{};
        if constexpr (std::same_as<StrLike, std::string_view>) {
            os << val;
        } else {
            os << sv_t(val);
        }
    }
};

// Integral types: accept both 'd' and 'u'
template <typename Int>
requires std::is_integral_v<Int>
struct formatter<Int> {
    static constexpr auto parse(sv_t fmt) -> std::size_t {
        return (fmt.starts_with("d") || fmt.starts_with("u")) ? 1u : 0u;
    }
    static auto format_to(std::ostream &os, const Int &val, sv_t fmt = "_") -> void {
        if (fmt.starts_with("d")) {
            os << static_cast<std::int64_t>(val);
        } else if (fmt.starts_with("u")) {
            using U64 = std::uint64_t;
            os << static_cast<U64>(val);
        } else if (fmt.starts_with("_")) { // default for %_
            if constexpr (std::is_signed_v<Int>) {
                os << static_cast<std::int64_t>(val);
            } else {
                os << static_cast<std::uint64_t>(val);
            }
        } else {
            throw format_error{};
        }
    }
};

// vector default formatting for %_
template <typename T>
struct formatter<std::vector<T>> {
    static constexpr auto parse(sv_t) -> std::size_t { return 0u; } // only via %_
    static auto format_to(std::ostream &os, const std::vector<T> &vec, sv_t fmt = "_") -> void {
        if (!fmt.starts_with("_")) throw format_error{};
        os << '[';
        bool first = true;
        for (const auto &e : vec) {
            if (!first) os << ',';
            first = false;
            formatter<T>::format_to(os, e, sv_t{"_"});
        }
        os << ']';
    }
};

// Fallback formatter for %_ using operator<<
template <typename T>
struct formatter {
    static constexpr auto parse(sv_t) -> std::size_t { return 0u; }
    static auto format_to(std::ostream &os, const T &val, sv_t fmt = "_") -> void {
        if (!fmt.starts_with("_")) throw format_error{};
        os << val;
    }
};

// Alias
template <typename... Args>
using format_string_t = format_string<std::decay_t<Args>...>;

// Runtime printf implementation using compile-time validated format string
namespace detail {
inline void write_until_spec(std::ostream &os, sv_t fmt, std::size_t &cursor) {
    while (cursor < fmt.size()) {
        if (fmt[cursor] == '%') {
            if (cursor + 1 < fmt.size() && fmt[cursor + 1] == '%') {
                os << '%';
                cursor += 2;
                continue;
            }
            return; // next specifier starts at cursor
        }
        os << fmt[cursor++];
    }
}

inline char read_spec(sv_t fmt, std::size_t &cursor) {
    if (cursor >= fmt.size() || fmt[cursor] != '%') throw format_error{"missing specifier after '%'"};
    if (cursor + 1 >= fmt.size()) throw format_error{"missing specifier after '%'"};
    char c = fmt[cursor + 1];
    cursor += 2; // consume '%' and spec char
    return c;
}

} // namespace detail

inline void vprintf_impl(std::ostream &os, sv_t fmt, std::size_t &cursor) {
    // Flush remaining literal text (handle %%)
    detail::write_until_spec(os, fmt, cursor);
    // Then output tail literals
    while (cursor < fmt.size()) {
        if (fmt[cursor] == '%') {
            if (cursor + 1 < fmt.size() && fmt[cursor + 1] == '%') {
                os << '%';
                cursor += 2;
            } else {
                // Unused extra specifier would be a logic error (guarded by compile-time checker)
                char c = detail::read_spec(fmt, cursor);
                (void)c; // ignore
            }
        } else {
            os << fmt[cursor++];
        }
    }
}

// Format one argument and advance
template <typename T>
inline void vprintf_one(std::ostream &os, sv_t fmt, std::size_t &cursor, const T &arg) {
    detail::write_until_spec(os, fmt, cursor);
    char spec = detail::read_spec(fmt, cursor);
    switch (spec) {
    case 's': formatter<std::decay_t<T>>::format_to(os, arg, sv_t{"s"}); break;
    case 'd': formatter<std::decay_t<T>>::format_to(os, arg, sv_t{"d"}); break;
    case 'u': formatter<std::decay_t<T>>::format_to(os, arg, sv_t{"u"}); break;
    case '_': formatter<std::decay_t<T>>::format_to(os, arg, sv_t{"_"}); break;
    default: throw format_error{"invalid specifier"};
    }
}

// Public API
template <typename... Args>
inline auto printf(format_string_t<Args...> fmt, const Args &...args) -> void {
    std::size_t cursor = 0;
    // Stream to std::cout by default
    (vprintf_one(std::cout, fmt.get_format(), cursor, args), ...);
    // Write remaining tail literals
    vprintf_impl(std::cout, fmt.get_format(), cursor);
}

} // namespace sjtu
