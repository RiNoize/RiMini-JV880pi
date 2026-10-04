#include "RiBulkDisplay.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <random>
using namespace RiBulkDisplay;
Frame make(std::string_view a, std::string_view b = "") {
    auto f = fallback(b, false);
    for (int i=0;i<40;++i) f.text[(std::size_t)i]=' ';
    for (std::size_t i=0;i<a.size() && i<24;++i) f.text[i]=(uint8_t)a[i];
    return f;
}
int main() {
    const Context a{10, 3, false}, b{11, 3, false}, c{11, 4, false}, d{11, 4, true};
    const auto normal = make("PR-A:011", "Warm Pad");
    const auto busy = make("Now Bulk Receiving...");
    const auto orig = busy;
    const auto alt = fallback("New Patch",false);
    assert(isReceiveNotice(busy));
    assert(isReceiveNotice(make("Now Bulk", "Receiving")));
    assert(isReceiveNotice(make("** NOW BULK RECEIVING **")));
    assert(isReceiveNotice(make("Now Bulk Recieving")));
    for (const auto& f : {make("Bulk Dump Error"), make("Checksum Error"),
             make("Now Bulk Receiving", "MIDI Error"), make("Now Bulk Receiving", "10"),
             make("Now Bulk Sending"), make("Now Bulk Receive"), make(""), normal})
        assert(!isReceiveNotice(f));
    auto offscreen = make("");
    for (std::size_t i=0;i<16;++i) offscreen.text[24+i] = (uint8_t)std::string_view("nowbulkreceiving ")[i];
    assert(!isReceiveNotice(offscreen));
    Filter filter;
    assert(filter.select(busy,a,alt,true) == alt); // first frame fallback
    assert(filter.select(normal,a,alt,true) == normal);
    for (int i=0;i<10000;++i) assert(filter.select(busy,a,alt,true) == normal);
    assert(busy == orig); // the display policy never changes its input
    assert(filter.select(busy,a,alt,false) == busy);
    assert(filter.select(busy,a,alt,true) == normal); // toggle does not cache the notice
    assert(filter.select(busy,b,alt,true) == alt); // new program
    assert(filter.select(normal,b,alt,true) == normal);
    assert(filter.select(busy,c,alt,true) == alt); // new expansion
    assert(filter.select(normal,c,alt,true) == normal);
    assert(filter.select(busy,d,alt,true) == alt); // new mode
    const auto error = make("MIDI Buffer Full!");
    assert(filter.select(error,d,alt,true) == error); // errors are not swallowed
    auto cursor = normal; cursor.cursor=1; cursor.glyphs[3]=7;
    assert(filter.select(cursor,a,alt,true)==cursor);
    auto held=filter.select(busy,a,alt,true);
    assert(held.cursor==0 && held.glyphs==cursor.glyphs && held.text==cursor.text);
    std::mt19937 random(880);
    for (int i=0;i<20000;++i) {
        Frame f; for (auto& ch:f.text) ch=(uint8_t)random();
        auto saved=f;
        const auto shown=filter.select(f,{i,0,false},alt,true);
        assert(f==saved);
        if (!isReceiveNotice(f)) assert(shown==f);
    }
    std::cout << "PASS: receive notice matching, 10000 repeated edits, 20000 arbitrary frames, errors, ROM/program/mode invalidation, toggle, glyphs/cursor and read-only input\n";
}
