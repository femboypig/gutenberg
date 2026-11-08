#import "GBSceneView.h"
#import "GBDrawingView.h"
#include "gutenberg/layout.hpp"
#include <algorithm>
#include <cmath>
#include <set>

using namespace gutenberg;
namespace {
UIColor* color(uint32_t argb) {
    return [UIColor colorWithRed:((argb >> 16) & 255) / 255.0
                           green:((argb >> 8) & 255) / 255.0
                            blue:(argb & 255) / 255.0
                           alpha:(argb >> 24) / 255.0];
}
NSString* text(const std::string& value) {
    return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding]
               ?: @"";
}
bool isKind(const ViewNode& node, std::string_view name) {
    return node.kind == "Landroid/widget/" + std::string(name) + ";";
}
} // namespace

@interface GBTextLabel : UILabel
@property(nonatomic) UIEdgeInsets textInsets;
@property(nonatomic) NSInteger verticalGravity;
@end
@implementation GBTextLabel
- (void)drawTextInRect:(CGRect)rect {
    CGRect inner = UIEdgeInsetsInsetRect(rect, self.textInsets);
    if ((self.verticalGravity & 0x70) != 0x10) {
        CGFloat height =
            [super textRectForBounds:inner limitedToNumberOfLines:self.numberOfLines].size.height;
        if ((self.verticalGravity & 0x70) == 0x50)
            inner.origin.y += MAX(0, inner.size.height - height);
        inner.size.height = height;
    }
    [super drawTextInRect:inner];
}
@end

@implementation GBSceneView {
    Scene _scene;
    NSMutableDictionary<NSNumber*, UIView*>* _views;
    std::map<uint64_t, std::pair<float, float>> _sentSizes;
}
- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _views = [NSMutableDictionary dictionary];
        self.backgroundColor = UIColor.systemBackgroundColor;
        self.clipsToBounds = YES;
    }
    return self;
}
- (UIView*)createView:(const ViewNode&)node {
    UIView* view;
    if (node.customDrawing) {
        GBDrawingView* drawing = [GBDrawingView new];
        __weak GBSceneView* weakSelf = self;
        const auto handle = node.handle;
        drawing.onTouch = ^(int action, float x, float y, float pressure, int64_t timeMs) {
            GBSceneView* owner = weakSelf;
            if (owner.onTouch)
                owner.onTouch(handle, action, x, y, pressure, timeMs);
        };
        view = drawing;
    } else if (isKind(node, "EditText")) {
        UITextField* field = [UITextField new];
        field.borderStyle = UITextBorderStyleRoundedRect;
        field.accessibilityIdentifier = @"guest.input";
        [field addTarget:self action:@selector(textChanged:) forControlEvents:UIControlEventEditingChanged];
        view = field;
    } else if (isKind(node, "Switch") || isKind(node, "CheckBox") || isKind(node, "ToggleButton") ||
               isKind(node, "RadioButton")) {
        UISwitch* toggle = [UISwitch new];
        [toggle addTarget:self action:@selector(checkedChanged:) forControlEvents:UIControlEventValueChanged];
        view = toggle;
    } else if (isKind(node, "Button") || isKind(node, "ImageButton")) {
        UIButton* button = [UIButton buttonWithType:UIButtonTypeSystem];
        button.configuration = UIButtonConfiguration.borderedTintedButtonConfiguration;
        [button addTarget:self action:@selector(clicked:) forControlEvents:UIControlEventTouchUpInside];
        view = button;
    } else if (isKind(node, "TextView")) {
        UILabel* label = [GBTextLabel new];
        label.numberOfLines = 0;
        view = label;
    } else if (isKind(node, "ScrollView") || isKind(node, "HorizontalScrollView")) {
        UIScrollView* scroll = [UIScrollView new];
        scroll.keyboardDismissMode = UIScrollViewKeyboardDismissModeInteractive;
        view = scroll;
    } else if (isKind(node, "ProgressBar")) {
        UIActivityIndicatorView* indicator = [[UIActivityIndicatorView alloc]
            initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
        [indicator startAnimating];
        view = indicator;
    } else if (isKind(node, "ImageView"))
        view = [UIImageView new];
    else {
        view = [UIView new];
        UITapGestureRecognizer* tap = [[UITapGestureRecognizer alloc] initWithTarget:self
                                                                              action:@selector(tapped:)];
        [view addGestureRecognizer:tap];
    }
    view.tag = static_cast<NSInteger>(node.handle);
    view.accessibilityIdentifier = [NSString stringWithFormat:@"guest.view.%d", node.androidId];
    return view;
}
- (void)displayScene:(const Scene&)scene {
    std::set<uint64_t> reachable;
    try {
        reachable = validateScene(scene);
    } catch (const std::exception& error) {
        if (self.onLayoutError)
            self.onLayoutError(@(error.what()));
        return;
    }
    _scene = scene;
    for (const auto& [handle, node] : scene.views) {
        if (!reachable.contains(handle))
            continue;
        UIView* view = _views[@(handle)];
        if (!view) {
            view = [self createView:node];
            _views[@(handle)] = view;
        }
        view.hidden = !node.visible;
        view.userInteractionEnabled = node.enabled;
        view.backgroundColor = node.background ? color(node.background) : UIColor.clearColor;
        if ([view isKindOfClass:GBDrawingView.class])
            [(GBDrawingView*)view setDisplayList:node.drawing];
        UIFont* font = [UIFont systemFontOfSize:std::clamp<CGFloat>(node.textSize, 1, 300)];
        if ([view isKindOfClass:UILabel.class]) {
            UILabel* label = (UILabel*)view;
            ((GBTextLabel*)label).textInsets = UIEdgeInsetsMake(node.padding.top, node.padding.left,
                                                                node.padding.bottom, node.padding.right);
            ((GBTextLabel*)label).verticalGravity = node.gravity;
            label.text = text(node.text);
            label.font = font;
            label.textColor = color(node.textColor);
            label.textAlignment = (node.gravity & 7) == 1   ? NSTextAlignmentCenter
                                  : (node.gravity & 7) == 5 ? NSTextAlignmentRight
                                                            : NSTextAlignmentLeft;
        } else if ([view isKindOfClass:UIButton.class]) {
            UIButton* button = (UIButton*)view;
            [button setTitle:text(node.text) forState:UIControlStateNormal];
            button.titleLabel.font = font;
            button.enabled = node.enabled;
        } else if ([view isKindOfClass:UITextField.class]) {
            UITextField* field = (UITextField*)view;
            if (!field.isFirstResponder)
                field.text = text(node.text);
            field.placeholder = text(node.hint);
            field.font = font;
            field.textColor = color(node.textColor);
            field.enabled = node.enabled;
        } else if ([view isKindOfClass:UISwitch.class]) {
            UISwitch* toggle = (UISwitch*)view;
            toggle.on = node.checked;
            toggle.enabled = node.enabled;
            toggle.accessibilityLabel = text(node.text);
        }
    }
    std::set<uint64_t> attached;
    for (const auto& [handle, node] : scene.views) {
        if (!reachable.contains(handle))
            continue;
        UIView* parent = _views[@(handle)];
        for (auto child : node.children) {
            UIView* childView = _views[@(child)];
            if (childView && childView.superview != parent)
                [parent addSubview:childView];
            attached.insert(child);
        }
    }
    for (NSNumber* handle in _views.allKeys) {
        if (!reachable.contains(handle.unsignedLongLongValue)) {
            [_views[handle] removeFromSuperview];
            [_views removeObjectForKey:handle];
            _sentSizes.erase(handle.unsignedLongLongValue);
        } else if (!attached.contains(handle.unsignedLongLongValue) &&
                   handle.unsignedLongLongValue != scene.root)
            [_views[handle] removeFromSuperview];
    }
    UIView* root = _views[@(scene.root)];
    if (root && root.superview != self)
        [self addSubview:root];
    [self setNeedsLayout];
}
- (void)layoutSubviews {
    [super layoutSubviews];
    if (!_scene.root)
        return;
    try {
        auto layout = layoutScene(
            _scene, {static_cast<float>(self.bounds.size.width), static_cast<float>(self.bounds.size.height)},
            [self](const ViewNode& node, gutenberg::Size maximum) {
                UIView* view = self->_views[@(node.handle)];
                CGSize limit = CGSizeMake(std::isfinite(maximum.width) ? maximum.width : CGFLOAT_MAX,
                                          std::isfinite(maximum.height) ? maximum.height : CGFLOAT_MAX);
                CGSize size = CGSizeZero;
                if ([view isKindOfClass:UILabel.class] || [view isKindOfClass:UIButton.class] ||
                    [view isKindOfClass:UITextField.class] || [view isKindOfClass:UISwitch.class] ||
                    [view isKindOfClass:UIImageView.class])
                    size = [view sizeThatFits:limit];
                if ([view isKindOfClass:UITextField.class] || [view isKindOfClass:UIButton.class])
                    size.height = MAX(44, size.height);
                if ([view isKindOfClass:UITextField.class])
                    size.width = MAX(64, size.width);
                return gutenberg::Size{static_cast<float>(size.width), static_cast<float>(size.height)};
            });
        std::vector<ViewSize> sizes;
        for (const auto& [handle, box] : layout) {
            UIView* view = _views[@(handle)];
            view.frame = CGRectMake(box.frame.x, box.frame.y, box.frame.width, box.frame.height);
            if ([view isKindOfClass:UIScrollView.class])
                ((UIScrollView*)view).contentSize = CGSizeMake(box.content.width, box.content.height);
            const auto size = std::pair{std::floor(box.frame.width), std::floor(box.frame.height)};
            if (!_sentSizes.contains(handle) || _sentSizes.at(handle) != size) {
                _sentSizes[handle] = size;
                sizes.push_back({handle, size.first, size.second});
            }
        }
        if (!sizes.empty() && self.onGeometry)
            self.onGeometry(sizes);
    } catch (const std::exception& error) {
        _scene.root = 0;
        if (self.onLayoutError)
            self.onLayoutError(@(error.what()));
    }
}
- (void)clicked:(UIControl*)sender {
    if (self.onClick)
        self.onClick(static_cast<uint64_t>(sender.tag));
}
- (void)tapped:(UITapGestureRecognizer*)sender {
    if (self.onClick)
        self.onClick(static_cast<uint64_t>(sender.view.tag));
}
- (void)textChanged:(UITextField*)sender {
    if (self.onTextChanged)
        self.onTextChanged(static_cast<uint64_t>(sender.tag), sender.text ?: @"");
}
- (void)checkedChanged:(UISwitch*)sender {
    if (self.onCheckedChanged)
        self.onCheckedChanged(static_cast<uint64_t>(sender.tag), sender.on);
}
@end
