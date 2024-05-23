// SAMWrapper.mm
#include <opencv2/opencv.hpp>
#import "SamWrapper.h"
#import "ImageConversionUtilities.h"
#include "sam.h"

@implementation SAMWrapper

- (UIImage *)processImage:(UIImage *)image {
    cv::Mat cvImage = [ImageConversionUtilities cvMatFromUIImage:image];

    if (cvImage.empty()) {
        NSLog(@"The source image is empty.");
        return nil;
    }

    Sam sam;

    NSString *encoderPath = [[NSBundle mainBundle] pathForResource:@"mobile_sam_preprocess" ofType:@"onnx"];
    NSString *decoderPath = [[NSBundle mainBundle] pathForResource:@"mobile_sam" ofType:@"onnx"];
    
    if (!encoderPath || !decoderPath) {
        NSLog(@"Failed to find model files!");
        return nil;
    }

    std::string encoderPathStr = [encoderPath UTF8String];
    std::string decoderPathStr = [decoderPath UTF8String];

    if (!sam.loadModel(encoderPathStr, decoderPathStr, 1, "cpu")) {
        NSLog(@"Failed to load models with paths: %@, %@", encoderPath, decoderPath);
        return nil;
    }

    cv::Size inputSize = sam.getInputSize();
    if (inputSize.width <= 0 || inputSize.height <= 0) {
        NSLog(@"Invalid input size: %d x %d", inputSize.width, inputSize.height);
        return nil;
    }

    if (cvImage.size() != inputSize) {
        try {
            cv::resize(cvImage, cvImage, inputSize);
        } catch (const cv::Exception& e) {
            NSLog(@"Failed to resize image: %s", e.what());
            return nil;
        }
    }

    if (!sam.preprocessImage(cvImage)) {
        NSLog(@"Failed to preprocess image");
        return nil;
    }

    std::list<cv::Point> points;
    std::list<cv::Point> negativePoints;
    std::list<cv::Rect> rects;

    cv::Mat mask = sam.getMask(points, negativePoints, rects, -1, false);

    if (mask.empty()) {
        NSLog(@"Failed to generate mask from image.");
        return nil;
    }

    UIImage *resultImage = [ImageConversionUtilities UIImageFromCVMat:mask];
    if (!resultImage) {
        NSLog(@"Failed to convert cv::Mat back to UIImage.");
        return nil;
    }

    return resultImage;
}

@end
