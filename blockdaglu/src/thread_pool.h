#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Minimal work-stealing thread pool (C++17).
// - Per-worker deques (LIFO for local, FIFO for steals).
// - Waits via condition_variable.
class ThreadPool {
public:
  explicit ThreadPool(std::size_t thread_count);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  void submit(std::function<void()> task);
  void wait_idle();

  // Runs fn(i) for i in [begin, end) using chunked work-sharing.
  // This submits only O(thread_count) tasks (not one task per iteration).
  void parallel_for(std::size_t begin,
                    std::size_t end,
                    const std::function<void(std::size_t)>& fn,
                    std::size_t chunk = 1);

  std::size_t thread_count() const { return workers_.size(); }

private:
  struct Worker {
    std::deque<std::function<void()>> deque;
    std::mutex mtx;
  };

  void worker_loop(std::size_t index);
  bool try_pop_local(std::size_t index, std::function<void()>& out);
  bool try_steal(std::size_t thief, std::function<void()>& out);

  std::vector<std::thread> threads_;
  std::vector<std::unique_ptr<Worker>> workers_;

  std::mutex cv_mtx_;
  std::condition_variable cv_;

  std::atomic<bool> stop_{false};
  std::atomic<std::size_t> in_flight_{0};
  std::atomic<std::size_t> rr_{0};
};
