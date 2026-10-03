#pragma once
#include "JuceHeader.h"
#include "rom.h"
#include "sha1.h"

namespace RiRom {
inline juce::File directory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("JV880");
}
inline juce::MemoryBlock readBounded(juce::InputStream& in, size_t maxSize) {
    juce::MemoryBlock result;
    char buffer[16384];
    for (;;) {
        const int n = in.read(buffer, sizeof(buffer));
        if (n < 0) throw std::runtime_error("Error al leer archivo");
        if (n == 0) break;
        if (result.getSize() + (size_t)n > maxSize) throw std::runtime_error("Archivo demasiado grande");
        result.append(buffer, (size_t)n);
    }
    return result;
}
inline juce::String identifyAndWrite(const juce::String& name, const juce::MemoryBlock& bytes) {
    if (bytes.getSize() == 0 || bytes.getSize() > 8u*1024*1024) return {};
    uint8_t digest[20]{};
    char hex[41]{};
    sha1::calc(bytes.getData(), (int)bytes.getSize(), digest);
    sha1::toHexString(digest, hex);
    const auto basename = name.replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false).toLowerCase();
    int index = -1;
    bool decoded = false;
    for (int i=0; i<(int)romCountChk; ++i) {
        if (bytes.getSize() != romInfos[i].length) continue;
        if (romInfos[i].checksum == hex) { index=i; break; }
        if (romInfos[i].needsUnscramble && romInfos[i].checksumUnscrambled == hex) { index=i; decoded=true; break; }
    }
    // NVRAM is mutable; firmware/wave ROMs still require a known checksum.
    if (index < 0 && bytes.getSize() == 32768 && (basename.contains("nvram") || basename == "nvram.bin")) index=0;
    if (index < 0) return {};
    auto root = directory();
    if (root.createDirectory().failed()) throw std::runtime_error("No se pudo crear la carpeta privada de ROMs");
    auto destination = decoded ? root.getChildFile("Cache") : root;
    destination.createDirectory();
    auto file = destination.getChildFile(juce::String(romInfos[index].filename));
    if (!file.replaceWithData(bytes.getData(), bytes.getSize())) throw std::runtime_error("No se pudo guardar una ROM");
    if (!decoded && romInfos[index].needsUnscramble) root.getChildFile("Cache").getChildFile(juce::String(romInfos[index].filename)).deleteFile();
    return juce::String(romInfos[index].filename);
}
inline juce::String importFiles(const juce::Array<juce::URL>& urls) {
    int imported=0, ignored=0;
    for (const auto& url : urls) {
        auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
        if (!stream) throw std::runtime_error("No se pudo abrir el archivo seleccionado");
        auto data = readBounded(*stream, 256u*1024*1024);
        const auto* b = static_cast<const uint8_t*>(data.getData());
        const bool zip = data.getSize() >= 4 && b[0]=='P' && b[1]=='K' && b[2]==3 && b[3]==4;
        if (zip) {
            juce::MemoryInputStream memory(data, false);
            juce::ZipFile archive(memory);
            if (archive.getNumEntries() > 512) throw std::runtime_error("ZIP con demasiadas entradas");
            size_t decodedTotal=0;
            for (int i=0; i<archive.getNumEntries(); ++i) {
                const auto* entry = archive.getEntry(i);
                if (!entry || entry->filename.endsWithChar('/') || entry->uncompressedSize <= 0) continue;
                if (entry->uncompressedSize > 8*1024*1024) { ++ignored; continue; }
                decodedTotal += (size_t)entry->uncompressedSize;
                if (decodedTotal > 256u*1024*1024) throw std::runtime_error("ZIP excede el limite de extraccion");
                std::unique_ptr<juce::InputStream> input(archive.createStreamForEntry(i));
                if (!input) throw std::runtime_error("Entrada ZIP ilegible");
                auto payload = readBounded(*input, 8u*1024*1024);
                if ((juce::int64)payload.getSize() != entry->uncompressedSize) throw std::runtime_error("Entrada ZIP incompleta");
                if (identifyAndWrite(entry->filename, payload).isNotEmpty()) ++imported; else ++ignored;
            }
        } else {
            if (identifyAndWrite(url.getFileName(), data).isNotEmpty()) ++imported; else ++ignored;
        }
    }
    return urls.isEmpty() ? juce::String() : juce::String(imported) + " ROMs importadas; " + juce::String(ignored) + " archivos omitidos. ";
}
inline juce::String missing() {
    juce::StringArray list;
    for (int i=0; i<(int)romCountRequired; ++i)
        if (!romInfos[i].loaded) list.add(juce::String(romInfos[i].filename));
    return list.joinIntoString(", ");
}
}
