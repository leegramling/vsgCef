#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace htmlui {

class Json
{
public:
    Json() = default;
    explicit Json(std::string raw);

    const std::string& raw() const { return raw_; }

    std::optional<std::string> maybeString(const std::string& key) const;
    std::optional<double> maybeNumber(const std::string& key) const;
    std::optional<bool> maybeBool(const std::string& key) const;

    std::string string(const std::string& key, const std::string& fallback = {}) const;
    uint64_t u64(const std::string& key, uint64_t fallback = 0) const;
    double number(const std::string& key, double fallback = 0.0) const;
    bool boolean(const std::string& key, bool fallback = false) const;

private:
    struct Value
    {
        std::string text;
        bool quoted = false;
    };

    static std::unordered_map<std::string, Value> parseObject(const std::string& raw);
    static std::string unescape(std::string_view value);

    std::string raw_ = "{}";
    std::unordered_map<std::string, Value> values_;
};

} // namespace htmlui
