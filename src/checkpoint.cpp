#include "checkpoint.hpp"
#include <algorithm>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace geode {
namespace {
std::filesystem::path utf8_path(const std::string &s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}

uint64_t checksum(std::span<const uint8_t> bytes) {
    uint64_t value = 1469598103934665603ULL;
    for (auto c : bytes)
        value = (value ^ c) * 1099511628211ULL;
    return value;
}

std::span<const uint8_t> view(const std::string &s) {
    return {reinterpret_cast<const uint8_t *>(s.data()), s.size()};
}
}

CheckpointJournal::CheckpointJournal(const std::string &directory, const std::string &fingerprint)
    : fingerprint_(fingerprint) {
    auto base = utf8_path(directory);
    std::filesystem::create_directories(base);
    path_ = base / ("geode-v1-" + std::to_string(checksum(view(fingerprint))) + ".bin");
#ifdef _WIN32
    auto handle = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open geode checkpoint (another process may be using it)");
    handle_ = reinterpret_cast<intptr_t>(handle);
#else
    handle_ = ::open(path_.c_str(), O_RDWR | O_CREAT, 0600);
    if (handle_ < 0)
        throw std::runtime_error("Cannot open geode checkpoint");
    if (flock(int(handle_), LOCK_EX | LOCK_NB)) {
        ::close(int(handle_));
        handle_ = -1;
        throw std::runtime_error("Geode checkpoint is already in use");
    }
#endif
    try {
        CheckpointRecord header;
        header.put(uint32_t(0x47444331));
        header.put(uint32_t(fingerprint.size()));
        header.bytes.insert(header.bytes.end(), fingerprint.begin(), fingerprint.end());
        CheckpointRecord frame;
        frame.put(uint64_t(header.bytes.size()));
        frame.bytes.insert(frame.bytes.end(), header.bytes.begin(), header.bytes.end());
        frame.put(checksum(header.bytes));
        auto size = std::filesystem::file_size(path_);
        if (size < frame.bytes.size()) {
            std::vector<uint8_t> prefix(size);
            std::ifstream partial(path_, std::ios::binary);
            partial.read(reinterpret_cast<char *>(prefix.data()), std::streamsize(size));
            if (!partial || !std::equal(prefix.begin(), prefix.end(), frame.bytes.begin()))
                throw std::runtime_error("Invalid geode checkpoint header");
            partial.close();
            truncate(0);
            append(header);
        }
    } catch (...) {
#ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
        ::close(int(handle_));
#endif
        handle_ = -1;
        throw;
    }
}

CheckpointJournal::~CheckpointJournal() {
    if (handle_ != -1) {
#ifdef _WIN32
        FlushFileBuffers(reinterpret_cast<HANDLE>(handle_));
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
#else
        fsync(int(handle_));
        ::close(int(handle_));
#endif
    }
}

void CheckpointJournal::write(std::span<const uint8_t> bytes) {
    while (!bytes.empty()) {
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(reinterpret_cast<HANDLE>(handle_), bytes.data(), DWORD(std::min<size_t>(bytes.size(), 1U << 30)),
                       &written, nullptr) ||
            !written)
            throw std::runtime_error("Geode checkpoint write failed");
#else
        auto written = ::write(int(handle_), bytes.data(), bytes.size());
        if (written <= 0)
            throw std::runtime_error("Geode checkpoint write failed");
#endif
        bytes = bytes.subspan(size_t(written));
    }
}

void CheckpointJournal::append(const CheckpointRecord &record) {
    CheckpointRecord frame;
    frame.put(uint64_t(record.bytes.size()));
    write(frame.bytes);
    write(record.bytes);
    frame.bytes.clear();
    frame.put(checksum(record.bytes));
    write(frame.bytes);
    // 不为每个小瓦片刷盘；断电后可能需要重算未刷盘的后缀。
    auto now = std::chrono::steady_clock::now();
    if (now - synced_ >= std::chrono::seconds(1)) {
#ifdef _WIN32
        if (!FlushFileBuffers(reinterpret_cast<HANDLE>(handle_)))
            throw std::runtime_error("Geode checkpoint flush failed");
#else
        if (fsync(int(handle_)))
            throw std::runtime_error("Geode checkpoint flush failed");
#endif
        synced_ = now;
    }
}

void CheckpointJournal::replay(const std::function<void(CheckpointRecord &)> &apply) {
    std::ifstream file(path_, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read geode checkpoint");
    uint64_t valid = 0, size = std::filesystem::file_size(path_);
    bool first = true;
    while (size - valid >= sizeof(uint64_t)) {
        uint64_t length = 0;
        file.read(reinterpret_cast<char *>(&length), sizeof(length));
        if (length > size - valid - 8 || size - valid - 8 - length < 8)
            break;
        if (length > uint64_t(INT32_MAX))
            throw std::runtime_error("Invalid geode checkpoint frame size");
        CheckpointRecord record;
        record.bytes.resize(size_t(length));
        file.read(reinterpret_cast<char *>(record.bytes.data()), std::streamsize(length));
        uint64_t expected = 0;
        file.read(reinterpret_cast<char *>(&expected), 8);
        if (!file || checksum(record.bytes) != expected)
            throw std::runtime_error("Geode checkpoint checksum mismatch");
        if (first) {
            first = false;
            if (record.get<uint32_t>() != 0x47444331 || record.get<uint32_t>() != fingerprint_.size() ||
                record.bytes.size() - record.offset != fingerprint_.size() ||
                std::memcmp(record.bytes.data() + record.offset, fingerprint_.data(), fingerprint_.size()))
                throw std::runtime_error("Incompatible geode checkpoint");
        } else
            apply(record);
        valid += 16 + length;
    }
    if (first)
        throw std::runtime_error("Missing geode checkpoint header");
    file.close();
    truncate(valid);
}

void CheckpointJournal::truncate(uint64_t length) {
#ifdef _WIN32
    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(length);
    if (!SetFilePointerEx(reinterpret_cast<HANDLE>(handle_), position, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(reinterpret_cast<HANDLE>(handle_)))
        throw std::runtime_error("Cannot recover geode checkpoint tail");
#else
    if (ftruncate(int(handle_), off_t(length)) || lseek(int(handle_), off_t(length), SEEK_SET) < 0)
        throw std::runtime_error("Cannot recover geode checkpoint tail");
#endif
}

std::string CheckpointJournal::path() const {
    auto s = path_.u8string();
    return {reinterpret_cast<const char *>(s.data()), s.size()};
}

void remove_checkpoint(const std::string &path) {
    if (!path.empty()) {
        std::error_code error;
        std::filesystem::remove(utf8_path(path), error);
        if (error)
            throw std::runtime_error("Cannot remove completed geode checkpoint: " + error.message());
    }
}
}
