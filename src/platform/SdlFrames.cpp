#include "platform/SdlFrames.h"

#include <cstdio>
#include <cstdint>

#include <SDL.h>

// The only image decoder in the port. Everything else reads RedLynx's own
// formats, which we implement; PNG we do not, and a photograph of a phone is
// not worth a fifth hand-rolled codec.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO  // we hand it bytes, so the DC port needs no stdio here
#include "stb_image.h"
#include "shim/Log.h"

namespace bb {
namespace {

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

// Decode a PNG straight into a texture. Nothing here cares how big the art is,
// so raising a frame's resolution later is a file swap and no code change.
SDL_Texture* LoadTexture(SDL_Renderer* ren, const std::string& path, int& w,
                         int& h) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) {
        LogError("frame: cannot read %s\n", path.c_str());
        return nullptr;
    }
    int comp = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h,
                                        &comp, 4);
    if (!px) {
        LogError("frame: %s: %s\n", path.c_str(),
         stbi_failure_reason());
        return nullptr;
    }
    // RGBA32 is SDL's endian-correct alias for stb's byte order.
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (tex) {
        SDL_UpdateTexture(tex, nullptr, px, w * 4);
        // The outer edge carries the device's silhouette in alpha.
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        // Smoothed, unlike the game: this is a photograph blown up several
        // times over, where nearest-neighbour would only show the seams.
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    } else {
        LogError("frame: SDL_CreateTexture: %s\n", SDL_GetError());
    }
    stbi_image_free(px);
    return tex;
}

// `<id>\t<label>`, with `#` comments and blank lines skipped -- the same shape
// as the game's own string table, and CRLF-tolerant for the same reason.
void ParseManifest(const std::vector<uint8_t>& text,
                   std::vector<DeviceFrame>& out) {
    const char* p = reinterpret_cast<const char*>(text.data());
    const char* end = p + text.size();
    while (p < end) {
        const char* nl = p;
        while (nl < end && *nl != '\n') ++nl;
        const char* stop = nl;
        if (stop > p && stop[-1] == '\r') --stop;

        if (stop > p && *p != '#') {
            const char* tab = p;
            while (tab < stop && *tab != '\t') ++tab;
            DeviceFrame f;
            f.ID.assign(p, tab);
            // No label is not a reason to drop a frame; show its id instead.
            f.Label = tab < stop ? std::string(tab + 1, stop) : f.ID;
            if (!f.ID.empty()) out.push_back(std::move(f));
        }
        p = nl + 1;
    }
}

}  // namespace

std::vector<DeviceFrame> LoadDeviceFrames(SDL_Renderer* ren,
                                          const std::string& dir) {
    std::vector<DeviceFrame> frames;
    if (!ren || dir.empty()) return frames;

    std::vector<uint8_t> manifest;
    if (!ReadFile(dir + "/frames.txt", manifest)) return frames;
    ParseManifest(manifest, frames);

    // Load the art, and drop any frame that does not have both halves -- a
    // frame with one side is worse than no frame at all.
    std::vector<DeviceFrame> loaded;
    for (DeviceFrame& f : frames) {
        const std::string base = dir + "/" + f.ID;
        f.Left = LoadTexture(ren, base + "/left.png", f.LeftW, f.LeftH);
        f.Right = LoadTexture(ren, base + "/right.png", f.RightW, f.RightH);
        if (f.Left && f.Right) {
            loaded.push_back(f);
            continue;
        }
        LogError("frame: %s incomplete, skipping\n", f.ID.c_str());
        if (f.Left) SDL_DestroyTexture(f.Left);
        if (f.Right) SDL_DestroyTexture(f.Right);
    }
    return loaded;
}

void FreeDeviceFrames(std::vector<DeviceFrame>& frames) {
    for (DeviceFrame& f : frames) {
        if (f.Left) SDL_DestroyTexture(f.Left);
        if (f.Right) SDL_DestroyTexture(f.Right);
        f.Left = f.Right = nullptr;
    }
    frames.clear();
}

}  // namespace bb
