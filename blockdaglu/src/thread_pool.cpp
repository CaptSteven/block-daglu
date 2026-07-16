#include "thread_pool.h"

#include <algorithm>
#include <exception>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "blockdaglu/env.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {

static bool EnvBool(const char* name, bool default_value) {
  if (const char* v = blockdaglu_getenv_compat(name)) {
    if (std::string(v) == "1" || std::string(v) == "true" || std::string(v) == "TRUE") return true;
    if (std::string(v) == "0" || std::string(v) == "false" || std::string(v) == "FALSE") return false;
  }
  return default_value;
}

static std::string EnvStr(const char* name, const char* default_value) {
  if (const char* v = blockdaglu_getenv_compat(name)) return std::string(v);
  return std::string(default_value ? default_value : "");
}

#if defined(_WIN32)

struct AffinityConfig {
  bool enabled = false;
  bool log_once = false;
  enum class Mode { kOff, kPCores, kECores } mode = Mode::kOff;
};

struct CpuSetInfoLite {
  ULONG id = 0;
  UCHAR efficiency = 0;
  WORD group = 0;
  BYTE number = 0;
  bool valid = false;
};

struct AffinitySelection {
  bool available = false;
  bool applied_via_cpuset = false;
  bool applied_via_group_affinity = false;
  UCHAR p_class = 0;
  UCHAR e_class = 0;
  std::vector<ULONG> selected_cpuset_ids;
  GROUP_AFFINITY selected_group_affinity{};
  std::vector<PROCESSOR_NUMBER> ideal_processors;
  std::string reason;
};

struct PowerConfig {
  bool enabled = true;    // default ON (can be disabled with USERALGO_POWER=0)
  bool log_once = false;  // gated by USERALGO_PROFILE
};

struct PowerSelection {
  bool available = false;
  bool applied_process_priority = false;
  bool applied_process_throttle_off = false;
  DWORD process_priority_class = 0;
  std::string reason;
};

static AffinityConfig ReadAffinityConfig() {
  // Default: pcores (performance-first default for organizer evaluation).
  // USERALGO_AFFINITY=off|pcores|ecores
  const std::string mode = EnvStr("USERALGO_AFFINITY", "pcores");
  AffinityConfig cfg;
  cfg.log_once = EnvBool("USERALGO_PROFILE", false);
  if (mode == "pcores") {
    cfg.enabled = true;
    cfg.mode = AffinityConfig::Mode::kPCores;
  } else if (mode == "ecores") {
    cfg.enabled = true;
    cfg.mode = AffinityConfig::Mode::kECores;
  } else {
    cfg.enabled = false;
    cfg.mode = AffinityConfig::Mode::kOff;
  }
  return cfg;
}

static std::string FormatULongList(const std::vector<ULONG>& xs, std::size_t max_items = 16) {
  std::ostringstream oss;
  oss << "[";
  for (std::size_t i = 0; i < xs.size() && i < max_items; ++i) {
    if (i) oss << ",";
    oss << xs[i];
  }
  if (xs.size() > max_items) oss << ",...";
  oss << "]";
  return oss.str();
}

static PowerConfig ReadPowerConfig() {
  PowerConfig cfg;
  cfg.log_once = EnvBool("USERALGO_PROFILE", false);
  // Default ON unless explicitly disabled.
  cfg.enabled = EnvBool("USERALGO_POWER", true);
  return cfg;
}

static bool TryApplyProcessPowerPolicy(PowerSelection& sel) {
  sel.applied_process_priority = false;
  sel.applied_process_throttle_off = false;
  sel.process_priority_class = HIGH_PRIORITY_CLASS;

  const HANDLE proc = GetCurrentProcess();
  if (sel.process_priority_class != 0) {
    if (SetPriorityClass(proc, sel.process_priority_class)) {
      sel.applied_process_priority = true;
    }
  }

  // Disable power throttling / EcoQoS if supported.
  HMODULE k32 = GetModuleHandleW(L"Kernel32.dll");
  if (!k32) return false;

  using SetProcessInformation_t = BOOL(WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
  auto pSetProcessInformation =
      reinterpret_cast<SetProcessInformation_t>(GetProcAddress(k32, "SetProcessInformation"));
  if (!pSetProcessInformation) {
    return false;
  }

  PROCESS_POWER_THROTTLING_STATE st{};
  st.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  st.ControlMask = 0;
  st.StateMask = 0;

#ifdef PROCESS_POWER_THROTTLING_EXECUTION_SPEED
  st.ControlMask |= PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
#endif
#ifdef PROCESS_POWER_THROTTLING_ECO_QOS
  st.ControlMask |= PROCESS_POWER_THROTTLING_ECO_QOS;
#endif

  if (st.ControlMask == 0) {
    return false;
  }

  if (pSetProcessInformation(proc, ProcessPowerThrottling, &st, static_cast<DWORD>(sizeof(st)))) {
    sel.applied_process_throttle_off = true;
    return true;
  }
  return false;
}

static void ApplyThreadPowerPolicy() {
  const int thread_priority = THREAD_PRIORITY_HIGHEST;

  HANDLE th = GetCurrentThread();
  (void)SetThreadPriority(th, thread_priority);

  HMODULE k32 = GetModuleHandleW(L"Kernel32.dll");
  if (!k32) return;
  using SetThreadInformation_t = BOOL(WINAPI*)(HANDLE, THREAD_INFORMATION_CLASS, LPVOID, DWORD);
  auto pSetThreadInformation =
      reinterpret_cast<SetThreadInformation_t>(GetProcAddress(k32, "SetThreadInformation"));
  if (!pSetThreadInformation) return;

  THREAD_POWER_THROTTLING_STATE st{};
  st.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
  st.ControlMask = 0;
  st.StateMask = 0;

#ifdef THREAD_POWER_THROTTLING_EXECUTION_SPEED
  st.ControlMask |= THREAD_POWER_THROTTLING_EXECUTION_SPEED;
#endif
#ifdef THREAD_POWER_THROTTLING_ECO_QOS
  st.ControlMask |= THREAD_POWER_THROTTLING_ECO_QOS;
#endif

  if (st.ControlMask == 0) return;

  (void)pSetThreadInformation(th, ThreadPowerThrottling, &st, static_cast<DWORD>(sizeof(st)));
}

static bool TryQueryCpuSets(std::vector<CpuSetInfoLite>& out, std::string& why) {
  out.clear();

  // Dynamically bind to avoid hard failure on older Windows / SDK mismatch.
  HMODULE k32 = GetModuleHandleW(L"Kernel32.dll");
  if (!k32) {
    why = "kernel32_missing";
    return false;
  }

  using GetSystemCpuSetInformation_t =
      BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
  auto pGetSystemCpuSetInformation =
      reinterpret_cast<GetSystemCpuSetInformation_t>(GetProcAddress(k32, "GetSystemCpuSetInformation"));
  if (!pGetSystemCpuSetInformation) {
    why = "GetSystemCpuSetInformation_unavailable";
    return false;
  }

  ULONG bytes = 0;
  // First call: query required buffer size.
  (void)pGetSystemCpuSetInformation(nullptr, 0, &bytes, GetCurrentProcess(), 0);
  if (bytes == 0) {
    why = "GetSystemCpuSetInformation_bytes0";
    return false;
  }

  std::vector<unsigned char> buf(bytes);
  if (!pGetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()),
                                   bytes,
                                   &bytes,
                                   GetCurrentProcess(),
                                   0)) {
    why = "GetSystemCpuSetInformation_fail";
    return false;
  }

  const unsigned char* p = buf.data();
  const unsigned char* e = buf.data() + bytes;
  while (p < e) {
    const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(p);
    if (info->Type == CpuSetInformation) {
      CpuSetInfoLite x;
      x.id = info->CpuSet.Id;
      x.efficiency = info->CpuSet.EfficiencyClass;
      x.group = info->CpuSet.Group;
      x.number = static_cast<BYTE>(info->CpuSet.LogicalProcessorIndex);
      x.valid = true;
      out.push_back(x);
    }
    if (info->Size == 0) break;
    p += info->Size;
  }

  if (out.empty()) {
    why = "no_cpuset_entries";
    return false;
  }

  return true;
}

static bool TryQueryEfficiencyClassesViaCoreRelation(std::vector<GROUP_AFFINITY>& p_core_masks,
                                                     std::vector<GROUP_AFFINITY>& e_core_masks,
                                                     UCHAR& p_class,
                                                     UCHAR& e_class,
                                                     std::string& why) {
  p_core_masks.clear();
  e_core_masks.clear();

  DWORD bytes = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
  if (bytes == 0) {
    why = "GetLogicalProcessorInformationEx_bytes0";
    return false;
  }
  std::vector<unsigned char> buf(bytes);
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
                                        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()),
                                        &bytes)) {
    why = "GetLogicalProcessorInformationEx_fail";
    return false;
  }

  std::vector<UCHAR> classes;
  const unsigned char* p = buf.data();
  const unsigned char* e = buf.data() + bytes;
  while (p < e) {
    const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(p);
    if (info->Relationship == RelationProcessorCore) {
      classes.push_back(info->Processor.EfficiencyClass);
    }
    if (info->Size == 0) break;
    p += info->Size;
  }
  if (classes.empty()) {
    why = "no_core_entries";
    return false;
  }
  p_class = *std::min_element(classes.begin(), classes.end());
  e_class = *std::max_element(classes.begin(), classes.end());

  p = buf.data();
  while (p < e) {
    const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(p);
    if (info->Relationship == RelationProcessorCore) {
      const UCHAR ec = info->Processor.EfficiencyClass;
      const WORD group_count = info->Processor.GroupCount;
      for (WORD gi = 0; gi < group_count; ++gi) {
        const GROUP_AFFINITY ga = info->Processor.GroupMask[gi];
        if (ec == p_class) p_core_masks.push_back(ga);
        if (ec == e_class) e_core_masks.push_back(ga);
      }
    }
    if (info->Size == 0) break;
    p += info->Size;
  }

  return true;
}

static AffinitySelection BuildAffinitySelection(const AffinityConfig& cfg) {
  AffinitySelection sel;
  sel.available = true;

  if (!cfg.enabled || cfg.mode == AffinityConfig::Mode::kOff) {
    sel.reason = "disabled";
    sel.available = false;
    return sel;
  }

  // Prefer CPU Sets API if available.
  std::vector<CpuSetInfoLite> sets;
  std::string why;
  if (TryQueryCpuSets(sets, why)) {
    UCHAR min_ec = 255;
    UCHAR max_ec = 0;
    for (const auto& s : sets) {
      min_ec = std::min(min_ec, s.efficiency);
      max_ec = std::max(max_ec, s.efficiency);
    }
    sel.p_class = min_ec;
    sel.e_class = max_ec;

    const UCHAR want = (cfg.mode == AffinityConfig::Mode::kPCores) ? min_ec : max_ec;
    for (const auto& s : sets) {
      if (s.valid && s.efficiency == want) {
        sel.selected_cpuset_ids.push_back(s.id);
        PROCESSOR_NUMBER pn{};
        pn.Group = s.group;
        pn.Number = s.number;
        pn.Reserved = 0;
        sel.ideal_processors.push_back(pn);
      }
    }
    if (sel.selected_cpuset_ids.empty()) {
      sel.reason = "cpuset_empty_after_filter";
      sel.available = false;
    } else {
      sel.reason = "cpuset_ok";
      sel.applied_via_cpuset = true;
    }
    return sel;
  }

  // Fallback: build a group affinity mask from core efficiency class.
  std::vector<GROUP_AFFINITY> p_masks;
  std::vector<GROUP_AFFINITY> e_masks;
  UCHAR p_class = 0;
  UCHAR e_class = 0;
  if (!TryQueryEfficiencyClassesViaCoreRelation(p_masks, e_masks, p_class, e_class, why)) {
    sel.reason = "fallback_fail:" + why;
    sel.available = false;
    return sel;
  }
  sel.p_class = p_class;
  sel.e_class = e_class;

  const bool want_p = (cfg.mode == AffinityConfig::Mode::kPCores);
  const auto& masks = want_p ? p_masks : e_masks;
  if (masks.empty()) {
    sel.reason = "fallback_empty_masks";
    sel.available = false;
    return sel;
  }

  // Combine within a single group. On typical desktops there is just one group.
  GROUP_AFFINITY ga{};
  ga.Group = masks[0].Group;
  ga.Mask = 0;
  for (const auto& m : masks) {
    if (m.Group != ga.Group) {
      // Multi-group: keep it conservative and do not pin.
      sel.reason = "fallback_multigroup";
      sel.available = false;
      return sel;
    }
    ga.Mask |= m.Mask;
  }
  if (ga.Mask == 0) {
    sel.reason = "fallback_mask0";
    sel.available = false;
    return sel;
  }
  sel.selected_group_affinity = ga;
  sel.applied_via_group_affinity = true;
  sel.reason = "group_affinity_ok";
  return sel;
}

static void ApplyAffinityForWorkerOncePerThread(const AffinitySelection& sel, std::size_t worker_index) {
  if (!sel.available) return;

  HANDLE th = GetCurrentThread();

  if (sel.applied_via_cpuset && !sel.selected_cpuset_ids.empty()) {
    HMODULE k32 = GetModuleHandleW(L"Kernel32.dll");
    if (k32) {
      using SetThreadSelectedCpuSets_t = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
      auto pSetThreadSelectedCpuSets =
          reinterpret_cast<SetThreadSelectedCpuSets_t>(GetProcAddress(k32, "SetThreadSelectedCpuSets"));
      if (pSetThreadSelectedCpuSets) {
        (void)pSetThreadSelectedCpuSets(th,
                                        sel.selected_cpuset_ids.data(),
                                        static_cast<ULONG>(sel.selected_cpuset_ids.size()));
      }
    }

    if (!sel.ideal_processors.empty()) {
      PROCESSOR_NUMBER pn = sel.ideal_processors[worker_index % sel.ideal_processors.size()];
      (void)SetThreadIdealProcessorEx(th, &pn, nullptr);
    }
    return;
  }

  if (sel.applied_via_group_affinity) {
    (void)SetThreadGroupAffinity(th, &sel.selected_group_affinity, nullptr);
  }
}

static const AffinitySelection& GetGlobalAffinitySelectionOnce() {
  static std::once_flag once;
  static AffinitySelection sel;
  static AffinityConfig cfg;
  std::call_once(once, [&]() {
    cfg = ReadAffinityConfig();
    sel = BuildAffinitySelection(cfg);

    if (cfg.log_once) {
      const char* mode_str = "off";
      if (cfg.mode == AffinityConfig::Mode::kPCores) mode_str = "pcores";
      if (cfg.mode == AffinityConfig::Mode::kECores) mode_str = "ecores";

      std::ostringstream oss;
      oss << "USERALGO_PROFILE,affinity_select"
          << ",mode=" << mode_str
          << ",enabled=" << (cfg.enabled ? 1 : 0)
          << ",p_class=" << static_cast<int>(sel.p_class)
          << ",e_class=" << static_cast<int>(sel.e_class)
          << ",api=" << (sel.applied_via_cpuset ? "cpuset" : (sel.applied_via_group_affinity ? "group_affinity" : "none"))
          << ",selected=" << (sel.applied_via_cpuset ? sel.selected_cpuset_ids.size() : 0)
          << ",ids=" << FormatULongList(sel.selected_cpuset_ids)
          << ",reason=" << sel.reason
          << "\n";
      std::cerr << oss.str();
    }
  });
  return sel;
}

static const PowerSelection& GetGlobalPowerSelectionOnce() {
  static std::once_flag once;
  static PowerSelection sel;
  static PowerConfig cfg;
  std::call_once(once, [&]() {
    cfg = ReadPowerConfig();
    if (!cfg.enabled) {
      sel.available = false;
      sel.reason = "disabled";
      return;
    }

    sel.available = true;
    (void)TryApplyProcessPowerPolicy(sel);

    if (cfg.log_once) {
      std::ostringstream oss;
      oss << "USERALGO_PROFILE,power_policy"
          << ",enabled=" << (cfg.enabled ? 1 : 0)
          << ",proc_prio=" << static_cast<unsigned long>(sel.process_priority_class)
          << ",proc_prio_ok=" << (sel.applied_process_priority ? 1 : 0)
          << ",proc_throttle_off_ok=" << (sel.applied_process_throttle_off ? 1 : 0)
          << ",reason=" << sel.reason
          << "\n";
      std::cerr << oss.str();
    }
  });
  return sel;
}

#endif  // _WIN32

}  // namespace

ThreadPool::ThreadPool(std::size_t thread_count) : workers_(thread_count) {
  if (thread_count == 0) {
    thread_count = 1;
  }

  workers_.clear();
  workers_.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++i) {
    workers_.emplace_back(std::make_unique<Worker>());
  }

  threads_.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++i) {
    threads_.emplace_back([this, i]() {
#if defined(_WIN32)
      const auto& sel = GetGlobalAffinitySelectionOnce();
      ApplyAffinityForWorkerOncePerThread(sel, i);
      const auto& psel = GetGlobalPowerSelectionOnce();
      if (psel.available) {
        ApplyThreadPowerPolicy();
      }
#endif
      worker_loop(i);
    });
  }
}

ThreadPool::~ThreadPool() {
  stop_.store(true, std::memory_order_release);
  cv_.notify_all();
  for (auto& t : threads_) {
    if (t.joinable()) {
      t.join();
    }
  }
}

void ThreadPool::submit(std::function<void()> task) {
  in_flight_.fetch_add(1, std::memory_order_acq_rel);

  const std::size_t idx = rr_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
  {
    std::lock_guard<std::mutex> lk(workers_[idx]->mtx);
    workers_[idx]->deque.emplace_back(std::move(task));
  }
  cv_.notify_one();
}

void ThreadPool::wait_idle() {
  std::unique_lock<std::mutex> lk(cv_mtx_);
  cv_.wait(lk, [&]() { return in_flight_.load(std::memory_order_acquire) == 0; });
}

void ThreadPool::parallel_for(std::size_t begin,
                              std::size_t end,
                              const std::function<void(std::size_t)>& fn,
                              std::size_t chunk) {
  if (end <= begin) return;
  if (chunk == 0) chunk = 1;

  const std::size_t n = end - begin;
  if (n == 1 || workers_.size() <= 1) {
    for (std::size_t i = begin; i < end; ++i) fn(i);
    return;
  }

  std::atomic<std::size_t> next{begin};
  std::atomic<bool> failed{false};
  std::mutex error_mtx;
  std::exception_ptr first_error;
  const std::size_t workers = workers_.size();
  for (std::size_t t = 0; t < workers; ++t) {
    submit([&]() {
      try {
        while (!failed.load(std::memory_order_acquire)) {
          const std::size_t i0 = next.fetch_add(chunk, std::memory_order_relaxed);
          if (i0 >= end) break;
          const std::size_t i1 = std::min(end, i0 + chunk);
          for (std::size_t i = i0; i < i1; ++i) {
            fn(i);
          }
        }
      } catch (...) {
        failed.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(error_mtx);
        if (!first_error) {
          first_error = std::current_exception();
        }
      }
    });
  }
  wait_idle();
  if (first_error) std::rethrow_exception(first_error);
}

bool ThreadPool::try_pop_local(std::size_t index, std::function<void()>& out) {
  auto& w = *workers_[index];
  std::lock_guard<std::mutex> lk(w.mtx);
  if (w.deque.empty()) {
    return false;
  }
  out = std::move(w.deque.back());
  w.deque.pop_back();
  return true;
}

bool ThreadPool::try_steal(std::size_t thief, std::function<void()>& out) {
  const std::size_t n = workers_.size();
  for (std::size_t k = 1; k < n; ++k) {
    const std::size_t victim = (thief + k) % n;
    auto& w = *workers_[victim];
    std::lock_guard<std::mutex> lk(w.mtx);
    if (w.deque.empty()) {
      continue;
    }
    out = std::move(w.deque.front());
    w.deque.pop_front();
    return true;
  }
  return false;
}

void ThreadPool::worker_loop(std::size_t index) {
  while (!stop_.load(std::memory_order_acquire)) {
    std::function<void()> task;
    if (try_pop_local(index, task) || try_steal(index, task)) {
      task();
      const std::size_t left = in_flight_.fetch_sub(1, std::memory_order_acq_rel) - 1;
      if (left == 0) {
        cv_.notify_all();
      }
      continue;
    }

    std::unique_lock<std::mutex> lk(cv_mtx_);
    cv_.wait_for(lk, std::chrono::milliseconds(2), [&]() {
      return stop_.load(std::memory_order_acquire) ||
             in_flight_.load(std::memory_order_acquire) != 0;
    });
  }
}
