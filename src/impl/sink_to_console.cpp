/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sink_to_console.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>

#include <fmt/chrono.h>
#include <fmt/color.h>

namespace soralog {

  namespace {

    using namespace std::chrono_literals;

    constexpr std::string_view separator = "  ";

    using cl = fmt::color;

    const auto mfc = [](cl cl) {
      return fmt::internal::make_foreground_color<char>(cl);
    };

    constexpr std::array<cl, static_cast<size_t>(Level::TRACE) + 1>
        level_to_color_map{
            cl::brown,         // OFF
            cl::red,           // CRITICAL
            cl::orange_red,    // ERROR
            cl::orange,        // WARNING
            cl::forest_green,  // INFO
            cl::dark_green,    // VERBOSE
            cl::medium_blue,   // DEBUG
            cl::gray,          // TRACE
        };

    template <typename... Args>
    inline void pass(Args &&... styles) {}

    enum V {};
    template <typename T>
    V put_style(char *&ptr, T style) {
      auto size = std::end(style) - std::begin(style);
      std::memcpy(ptr, std::begin(style), size);
      ptr += size;  // NOLINT
      return {};
    }

    template <typename... Args>
    void put_style(char *&ptr, Args &&... styles) {
      pass(put_style(ptr, std::forward<Args>(styles))...);
    };

    void put_reset_style(char *&ptr) {
      const auto &style = fmt::internal::data::reset_color;
      auto size = std::end(style) - std::begin(style) - 1;
      std::memcpy(ptr, std::begin(style), size);
      ptr = ptr + size;  // NOLINT
    }

    void put_thread_style(char *&ptr, bool dark = false) {
      ;  // No style
    }

    void put_level_style(char *&ptr, Level level) {
      assert(level <= Level::TRACE);
      auto color = level_to_color_map[static_cast<size_t>(level)];
      put_style(ptr, fmt::internal::make_foreground_color<char>(color),
                fmt::internal::make_emphasis<char>(fmt::emphasis::bold));
    }

    void put_name_style(char *&ptr) {
      put_style(ptr, fmt::internal::make_emphasis<char>(fmt::emphasis::bold));
    }

    void put_text_style(char *&ptr, Level level) {
      assert(level <= Level::TRACE);
      if (level <= Level::ERROR) {
        put_style(ptr, fmt::internal::make_emphasis<char>(fmt::emphasis::bold));
      } else if (level >= Level::DEBUG) {
        put_style(ptr,
                  fmt::internal::make_emphasis<char>(fmt::emphasis::italic));
      }
    }

    void put_separator(char *&ptr) {
      for (auto c : separator) {
        *ptr++ = c;  // NOLINT
      }
    }

    void put_level(char *&ptr, Level level) {
      const char *const end = ptr + 8;  // NOLINT
      const char *str = levelToStr(level);
      do {
        *ptr++ = *str++;  // NOLINT
      } while (*str != '\0');
      while (ptr < end) {
        *ptr++ = ' ';  // NOLINT
      }
    }

    void put_level_short(char *&ptr, Level level) {
      *ptr++ = levelToChar(level);  // NOLINT
    }

    template <typename T>
    void put_string(char *&ptr, const T &name) {
      for (auto c : name) {
        *ptr++ = c;  // NOLINT
      }
    }

    template <typename T>
    void put_string(char *&ptr, const T &name, size_t width) {
      if (width == 0)
        return;
      for (auto c : name) {
        if (c == '\0' or width == 0)
          break;
        *ptr++ = c;  // NOLINT
        --width;
      }
      while (width--) *ptr++ = ' ';  // NOLINT
    }

  }  // namespace

  SinkToConsole::SinkToConsole(std::string name, bool with_color,
                               ThreadFlag thread_flag, size_t events_capacity,
                               size_t buffer_size, size_t latency_ms)
      : Sink(std::move(name), thread_flag, events_capacity, buffer_size),
        with_color_(with_color),
        buffer_size_(buffer_size),
        latency_(latency_ms),
        buff_(buffer_size_) {
    sink_worker_ = std::make_unique<std::thread>([this] { run(); });
  }

  SinkToConsole::~SinkToConsole() {
    need_to_finalize_ = true;
    flush();
    sink_worker_->join();
    sink_worker_.reset();
  }

  void SinkToConsole::flush() noexcept {
    need_to_flush_ = true;
    condvar_.notify_one();
  }

  void SinkToConsole::run() {
    util::setThreadName("log:" + name_);

    auto next_flush = std::chrono::steady_clock::now();

    while (true) {
      if (events_.size() == 0) {
        if (need_to_finalize_) {
          return;
        }
      }

      std::unique_lock lock(mutex_);
      if (not condvar_.wait_for(lock, std::chrono::milliseconds(100),
                                [this] { return events_.size() > 0; })) {
        continue;
      }

      auto *const begin = buff_.data();
      auto *const end = buff_.data() + buff_.size();  // NOLINT
      auto *ptr = begin;

      decltype(1s / 1s) psec = 0;
      std::tm tm{};
      std::array<char, 17> datetime{};  // "00.00.00 00:00:00"

      while (true) {
        auto node = events_.get();
        if (node) {
          const auto &event = *node;

          const auto time = event.timestamp().time_since_epoch();
          const auto sec = time / 1s;
          const auto usec = time % 1s / 1us;

          if (psec != sec) {
            tm = fmt::localtime(sec);
            fmt::format_to_n(datetime.data(), datetime.size(),
                             "{:0>2}.{:0>2}.{:0>2} {:0>2}:{:0>2}:{:0>2}",
                             tm.tm_year % 100, tm.tm_mon + 1, tm.tm_mday,
                             tm.tm_hour, tm.tm_min, tm.tm_sec);
            psec = sec;
          }

          // Timestamp

          std::memcpy(ptr, datetime.data(), datetime.size());
          ptr = ptr + datetime.size();  // NOLINT

          if (with_color_) {
            const auto &style =
                fmt::internal::make_foreground_color<char>(fmt::color::gray);

            auto size = std::end(style) - std::begin(style);
            std::memcpy(ptr, std::begin(style),
                        std::end(style) - std::begin(style));
            ptr = ptr + size;  // NOLINT
          }

          ptr = fmt::format_to_n(ptr, end - ptr, ".{:0>6}", usec).out;

          if (with_color_) {
            put_reset_style(ptr);
          }

          put_separator(ptr);

          // Thread

          switch (thread_flag_) {
            case ThreadFlag::NAME:
              put_string(ptr, event.thread_name(), 15);
              put_separator(ptr);
              break;

            case ThreadFlag::ID:
              ptr = fmt::format_to_n(ptr, end - ptr, "T:{:<6}",
                                     event.thread_number())
                        .out;
              put_separator(ptr);
              break;

            default:
              break;
          }

          // Level

          if (with_color_) {
            put_level_style(ptr, event.level());
          }
          put_level(ptr, event.level());
          if (with_color_) {
            put_reset_style(ptr);
          }

          put_separator(ptr);

          // Name

          if (with_color_) {
            put_name_style(ptr);
          }
          put_string(ptr, event.name());
          if (with_color_) {
            put_reset_style(ptr);
          }

          put_separator(ptr);

          // Message

          if (with_color_) {
            put_text_style(ptr, event.level());
          }
          put_string(ptr, event.message());
          if (with_color_) {
            put_reset_style(ptr);
          }

          *ptr++ = '\n';  // NOLINT

          size_ -= event.message().size();
        }

        if ((end - ptr) < sizeof(Event) or not node
            or std::chrono::steady_clock::now() >= next_flush) {
          next_flush = std::chrono::steady_clock::now() + latency_;
          std::cout.write(begin, ptr - begin);
          ptr = begin;
        }

        if (not node) {
          bool true_v = true;
          if (need_to_flush_.compare_exchange_weak(true_v, false)) {
            std::cout.flush();
          }
          if (need_to_finalize_) {
            return;
          }
          break;
        }
      }
    }
  }
}  // namespace soralog
