#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <map>
namespace PresetCategories {
inline constexpr std::array<const char*, 12> names {
    "Uncategorized", "Bass", "Lead", "Pad", "Keys", "Pluck", "Bell", "Rhythm", "Atmosphere", "Motion", "FX", "Other"
};
inline juce::String canonical(const juce::String& value) {
    for (const auto* name : names) if (value.trim().equalsIgnoreCase(name)) return name;
    return names[0];
}
inline int index(const juce::String& value) {
    const auto category = canonical(value);
    for (size_t i = 0; i < names.size(); ++i) if (category == names[i]) return static_cast<int>(i);
    return 0;
}
inline juce::StringArray choices() {
    juce::StringArray result;
    for (const auto* name : names) result.add(name);
    return result;
}
// Presets can contain thousands of parameters. Parse only when a file changes;
// never load a sound just to discover its category. Refresh explicitly clears this cache.
class Cache {
    struct Entry { juce::int64 bytes = -1, modified = -1; juce::String category; };
    std::map<juce::String, Entry> entries;
public:
    void clear() { entries.clear(); }
    juce::String category(const juce::File& file) {
        auto& entry = entries[file.getFullPathName()];
        const auto bytes = file.getSize(), modified = file.getLastModificationTime().toMilliseconds();
        if (entry.bytes != bytes || entry.modified != modified) {
            entry.bytes = bytes; entry.modified = modified; entry.category = names[0];
            if (bytes > 0 && bytes <= 4 * 1024 * 1024) {
                const auto data = juce::JSON::parse(file);
                if (data.isObject() && data["format"].toString() == "SineStackPreset" && data["category"].isString())
                    entry.category = canonical(data["category"].toString());
            }
        }
        return entry.category;
    }
};
}
