#include "gutenberg/matrix.hpp"
#include "test.hpp"
#include <cmath>
#include <limits>

using namespace gutenberg;
namespace {
bool near(float a, float b) {
    return std::abs(a - b) < 0.0001f;
}
void point(std::array<float, 2> actual, float x, float y) {
    CHECK(near(actual[0], x) && near(actual[1], y));
}
} // namespace

TEST(matrix_composition_inversion_and_axis_alignment) {
    const auto matrix = Matrix3::translation(4, 8) * Matrix3::rotation(90) * Matrix3::scaling(2, 3);
    point(matrix.map(1, 2), -2, 10);
    point(matrix.mapVector(1, 2), -6, 2);
    CHECK(matrix.rectStaysRect());
    CHECK(matrix.inverted().has_value());
    const auto identity = *matrix.inverted() * matrix;
    for (unsigned n = 0; n < 9; ++n)
        CHECK(near(identity.values[n], Matrix3{}.values[n]));
    CHECK(!Matrix3::scaling(0, 1).inverted());
    CHECK(!Matrix3::rotation(45).rectStaysRect());
    CHECK(!Matrix3::scaling(std::numeric_limits<float>::infinity(), 1).inverted());
}

TEST(matrix_fits_rectangles_and_solves_affine_and_projective_correspondences) {
    const auto fit = Matrix3::fitRect({10, 20, 30, 30}, {0, 0, 100, 100}, 2);
    CHECK(fit.has_value());
    point(fit->map(10, 20), 0, 25);
    point(fit->map(30, 30), 100, 75);
    CHECK(!Matrix3::fitRect({0, 0, 0, 1}, {0, 0, 1, 1}, 0));
    const std::array<float, 6> triangle{0, 0, 1, 0, 0, 1}, skewed{3, 4, 5, 5, 4, 7};
    const auto affine = Matrix3::fitPoints(triangle, skewed);
    CHECK(affine && affine->affine());
    point(affine->map(0.5f, 0.5f), 4.5f, 6);
    const std::array<float, 8> square{0, 0, 1, 0, 1, 1, 0, 1}, trapezoid{0, 0, 2, 0, 1.5f, 1, 0.5f, 1};
    const auto perspective = Matrix3::fitPoints(square, trapezoid);
    CHECK(perspective && !perspective->affine());
    for (unsigned n = 0; n < square.size(); n += 2)
        point(perspective->map(square[n], square[n + 1]), trapezoid[n], trapezoid[n + 1]);
    CHECK(perspective->inverted().has_value());
    point(perspective->inverted()->map(1, 0.5f), 0.5f, 1.f / 3);
    const std::array<float, 6> collinear{0, 0, 1, 0, 2, 0};
    CHECK(!Matrix3::fitPoints(collinear, skewed));
}
