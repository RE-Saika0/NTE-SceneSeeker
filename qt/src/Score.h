#pragma once

#include <QPointF>
#include <QString>

// 计分与距离工具。参数集中放在这里，方便试玩后调整。
namespace Score {

constexpr double WORLD_PX = 26112.0;
// GeoGuessr-style exponential score curve:
// score = 5000 * exp(-10 * Distance / MaxDistance).
constexpr double K_DISTANCE = 10.0;
constexpr int MAX_SCORE_PER_ROUND = 5000;

// 评级阈值（按 5 回合满分 25000 计；回合数不足时会等比折算后再比较）
constexpr int RATING_S = 22500;
constexpr int RATING_A = 18000;
constexpr int RATING_B = 12000;

double mapDiagonalPx();

// 归一化坐标（0..1，左上原点）之间的像素距离与归一化距离。
double distancePixels(const QPointF &a, const QPointF &b);
double normalizedDistance(const QPointF &a, const QPointF &b);
// 直接以米为单位的距离，供计分与结算显示使用。
double distanceMeters(const QPointF &a, const QPointF &b);

int scoreForDistancePixels(double distancePixels);
int scoreForNormalizedDistance(double dNorm);
int scoreForDistanceWithMax(double distancePixels, double maxDistancePixels);
// 米制版本：score = 5000 * exp(-10 * distanceMeters / maxDistanceMeters)。
int scoreForDistanceMeters(double distanceMeters, double maxDistanceMeters);

// 26112 地图下 1px = 30.495 游戏单位 = 0.30495m。
double metersForPixels(double distancePixels);
QString formatDistance(double distancePixels);

// rounds 为实际回合数。不足 5 回合时把总分折算成 5 回合口径再评级。
QString ratingForTotal(int totalScore, int rounds = 5);

} // namespace Score
