#pragma once
#include "search.hpp"
#include "json.hpp"
#include <string>
#include <vector>

namespace geode {
using Json = nlohmann::json;

/** 正式结果文件来自同一份最终榜单，由桌面与CLI共用。 */
struct Exported {
    std::string csv, txt, json;
};

/**
 * 从同一中心列表生成带UTF-8 BOM、分号和CRLF的CSV及TXT，
 * 并回填三份绝对路径。调用方补齐字段后再写JSON；
 * 传入exports时只生成内存中的文件内容，
 * 不立即落盘；在壳收到保存请求前output保持为空。
 */
Exported export_results(const Options &o, const std::vector<Center> &centers, const std::string &directory,
                        const std::string &prefix, Json &body, Json *exports = nullptr);
void write_json(const std::string &path, const Json &body);

int desktop_protocol();
}
