#pragma once

#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace proc {

inline DWORD FindProcess(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

inline uintptr_t ModuleBase(DWORD pid, const wchar_t* moduleName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);

    uintptr_t base = 0;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName) == 0) {
                base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return base;
}

class Process {
public:
    Process() = default;
    ~Process() { Close(); }

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    bool Open(DWORD pid) {
        Close();
        handle_ = ::OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        pid_ = handle_ ? pid : 0;
        return handle_ != nullptr;
    }

    void Close() {
        if (handle_) {
            ::CloseHandle(handle_);
            handle_ = nullptr;
        }
        pid_ = 0;
    }

    bool IsOpen() const { return handle_ != nullptr; }
    DWORD Pid() const { return pid_; }

    bool Alive() const {
        if (!handle_) return false;
        DWORD code = 0;
        if (!::GetExitCodeProcess(handle_, &code)) return false;
        return code == STILL_ACTIVE;
    }

    bool ReadRaw(uintptr_t address, void* buffer, size_t size) const {
        if (!handle_ || address < 0x10000) return false;
        SIZE_T got = 0;
        if (!::ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(address), buffer, size, &got))
            return false;
        return got == size;
    }

    template <typename T>
    bool Read(uintptr_t address, T& out) const {
        return ReadRaw(address, &out, sizeof(T));
    }

    template <typename T>
    T ReadOr(uintptr_t address, T fallback = T{}) const {
        T value{};
        return Read<T>(address, value) ? value : fallback;
    }

    bool ImagePath(std::wstring& out) const {
        if (!handle_) return false;
        wchar_t buffer[MAX_PATH * 4]{};
        DWORD size = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
        if (!::QueryFullProcessImageNameW(handle_, 0, buffer, &size)) return false;
        out.assign(buffer, size);
        return true;
    }

private:
    HANDLE handle_ = nullptr;
    DWORD  pid_ = 0;
};

} // namespace proc
