#include "gutenberg/graphics.hpp"

namespace gutenberg {
bool applyTransform(Matrix3& matrix, const DrawCommand& command) {
    const auto& p = command.coordinates;
    Matrix3 transform;
    switch (command.operation) {
    case DrawOperation::translate:
        transform = Matrix3::translation(p[0], p[1]);
        break;
    case DrawOperation::scale:
        transform = Matrix3::scaling(p[0], p[1]);
        break;
    case DrawOperation::rotate:
        transform = Matrix3::rotation(p[0]);
        break;
    case DrawOperation::skew:
        transform = {{1, p[0], 0, p[1], 1, 0, 0, 0, 1}};
        break;
    case DrawOperation::concatMatrix:
    case DrawOperation::setMatrix:
        transform = {{p[0], p[1], p[2], p[3], p[4], p[5], 0, 0, 1}};
        if (command.operation == DrawOperation::setMatrix) {
            matrix = transform;
            return true;
        }
        break;
    default:
        return false;
    }
    matrix = matrix * transform;
    return true;
}
} // namespace gutenberg
