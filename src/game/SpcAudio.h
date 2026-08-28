// SpcAudio — RedLynx ".spc" audio (the `Spanc` voice class in the binary).
//
// Every sound in the game is one of these: music, speech, ambience and UI
// blips all use the same container. It is block-based ADPCM with a codebook
// carried per block, which is why it holds up at 8 kHz on a device with no
// audio hardware to speak of.
//
// Reversed from the voice class: header parse 0x100ac388, block load
// 0x100ac46c, sample loop 0x100ac0f4.
//
// Header, 32 bytes little-endian:
//   +0   u16 ?              0 in every shipped file
//   +2   u16 blockCount
//   +4   u16 blockSamples   2048
//   +6   s16 sampleRate     8000, or 4000 for the 2-bit variant
//   +10  u16 n              0 means 4; the codebook has n*n entries and the
//                           block body is n*256 bytes
//   +12  u32 totalSamples   0 means blockCount * blockSamples
//   +16..31 unused
//
// Block, 2 + 2*n*n + 256*n bytes (1058 with n = 4):
//   s16 predictor    the block's starting sample -- blocks are independent
//   s16 codebook[n*n]
//   packed codes, **low nibble first**
//
// Decoding is delta accumulation: `acc += codebook[code]`, emit `acc`. The
// codebooks are non-uniform and roughly symmetric (a shipped one runs -2391,
// -849, -421, -211, -84, -24, -3, 0, 0, 1, 25, 87, 208, 408, 837, 2295), so
// quiet passages stay quiet and transients still fit.
//
// Two sample rates exist. At 8000 the codes are 4-bit and one per sample. At
// 4000 they are 2-bit, the deltas are halved, and each is applied twice --
// upsampling to 8000 by linear interpolation as it decodes. The engine
// rejects any other rate/codebook combination outright.
//
// Verified by decoding every shipped sound: none drifts. A delta decoder that
// is even slightly wrong runs away within a second, and `scene_music1.spc`
// accumulates 548,864 samples over 68 seconds and lands back on 0. It is also
// exactly the length of the intro cutscene it plays under.
//
// **This holds the compressed stream and decodes a block at a time, which is
// the point of the format.** An earlier version of this class decoded the
// whole file into PCM at load and kept it, which is roughly four times the
// memory: the battle screen's banks came to 5.9 MB and a Dreamcast ran out of
// heap opening Broken Tranquility. The original never does that -- a `Spanc`
// keeps its file and 0x100ac46c pulls one block off it whenever the mixer has
// exhausted the last, which is why the engine can afford all five voice-over
// banks at once. Blocks are independent and fixed-size in both bytes and
// samples, so the block holding sample N is just N / BlockSamples() and there
// is no state to unwind: streaming costs one small buffer per voice and gives
// back three quarters of the memory.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bb {

class FilePack;

class SpcSound {
public:
    // The rate the mixer runs at; 4000 Hz sources are upsampled on decode.
    static constexpr int kRate = 8000;

    // Read and decode `path` from the pak. Returns false if it is missing or
    // its header is a combination the original would reject.
    bool Load(FilePack& pack, const std::string& path);

    bool Valid() const { return m_Total > 0; }
    const std::string& Path() const { return m_Path; }

    // Samples, not bytes: how long the sound is once decoded.
    std::size_t Count() const { return m_Total; }
    double Seconds() const { return double(m_Total) / kRate; }
    // What the sound costs to hold -- the compressed stream, which is about a
    // quarter of what its samples would be.
    std::size_t Bytes() const { return m_Data.size(); }

    // How many samples one block decodes to. Constant for a given sound, and
    // the reason random access is exact: sample N lives in block
    // N / BlockSamples() at offset N % BlockSamples().
    std::size_t BlockSamples() const { return m_BlockSamples; }
    std::size_t BlockCount() const { return m_Blocks; }

    // Decode block `block` into `out`, which must have room for
    // BlockSamples(). Returns how many samples are valid -- the last block is
    // short when `totalSamples` does not fall on a boundary. Zero for a block
    // past the end.
    //
    // Decoded signed 16-bit mono at kRate, at full volume. The original folds
    // the voice's volume into the codebook and shifts it back out during the
    // mix; decoding at unity here and scaling in the mixer gives the same
    // samples, and lets a volume change take effect immediately rather than
    // at the next block boundary.
    std::size_t DecodeBlock(std::size_t block, int16_t* out) const;

    // The whole sound at once, into a buffer the caller owns. Nothing in the
    // game does this -- it is for the tests, which CRC a decode against the
    // Python reference, and for anything measuring a track end to end.
    void DecodeAll(std::vector<int16_t>& out) const;

private:
    std::string m_Path;
    // The compressed file, held for as long as the sound is loaded. This is
    // what a Spanc keeps too.
    std::vector<uint8_t> m_Data;
    std::size_t m_Blocks = 0;
    std::size_t m_BlockBytes = 0;    // one block's footprint in the file
    std::size_t m_BlockSamples = 0;  // and what it decodes to
    std::size_t m_Body = 0;           // packed codes per block
    std::size_t m_Total = 0;          // samples in the whole sound
    int m_Codes = 0;                  // 16 at 8 kHz, 4 at 4 kHz
};

}  // namespace bb
