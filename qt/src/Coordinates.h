#pragma once

#include <QPointF>
#include <algorithm>

// 游戏世界坐标 <-> 26112 地图归一化坐标。
// 系数与 tools/annotator/coords.py 保持一致（frame map-2026-08）。
// Qt 端目前题库优先使用 x/y；gameX/gameY 是唯一真值，x/y 缺失时用这里换算。
namespace Coordinates {

constexpr double CAL_SOURCE = 13056.0;
constexpr double MAP_SIZE = 26112.0;

// game -> 13056 map pixels
constexpr double FWD_A = 0.016395909231800825;
constexpr double FWD_B = -4.298132834831891e-07;
constexpr double FWD_C = 6526.606932498549;
constexpr double FWD_D = -1.733694768928453e-06;
constexpr double FWD_E = 0.01639099064449048;
constexpr double FWD_F = 5210.986200917285;

// 13056 map pixels -> game
constexpr double INV_A = 60.99082329836263;
constexpr double INV_B = 0.0015993338409368073;
constexpr double INV_C = -398071.46426446334;
constexpr double INV_D = 0.006451072640966531;
constexpr double INV_E = 61.00912534587333;
constexpr double INV_F = -317959.8139227994;

inline QPointF gameToNormalized(double gameX, double gameY)
{
    const double mx = FWD_A * gameX + FWD_B * gameY + FWD_C;
    const double my = FWD_D * gameX + FWD_E * gameY + FWD_F;
    return QPointF(mx / CAL_SOURCE, my / CAL_SOURCE);
}

inline QPointF normalizedToGame(double x, double y)
{
    const double mx = x * CAL_SOURCE;
    const double my = y * CAL_SOURCE;
    return QPointF(INV_A * mx + INV_B * my + INV_C,
                   INV_D * mx + INV_E * my + INV_F);
}

inline QPointF clampNormalized(const QPointF &p)
{
    return QPointF(std::clamp(p.x(), 0.0, 1.0), std::clamp(p.y(), 0.0, 1.0));
}

} // namespace Coordinates
