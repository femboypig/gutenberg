#include "gutenberg/json.hpp"
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace gutenberg {
namespace {
class Parser {
  public:
    Parser(std::string_view input, JsonLimits limits) : input_(input), limits_(limits) {}
    JsonValue run() {
        if (input_.size() > limits_.bytes)
            fail("JSON size limit exceeded");
        if (input_.starts_with("\xef\xbb\xbf"))
            cursor_ = 3;
        auto value = parse(0);
        whitespace();
        if (cursor_ != input_.size())
            fail("trailing JSON data");
        return value;
    }

  private:
    [[noreturn]] void fail(std::string message) const {
        throw Error(ErrorCode::malformed_json, std::move(message) + " at byte " + std::to_string(cursor_));
    }
    void whitespace() {
        while (cursor_ < input_.size() && (input_[cursor_] == ' ' || input_[cursor_] == '\n' ||
                                           input_[cursor_] == '\r' || input_[cursor_] == '\t'))
            ++cursor_;
    }
    bool take(char c) {
        if (cursor_ < input_.size() && input_[cursor_] == c) {
            ++cursor_;
            return true;
        }
        return false;
    }
    void require(char c) {
        if (!take(c))
            fail("unexpected JSON token");
    }
    char16_t escape() {
        unsigned result = 0;
        for (unsigned n = 0; n < 4; ++n) {
            if (cursor_ == input_.size())
                fail("truncated unicode escape");
            const char c = input_[cursor_++];
            const int digit = c >= '0' && c <= '9'   ? c - '0'
                              : c >= 'a' && c <= 'f' ? c - 'a' + 10
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                     : -1;
            if (digit < 0)
                fail("invalid unicode escape");
            result = result * 16 + static_cast<unsigned>(digit);
        }
        return static_cast<char16_t>(result);
    }
    std::u16string string() {
        require('"');
        std::u16string result;
        while (cursor_ < input_.size()) {
            const auto start = cursor_;
            while (cursor_ < input_.size() && input_[cursor_] != '"' && input_[cursor_] != '\\') {
                if (static_cast<unsigned char>(input_[cursor_]) < 0x20)
                    fail("unescaped control character");
                ++cursor_;
            }
            try {
                result += utf16(input_.substr(start, cursor_ - start));
            } catch (const Error&) {
                fail("invalid UTF-8 in JSON string");
            }
            if (take('"'))
                return result;
            require('\\');
            if (cursor_ == input_.size())
                fail("truncated string escape");
            switch (input_[cursor_++]) {
            case '"':
                result += u'"';
                break;
            case '\\':
                result += u'\\';
                break;
            case '/':
                result += u'/';
                break;
            case 'b':
                result += u'\b';
                break;
            case 'f':
                result += u'\f';
                break;
            case 'n':
                result += u'\n';
                break;
            case 'r':
                result += u'\r';
                break;
            case 't':
                result += u'\t';
                break;
            case 'u':
                result += escape();
                break;
            default:
                fail("invalid string escape");
            }
        }
        fail("unterminated JSON string");
    }
    JsonValue number() {
        const auto start = cursor_;
        (void)take('-');
        if (!take('0')) {
            if (cursor_ == input_.size() || input_[cursor_] < '1' || input_[cursor_] > '9')
                fail("invalid JSON number");
            while (cursor_ < input_.size() && input_[cursor_] >= '0' && input_[cursor_] <= '9')
                ++cursor_;
        }
        bool integral = true;
        if (take('.')) {
            integral = false;
            const auto digits = cursor_;
            while (cursor_ < input_.size() && input_[cursor_] >= '0' && input_[cursor_] <= '9')
                ++cursor_;
            if (digits == cursor_)
                fail("missing fraction digits");
        }
        if (take('e') || take('E')) {
            integral = false;
            if (!take('+'))
                (void)take('-');
            const auto digits = cursor_;
            while (cursor_ < input_.size() && input_[cursor_] >= '0' && input_[cursor_] <= '9')
                ++cursor_;
            if (digits == cursor_)
                fail("missing exponent digits");
        }
        const auto text = input_.substr(start, cursor_ - start);
        if (integral) {
            std::int64_t value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error == std::errc{} && end == text.data() + text.size())
                return JsonValue{value};
        }
        const std::string copy(text);
        char* end = nullptr;
        const auto value = std::strtod(copy.c_str(), &end);
        if (!std::isfinite(value) || end != copy.data() + copy.size())
            fail("JSON number is out of range");
        return JsonValue{value};
    }
    JsonValue parse(unsigned depth) {
        if (depth > limits_.depth || ++nodes_ > limits_.nodes)
            fail("JSON complexity limit exceeded");
        whitespace();
        if (take('{')) {
            JsonValue::Object values;
            std::unordered_map<std::u16string, std::size_t> indices;
            whitespace();
            if (take('}'))
                return JsonValue{std::move(values)};
            for (;;) {
                whitespace();
                const auto name = string();
                whitespace();
                require(':');
                auto value = parse(depth + 1);
                const auto found = indices.find(name);
                if (found != indices.end())
                    values[found->second].second = std::move(value);
                else {
                    indices.emplace(name, values.size());
                    values.emplace_back(name, std::move(value));
                }
                whitespace();
                if (take('}'))
                    break;
                require(',');
            }
            return JsonValue{std::move(values)};
        }
        if (take('[')) {
            JsonValue::Array values;
            whitespace();
            if (take(']'))
                return JsonValue{std::move(values)};
            for (;;) {
                values.push_back(parse(depth + 1));
                whitespace();
                if (take(']'))
                    break;
                require(',');
            }
            return JsonValue{std::move(values)};
        }
        if (cursor_ < input_.size() && input_[cursor_] == '"')
            return JsonValue{string()};
        for (const auto literal :
             {std::string_view("true"), std::string_view("false"), std::string_view("null")})
            if (input_.substr(cursor_).starts_with(literal)) {
                cursor_ += literal.size();
                if (literal == "null")
                    return {};
                return JsonValue{literal == "true"};
            }
        return number();
    }
    std::string_view input_;
    JsonLimits limits_;
    std::size_t cursor_ = 0, nodes_ = 0;
};

class Writer {
  public:
    Writer(unsigned indent, JsonLimits limits) : indent_(indent), limits_(limits) {
        if (indent > 32)
            throw Error(ErrorCode::malformed_json, "JSON indentation limit exceeded");
    }
    std::string run(const JsonValue& value) {
        write(value, 0);
        return std::move(output_);
    }

  private:
    void append(std::string_view text) {
        if (text.size() > limits_.bytes - output_.size())
            throw Error(ErrorCode::malformed_json, "JSON output size limit exceeded");
        output_ += text;
    }
    void newline(unsigned depth) {
        if (indent_) {
            append("\n");
            append(std::string(static_cast<std::size_t>(depth) * indent_, ' '));
        }
    }
    void quote(std::u16string_view text) {
        append("\"");
        constexpr char hex[] = "0123456789abcdef";
        for (std::size_t n = 0; n < text.size(); ++n) {
            const auto c = text[n];
            if (c == '"')
                append("\\\"");
            else if (c == '\\')
                append("\\\\");
            else if (c == '\b')
                append("\\b");
            else if (c == '\f')
                append("\\f");
            else if (c == '\n')
                append("\\n");
            else if (c == '\r')
                append("\\r");
            else if (c == '\t')
                append("\\t");
            else if (c >= 0xd800 && c <= 0xdbff && n + 1 < text.size() && text[n + 1] >= 0xdc00 &&
                     text[n + 1] <= 0xdfff) {
                append(utf8(text.substr(n, 2)));
                ++n;
            } else if (c < 0x20 || (c >= 0xd800 && c <= 0xdfff)) {
                const char escaped[]{
                    '\\', 'u', hex[(c >> 12) & 15], hex[(c >> 8) & 15], hex[(c >> 4) & 15], hex[c & 15]};
                append(std::string_view(escaped, 6));
            } else
                append(utf8(text.substr(n, 1)));
        }
        append("\"");
    }
    void write(const JsonValue& value, unsigned depth) {
        if (depth > limits_.depth || ++nodes_ > limits_.nodes)
            throw Error(ErrorCode::malformed_json, "JSON output complexity limit exceeded");
        if (std::holds_alternative<std::nullptr_t>(value.data))
            append("null");
        else if (const auto* boolean = std::get_if<bool>(&value.data))
            append(*boolean ? "true" : "false");
        else if (const auto* integer = std::get_if<std::int64_t>(&value.data))
            append(std::to_string(*integer));
        else if (const auto* number = std::get_if<double>(&value.data)) {
            if (!std::isfinite(*number))
                throw Error(ErrorCode::malformed_json, "non-finite JSON number");
            char bytes[128];
            const auto [end, error] = std::to_chars(bytes, bytes + sizeof(bytes), *number);
            if (error != std::errc{})
                throw Error(ErrorCode::malformed_json, "cannot format JSON number");
            append(std::string_view(bytes, static_cast<std::size_t>(end - bytes)));
        } else if (const auto* text = std::get_if<std::u16string>(&value.data))
            quote(*text);
        else if (const auto* array = std::get_if<JsonValue::Array>(&value.data)) {
            append("[");
            for (std::size_t n = 0; n < array->size(); ++n) {
                if (n)
                    append(",");
                newline(depth + 1);
                write((*array)[n], depth + 1);
            }
            if (!array->empty())
                newline(depth);
            append("]");
        } else {
            const auto& object = std::get<JsonValue::Object>(value.data);
            append("{");
            for (std::size_t n = 0; n < object.size(); ++n) {
                if (n)
                    append(",");
                newline(depth + 1);
                quote(object[n].first);
                append(indent_ ? ": " : ":");
                write(object[n].second, depth + 1);
            }
            if (!object.empty())
                newline(depth);
            append("}");
        }
    }
    unsigned indent_;
    JsonLimits limits_;
    std::string output_;
    std::size_t nodes_ = 0;
};
} // namespace

JsonValue parseJson(std::string_view text, JsonLimits limits) {
    return Parser(text, limits).run();
}
std::string stringifyJson(const JsonValue& value, unsigned indent, JsonLimits limits) {
    return Writer(indent, limits).run(value);
}

} // namespace gutenberg
