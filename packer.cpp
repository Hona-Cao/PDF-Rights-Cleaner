#include <windows.h>
#include <compressapi.h>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#pragma comment(lib, "cabinet.lib")

struct Header {
    std::uint32_t magic = 0x31585050; // PPX1
    std::uint32_t algorithm = COMPRESS_ALGORITHM_XPRESS_HUFF;
    std::uint64_t original_size = 0;
    std::uint64_t packed_size = 0;
};

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::wcerr << L"usage: packer <input> <output>\n";
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary | std::ios::ate);
    if (!in) return 3;
    auto n = static_cast<std::size_t>(in.tellg());
    in.seekg(0);
    std::vector<unsigned char> source(n);
    if (n && !in.read(reinterpret_cast<char*>(source.data()), static_cast<std::streamsize>(n))) return 4;

    COMPRESSOR_HANDLE compressor = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor)) return 5;
    SIZE_T needed = 0;
    Compress(compressor, source.data(), source.size(), nullptr, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseCompressor(compressor);
        return 6;
    }
    std::vector<unsigned char> packed(needed);
    if (!Compress(compressor, source.data(), source.size(), packed.data(), packed.size(), &needed)) {
        CloseCompressor(compressor);
        return 7;
    }
    CloseCompressor(compressor);
    packed.resize(needed);

    Header header;
    header.original_size = source.size();
    header.packed_size = packed.size();
    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    if (!out) return 8;
    out.write(reinterpret_cast<char const*>(&header), sizeof(header));
    out.write(reinterpret_cast<char const*>(packed.data()), static_cast<std::streamsize>(packed.size()));
    std::wcout << argv[1] << L": " << n << L" -> " << packed.size() << L" bytes\n";
    return out ? 0 : 9;
}
