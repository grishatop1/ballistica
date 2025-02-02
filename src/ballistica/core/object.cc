// Released under the MIT License. See LICENSE for details.

#include "ballistica/core/object.h"

#include <mutex>
#include <unordered_map>

#include "ballistica/app/app.h"
#include "ballistica/generic/utils.h"
#include "ballistica/platform/platform.h"

namespace ballistica {

void Object::LsObjects() {
#if BA_DEBUG_BUILD
  std::string s;
  {
    std::scoped_lock lock(g_app->object_list_mutex);
    s = std::to_string(g_app->object_count) + " Objects at time "
        + std::to_string(GetRealTime()) + ";";

    if (explicit_bool(true)) {
      std::unordered_map<std::string, int> obj_map;

      // Tally up counts for all types.
      int count = 0;
      for (Object* o = g_app->object_list_first; o != nullptr;
           o = o->object_next_) {
        count++;
        std::string obj_name = o->GetObjectTypeName();
        auto i = obj_map.find(obj_name);
        if (i == obj_map.end()) {
          obj_map[obj_name] = 1;
        } else {
          // Getting complaints that 'second' is unused, but we sort and print
          // using this value like 10 lines down. Hmmm.
#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedValue"
          i->second++;
#pragma clang diagnostic pop
        }
      }

      // Now sort them by count and print.
      std::vector<std::pair<int, std::string> > sorted;
      sorted.reserve(obj_map.size());
      for (auto&& i : obj_map) {
        sorted.emplace_back(i.second, i.first);
      }
      std::sort(sorted.rbegin(), sorted.rend());
      for (auto&& i : sorted) {
        s += "\n   " + std::to_string(i.first) + ": " + i.second;
      }
      assert(count == g_app->object_count);
    }
  }
  Log(LogLevel::kInfo, s);
#else
  Log(LogLevel::kInfo, "LsObjects() only functions in debug builds.");
#endif  // BA_DEBUG_BUILD
}

Object::Object() {
#if BA_DEBUG_BUILD
  // Mark when we were born.
  object_birth_time_ = GetRealTime();

  // Add ourself to the global object list.
  std::scoped_lock lock(g_app->object_list_mutex);
  object_prev_ = nullptr;
  object_next_ = g_app->object_list_first;
  g_app->object_list_first = this;
  if (object_next_) {
    object_next_->object_prev_ = this;
  }
  g_app->object_count++;
#endif  // BA_DEBUG_BUILD
}

Object::~Object() {
#if BA_DEBUG_BUILD
  // Pull ourself from the global obj list.
  std::scoped_lock lock(g_app->object_list_mutex);
  if (object_next_) {
    object_next_->object_prev_ = object_prev_;
  }
  if (object_prev_) {
    object_prev_->object_next_ = object_next_;
  } else {
    g_app->object_list_first = object_next_;
  }
  g_app->object_count--;

  // More sanity checks.
  if (object_strong_ref_count_ != 0) {
    // Avoiding Log for these low level errors; can lead to deadlock.
    printf(
        "Warning: Object is dying with non-zero ref-count; this is bad. "
        "(this might mean the object raised an exception in its constructor"
        " after being strong-referenced first).\n");
  }

#endif  // BA_DEBUG_BUILD

  // Invalidate all our weak refs.
  // We could call Release() on each but we'd have to deactivate the
  // thread-check since virtual functions won't work as expected in a
  // destructor. Also we can take a few shortcuts here since we know
  // we're deleting the entire list, not just one object.
  while (object_weak_refs_) {
    auto tmp{object_weak_refs_};
    object_weak_refs_ = tmp->next_;
    tmp->prev_ = nullptr;
    tmp->next_ = nullptr;
    tmp->obj_ = nullptr;
  }
}

auto Object::GetObjectTypeName() const -> std::string {
  // Default implementation just returns type name.
  return g_platform->DemangleCXXSymbol(typeid(*this).name());
}

auto Object::GetObjectDescription() const -> std::string {
  return "<" + GetObjectTypeName() + " object at " + Utils::PtrToString(this)
         + ">";
}

auto Object::GetDefaultOwnerThread() const -> ThreadTag {
  return ThreadTag::kLogic;
}

auto Object::GetThreadOwnership() const -> Object::ThreadOwnership {
#if BA_DEBUG_BUILD
  return thread_ownership_;
#else
  FatalError("Should not be called in release builds.");
  return ThreadOwnership::kClassDefault;
#endif
}

#if BA_DEBUG_BUILD

static auto GetCurrentThreadTag() -> ThreadTag {
  if (InMainThread()) {
    return ThreadTag::kMain;
  } else if (InLogicThread()) {
    return ThreadTag::kLogic;
  } else if (InAudioThread()) {
    return ThreadTag::kAudio;
  } else if (InNetworkWriteThread()) {
    return ThreadTag::kNetworkWrite;
  } else if (InAssetsThread()) {
    return ThreadTag::kAssets;
  } else if (InBGDynamicsThread()) {
    return ThreadTag::kBGDynamics;
  } else {
    throw Exception(std::string("unrecognized thread: ")
                    + GetCurrentThreadName());
  }
}

auto Object::ObjectUpdateForAcquire() -> void {
  ThreadOwnership thread_ownership = GetThreadOwnership();

  // If we're set to use the next-referencing thread and haven't set one
  // yet, do so.
  if (thread_ownership == ThreadOwnership::kNextReferencing
      && owner_thread_ == ThreadTag::kInvalid) {
    owner_thread_ = GetCurrentThreadTag();
  }
}

auto Object::ObjectThreadCheck() -> void {
  if (!thread_checks_enabled_) {
    return;
  }

  ThreadOwnership thread_ownership = GetThreadOwnership();

  ThreadTag t;
  if (thread_ownership == ThreadOwnership::kClassDefault) {
    t = GetDefaultOwnerThread();
  } else {
    t = owner_thread_;
  }
#define DO_FAIL(THREADNAME)                                                \
  throw Exception("ObjectThreadCheck failed for " + GetObjectDescription() \
                  + "; expected " THREADNAME " thread; got "               \
                  + GetCurrentThreadName())
  switch (t) {
    case ThreadTag::kMain:
      if (!InMainThread()) {
        DO_FAIL("Main");
      }
      break;
    case ThreadTag::kLogic:
      if (!InLogicThread()) {
        DO_FAIL("Logic");
      }
      break;
    case ThreadTag::kAudio:
      if (!InAudioThread()) {
        DO_FAIL("Audio");
      }
      break;
    case ThreadTag::kNetworkWrite:
      if (!InNetworkWriteThread()) {
        DO_FAIL("NetworkWrite");
      }
      break;
    case ThreadTag::kAssets:
      if (!InAssetsThread()) {
        DO_FAIL("Assets");
      }
      break;
    case ThreadTag::kBGDynamics:
      if (!InBGDynamicsThread()) {
        DO_FAIL("BGDynamics");
      }
      break;
    default:
      throw Exception();
  }
#undef DO_FAIL
}
#endif  // BA_DEBUG_BUILD

}  // namespace ballistica
