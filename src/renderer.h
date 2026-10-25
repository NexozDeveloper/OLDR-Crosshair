#pragma once
#include <cstdint>
#include <vector>

#include "settings.h"

namespace oldr {

struct Bitmap {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
};

class Renderer {
public:
    const Bitmap& Compose(const Settings& s, double seconds, bool* changed = nullptr);

    static bool IsAnimated(const Settings& s) { return s.rainbow || s.pulse; }

    static void RenderTile(int iconIndex, int tilePx, float stroke, std::vector<uint8_t>& alpha);

private:
    struct GeoKey {
        int icon = -1, size = 0, thickness = 0, rotation = 0;
        bool outline = false; int outlineWidth = 0;
        bool glow = false; int glowRadius = 0;
        bool dot = false; int dotSize = 0;
        bool operator==(const GeoKey& o) const {
            return icon == o.icon && size == o.size && thickness == o.thickness &&
                   rotation == o.rotation && outline == o.outline && outlineWidth == o.outlineWidth &&
                   glow == o.glow && glowRadius == o.glowRadius && dot == o.dot && dotSize == o.dotSize;
        }
    };

    void BuildMasks(const Settings& s);

    GeoKey key_;
    Settings last_;
    bool haveLast_ = false;
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> main_, outline_, glow_, dot_;
    Bitmap bmp_;
};

} 
