#include "json_util.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace {

class JsonParser {
public:
    explicit JsonParser(std::string input) : input_(std::move(input)) {}

    JsonValue parse() {
        skipWhitespace();
        auto value = parseValue();
        skipWhitespace();
        return value;
    }

private:
    std::string input_;
    size_t pos_ = 0;

    char peek() const {
        return pos_ < input_.size() ? input_[pos_] : '\0';
    }

    char get() {
        return pos_ < input_.size() ? input_[pos_++] : '\0';
    }

    void skipWhitespace() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    JsonValue parseValue() {
        skipWhitespace();
        const char c = peek();

        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseStringValue();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();

        JsonValue value;
        return value;
    }

    JsonValue parseObject() {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        get();

        skipWhitespace();
        if (peek() == '}') {
            get();
            return value;
        }

        while (true) {
            skipWhitespace();
            if (peek() != '"') break;

            const std::string key = parseString();
            skipWhitespace();
            if (get() != ':') break;

            value.objectValue[key] = parseValue();
            skipWhitespace();

            const char delim = get();
            if (delim == '}') break;
            if (delim != ',') break;
        }

        return value;
    }

    JsonValue parseArray() {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        get();

        skipWhitespace();
        if (peek() == ']') {
            get();
            return value;
        }

        while (true) {
            value.arrayValue.push_back(parseValue());
            skipWhitespace();

            const char delim = get();
            if (delim == ']') break;
            if (delim != ',') break;
        }

        return value;
    }

    JsonValue parseStringValue() {
        JsonValue value;
        value.type = JsonValue::Type::String;
        value.stringValue = parseString();
        return value;
    }

    std::string parseString() {
        std::string result;
        if (get() != '"') {
            return result;
        }

        while (pos_ < input_.size()) {
            const char c = get();
            if (c == '"') {
                break;
            }
            if (c == '\\' && pos_ < input_.size()) {
                const char esc = get();
                switch (esc) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: result.push_back(esc); break;
                }
            } else {
                result.push_back(c);
            }
        }

        return result;
    }

    JsonValue parseNumber() {
        JsonValue value;
        value.type = JsonValue::Type::Number;

        const size_t start = pos_;
        if (peek() == '-') {
            get();
        }
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            get();
        }
        if (peek() == '.') {
            get();
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                get();
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            get();
            if (peek() == '+' || peek() == '-') {
                get();
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                get();
            }
        }

        value.numberValue = std::stod(input_.substr(start, pos_ - start));
        return value;
    }

    JsonValue parseBool() {
        JsonValue value;
        value.type = JsonValue::Type::Bool;

        if (input_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            value.boolValue = true;
        } else if (input_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            value.boolValue = false;
        }

        return value;
    }

    JsonValue parseNull() {
        if (input_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
        }
        return JsonValue{};
    }
};

} // namespace

const JsonValue* JsonValue::get(const std::string& key) const {
    if (type != Type::Object) {
        return nullptr;
    }
    const auto it = objectValue.find(key);
    return it == objectValue.end() ? nullptr : &it->second;
}

std::optional<std::string> JsonValue::getString(const std::string& key) const {
    const JsonValue* child = get(key);
    if (!child || child->type != Type::String) {
        return std::nullopt;
    }
    return child->stringValue;
}

std::optional<double> JsonValue::getNumber(const std::string& key) const {
    const JsonValue* child = get(key);
    if (!child || child->type != Type::Number) {
        return std::nullopt;
    }
    return child->numberValue;
}

std::optional<bool> JsonValue::getBool(const std::string& key) const {
    const JsonValue* child = get(key);
    if (!child || child->type != Type::Bool) {
        return std::nullopt;
    }
    return child->boolValue;
}

std::optional<JsonValue> parseJsonFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    JsonParser parser(oss.str());
    return parser.parse();
}

std::string jsonEscape(const std::string& input) {
    std::string result;
    result.reserve(input.size());

    for (char c : input) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(c); break;
        }
    }

    return result;
}
