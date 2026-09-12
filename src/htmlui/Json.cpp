#include "htmlui/Json.h"

#include <cstdlib>

namespace htmlui {
namespace {

void skipWhitespace(std::string_view text, std::size_t& pos)
{
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\r' || text[pos] == '\t')) ++pos;
}

std::optional<std::string> parseString(std::string_view text, std::size_t& pos)
{
    if (pos >= text.size() || text[pos] != '"') return {};
    ++pos;

    std::string result;
    while (pos < text.size())
    {
        const char c = text[pos++];
        if (c == '"') return result;
        if (c == '\\' && pos < text.size())
        {
            const char escaped = text[pos++];
            switch (escaped)
            {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: result.push_back(escaped); break;
            }
        }
        else
        {
            result.push_back(c);
        }
    }
    return {};
}

std::string parsePrimitive(std::string_view text, std::size_t& pos)
{
    const std::size_t start = pos;
    while (pos < text.size() && text[pos] != ',' && text[pos] != '}') ++pos;
    std::size_t end = pos;
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\n' || text[end - 1] == '\r' || text[end - 1] == '\t')) --end;
    return std::string(text.substr(start, end - start));
}

void skipNested(std::string_view text, std::size_t& pos, char open, char close)
{
    int depth = 1;
    ++pos;
    while (pos < text.size() && depth > 0)
    {
        if (text[pos] == '"')
        {
            auto ignored = parseString(text, pos);
            (void)ignored;
        }
        else if (text[pos] == open)
        {
            ++depth;
            ++pos;
        }
        else if (text[pos] == close)
        {
            --depth;
            ++pos;
        }
        else
        {
            ++pos;
        }
    }
}

} // namespace

Json::Json(std::string raw) :
    raw_(std::move(raw)),
    values_(parseObject(raw_))
{
}

std::optional<std::string> Json::maybeString(const std::string& key) const
{
    auto it = values_.find(key);
    if (it == values_.end() || !it->second.quoted) return {};
    return it->second.text;
}

std::optional<double> Json::maybeNumber(const std::string& key) const
{
    auto it = values_.find(key);
    if (it == values_.end() || it->second.quoted) return {};

    char* end = nullptr;
    const double value = std::strtod(it->second.text.c_str(), &end);
    if (!end || *end != '\0') return {};
    return value;
}

std::optional<bool> Json::maybeBool(const std::string& key) const
{
    auto it = values_.find(key);
    if (it == values_.end() || it->second.quoted) return {};
    if (it->second.text == "true") return true;
    if (it->second.text == "false") return false;
    return {};
}

std::string Json::string(const std::string& key, const std::string& fallback) const
{
    return maybeString(key).value_or(fallback);
}

uint64_t Json::u64(const std::string& key, uint64_t fallback) const
{
    const auto value = maybeNumber(key);
    return value ? static_cast<uint64_t>(*value) : fallback;
}

double Json::number(const std::string& key, double fallback) const
{
    return maybeNumber(key).value_or(fallback);
}

bool Json::boolean(const std::string& key, bool fallback) const
{
    return maybeBool(key).value_or(fallback);
}

std::unordered_map<std::string, Json::Value> Json::parseObject(const std::string& raw)
{
    std::unordered_map<std::string, Value> values;
    std::string_view text(raw);
    std::size_t pos = 0;
    skipWhitespace(text, pos);
    if (pos >= text.size() || text[pos] != '{') return values;
    ++pos;

    while (pos < text.size())
    {
        skipWhitespace(text, pos);
        if (pos >= text.size() || text[pos] == '}') break;

        auto key = parseString(text, pos);
        if (!key) break;

        skipWhitespace(text, pos);
        if (pos >= text.size() || text[pos] != ':') break;
        ++pos;
        skipWhitespace(text, pos);
        if (pos >= text.size()) break;

        Value value;
        if (text[pos] == '"')
        {
            auto parsed = parseString(text, pos);
            if (!parsed) break;
            value.text = *parsed;
            value.quoted = true;
        }
        else if (text[pos] == '{')
        {
            const std::size_t start = pos;
            skipNested(text, pos, '{', '}');
            value.text = std::string(text.substr(start, pos - start));
        }
        else if (text[pos] == '[')
        {
            const std::size_t start = pos;
            skipNested(text, pos, '[', ']');
            value.text = std::string(text.substr(start, pos - start));
        }
        else
        {
            value.text = parsePrimitive(text, pos);
        }

        values.emplace(std::move(*key), std::move(value));
        skipWhitespace(text, pos);
        if (pos < text.size() && text[pos] == ',') ++pos;
    }

    return values;
}

std::string Json::unescape(std::string_view value)
{
    std::size_t pos = 0;
    return parseString(value, pos).value_or(std::string(value));
}

} // namespace htmlui
