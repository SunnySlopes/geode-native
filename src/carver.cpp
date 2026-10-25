#include "carver.hpp"
#include "geode.hpp"
#include "legacy_random.hpp"
#include <array>
#include <cmath>
#include <deque>
#include <numbers>
#include <unordered_map>
#include <vector>

namespace geode {
namespace {
const auto &sine_table() {
    static const auto table = [] {
        std::array<float, 65536> a{};
        for (int i = 0; i < 65536; ++i)
            a[i] = float(std::sin(double(i) * std::numbers::pi * 2.0 / 65536.0));
        return a;
    }();
    return table;
}

float sine(float v) {
    return sine_table()[int(v * 10430.378f) & 65535];
}

float cosine(float v) {
    return sine_table()[int(v * 10430.378f + 16384.0f) & 65535];
}

float uniform(LegacyRandom &r, float lo, float hi) {
    return lo + r.next_float() * (hi - lo);
}

struct Shape {
    double x, y, z, rx, ry, floor;
    int canyon = -1;
};

struct Source {
    std::vector<Shape> shapes;
    std::vector<std::array<float, 384>> widths;
};

void cave_tunnel(Source &out, uint64_t seed, double x, double y, double z, double horizontal, double vertical,
                 float thickness, float yaw, float pitch, int step, int length, double y_scale, double floor) {
    LegacyRandom r(seed);
    int branch = r.bounded(length / 2) + length / 4;
    bool steep = r.bounded(6) == 0;
    float dyaw = 0, dpitch = 0;
    for (int s = step; s < length; ++s) {
        double rx = 1.5 + sine(float(std::numbers::pi) * s / length) * thickness, ry = rx * y_scale;
        float c = cosine(pitch);
        x += cosine(yaw) * c;
        y += sine(pitch);
        z += sine(yaw) * c;
        pitch *= steep ? 0.92f : 0.7f;
        pitch += dpitch * 0.1f;
        yaw += dyaw * 0.1f;
        dpitch *= 0.9f;
        dyaw *= 0.75f;
        float a = r.next_float(), b = r.next_float(), d = r.next_float();
        dpitch += (a - b) * d * 2.0f;
        a = r.next_float();
        b = r.next_float();
        d = r.next_float();
        dyaw += (a - b) * d * 4.0f;
        if (s == branch && thickness > 1.0f) {
            uint64_t child = r.next_long();
            float t = r.next_float() * 0.5f + 0.5f;
            cave_tunnel(out, child, x, y, z, horizontal, vertical, t, yaw - float(std::numbers::pi / 2), pitch / 3.0f,
                        s, length, 1.0, floor);
            child = r.next_long();
            t = r.next_float() * 0.5f + 0.5f;
            cave_tunnel(out, child, x, y, z, horizontal, vertical, t, yaw + float(std::numbers::pi / 2), pitch / 3.0f,
                        s, length, 1.0, floor);
            return;
        }
        if (r.bounded(4) != 0)
            out.shapes.push_back({x, y, z, rx * horizontal, ry * vertical, floor});
    }
}

void cave(Source &out, LegacyRandom &r, int cx, int cz, bool deep) {
    int count = r.bounded(r.bounded(r.bounded(15) + 1) + 1);
    for (int k = 0; k < count; ++k) {
        double x = cx * 16 + r.bounded(16), y = r.bounded(deep ? 104 : 237) - 56, z = cz * 16 + r.bounded(16);
        double horizontal = uniform(r, 0.7f, 1.4f), vertical = uniform(r, 0.8f, 1.3f), floor = uniform(r, -1.0f, -0.4f);
        int tunnels = 1;
        if (r.bounded(4) == 0) {
            double height = uniform(r, 0.1f, 0.9f);
            float thickness = 1.0f + r.next_float() * 6.0f;
            double radius = 1.5 + sine(float(std::numbers::pi / 2)) * thickness;
            out.shapes.push_back({x + 1, y, z, radius, radius * height, floor});
            tunnels += r.bounded(4);
        }
        for (int p = 0; p < tunnels; ++p) {
            float yaw = r.next_float() * float(std::numbers::pi * 2), pitch = (r.next_float() - 0.5f) / 4.0f;
            float a = r.next_float(), b = r.next_float(), thickness = a * 2.0f + b;
            if (r.bounded(10) == 0) {
                a = r.next_float();
                b = r.next_float();
                thickness *= a * b * 3.0f + 1.0f;
            }
            int length = 112 - r.bounded(28);
            uint64_t seed = r.next_long();
            cave_tunnel(out, seed, x, y, z, horizontal, vertical, thickness, yaw, pitch, 0, length, 1.0, floor);
        }
    }
}

void canyon(Source &out, LegacyRandom &r, int cx, int cz) {
    double x = cx * 16 + r.bounded(16), y = r.bounded(58) + 10, z = cz * 16 + r.bounded(16);
    float yaw = r.next_float() * float(std::numbers::pi * 2), pitch = uniform(r, -0.125f, 0.125f);
    float a = r.next_float(), b = r.next_float(), thickness = a * 4.0f + b * 2.0f;
    int length = int(112 * uniform(r, 0.75f, 1.0f));
    LegacyRandom path(r.next_long());
    int id = int(out.widths.size());
    auto &widths = out.widths.emplace_back();
    float factor = 1;
    for (int i = 0; i < 384; ++i) {
        if (i == 0 || path.bounded(3) == 0) {
            a = path.next_float();
            b = path.next_float();
            factor = 1.0f + a * b;
        }
        widths[i] = factor * factor;
    }
    float dyaw = 0, dpitch = 0;
    for (int s = 0; s < length; ++s) {
        double rx = 1.5 + sine(s * float(std::numbers::pi) / length) * thickness, ry = rx * 3.0;
        rx *= uniform(path, 0.75f, 1.0f);
        float center = 1.0f - std::abs(0.5f - float(s) / length) * 2.0f;
        float scale = 1.0f + 0.0f * center;
        ry = scale * ry * uniform(path, 0.75f, 1.0f);
        float c = cosine(pitch), t = sine(pitch);
        x += cosine(yaw) * c;
        y += t;
        z += sine(yaw) * c;
        pitch *= 0.7f;
        pitch += dpitch * 0.05f;
        yaw += dyaw * 0.05f;
        dpitch *= 0.8f;
        dyaw *= 0.5f;
        a = path.next_float();
        b = path.next_float();
        t = path.next_float();
        dpitch += (a - b) * t * 2.0f;
        a = path.next_float();
        b = path.next_float();
        t = path.next_float();
        dyaw += (a - b) * t * 4.0f;
        if (path.bounded(4) != 0)
            out.shapes.push_back({x, y, z, rx, ry, 0, id});
    }
}
}

struct Carver::Impl {
    uint64_t seed;
    std::array<uint64_t, 3> a{}, b{};
    std::unordered_map<uint64_t, Source> sources;
    std::deque<uint64_t> order;

    struct Section {
        int x, y, z;
        bool operator==(const Section &) const = default;
    };

    struct Hash {
        size_t operator()(Section s) const {
            return key(s.x, s.z) ^ (uint64_t(int64_t(s.y)) * 0x9E3779B97F4A7C15ULL);
        }
    };

    std::unordered_map<Section, Source, Hash> sections;
    std::deque<Section> section_order;

    explicit Impl(uint64_t s) : seed(s) {
        for (int i = 0; i < 3; ++i) {
            LegacyRandom r(seed + i);
            a[i] = r.next_long();
            b[i] = r.next_long();
        }
    }

    const Source &source(int cx, int cz) {
        auto found = sources.find(key(cx, cz));
        if (found != sources.end())
            return found->second;
        Source out;
        // 原版1.21.1主世界的53个生物群系共用这三种雕刻器及其顺序。
        for (int i = 0; i < 3; ++i) {
            LegacyRandom r((uint64_t(int64_t(cx)) * a[i]) ^ (uint64_t(int64_t(cz)) * b[i]) ^ (seed + i));
            if (r.next_float() > (i == 0 ? 0.15f : i == 1 ? 0.07f : 0.01f))
                continue;
            if (i < 2)
                cave(out, r, cx, cz, i == 1);
            else
                canyon(out, r, cx, cz);
        }
        // 限制移动来源缓存；旧隧道几何确定，可在淘汰后重新计算。
        if (sources.size() >= 8192) {
            sources.erase(order.front());
            order.pop_front();
        }
        auto k = key(cx, cz);
        order.push_back(k);
        return sources.emplace(k, std::move(out)).first->second;
    }

    const Source &section(int cx, int cy, int cz) {
        Section k{cx, cy, cz};
        auto found = sections.find(k);
        if (found != sections.end())
            return found->second;
        Source out;
        int x0 = cx * 16, x1 = x0 + 15, y0 = cy * 16, y1 = y0 + 15, z0 = cz * 16, z1 = z0 + 15;
        for (int sx = cx - 8; sx <= cx + 8; ++sx)
            for (int sz = cz - 8; sz <= cz + 8; ++sz) {
                const auto &origin = source(sx, sz);
                std::vector<int> widths(origin.widths.size(), -1);
                for (auto s : origin.shapes) {
                    if (x1 + 0.5 <= s.x - s.rx || x0 + 0.5 >= s.x + s.rx || z1 + 0.5 <= s.z - s.rx ||
                        z0 + 0.5 >= s.z + s.rx)
                        continue;
                    if (y1 <= int(std::floor(s.y - s.ry)) - 1 || y0 > int(std::floor(s.y + s.ry)) + 1)
                        continue;
                    // 复制峡谷宽度到当前分区，避免来源缓存淘汰使本次查询失效。
                    if (s.canyon >= 0) {
                        auto &id = widths[s.canyon];
                        if (id < 0) {
                            id = int(out.widths.size());
                            out.widths.push_back(origin.widths[s.canyon]);
                        }
                        s.canyon = id;
                    }
                    out.shapes.push_back(s);
                }
            }
        if (sections.size() >= 8192) {
            sections.erase(section_order.front());
            section_order.pop_front();
        }
        section_order.push_back(k);
        return sections.emplace(k, std::move(out)).first->second;
    }
};

Carver::Carver(uint64_t seed) : impl_(std::make_unique<Impl>(seed)) {}

Carver::~Carver() = default;

bool Carver::contains(int x, int y, int z) {
    if (y <= -63 || y > 312)
        return false;
    const auto &source = impl_->section(floor_div(x, 16), floor_div(y, 16), floor_div(z, 16));
    for (auto &s : source.shapes) {
        if (x + 0.5 <= s.x - s.rx || x + 0.5 >= s.x + s.rx || z + 0.5 <= s.z - s.rx || z + 0.5 >= s.z + s.rx)
            continue;
        if (y <= int(std::floor(s.y - s.ry)) - 1 || y > int(std::floor(s.y + s.ry)) + 1)
            continue;
        double dx = (x + 0.5 - s.x) / s.rx, dz = (z + 0.5 - s.z) / s.rx, dy = (y - 0.5 - s.y) / s.ry,
               horizontal = dx * dx + dz * dz;
        if (horizontal >= 1)
            continue;
        if (s.canyon < 0) {
            if (dy > s.floor && horizontal + dy * dy < 1)
                return true;
        } else if (horizontal * source.widths[s.canyon][y + 63] + dy * dy / 6.0 < 1)
            return true;
    }
    return false;
}
}
