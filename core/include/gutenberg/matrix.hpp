#pragma once
#include <array>
#include <optional>
#include <span>

namespace gutenberg {
struct Matrix3 {
    std::array<float, 9> values{1, 0, 0, 0, 1, 0, 0, 0, 1};
    [[nodiscard]] static Matrix3 translation(float x, float y);
    [[nodiscard]] static Matrix3 scaling(float x, float y);
    [[nodiscard]] static Matrix3 rotation(float degrees);
    [[nodiscard]] Matrix3 operator*(const Matrix3& other) const;
    [[nodiscard]] std::optional<Matrix3> inverted() const;
    [[nodiscard]] std::array<float, 2> map(float x, float y) const;
    [[nodiscard]] std::array<float, 2> mapVector(float x, float y) const;
    [[nodiscard]] bool affine() const;
    [[nodiscard]] bool rectStaysRect() const;
    [[nodiscard]] static std::optional<Matrix3> fitRect(std::array<float, 4> source,
                                                        std::array<float, 4> destination, unsigned mode);
    [[nodiscard]] static std::optional<Matrix3> fitPoints(std::span<const float> source,
                                                          std::span<const float> destination);
};
} // namespace gutenberg
