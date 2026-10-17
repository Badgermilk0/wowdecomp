#pragma once

// What awesome_wotlk's MSDF files take from its own tool headers (hookkit/accessor.h, Utils.h, Constants.h,
// Math/Primitives.h, Client/Client.h), so those files port with few changes.

#include <windows.h>

#include <atomic>
#include <cmath>
#include <string>
#include <type_traits>

namespace utils {
template <typename T>
struct DefaultConstruct {
    T operator()() const { return T{}; }
};

// a lazily built, never destroyed instance per Tag: free of static initialisation and destruction order, and safe to
// reach from the background threads during shutdown
template <typename T, typename Tag, auto Factory = DefaultConstruct<T>{}>
class Accessor {
public:
    static T& get() {
        if (T* ptr = instance_.load(std::memory_order_acquire)) {
            return *ptr;
        }

        return getSlow();
    }

    T* operator->() const { return &get(); }

    T& operator*() const { return get(); }

    explicit operator T&() const { return get(); }

    template <typename Key>
    decltype(auto) operator[](Key&& idx) const {
        return get()[std::forward<Key>(idx)];
    }

private:
    __declspec(noinline) static T& getSlow() {
        static T& instance = *new T(Factory());
        instance_.store(&instance, std::memory_order_release);
        return instance;
    }

    inline static constinit std::atomic<T*> instance_{nullptr};
};
}  // namespace utils

// placeholder memory mapping, Windows 10 1803 and later
struct Win10Api {
    decltype(&::VirtualAlloc2) virtual_alloc2 = nullptr;
    decltype(&::MapViewOfFile3) map_view_of_file3 = nullptr;
    decltype(&::UnmapViewOfFile2) unmap_view_of_file2 = nullptr;
    decltype(&::PrefetchVirtualMemory) prefetch_virtual_memory = nullptr;
};

inline const Win10Api& win10Api() noexcept {
    static const Win10Api api = []() noexcept {
        HMODULE kernel = GetModuleHandleW(L"kernelbase.dll");

        if (!kernel) {
            return Win10Api{};
        }

        return Win10Api{
            .virtual_alloc2 = reinterpret_cast<decltype(&::VirtualAlloc2)>(GetProcAddress(kernel, "VirtualAlloc2")),
            .map_view_of_file3 = reinterpret_cast<decltype(&::MapViewOfFile3)>(GetProcAddress(kernel, "MapViewOfFile3")),
            .unmap_view_of_file2 = reinterpret_cast<decltype(&::UnmapViewOfFile2)>(GetProcAddress(kernel, "UnmapViewOfFile2")),
            .prefetch_virtual_memory = reinterpret_cast<decltype(&::PrefetchVirtualMemory)>(GetProcAddress(kernel, "PrefetchVirtualMemory")),
        };
    }();
    return api;
}

inline bool isWin10() noexcept {
    const Win10Api& api = win10Api();
    return api.virtual_alloc2 && api.map_view_of_file3 && api.unmap_view_of_file2;
}

inline constexpr double kEps9d = 1e-9;

struct Vec2d {
    double x = 0.0;
    double y = 0.0;

    Vec2d operator+(const Vec2d& o) const { return { x + o.x, y + o.y }; }
    Vec2d operator-(const Vec2d& o) const { return { x - o.x, y - o.y }; }
    Vec2d operator*(double s) const { return { x * s, y * s }; }
    double dot(const Vec2d& o) const { return x * o.x + y * o.y; }
    double lengthSq() const { return x * x + y * y; }
    double length() const { return std::sqrt(lengthSq()); }
    double distance(const Vec2d& o) const { return (*this - o).length(); }
};

namespace client {
// the game locale ("enUS", ...), read once on the main thread by MsdfSetGameLocale; the cache paths use it from
// background threads too
const std::string& getGameLocale();
}  // namespace client
