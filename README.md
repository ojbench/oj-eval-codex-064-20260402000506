# Problem 064 - Modern C++ printf

**ACMOJ Problem ID**: 2696

## Table of Contents

- [Problem 064 - Modern C++ printf](#problem-064-modern-c-printf)
  - [Table of Contents](#table-of-contents)
  - [Introduction](#introduction)
    - [Background](#background)
  - [Assignment Description](#assignment-description)
    - [Grade Composition](#grade-composition)
  - [Assignment Requirements](#assignment-requirements)
    - [Input Format](#input-format)
    - [Output Format](#output-format)
    - [Samples](#samples)
    - [Data Constraints](#data-constraints)
  - [Per-Testcase Resource Limits](#per-testcase-resource-limits)
  - [Test Data](#test-data)
  - [Submission Requirements](#submission-requirements)
    - [OJ Git Repository Compilation Process](#oj-git-repository-compilation-process)
    - [Git Configuration Requirements](#git-configuration-requirements)
    - [Submission Guidelines](#submission-guidelines)
    - [Evaluation Notes](#evaluation-notes)
    - [Academic Integrity](#academic-integrity)

## Introduction

### Background

Implement type-safe printf with compile-time format string validation using C++20 consteval.

## Assignment Description

### Grade Composition

| Grading Component | Percentage |
| :--: | :--: |
| Pass **2696. Modern C++ printf** | 80% |
| Code Review | 20% |

Here are several points that need clarification:

- In the Code Review, we will **strictly examine your code style and repository organization structure, etc.**

- This assignment provides some sample data for testing, stored in the `/workspace/data/064/data_test/` directory. Note that these are not the test cases on the Online Judge. Passing all local test cases does not guarantee that you will pass the OJ tests.

- Besides the provided sample data, we also encourage you to design your own test data based on your program logic to assist debugging.

## Assignment Requirements

### Problem Description

现在, 你需要支持一个安全的, 能够在编译期检验格式串 format string 是否合法的 `printf`. 为了实现这一功能, 你需要用到 C++ 20 的特性 `consteval`. 你需要完成对应如下的需求:

1. `%s` 表示一个字符串参数, 参数为 `std::string`, `const char *`, `char *` 或者 `std::string_view`.
2. `%d` 把参数转化为 `int64_t` 之后再输出, 参数必须为整数类型.
3. `%u` 把参数转化为 `uint64_t` 之后再输出, 参数必须为整数类型.
4. `%%` 表示转义, 实际只输出一个 "%".
5. `%_` 可以接受任何一个参数, 采用默认的 format 方法. 有符号整数默认是 `%d`; 无符号整数默认是 `%u`; `vector` 对于每个成员采用默认的 format 方法, 中间用 `,` 隔开, 两端用 `[]` 包裹, 比如 `[1,2,3]`; `string` 默认是 `%s`.

你只需要实现如下接口即可:

```cpp
template <typename... Args>
inline auto printf(format_string_t fmt, const Args &...args) -> void;
```

其中 `format_string_t` 你可以自行设计, 只要能满足在编译期完成错误检查即可. 下面给出一种参考的实现, 请注意这不是唯一实现, 你可以自行参考标准库 `std::format` 或 `std::print` 的实现. 在以下的模板中, 我们已经基本实现了编译期检查的功能, 请特化 `formatter` 模板类, 补全 `printf` 的定义, 实现类似的功能即可.

```cpp
#pragma once
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <exception>
#include <iostream>
#include <ostream>
#include <span>
#include <string_view>

namespace sjtu {

using sv_t = std::string_view;

struct format_error : std::exception {
public:
    format_error(const char *msg = "invalid format") : msg(msg) {}
    auto what() const noexcept -> const char * override {
        return msg;
    }

private:
    const char *msg;
};

template <typename Tp>
struct formatter;

struct format_info {
    inline static constexpr auto npos = static_cast<std::size_t>(-1);
    std::size_t position; // where is the specifier
    std::size_t consumed; // how many characters consumed
};

template <typename... Args>
struct format_string {
public:
    // must be constructed at compile time, to ensure the format string is valid
    consteval format_string(const char *fmt);
    constexpr auto get_format() const -> std::string_view {
        return fmt_str;
    }
    constexpr auto get_index() const -> std::span<const format_info> {
        return fmt_idx;
    }

private:
    inline static constexpr auto Nm = sizeof...(Args);
    std::string_view fmt_str;            // the format string
    std::array<format_info, Nm> fmt_idx; // where are the specifiers
};

consteval auto find_specifier(sv_t &fmt) -> bool {
    do {
        if (const auto next = fmt.find('%'); next == sv_t::npos) {
            return false;
        } else if (next + 1 == fmt.size()) {
            throw format_error{"missing specifier after '%'"};
        } else if (fmt[next + 1] == '%') {
            // escape the specifier
            fmt.remove_prefix(next + 2);
        } else {
            fmt.remove_prefix(next + 1);
            return true;
        }
    } while (true);
};

template <typename... Args>
consteval auto compile_time_format_check(sv_t fmt, std::span<format_info> idx) -> void {
    std::size_t n = 0;
    auto parse_fn = [&fmt, &n, idx](const auto &parser) {
        const auto last_pos = fmt.begin();
        if (!find_specifier(fmt)) {
            // no specifier found
            idx[n++] = {
                .position = format_info::npos,
                .consumed = 0,
            };
            return;
        }

        const auto position = static_cast<std::size_t>(fmt.begin() - last_pos - 1);
        const auto consumed = parser.parse(fmt);

        idx[n++] = {
            .position = position,
            .consumed = consumed,
        };

        if (consumed > 0) {
            fmt.remove_prefix(n);
        } else if (fmt.starts_with("_")) {
            fmt.remove_prefix(1);
        } else {
            throw format_error{"invalid specifier"};
        }
    };

    (parse_fn(formatter<Args>{}), ...);
    if (find_specifier(fmt)) // extra specifier found
        throw format_error{"too many specifiers"};
}

template <typename... Args>
consteval format_string<Args...>::format_string(const char *fmt) :
    fmt_str(fmt), fmt_idx() {
    compile_time_format_check<Args...>(fmt_str, fmt_idx);
}

template <typename StrLike>
    requires(
        std::same_as<StrLike, std::string> ||      //
        std::same_as<StrLike, std::string_view> || //
        std::same_as<StrLike, char *> ||           //
        std::same_as<StrLike, const char *>        //
    )
struct formatter<StrLike> {
    static constexpr auto parse(sv_t fmt) -> std::size_t {
        return fmt.starts_with("s") ? 1 : 0;
    }
    static auto format_to(std::ostream &os, const StrLike &val, sv_t fmt = "s") -> void {
        if (fmt.starts_with("s")) {
            os << static_cast<sv_t>(val);
        } else {
            throw format_error{};
        }
    }
};

template <typename... Args>
using format_string_t = format_string<std::decay_t<Args>...>;

template <typename... Args>
inline auto printf(format_string_t<Args...> fmt, const Args &...args) -> void;

} // namespace sjtu

```

### Input Format

See the problem description above.

### Output Format

See the problem description above.

### Samples

No sample data provided for this problem.

### Data Constraints

See the problem description for constraints.

## Per-Testcase Resource Limits

- **Time Limit (per test case)**: 1000 ms
- **Memory Limit (per test case)**: 512 MiB
- **Disk Usage**: No disk usage is permitted.

## Test Data

The test data for this problem is located at `/workspace/data/064/data_test/`.

## Submission Requirements

### OJ Git Repository Compilation Process

For Git compilation, we will first clone the repository using a command similar to:
```bash
git clone <repo_url> . --depth 1 --recurse-submodules --shallow-submodules --no-local
```

Then we check if there is a `CMakeLists.txt` file. If it exists, we run (if not, a warning message will be displayed):
```bash
cmake .
```

Finally, we check if there is any of `GNUmakefile`/`makefile`/`Makefile` (if cmake was run previously, this will be the generated Makefile). If it exists, we run (if not, a warning message will be displayed):
```bash
make
```

After this process is complete, we will use the `code` file in the project root directory as the compilation result.

A `CMakeLists.txt` file is provided in the project. You can use or modify it as needed. The local environment has gcc-13 and g++-13 available.

### Git Configuration Requirements

**IMPORTANT**: You must create a `.gitignore` file in your project root directory to avoid OJ evaluation conflicts.

The `.gitignore` file should include at least the following entries:

```gitignore
CMakeFiles/
CMakeCache.txt
```

### Submission Guidelines

- The submitted code must be able to compile successfully through the above compilation process
- The compiled executable file name must be `code`
- The program needs to be able to read data from standard input and write results to standard output
- Please ensure the code runs correctly within the given time and space limits
- **You must use C++ or C language** to implement this assignment

### Evaluation Notes

- The evaluation system will test your program using the provided test data
- The program output must exactly match the expected output (including format)
- Exceeding time or memory limits will be judged as the corresponding error type
- Please pay attention to the overall time performance of your code and the time complexity of each part of your algorithm.

### Academic Integrity

If any violations are found during evaluation or code review (including but not limited to using unconventional methods to pass test cases), your final score may be significantly reduced or become **0 points**.
