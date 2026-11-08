#import "GBSceneView.h"
#include <algorithm>
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
bool isLinear(const ViewNode& node) {
    return isKind(node, "LinearLayout") || isKind(node, "TableLayout") || isKind(node, "TableRow");
}
} // namespace

@implementation GBSceneView {
    Scene _scene;
    NSMutableDictionary<NSNumber*, UIView*>* _views;
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
    if (isKind(node, "EditText")) {
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
        UILabel* label = [UILabel new];
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
    _scene = scene;
    for (const auto& [handle, node] : scene.views) {
        UIView* view = _views[@(handle)];
        if (!view) {
            view = [self createView:node];
            _views[@(handle)] = view;
        }
        view.hidden = !node.visible;
        view.userInteractionEnabled = node.enabled;
        view.backgroundColor = node.background ? color(node.background) : UIColor.clearColor;
        UIFont* font = [UIFont systemFontOfSize:std::clamp<CGFloat>(node.textSize, 1, 300)];
        if ([view isKindOfClass:UILabel.class]) {
            UILabel* label = (UILabel*)view;
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
        UIView* parent = _views[@(handle)];
        for (auto child : node.children) {
            UIView* childView = _views[@(child)];
            if (childView && childView.superview != parent)
                [parent addSubview:childView];
            attached.insert(child);
        }
    }
    for (NSNumber* handle in _views.allKeys) {
        if (!scene.views.contains(handle.unsignedLongLongValue)) {
            [_views[handle] removeFromSuperview];
            [_views removeObjectForKey:handle];
        } else if (!attached.contains(handle.unsignedLongLongValue) &&
                   handle.unsignedLongLongValue != scene.root)
            [_views[handle] removeFromSuperview];
    }
    UIView* root = _views[@(scene.root)];
    if (root && root.superview != self)
        [self addSubview:root];
    [self setNeedsLayout];
}
- (CGSize)measure:(uint64_t)handle available:(CGSize)available depth:(NSUInteger)depth {
    if (depth > 256 || !_scene.views.contains(handle))
        return CGSizeZero;
    const auto& node = _scene.views.at(handle);
    if (!node.visible)
        return CGSizeZero;
    const CGFloat width = node.width >= 0 ? node.width : std::max<CGFloat>(0, available.width);
    const CGSize inner =
        CGSizeMake(std::max<CGFloat>(0, width - node.padding.left - node.padding.right),
                   std::max<CGFloat>(0, available.height - node.padding.top - node.padding.bottom));
    CGSize size = CGSizeZero;
    if (!node.children.empty()) {
        for (auto child : node.children) {
            CGSize measured = [self measure:child available:inner depth:depth + 1];
            if (isLinear(node)) {
                if (node.orientation == 1) {
                    size.height += measured.height;
                    size.width = std::max(size.width, measured.width);
                } else {
                    size.width += measured.width;
                    size.height = std::max(size.height, measured.height);
                }
            } else {
                size.width = std::max(size.width, measured.width);
                size.height = std::max(size.height, measured.height);
            }
        }
        size.width += node.padding.left + node.padding.right;
        size.height += node.padding.top + node.padding.bottom;
    } else {
        UIView* view = _views[@(handle)];
        size = [view sizeThatFits:CGSizeMake(inner.width, CGFLOAT_MAX)];
        if ([view isKindOfClass:UITextField.class] || [view isKindOfClass:UIButton.class])
            size.height = std::max<CGFloat>(44, size.height);
        size.width += node.padding.left + node.padding.right;
        size.height += node.padding.top + node.padding.bottom;
    }
    if (node.width == -1)
        size.width = available.width;
    else if (node.width >= 0)
        size.width = node.width;
    if (node.height == -1)
        size.height = available.height;
    else if (node.height >= 0)
        size.height = node.height;
    size.width = std::min(std::max<CGFloat>(0, size.width), std::max<CGFloat>(0, available.width));
    size.height = std::max<CGFloat>(0, size.height);
    return size;
}
- (void)layoutNode:(uint64_t)handle frame:(CGRect)frame depth:(NSUInteger)depth {
    if (depth > 256 || !_scene.views.contains(handle))
        return;
    const auto& node = _scene.views.at(handle);
    UIView* view = _views[@(handle)];
    view.frame = frame;
    CGRect inner =
        UIEdgeInsetsInsetRect(view.bounds, UIEdgeInsetsMake(node.padding.top, node.padding.left,
                                                            node.padding.bottom, node.padding.right));
    inner.size.width = std::max<CGFloat>(0, inner.size.width);
    inner.size.height = std::max<CGFloat>(0, inner.size.height);
    const bool vertical = node.orientation == 1;
    CGFloat fixed = 0, totalWeight = 0;
    if (isLinear(node))
        for (auto child : node.children) {
            if (!_scene.views.contains(child) || !_scene.views.at(child).visible)
                continue;
            const auto& item = _scene.views.at(child);
            const auto measured = [self measure:child available:inner.size depth:depth + 1];
            fixed += vertical ? measured.height : measured.width;
            totalWeight += std::max(0.0f, item.weight);
        }
    CGFloat cursor = vertical ? inner.origin.y : inner.origin.x;
    const CGFloat remainder = std::max<CGFloat>(0, (vertical ? inner.size.height : inner.size.width) - fixed);
    CGSize scrollContent = CGSizeZero;
    for (auto child : node.children) {
        if (!_scene.views.contains(child) || !_scene.views.at(child).visible)
            continue;
        const auto& item = _scene.views.at(child);
        CGSize available = inner.size;
        if ([view isKindOfClass:UIScrollView.class])
            available.height = std::max<CGFloat>(inner.size.height, 10000);
        CGSize measured = [self measure:child available:available depth:depth + 1];
        CGRect childFrame = CGRectMake(inner.origin.x, inner.origin.y, measured.width, measured.height);
        if (isLinear(node)) {
            if (totalWeight > 0 && item.weight > 0) {
                if (vertical)
                    childFrame.size.height += remainder * item.weight / totalWeight;
                else
                    childFrame.size.width += remainder * item.weight / totalWeight;
            }
            if (vertical) {
                childFrame.origin.y = cursor;
                cursor += childFrame.size.height;
            } else {
                childFrame.origin.x = cursor;
                cursor += childFrame.size.width;
            }
        }
        [self layoutNode:child frame:childFrame depth:depth + 1];
        scrollContent.width = std::max(scrollContent.width, CGRectGetMaxX(childFrame));
        scrollContent.height = std::max(scrollContent.height, CGRectGetMaxY(childFrame));
    }
    if ([view isKindOfClass:UIScrollView.class])
        ((UIScrollView*)view).contentSize = scrollContent;
}
- (void)layoutSubviews {
    [super layoutSubviews];
    if (_scene.root)
        [self layoutNode:_scene.root frame:self.bounds depth:0];
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
