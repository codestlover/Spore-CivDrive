
#pragma once
#include <Windows.h>
#include <cstdint>

namespace callers {
inline bool Executable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION mbi{};
    return VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
           (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

inline bool Inside(uintptr_t address, uintptr_t base, size_t size) {
    return address >= base && address - base < size;
}

inline bool CallReturn(uintptr_t address, uintptr_t base, size_t size) {
    if (!Inside(address - 8, base, size) || !Inside(address, base, size) || !Executable(address) ||
        !Executable(address - 8))
        return false;
    auto* p = reinterpret_cast<const unsigned char*>(address);
    unsigned char m1 = p[-1], m2 = p[-2], m5 = p[-5];
    if (m5 == 0xE8)
        return true;
    if (m2 == 0xFF && ((m1 & 0xF8) == 0xD0 || ((m1 & 0xF8) == 0x10 && (m1 & 7) != 4 && (m1 & 7) != 5)))
        return true;
    if (p[-3] == 0xFF && (((m2 & 0xF8) == 0x50 && (m2 & 7) != 4) || m2 == 0x14))
        return true;
    if (p[-4] == 0xFF && p[-3] == 0x54)
        return true;
    if (p[-6] == 0xFF && (m5 == 0x15 || ((m5 & 0xF8) == 0x90 && (m5 & 7) != 4)))
        return true;
    return p[-7] == 0xFF && p[-6] == 0x94;
}

inline uintptr_t GameCaller(const uintptr_t* returnSlot, uintptr_t base, size_t size) {
    if (Inside(*returnSlot, base, size))
        return *returnSlot;
    auto* top = reinterpret_cast<const uintptr_t*>(reinterpret_cast<const NT_TIB*>(NtCurrentTeb())->StackBase);
    for (int i = 1; i < 64 && returnSlot + i < top; ++i)
        if (CallReturn(returnSlot[i], base, size))
            return returnSlot[i];
    return 0;
}
}

