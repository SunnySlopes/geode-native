#pragma once
#include <cstdint>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace geode {
struct CheckpointRecord {
    std::vector<uint8_t> bytes;
    size_t offset = 0;

    template <class T> void put(T value) {
        auto p = reinterpret_cast<const uint8_t *>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }

    template <class T> T get() {
        if (bytes.size() - offset < sizeof(T))
            throw std::runtime_error("Truncated geode checkpoint record");
        T value;
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        offset += sizeof(T);
        return value;
    }

    void end() const {
        if (offset != bytes.size())
            throw std::runtime_error("Invalid geode checkpoint record length");
    }
};

// 数据和校验值共同提交；未写完的末条记录
// 在恢复时丢弃，完整但损坏的记录须报错，不能静默跳过。
class CheckpointJournal {
    std::filesystem::path path_;
    std::string fingerprint_;
    intptr_t handle_ = -1;
    std::chrono::steady_clock::time_point synced_{};
    void write(std::span<const uint8_t> bytes);
    void truncate(uint64_t length);

  public:
    CheckpointJournal(const std::string &directory, const std::string &fingerprint);
    ~CheckpointJournal();
    CheckpointJournal(const CheckpointJournal &) = delete;
    void replay(const std::function<void(CheckpointRecord &)> &apply);
    void append(const CheckpointRecord &record);
    std::string path() const;
};

void remove_checkpoint(const std::string &path);
}
