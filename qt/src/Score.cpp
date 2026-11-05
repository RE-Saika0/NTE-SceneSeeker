#include "Score.h"

#include <QChar>
#include <cmath>
#include <algorithm>

namespace Score {

double mapDiagonalPx()
{
    static const double diagonal = std::sqrt(WORLD_PX * WORLD_PX + WORLD_PX * WORLD_PX);
    return diagonal;
}

double distancePixels(const QPointF &a, const QPointF &b)
{
    const double dx = (a.x() - b.x()) * WORLD_PX;
    const double dy = (a.y() - b.y()) * WORLD_PX;
    return std::sqrt(dx * dx + dy * dy);
}

double normalizedDistance(const QPointF &a, const QPointF &b)
{
    return distancePixels(a, b) / mapDiagonalPx();
}

double distanceMeters(const QPointF &a, const QPointF &b)
{
    return metersForPixels(distancePixels(a, b));
}

int scoreForDistancePixels(double distancePixels)
{
    if (!std::isfinite(distancePixels) || distancePixels < 0.0)
        return 0;
    const double dNorm = distancePixels / mapDiagonalPx();
    return scoreForNormalizedDistance(dNorm);
}

int scoreForNormalizedDistance(double dNorm)
{
    if (!std::isfinite(dNorm) || dNorm < 0.0)
        return 0;
    dNorm = std::clamp(dNorm, 0.0, 1.0);
    return static_cast<int>(std::lround(MAX_SCORE_PER_ROUND * std::exp(-K_DISTANCE * dNorm)));
}

int scoreForDistanceWithMax(double distancePixels, double maxDistancePixels)
{
    if (!std::isfinite(distancePixels) || !std::isfinite(maxDistancePixels)
        || distancePixels < 0.0 || maxDistancePixels <= 0.0)
        return 0;
    return scoreForNormalizedDistance(distancePixels / maxDistancePixels);
}

int scoreForDistanceMeters(double distanceMeters, double maxDistanceMeters)
{
    if (!std::isfinite(distanceMeters) || !std::isfinite(maxDistanceMeters)
        || distanceMeters < 0.0 || maxDistanceMeters <= 0.0)
        return 0;
    return scoreForNormalizedDistance(distanceMeters / maxDistanceMeters);
}

double metersForPixels(double distancePixels)
{
    return distancePixels * 0.30495;
}

QString formatDistance(double distancePixels)
{
    const double meters = metersForPixels(distancePixels);
    if (meters >= 1000.0)
        return QStringLiteral("%1 km").arg(meters / 1000.0, 0, 'f', 2);
    return QStringLiteral("%1 m").arg(meters, 0, 'f', 0);
}

QString ratingForTotal(int totalScore, int rounds)
{
    if (rounds <= 0)
        return QStringLiteral("C");
    const int scaled = static_cast<int>(std::lround(totalScore * 5.0 / rounds));
    if (scaled >= RATING_S)
        return QStringLiteral("S");
    if (scaled >= RATING_A)
        return QStringLiteral("A");
    if (scaled >= RATING_B)
        return QStringLiteral("B");
    return QStringLiteral("C");
}

} // namespace Score
