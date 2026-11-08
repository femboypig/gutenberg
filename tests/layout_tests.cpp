#include "gutenberg/layout.hpp"
#include "test.hpp"
#include <cmath>

using namespace gutenberg;
namespace {
ViewNode make(std::uint64_t id, std::string kind, float width = -2, float height = -2) {
    ViewNode node;
    node.handle = id;
    node.kind = "Landroid/widget/" + kind + ";";
    node.width = width;
    node.height = height;
    return node;
}
Size intrinsic(const ViewNode& node, Size maximum) {
    if (node.kind == "Landroid/widget/EditText;")
        return {std::min(64.0f, maximum.width), 44};
    return {std::min(80.0f, maximum.width), 20};
}
} // namespace

TEST(layout_shrinks_weighted_match_parent_children) {
    Scene scene;
    scene.root = 1;
    scene.views[1] = make(1, "LinearLayout", -1, -1);
    scene.views[1].children = {2, 3};
    for (auto id : {2U, 3U}) {
        scene.views[id] = make(id, "EditText", -1);
        scene.views[id].weight = 1;
    }
    const auto layout = layoutScene(scene, {320, 600}, intrinsic);
    CHECK(layout.at(2).frame.width == 160);
    CHECK(layout.at(3).frame.x == 160);
    CHECK(layout.at(3).frame.x + layout.at(3).frame.width <= 320);
}

TEST(layout_scroll_wrap_does_not_expand_match_parent_to_infinity) {
    Scene scene;
    scene.root = 1;
    scene.views[1] = make(1, "LinearLayout", -1, -1);
    scene.views[1].orientation = 1;
    scene.views[1].children = {2, 3};
    scene.views[2] = make(2, "Button", -1, 44);
    scene.views[3] = make(3, "ScrollView", -1);
    scene.views[3].children = {4};
    scene.views[4] = make(4, "LinearLayout", -1, -1);
    scene.views[4].orientation = 1;
    scene.views[4].children = {5};
    scene.views[5] = make(5, "TextView", -2, -1);
    scene.views[5].padding = {10, 10, 10, 10};
    scene.views[5].layoutGravity = 17;
    const auto layout = layoutScene(scene, {320, 600}, intrinsic);
    CHECK(layout.at(3).frame.height == 40);
    CHECK(layout.at(4).frame.height == 40);
    CHECK(layout.at(5).frame.height == 40);
    CHECK(layout.at(5).frame.x == 110);
    CHECK(layout.at(3).frame.y == 44);
}

TEST(layout_table_columns_align_across_rows) {
    Scene scene;
    scene.root = 1;
    scene.views[1] = make(1, "TableLayout", -1, -1);
    scene.views[1].children = {2, 3};
    for (auto id : {2U, 3U})
        scene.views[id] = make(id, "TableRow");
    scene.views[2].children = {4, 5};
    scene.views[3].children = {6, 7};
    for (auto id : {4U, 5U, 6U, 7U}) {
        scene.views[id] = make(id, id < 6 ? "TextView" : "EditText", id < 6 ? -2 : -1);
        scene.views[id].weight = 1;
    }
    const auto layout = layoutScene(scene, {320, 600}, intrinsic);
    CHECK(layout.at(4).frame.width == layout.at(6).frame.width);
    CHECK(layout.at(5).frame.x == layout.at(7).frame.x);
    CHECK(layout.at(7).frame.x + layout.at(7).frame.width <= 320);
}

TEST(layout_rejects_cycles_and_non_finite_geometry) {
    Scene scene;
    scene.root = 1;
    scene.views[1] = make(1, "LinearLayout");
    scene.views[1].children = {1};
    test::rejects([&] { (void)layoutScene(scene, {320, 600}, intrinsic); });
    scene.views[1].children.clear();
    scene.views[1].textSize = std::numeric_limits<float>::quiet_NaN();
    test::rejects([&] { (void)layoutScene(scene, {320, 600}, intrinsic); });
}

TEST(layout_handles_deep_unweighted_hierarchies_linearly) {
    Scene scene;
    scene.root = 1;
    for (unsigned n = 1; n <= 40; ++n) {
        scene.views[n] = make(n, "LinearLayout", -1, -2);
        scene.views[n].orientation = 1;
        if (n < 40)
            scene.views[n].children = {n + 1};
    }
    const auto layout = layoutScene(scene, {320, 600}, intrinsic);
    CHECK(layout.size() == 40);
}
