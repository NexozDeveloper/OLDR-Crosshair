#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#include "lucide_icons.h"

namespace oldr {
namespace {

constexpr float kBox = 24.0f;
constexpr float kView = 36.0f;
constexpr float kMargin = 6.0f;

void RasterMask(const char* body, float stroke, float rotation, float scale, float tx, float ty,
                int cw, int ch, std::vector<uint8_t>& alpha) {
    static NSVGrasterizer* rast = nsvgCreateRasterizer();

    char head[512];
    std::snprintf(head, sizeof(head),
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%g\" height=\"%g\" "
                  "viewBox=\"%g %g %g %g\"><g transform=\"rotate(%.3f 12 12)\" fill=\"none\" "
                  "stroke=\"#FFFFFF\" stroke-width=\"%.3f\" stroke-linecap=\"round\" "
                  "stroke-linejoin=\"round\">",
                  kView, kView, -kMargin, -kMargin, kView, kView, rotation, stroke);
    std::string svg = head;
    svg += body;
    svg += "</g></svg>";

    std::vector<char> text(svg.begin(), svg.end());
    text.push_back('\0');

    alpha.assign(static_cast<size_t>(cw) * ch, 0);
    NSVGimage* image = nsvgParse(text.data(), "px", 96.0f);
    if (!image) return;

    std::vector<uint8_t> rgba(static_cast<size_t>(cw) * ch * 4);
    nsvgRasterize(rast, image, tx, ty, scale, rgba.data(), cw, ch, cw * 4);
    nsvgDelete(image);
    for (size_t i = 0; i < alpha.size(); ++i) alpha[i] = rgba[i * 4 + 3];
}

void BoxBlur(std::vector<uint8_t>& a, int w, int h, int radius) {
    if (radius < 1) return;
    const int r = std::max(1, static_cast<int>(radius / 1.7f + 0.5f));
    std::vector<float> tmp(a.size());
    std::vector<float> cur(a.begin(), a.end());
    const float inv = 1.0f / (2 * r + 1);
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) { 
            const float* src = &cur[static_cast<size_t>(y) * w];
            float* dst = &tmp[static_cast<size_t>(y) * w];
            float sum = 0;
            for (int x = 0; x <= r && x < w; ++x) sum += src[x];
            for (int x = 0; x < w; ++x) {
                dst[x] = sum * inv;
                const int add = x + r + 1, sub = x - r;
                sum += (add < w ? src[add] : 0.0f) - (sub >= 0 ? src[sub] : 0.0f);
            }
        }
        for (int x = 0; x < w; ++x) { 
            float sum = 0;
            for (int y = 0; y <= r && y < h; ++y) sum += tmp[static_cast<size_t>(y) * w + x];
            for (int y = 0; y < h; ++y) {
                cur[static_cast<size_t>(y) * w + x] = sum * inv;
                const int add = y + r + 1, sub = y - r;
                sum += (add < h ? tmp[static_cast<size_t>(add) * w + x] : 0.0f) -
                       (sub >= 0 ? tmp[static_cast<size_t>(sub) * w + x] : 0.0f);
            }
        }
    }
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = static_cast<uint8_t>(std::clamp(cur[i] + 0.5f, 0.0f, 255.0f));
}

uint32_t HueToRgb(double hueDeg) {
    double h = std::fmod(hueDeg, 360.0);
    if (h < 0) h += 360.0;
    const double c = 1.0, x = 1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0);
    double r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    return (static_cast<uint32_t>(r * 255 + 0.5) << 16) | (static_cast<uint32_t>(g * 255 + 0.5) << 8) |
           static_cast<uint32_t>(b * 255 + 0.5);
}

struct Px { uint32_t a = 0, r = 0, g = 0, b = 0; }; 

inline void Over(Px& d, uint8_t mask, uint32_t rgb, uint32_t globalAlpha) {
    if (!mask) return;
    const uint32_t sa = (mask * globalAlpha + 127) / 255;
    if (!sa) return;
    const uint32_t inv = 255 - sa;
    d.r = (((rgb >> 16) & 255) * sa + d.r * inv + 127) / 255;
    d.g = (((rgb >> 8) & 255) * sa + d.g * inv + 127) / 255;
    d.b = ((rgb & 255) * sa + d.b * inv + 127) / 255;
    d.a = sa + (d.a * inv + 127) / 255;
}

int DotIndex() {
    for (int i = 0; i < kIconCount; ++i)
        if (std::strcmp(kIcons[i].id, "dot") == 0) return i;
    return 0;
}

} 

void Renderer::RenderTile(int iconIndex, int tilePx, float stroke, std::vector<uint8_t>& alpha) {
    iconIndex = std::clamp(iconIndex, 0, kIconCount - 1);
    RasterMask(kIcons[iconIndex].body, stroke, 0.0f, tilePx / kView, 0.0f, 0.0f, tilePx, tilePx, alpha);
}

void Renderer::BuildMasks(const Settings& s) {
    const float scale = s.size / kBox; 
    const int inner = static_cast<int>(std::ceil(kView * scale));
    const int pad = s.glow ? s.glowRadius * 2 + 2 : 0;
    w_ = h_ = inner + pad * 2;

    const char* body = kIcons[std::clamp(s.icon, 0, kIconCount - 1)].body;
    const float stroke = s.thickness / 10.0f;
    const float ow = s.outlineWidth / 10.0f;
    const float rot = static_cast<float>(s.rotation);

    RasterMask(body, stroke, rot, scale, static_cast<float>(pad), static_cast<float>(pad), w_, h_, main_);

    const char* dotBody = kIcons[DotIndex()].body;
    const float dotScale = std::max(1, s.dotSize) / 4.0f;
    const float dotOrigin = w_ / 2.0f - (kMargin + 12.0f) * dotScale;
    if (s.dot) RasterMask(dotBody, 2.0f, 0.0f, dotScale, dotOrigin, dotOrigin, w_, h_, dot_);
    else dot_.clear();

    if (s.outline) {
        RasterMask(body, stroke + ow * 2.0f, rot, scale, static_cast<float>(pad), static_cast<float>(pad),
                   w_, h_, outline_);
        if (s.dot) {
            std::vector<uint8_t> dotOutline;
            RasterMask(dotBody, 2.0f + ow * 2.0f, 0.0f, dotScale, dotOrigin, dotOrigin, w_, h_, dotOutline);
            for (size_t i = 0; i < outline_.size(); ++i) outline_[i] = std::max(outline_[i], dotOutline[i]);
        }
    } else {
        outline_.clear();
    }

    if (s.glow) {
        glow_ = main_;
        const auto& under = outline_.empty() ? main_ : outline_;
        for (size_t i = 0; i < glow_.size(); ++i) glow_[i] = std::max(glow_[i], under[i]);
        if (!dot_.empty())
            for (size_t i = 0; i < glow_.size(); ++i) glow_[i] = std::max(glow_[i], dot_[i]);
        BoxBlur(glow_, w_, h_, s.glowRadius);
    } else {
        glow_.clear();
    }
}

const Bitmap& Renderer::Compose(const Settings& s, double seconds, bool* changed) {
    GeoKey k;
    k.icon = s.icon; k.size = s.size; k.thickness = s.thickness; k.rotation = s.rotation;
    k.outline = s.outline; k.outlineWidth = s.outline ? s.outlineWidth : 0;
    k.glow = s.glow; k.glowRadius = s.glow ? s.glowRadius : 0;
    k.dot = s.dot; k.dotSize = s.dot ? s.dotSize : 0;

    const bool rebuild = !(k == key_) || bmp_.px.empty();
    if (rebuild) { BuildMasks(s); key_ = k; }

    const bool animated = IsAnimated(s);
    const bool sameLook = haveLast_ && s.color == last_.color && s.outlineColor == last_.outlineColor &&
                          s.glowColor == last_.glowColor && s.dotColor == last_.dotColor &&
                          s.opacity == last_.opacity && s.glowStrength == last_.glowStrength &&
                          s.rainbow == last_.rainbow && s.pulse == last_.pulse;
    if (!rebuild && !animated && sameLook) {
        if (changed) *changed = false;
        return bmp_;
    }
    last_ = s; haveLast_ = true;
    if (changed) *changed = true;

    uint32_t mainRgb = s.color, glowRgb = s.glowColor;
    if (s.rainbow) mainRgb = glowRgb = HueToRgb(seconds * s.rainbowSpeed);

    double alphaF = s.opacity / 100.0;
    if (s.pulse) {
        const double wave = 0.5 + 0.5 * std::sin(seconds * 6.283185307 * (s.pulseSpeed / 100.0));
        alphaF *= 0.35 + 0.65 * wave;
    }
    const uint32_t ga = static_cast<uint32_t>(std::clamp(alphaF, 0.0, 1.0) * 255.0 + 0.5);
    const uint32_t glowGain = static_cast<uint32_t>(s.glowStrength);   // percent

    bmp_.w = w_; bmp_.h = h_;
    bmp_.px.assign(static_cast<size_t>(w_) * h_, 0);
    const bool hasGlow = !glow_.empty(), hasOutline = !outline_.empty(), hasDot = !dot_.empty();

    for (size_t i = 0; i < bmp_.px.size(); ++i) {
        Px p;
        if (hasGlow && glow_[i]) {
            const uint32_t m = std::min<uint32_t>(255, glow_[i] * glowGain / 100);
            Over(p, static_cast<uint8_t>(m), glowRgb, ga);
        }
        if (hasOutline) Over(p, outline_[i], s.outlineColor, ga);
        Over(p, main_[i], mainRgb, ga);
        if (hasDot) Over(p, dot_[i], s.dotColor, ga);
        bmp_.px[i] = (p.a << 24) | (p.r << 16) | (p.g << 8) | p.b;
    }
    return bmp_;
}

} 
