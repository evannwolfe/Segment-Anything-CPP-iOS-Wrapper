#import "ImageConversionUtilities.h"
#import <Foundation/Foundation.h>
#import <opencv2/imgcodecs/ios.h>


@implementation ImageConversionUtilities

+ (cv::Mat)cvMatFromUIImage:(UIImage *)image {
    CGImageRef imageRef = image.CGImage;

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

    CGFloat cols = CGImageGetWidth(imageRef);
    CGFloat rows = CGImageGetHeight(imageRef);
    cv::Mat cvMat(rows, cols, CV_8UC4);

    CGContextRef contextRef = CGBitmapContextCreate(cvMat.data, cols, rows, 8, cvMat.step[0], colorSpace, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrderDefault);
    CGContextDrawImage(contextRef, CGRectMake(0, 0, cols, rows), imageRef);

    CGContextRelease(contextRef);
    CGColorSpaceRelease(colorSpace);

    cv::Mat cvMatBGR;
    cv::cvtColor(cvMat, cvMatBGR, cv::COLOR_RGBA2BGR);

    return cvMatBGR;
}

+ (UIImage *)UIImageFromCVMat:(cv::Mat)cvMat {
    if (cvMat.channels() == 3) {
        cv::cvtColor(cvMat, cvMat, cv::COLOR_BGR2GRAY);
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceGray();
    NSData *data = [NSData dataWithBytes:cvMat.data length:cvMat.elemSize() * cvMat.total()];
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
    
    CGImageRef imageRef = CGImageCreate(cvMat.cols, cvMat.rows, 8, 8, cvMat.step[0],
                                        colorSpace, kCGBitmapByteOrderDefault,
                                        provider, NULL, false, kCGRenderingIntentDefault);

    if (!imageRef) {
        NSLog(@"Failed to create CGImage");
        CGDataProviderRelease(provider);
        CGColorSpaceRelease(colorSpace);
        return nil;
    }

    UIImage *result = [UIImage imageWithCGImage:imageRef];
    CGImageRelease(imageRef);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
    return result;
}

@end

