#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// GUI-only presentation policy. Never receives a pointer to MCU/ROM/MIDI state.
// The JV displays 24 characters on each of two rows, at DDRAM offsets 0 and 40.
namespace RiBulkDisplay {
struct Frame {
    std::array<uint8_t, 80> text{};
    std::array<uint8_t, 64> glyphs{};
    uint32_t address = 0, cursor = 0;
    bool operator==(const Frame&) const = default;
};
struct Context {
    int program = -1, expansion = -1;
    bool rhythm = false;
    bool operator==(const Context&) const = default;
};
inline bool isReceiveNotice(const Frame& frame) noexcept {
    std::array<char, 48> letters{};
    std::size_t n = 0;
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 24; ++column) {
            unsigned ch = frame.text[(std::size_t)(row * 40 + column)];
            if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
            if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
                letters[n++] = (char)ch;
            else if (ch != ' ' && ch != 0 && ch != '.' && ch != '*' &&
                     ch != '<' && ch != '>' && ch != ':' && ch != '-')
                return false; // Unknown/custom characters: preserve the real display.
        }
    }
    const std::string_view message(letters.data(), n);
    // Exact complete visible message, NOT a substring/SysEx-activity test.
    // Extra words, errors, numbers or another line of information are preserved.
    return message == "nowbulkreceiving" || message == "nowbulkrecieving";
}
inline Frame fallback(std::string_view name, bool rhythm) noexcept {
    Frame frame;
    frame.text.fill(' ');
    auto line = [&](int offset, std::string_view value) {
        for (std::size_t i = 0; i < value.size() && i < 24; ++i) {
            const unsigned char ch = (unsigned char)value[i];
            frame.text[(std::size_t)offset + i] = ch >= 32 && ch < 127 ? ch : ' ';
        }
    };
    line(0, rhythm ? "RiJV880 - Rhythm Set" : "RiJV880 - Patch");
    line(40, name);
    return frame;
}
class Filter {
public:
    Frame select(const Frame& live, Context next, const Frame& alternate,
                 bool enabled) noexcept {
        if (!haveContext || next != context) {
            haveContext = true;
            context = next;
            haveClean = false; // Never show a cached patch from another selection.
        }
        const bool notice = isReceiveNotice(live);
        if (!notice) {
            clean = live;
            haveClean = true;
        }
        if (enabled && notice) {
            auto result = haveClean ? clean : alternate;
            result.cursor = 0; // Do not blink an obsolete cursor over a held frame.
            return result;
        }
        return live; // Normal/error messages and disabled mode pass through unchanged.
    }
private:
    Context context{};
    Frame clean{};
    bool haveContext = false, haveClean = false;
};
} // namespace RiBulkDisplay
