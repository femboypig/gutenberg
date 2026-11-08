import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let output = URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true)
let icon = output.appendingPathComponent("AppIcon.appiconset",isDirectory:true)
try FileManager.default.createDirectory(at:icon,withIntermediateDirectories:true)

func drawIcon(size:Int,to file:URL) throws {
    let space = CGColorSpaceCreateDeviceRGB()
    guard let context = CGContext(data:nil,width:size,height:size,bitsPerComponent:8,bytesPerRow:size * 4,space:space,bitmapInfo:CGImageAlphaInfo.noneSkipLast.rawValue) else {
        throw NSError(domain:"GutenbergIcon",code:1)
    }
    context.scaleBy(x:CGFloat(size) / 1024,y:CGFloat(size) / 1024)
    context.setFillColor(CGColor(red:0.055,green:0.085,blue:0.09,alpha:1))
    context.fill(CGRect(x:0,y:0,width:1024,height:1024))
    let colors = [CGColor(red:0.09,green:0.23,blue:0.20,alpha:1),CGColor(red:0.035,green:0.065,blue:0.07,alpha:1)] as CFArray
    let gradient = CGGradient(colorsSpace:space,colors:colors,locations:[0,1])!
    context.drawLinearGradient(gradient,start:CGPoint(x:0,y:1024),end:CGPoint(x:1024,y:0),options:[])

    context.setShadow(offset:CGSize(width:0,height:-16),blur:40,color:CGColor(gray:0,alpha:0.25))
    let left = CGMutablePath()
    left.move(to:CGPoint(x:222,y:280))
    left.addLine(to:CGPoint(x:222,y:735))
    left.addCurve(to:CGPoint(x:494,y:680),control1:CGPoint(x:337,y:765),control2:CGPoint(x:400,y:732))
    left.addLine(to:CGPoint(x:494,y:230))
    left.addCurve(to:CGPoint(x:222,y:280),control1:CGPoint(x:394,y:288),control2:CGPoint(x:332,y:305))
    context.setFillColor(CGColor(red:0.92,green:0.97,blue:0.94,alpha:1)); context.addPath(left); context.fillPath()
    let right = CGMutablePath()
    right.move(to:CGPoint(x:530,y:230))
    right.addLine(to:CGPoint(x:530,y:680))
    right.addCurve(to:CGPoint(x:802,y:735),control1:CGPoint(x:624,y:732),control2:CGPoint(x:687,y:765))
    right.addLine(to:CGPoint(x:802,y:280))
    right.addCurve(to:CGPoint(x:530,y:230),control1:CGPoint(x:692,y:305),control2:CGPoint(x:630,y:288))
    context.setFillColor(CGColor(red:0.32,green:0.87,blue:0.66,alpha:1)); context.addPath(right); context.fillPath()
    context.setShadow(offset:.zero,blur:0,color:nil)
    context.setStrokeColor(CGColor(red:0.055,green:0.085,blue:0.09,alpha:0.6)); context.setLineWidth(19); context.setLineCap(.round)
    for y in [CGFloat(580), CGFloat(495), CGFloat(410)] {
        context.move(to:CGPoint(x:590,y:y)); context.addQuadCurve(to:CGPoint(x:738,y:y + 27),control:CGPoint(x:667,y:y + 35)); context.strokePath()
    }
    guard let image = context.makeImage(), let destination = CGImageDestinationCreateWithURL(file as CFURL,UTType.png.identifier as CFString,1,nil) else {
        throw NSError(domain:"GutenbergIcon",code:2)
    }
    CGImageDestinationAddImage(destination,image,nil)
    if !CGImageDestinationFinalize(destination) { throw NSError(domain:"GutenbergIcon",code:3) }
}

var images:[[String:String]] = []
let specifications:[(String,Double,[Int])] = [
    ("iphone",20,[2,3]),("iphone",29,[2,3]),("iphone",40,[2,3]),("iphone",60,[2,3]),
    ("ipad",20,[1,2]),("ipad",29,[1,2]),("ipad",40,[1,2]),("ipad",76,[1,2]),("ipad",83.5,[2]),("ios-marketing",1024,[1])
]
var written = Set<Int>()
for (idiom,points,scales) in specifications {
    for scale in scales {
        let pixels = Int(points * Double(scale))
        let filename = "icon-\(pixels).png"
        if written.insert(pixels).inserted { try drawIcon(size:pixels,to:icon.appendingPathComponent(filename)) }
        let dimension = points.rounded() == points ? String(Int(points)) : String(points)
        images.append(["idiom":idiom,"size":"\(dimension)x\(dimension)","scale":"\(scale)x","filename":filename])
    }
}
let contents:[String:Any] = ["images":images,"info":["author":"gutenberg","version":1]]
try JSONSerialization.data(withJSONObject:contents,options:[.prettyPrinted,.sortedKeys]).write(to:icon.appendingPathComponent("Contents.json"))
try JSONSerialization.data(withJSONObject:["info":["author":"gutenberg","version":1]],options:[.prettyPrinted,.sortedKeys]).write(to:output.appendingPathComponent("Contents.json"))
