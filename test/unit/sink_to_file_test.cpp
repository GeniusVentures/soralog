/**
 * Copyright Soramitsu Co., 2021-2023
 * Copyright Quadrivium Co., 2023
 * All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <fstream>

#include "soralog/impl/sink_to_file.hpp"

using namespace soralog;
using namespace testing;
using namespace std::chrono_literals;

class SinkToFileTest : public ::testing::Test {
 public:
  struct FakeLogger {
    explicit FakeLogger(std::shared_ptr<SinkToFile> sink)
        : sink_(std::move(sink)) {}

    template <typename... Args>
    void debug(std::string_view format, const Args &...args) {
      sink_->push("logger", Level::DEBUG, format, args...);
    }

    void flush() {
      sink_->flush();
    }

   private:
    std::shared_ptr<SinkToFile> sink_;
  };

  void SetUp() override {
    std::string path(
        (std::filesystem::temp_directory_path() / "soralog_test_XXXXXX")
            .c_str());
    if (mkstemp(path.data()) == -1) {
      FAIL() << "Can't create output file for test";
    }
    path_ = std::filesystem::path(path);
  }
  void TearDown() override {
    std::remove(path_.native().data());
  }

  std::shared_ptr<FakeLogger> createLogger(std::chrono::milliseconds latency,
                                           size_t capacity = 4) {
    auto sink = std::make_shared<SinkToFile>(
        "file",
        Level::TRACE,
        path_,
        Sink::ThreadInfoType::NONE,  // ignore thread info
        capacity,                    // capacity: events
        64,                          // max message length: 64 byte
        16384,                       // buffers size: 16 Kb
        latency.count());
    return std::make_shared<FakeLogger>(std::move(sink));
  }

  size_t countLines() const {
    std::ifstream in(path_);
    size_t lines = 0;
    for (std::string line; std::getline(in, line);) {
      if (not line.empty()) {
        ++lines;
      }
    }
    return lines;
  }

 private:
  std::filesystem::path path_;
};

TEST_F(SinkToFileTest, Logging) {
  auto logger = createLogger(20ms);
  auto delay = 1ms;
  int count = 100;
  for (int round = 1; round <= 3; ++round) {
    for (int i = 1; i <= count; ++i) {
      logger->debug(
          "round: {}, message: {}, delay: {}ms", round, i, abs(i - count / 2));
      std::this_thread::sleep_for(delay * abs(i - count / 2));
    }
  }
  logger->flush();
}

// A queued event must not cost a whole latency_ to reach the file. flush() used
// to write one event and return, so N queued events took N * latency_ to drain
// and teardown blocked in the worker join() for just as long.
TEST_F(SinkToFileTest, DrainsWholeQueueAtOnce) {
  constexpr size_t kCount = 10;
  constexpr auto kLatency = 1000ms;

  auto started = std::chrono::steady_clock::now();
  {
    // Capacity above kCount, so no event is drained by push() hitting a full
    // queue -- the whole batch has to be waiting when the sink is destroyed.
    auto logger = createLogger(kLatency, kCount * 2);
    for (size_t i = 1; i <= kCount; ++i) {
      logger->debug("message: {}", i);
    }
  }  // sink destroyed here; it must drain all kCount events in one pass
  auto elapsed = std::chrono::steady_clock::now() - started;

  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
  EXPECT_LT(elapsed_ms, kCount * kLatency / 2)
      << "took " << elapsed_ms.count() << "ms; one event per latency_ would be "
      << (kCount * kLatency).count() << "ms";
  EXPECT_EQ(countLines(), kCount) << "events were dropped while draining";
}
