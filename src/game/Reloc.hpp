
#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>

namespace reloc {
constexpr uintptr_t kPreferredBase = 0x400000;

inline const IMAGE_DATA_DIRECTORY* Directory(const unsigned char* image, size_t size) {
    if (size < sizeof(IMAGE_DOS_HEADER))
        return nullptr;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || size < sizeof(IMAGE_NT_HEADERS32) ||
        uint32_t(dos->e_lfanew) > size - sizeof(IMAGE_NT_HEADERS32))
        return nullptr;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        (nt->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED) ||
        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_BASERELOC)
        return nullptr;
    const IMAGE_DATA_DIRECTORY* dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!dir->VirtualAddress || dir->Size < sizeof(IMAGE_BASE_RELOCATION) || dir->Size > size ||
        dir->VirtualAddress > size - dir->Size)
        return nullptr;
    return dir;
}

inline bool Unrelocate(const unsigned char* image, size_t size, uintptr_t base, uint32_t rva, unsigned char* bytes,
                       size_t n) {
    uint32_t delta = uint32_t(base - kPreferredBase);
    if (!delta)
        return true;
    const IMAGE_DATA_DIRECTORY* dir = Directory(image, size);
    if (!dir || rva > size || n > size - rva)
        return false;
    for (uint32_t at = dir->VirtualAddress, end = at + dir->Size; end - at >= sizeof(IMAGE_BASE_RELOCATION);) {
        auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(image + at);
        if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION) || block->SizeOfBlock > end - at)
            return false;
        uint32_t page = block->VirtualAddress;
        if (page < rva + n && rva < page + 0x1000 + 3) {
            auto* entry = reinterpret_cast<const WORD*>(block + 1);
            size_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            for (size_t i = 0; i < count; ++i) {
                unsigned type = entry[i] >> 12;
                uint32_t fix = page + (entry[i] & 0xfff);
                if (type == IMAGE_REL_BASED_ABSOLUTE || fix + 4 <= rva || fix >= rva + n)
                    continue;
                if (type != IMAGE_REL_BASED_HIGHLOW || fix > size - 4)
                    return false;
                uint32_t value = 0;
                std::memcpy(&value, image + fix, 4);
                value -= delta;
                for (uint32_t k = 0; k < 4; ++k)
                    if (fix + k >= rva && fix + k - rva < n)
                        bytes[fix + k - rva] = uint8_t(value >> 8 * k);
            }
        }
        at += block->SizeOfBlock;
    }
    return true;
}
}

