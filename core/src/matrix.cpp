#include "gutenberg/matrix.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace gutenberg {
Matrix3 Matrix3::translation(float x, float y) {
    return {{1, 0, x, 0, 1, y, 0, 0, 1}};
}
Matrix3 Matrix3::scaling(float x, float y) {
    return {{x, 0, 0, 0, y, 0, 0, 0, 1}};
}
Matrix3 Matrix3::rotation(float degrees) {
    float c, s;
    if (std::isfinite(degrees) && std::fmod(degrees, 90.f) == 0) {
        const auto quadrant = std::fmod(degrees / 90.f, 4.f);
        c = quadrant == 0 ? 1.f : std::abs(quadrant) == 2 ? -1.f : 0.f;
        s = quadrant == 1 || quadrant == -3 ? 1.f : quadrant == -1 || quadrant == 3 ? -1.f : 0.f;
    } else {
        const auto radians = static_cast<double>(degrees) * std::numbers::pi / 180;
        c = static_cast<float>(std::cos(radians));
        s = static_cast<float>(std::sin(radians));
    }
    return {{c, -s, 0, s, c, 0, 0, 0, 1}};
}
Matrix3 Matrix3::operator*(const Matrix3& other) const {
    Matrix3 result;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 3; ++col) {
            double sum = 0;
            for (unsigned k = 0; k < 3; ++k)
                sum += static_cast<double>(values[row * 3 + k]) * other.values[k * 3 + col];
            result.values[row * 3 + col] = static_cast<float>(sum);
        }
    return result;
}
std::optional<Matrix3> Matrix3::inverted() const {
    const auto& m = values;
    const double a = static_cast<double>(m[4]) * m[8] - static_cast<double>(m[5]) * m[7];
    const double b = static_cast<double>(m[5]) * m[6] - static_cast<double>(m[3]) * m[8];
    const double c = static_cast<double>(m[3]) * m[7] - static_cast<double>(m[4]) * m[6];
    const double determinant = m[0] * a + m[1] * b + m[2] * c;
    if (determinant == 0 || !std::isfinite(determinant))
        return {};
    const std::array<double, 9> cofactors{a,
                                          static_cast<double>(m[2]) * m[7] - static_cast<double>(m[1]) * m[8],
                                          static_cast<double>(m[1]) * m[5] - static_cast<double>(m[2]) * m[4],
                                          b,
                                          static_cast<double>(m[0]) * m[8] - static_cast<double>(m[2]) * m[6],
                                          static_cast<double>(m[2]) * m[3] - static_cast<double>(m[0]) * m[5],
                                          c,
                                          static_cast<double>(m[1]) * m[6] - static_cast<double>(m[0]) * m[7],
                                          static_cast<double>(m[0]) * m[4] -
                                              static_cast<double>(m[1]) * m[3]};
    Matrix3 result;
    for (unsigned n = 0; n < 9; ++n) {
        result.values[n] = static_cast<float>(cofactors[n] / determinant);
        if (!std::isfinite(result.values[n]))
            return {};
    }
    return result;
}
std::array<float, 2> Matrix3::map(float x, float y) const {
    const auto& m = values;
    const float w = m[6] * x + m[7] * y + m[8];
    const float scale = w == 0 ? 0 : 1 / w;
    return {(m[0] * x + m[1] * y + m[2]) * scale, (m[3] * x + m[4] * y + m[5]) * scale};
}
std::array<float, 2> Matrix3::mapVector(float x, float y) const {
    if (affine())
        return {values[0] * x + values[1] * y, values[3] * x + values[4] * y};
    const auto origin = map(0, 0), point = map(x, y);
    return {point[0] - origin[0], point[1] - origin[1]};
}
bool Matrix3::affine() const {
    return values[6] == 0 && values[7] == 0 && values[8] == 1;
}
bool Matrix3::rectStaysRect() const {
    return affine() && ((values[1] == 0 && values[3] == 0) || (values[0] == 0 && values[4] == 0));
}
std::optional<Matrix3> Matrix3::fitRect(std::array<float, 4> source, std::array<float, 4> destination,
                                        unsigned mode) {
    const auto sw = source[2] - source[0], sh = source[3] - source[1];
    if (!(sw > 0 && sh > 0) || mode > 3)
        return {};
    const auto dw = destination[2] - destination[0], dh = destination[3] - destination[1];
    if (!(dw > 0 && dh > 0))
        return scaling(0, 0);
    auto sx = dw / sw, sy = dh / sh;
    if (mode)
        sx = sy = std::min(sx, sy);
    const auto alignment = mode == 2 ? 0.5f : mode == 3 ? 1.f : 0.f;
    return translation(destination[0] + (dw - sw * sx) * alignment,
                       destination[1] + (dh - sh * sy) * alignment) *
           scaling(sx, sy) * translation(-source[0], -source[1]);
}
std::optional<Matrix3> Matrix3::fitPoints(std::span<const float> source, std::span<const float> destination) {
    if (source.size() != destination.size() || source.size() % 2 || source.size() > 8)
        return {};
    if (source.empty())
        return Matrix3{};
    if (source.size() == 2)
        return translation(destination[0] - source[0], destination[1] - source[1]);
    if (source.size() == 4) {
        const double sx = source[2] - source[0], sy = source[3] - source[1];
        const double dx = destination[2] - destination[0], dy = destination[3] - destination[1];
        const auto length = sx * sx + sy * sy;
        if (!(length > 0))
            return {};
        const auto real = static_cast<float>((sx * dx + sy * dy) / length);
        const auto imaginary = static_cast<float>((sx * dy - sy * dx) / length);
        return translation(destination[0], destination[1]) *
               Matrix3{{real, -imaginary, 0, imaginary, real, 0, 0, 0, 1}} *
               translation(-source[0], -source[1]);
    }
    std::array<std::array<double, 9>, 8> equations{};
    const auto count = source.size();
    for (std::size_t n = 0; n < count; n += 2) {
        const double x = source[n], y = source[n + 1], u = destination[n], v = destination[n + 1];
        equations[n] = {x, y, 1, 0, 0, 0, count == 8 ? -u * x : 0, count == 8 ? -u * y : 0, 0};
        equations[n + 1] = {0, 0, 0, x, y, 1, count == 8 ? -v * x : 0, count == 8 ? -v * y : 0, 0};
        equations[n][count] = u;
        equations[n + 1][count] = v;
    }
    for (std::size_t col = 0; col < count; ++col) {
        auto pivot = col;
        for (auto row = col + 1; row < count; ++row)
            if (std::abs(equations[row][col]) > std::abs(equations[pivot][col]))
                pivot = row;
        if (equations[pivot][col] == 0 || !std::isfinite(equations[pivot][col]))
            return {};
        std::swap(equations[col], equations[pivot]);
        const auto scale = equations[col][col];
        for (auto k = col; k <= count; ++k)
            equations[col][k] /= scale;
        for (std::size_t row = 0; row < count; ++row) {
            if (row == col)
                continue;
            const auto factor = equations[row][col];
            for (auto k = col; k <= count; ++k)
                equations[row][k] -= factor * equations[col][k];
        }
    }
    Matrix3 result;
    for (std::size_t n = 0; n < count; ++n) {
        result.values[n] = static_cast<float>(equations[n][count]);
        if (!std::isfinite(result.values[n]))
            return {};
    }
    return result;
}
} // namespace gutenberg
