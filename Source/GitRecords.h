#pragma once

#include <Miro/Reflect.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Reads git's output into MIRO_REFLECT structs. git cannot emit safe JSON, so a
// struct's fields become a `--format` string in declaration order, joined by a
// unit separator, and a record's values are split on it and filled back in.
namespace CodeHealth::GitRecords
{
constexpr auto fieldSeparator = '\x1f';
constexpr auto recordSeparator = '\x1e';

struct Field
{
    std::string_view name;
    Miro::PrimitiveRef value;
};

// Lists a struct's fields, in order, by walking its MIRO_REFLECT.
class FieldCollector final : public Miro::Reflector
{
public:
    FieldCollector()
        : Reflector({.mode = Miro::Mode::Load})
    {
    }

    void visit(Miro::PrimitiveRef value) override
    {
        fields.push_back({currentName, value});
    }
    void writeNull() override {}
    Miro::ValueKind kind() const override { return Miro::ValueKind::Object; }

    Reflector& atKey(std::string_view name, Miro::Options) override
    {
        currentName = name;
        return *this;
    }

    Reflector& atIndex(std::size_t, Miro::Options) override { return *this; }

    std::vector<Field> fields;

private:
    std::string_view currentName;
};

template <typename T>
std::vector<Field> collectFields(T& record)
{
    auto collector = FieldCollector {};
    record.reflect(collector);
    return collector.fields;
}

std::string getPlaceholder(std::string_view fieldName);
bool assignFieldFromText(Miro::PrimitiveRef target, std::string_view text);
std::vector<std::string_view> splitFields(std::string_view text);

template <typename T>
std::string makeFormat(std::string_view separator)
{
    auto record = T {};
    auto format = std::string {};

    for (auto& field: collectFields(record))
        format += (format.empty() ? "" : std::string {separator})
                  + getPlaceholder(field.name);

    return format;
}

// Nothing when the field count differs or a value does not convert, as for
// `git cat-file`'s "<name> missing".
template <typename T>
std::optional<T> readRecord(std::string_view text)
{
    auto record = T {};
    auto fields = collectFields(record);
    auto values = splitFields(text);

    if (values.size() != fields.size())
        return std::nullopt;

    for (auto index = size_t {0}; index < fields.size(); ++index)
        if (!assignFieldFromText(fields[index].value, values[index]))
            return std::nullopt;

    return record;
}

struct LogRecord
{
    std::string parentHashes;
    std::string authorName;
    std::int64_t authorTime = 0;

    MIRO_REFLECT(parentHashes, authorName, authorTime)
};

struct BlobHeader
{
    std::string objectName;
    std::string objectType;
    std::int64_t objectSize = 0;

    MIRO_REFLECT(objectName, objectType, objectSize)
};
} // namespace CodeHealth::GitRecords
