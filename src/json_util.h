#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::unordered_map<std::string, JsonValue> objectValue;

    [[nodiscard]] bool isObject() const { return type == Type::Object; }
    [[nodiscard]] bool isArray() const { return type == Type::Array; }
    [[nodiscard]] bool isString() const { return type == Type::String; }
    [[nodiscard]] bool isNumber() const { return type == Type::Number; }
    [[nodiscard]] bool isBool() const { return type == Type::Bool; }

    [[nodiscard]] const JsonValue* get(const std::string& key) const;
    [[nodiscard]] std::optional<std::string> getString(const std::string& key) const;
    [[nodiscard]] std::optional<double> getNumber(const std::string& key) const;
    [[nodiscard]] std::optional<bool> getBool(const std::string& key) const;
};

[[nodiscard]] std::optional<JsonValue> parseJsonFile(const std::string& path);
[[nodiscard]] std::string jsonEscape(const std::string& input);
