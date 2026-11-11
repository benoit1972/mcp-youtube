#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// A reader for AIFF and AIFF-C files, for the files that the audio library of the host refuses: sample-pack AIFFs come in many variants
// (AIFF-C with little-endian "sowt" samples, 32-bit floats, extra chunks before the format chunk, odd chunk sizes, an SSND chunk that is
// cut short...). It gives the sound as mono floats, and when it cannot, it says exactly why.
namespace amb
{
struct AiffResult
{
    bool ok = false;
    std::string why;                 // when it did not work: what the file is
    std::vector<float> mono;
    double sampleRate = 0.0;
    int channels = 0, bits = 0;
    std::string format;              // "AIFF", or "AIFC <compression>"
};

namespace aiffdetail
{
inline uint32_t be32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3]; }
inline uint32_t be16 (const uint8_t* p) { return ((uint32_t) p[0] << 8) | (uint32_t) p[1]; }
inline std::string tag (const uint8_t* p)
{
    std::string s;
    for (int i = 0; i < 4; ++i) s += (p[i] >= 32 && p[i] < 127) ? (char) p[i] : '?';
    return s;
}
// the sample rate is a 80-bit IEEE extended number
inline double extended (const uint8_t* p)
{
    const int exponent = (int) (be16 (p) & 0x7fff);
    const bool negative = (p[0] & 0x80) != 0;
    const uint64_t mantissa = ((uint64_t) be32 (p + 2) << 32) | (uint64_t) be32 (p + 6);
    if (exponent == 0 && mantissa == 0) return 0.0;
    const double v = std::ldexp ((double) mantissa, exponent - 16383 - 63);
    return negative ? -v : v;
}
} // namespace aiffdetail

inline AiffResult parseAiff (const uint8_t* d, size_t n, double maxSeconds = 60.0)
{
    using namespace aiffdetail;
    AiffResult r;
    if (n == 0) { r.why = "THE FILE IS EMPTY (0 BYTES: NOT DOWNLOADED YET?)"; return r; }
    if (n < 12) { r.why = "THE FILE IS TOO SMALL (" + std::to_string (n) + " BYTES)"; return r; }
    if (std::memcmp (d, "FORM", 4) != 0) { r.why = "NOT AN AIFF: THE FILE STARTS WITH '" + tag (d) + "'"; return r; }
    const std::string form = tag (d + 8);
    const bool aifc = form == "AIFC";
    if (! aifc && form != "AIFF") { r.why = "NOT AN AIFF: FORM TYPE '" + form + "'"; return r; }

    int channels = 0, bits = 0; uint32_t frames = 0; double rate = 0.0;
    std::string compression = "NONE";
    bool haveComm = false;
    const uint8_t* sound = nullptr; size_t soundBytes = 0;

    size_t pos = 12;
    while (pos + 8 <= n)
    {
        const std::string id = tag (d + pos);
        const size_t size = be32 (d + pos + 4);
        const size_t body = pos + 8;
        const size_t avail = std::min (size, n - body);
        if (id == "COMM" && avail >= 18)
        {
            channels = (int) be16 (d + body); frames = be32 (d + body + 2); bits = (int) be16 (d + body + 6); rate = extended (d + body + 8);
            if (aifc && avail >= 22) compression = tag (d + body + 18);
            haveComm = true;
        }
        else if (id == "SSND" && avail >= 8 && sound == nullptr)
        {
            const size_t offset = be32 (d + body);
            if (8 + offset <= avail) { sound = d + body + 8 + offset; soundBytes = avail - 8 - offset; }
        }
        pos = body + size + (size & 1);                 // chunks are padded to an even size
        if (size > n) break;
    }
    if (! haveComm) { r.why = "THE AIFF HAS NO FORMAT CHUNK"; return r; }
    r.format = aifc ? "AIFC " + compression : "AIFF";
    if (sound == nullptr || soundBytes == 0) { r.why = "THE AIFF HAS NO AUDIO DATA"; return r; }
    if (channels < 1 || channels > 16 || rate < 1000.0 || rate > 1.0e6) { r.why = "THE AIFF HAS AN ODD FORMAT (" + std::to_string (channels) + " CHANNELS, " + std::to_string ((long) rate) + " HZ)"; return r; }

    enum Kind { BigInt, LittleInt, Float32, Float64, Unsigned8 } kind = BigInt;
    if (compression == "NONE" || compression == "twos" || compression == "in24" || compression == "in32") kind = BigInt;
    else if (compression == "sowt") kind = LittleInt;
    else if (compression == "fl32" || compression == "FL32") { kind = Float32; bits = 32; }
    else if (compression == "fl64" || compression == "FL64") { kind = Float64; bits = 64; }
    else if (compression == "raw ") { kind = Unsigned8; bits = 8; }
    else { r.why = "AN AIFF-C WITH A COMPRESSION THIS PLUGIN DOES NOT KNOW ('" + compression + "')"; return r; }
    if (kind == BigInt || kind == LittleInt) if (bits < 1 || bits > 32) { r.why = "UNSUPPORTED BIT DEPTH (" + std::to_string (bits) + ")"; return r; }

    const size_t bytes = (size_t) ((bits + 7) / 8);
    const size_t frameBytes = bytes * (size_t) channels;
    size_t available = soundBytes / frameBytes;
    if (frames > 0) available = std::min<size_t> (available, frames);
    available = std::min<size_t> (available, (size_t) (rate * maxSeconds));
    if (available < 16) { r.why = "THE AIFF HAS NO USABLE AUDIO"; return r; }

    r.mono.resize (available);
    for (size_t f = 0; f < available; ++f)
    {
        double sum = 0.0;
        for (int c = 0; c < channels; ++c)
        {
            const uint8_t* s = sound + f * frameBytes + (size_t) c * bytes;
            double v = 0.0;
            switch (kind)
            {
                case Float32: { uint32_t u = be32 (s); float x; std::memcpy (&x, &u, 4); v = x; break; }
                case Float64: { uint64_t u = ((uint64_t) be32 (s) << 32) | be32 (s + 4); double x; std::memcpy (&x, &u, 8); v = x; break; }
                case Unsigned8: v = ((int) s[0] - 128) / 128.0; break;
                default:
                {
                    int64_t x = 0;
                    if (kind == BigInt) for (size_t b = 0; b < bytes; ++b) x = (x << 8) | s[b];
                    else for (size_t b = bytes; b-- > 0;) x = (x << 8) | s[b];
                    const int shift = (int) (bytes * 8) - bits;                       // the sample is left-justified in its bytes
                    const int total = (int) (bytes * 8);
                    if (total < 64 && (x & ((int64_t) 1 << (total - 1)))) x -= ((int64_t) 1 << total);   // sign
                    (void) shift;
                    v = (double) x / (double) ((int64_t) 1 << (total - 1));
                    break;
                }
            }
            sum += v;
        }
        const double m = sum / channels;
        r.mono[f] = (float) (std::isfinite (m) ? std::clamp (m, -4.0, 4.0) : 0.0);
    }
    r.ok = true; r.sampleRate = rate; r.channels = channels; r.bits = bits;
    return r;
}
} // namespace amb
