#include <opencv2/opencv.hpp>
#include "sam.h"

Sam::Sam(){}
Sam::~Sam(){
  if(loadingModel){
    return;
  }
  if(preprocessing){
    return;
  }
  clearLoadModel();
  clearPreviousMasks();
}

bool Sam::clearLoadModel(){
  try{
    Ort::Session* pre = sessionEncoder.release();
    Ort::Session* sam = sessionDecoder.release();
    delete pre;
    delete sam;
    inputShapeEncoder.resize(0);
    outputShapeEncoder.resize(0);
    intermShapePre.resize(0);
    outputTensorValuesEncoder.resize(0);
    intermTensorValuesPre.resize(0);
  }catch(Ort::Exception& e){
    return false;
  }
  return true;
}

void Sam::clearPreviousMasks(){
  previousMasks.resize(0);
}

void Sam::resizePreviousMasks(int previousMaskIdx){
  if(previousMasks.size() > previousMaskIdx + 1){
    previousMasks.resize(previousMaskIdx + 1);
  }
}

void Sam::terminatePreprocessing(){
  runOptionsEncoder.SetTerminate();
  terminating = true;
}

void Sam::changeMode(SamMode modeTo){
  mode = modeTo;
}

bool modelExists(const std::string& modelPath){
  std::ifstream f(modelPath);
  if (!f.good()) {
    return false;
  }
  return true;
}

bool Sam::loadModel(const std::string& encoderPath, const std::string& decoderPath, int threadsNumber, std::string device){
    try{
        loadingStart();
        if(!clearLoadModel()){
            loadingEnd();
            return false;
        }
        if(!modelExists(encoderPath) || !modelExists(decoderPath)){
            loadingEnd();
            return false;
        }
        for(int i = 0; i < 2; i++){
            auto& option = sessionOptions[i];
            option.SetIntraOpNumThreads(threadsNumber);
            option.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            if(device == "cpu"){
                continue;
            }
            if(device.substr(0, 5) == "cuda:"){
                int gpuDeviceId = std::stoi(device.substr(5));
                OrtCUDAProviderOptions options;
                options.device_id = gpuDeviceId;
                option.AppendExecutionProvider_CUDA(options);
            }
        }
        sessionEncoder = std::make_unique<Ort::Session>(env, encoderPath.c_str(), sessionOptions[0]);
        auto inputNamesEncoder = getInputNamesEncoder();
        auto outputNamesEncoder = getOutputNamesEncoder();
        if(sessionEncoder->GetInputCount() != inputNamesEncoder.size() || sessionEncoder->GetOutputCount() != outputNamesEncoder.size()){
            loadingEnd();
            return false;
        }
        sessionDecoder = std::make_unique<Ort::Session>(env, decoderPath.c_str(), sessionOptions[1]);
        auto inputNamesDecoder = getInputNamesDecoder();
        auto outputNamesDecoder = getOutputNamesDecoder();
        if(sessionDecoder->GetInputCount() != inputNamesDecoder.size() || sessionDecoder->GetOutputCount() != outputNamesDecoder.size()){
            loadingEnd();
            return false;
        }
        inputShapeEncoder = sessionEncoder->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        outputShapeEncoder = sessionEncoder->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if(mode == EfficientSAM){
            inputShapeEncoder = {1, 3, 1024, 1024};
            outputShapeEncoder = {1, 256, 64, 64};
        }
        if(inputShapeEncoder.size() != 4 || outputShapeEncoder.size() != 4){
            loadingEnd();
            return false;
        }
        if(mode == HQSAM){
            intermShapePre = sessionDecoder->GetInputTypeInfo(1).GetTensorTypeAndShapeInfo().GetShape();
            if (intermShapePre.size() != 5) {
                loadingEnd();
                return false;
            }
        }
    } catch(Ort::Exception& e){
        loadingEnd();
        return false;
    }
    if(terminating){
        loadingEnd();
        return false;
    }
    loadingEnd();
    return true;
}


void Sam::loadingStart(){
  loadingModel = true;
}

void Sam::loadingEnd(){
  loadingModel = false;
  terminating = false;
}

cv::Size Sam::getInputSize(){
  if (inputShapeEncoder.size() < 4) {
      std::cerr << "Error: inputShapeEncoder does not contain enough elements." << std::endl;
      return cv::Size();
  }
  return cv::Size((int)inputShapeEncoder[3], (int)inputShapeEncoder[2]);
}


bool Sam::preprocessImage(const cv::Mat& image){
  try{
    preprocessingStart();
    if(image.size() != cv::Size((int)inputShapeEncoder[3], (int)inputShapeEncoder[2])){
      preprocessingEnd();
      return false;
    }
    if(image.channels() != 3){
      preprocessingEnd();
      return false;
    }
    std::vector<float> inputTensorValuesFloat;
    std::vector<uint8_t> inputTensorValuesInt;
    bool isInputTensorFloat = (mode == EfficientSAM || mode == EdgeSAM);
    if(isInputTensorFloat){
      inputTensorValuesFloat.resize(inputShapeEncoder[0] * inputShapeEncoder[1] * inputShapeEncoder[2] * inputShapeEncoder[3]);
      for(int i = 0; i < inputShapeEncoder[2]; i++){
        for(int j = 0; j < inputShapeEncoder[3]; j++){
          int64_t pos = i * inputShapeEncoder[3] + j;
          int64_t size = inputShapeEncoder[2] * inputShapeEncoder[3];
          inputTensorValuesFloat[pos + size * 0] = image.at<cv::Vec3b>(i, j)[2] / 255.0;
          inputTensorValuesFloat[pos + size * 1] = image.at<cv::Vec3b>(i, j)[1] / 255.0;
          inputTensorValuesFloat[pos + size * 2] = image.at<cv::Vec3b>(i, j)[0] / 255.0;
        }
      }
    }else{
      inputTensorValuesInt.resize(inputShapeEncoder[0] * inputShapeEncoder[1] * inputShapeEncoder[2] * inputShapeEncoder[3]);
      for(int i = 0; i < inputShapeEncoder[2]; i++){
        for(int j = 0; j < inputShapeEncoder[3]; j++){
          int64_t pos = i * inputShapeEncoder[3] + j;
          int64_t size = inputShapeEncoder[2] * inputShapeEncoder[3];
          inputTensorValuesInt[pos + size * 0] = image.at<cv::Vec3b>(i, j)[2];
          inputTensorValuesInt[pos + size * 1] = image.at<cv::Vec3b>(i, j)[1];
          inputTensorValuesInt[pos + size * 2] = image.at<cv::Vec3b>(i, j)[0];
        }
      }
    }
    auto inputTensor = isInputTensorFloat ?
    Ort::Value::CreateTensor<float>(memoryInfo, inputTensorValuesFloat.data(), inputTensorValuesFloat.size(), inputShapeEncoder.data(), inputShapeEncoder.size()) :
    Ort::Value::CreateTensor<uint8_t>(memoryInfo, inputTensorValuesInt.data(), inputTensorValuesInt.size(), inputShapeEncoder.data(), inputShapeEncoder.size());
    outputTensorValuesEncoder = std::vector<float>(outputShapeEncoder[0] * outputShapeEncoder[1] * outputShapeEncoder[2] * outputShapeEncoder[3]);
    std::vector<Ort::Value> outputTensors;
    outputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, outputTensorValuesEncoder.data(), outputTensorValuesEncoder.size(), outputShapeEncoder.data(), outputShapeEncoder.size()));
    if(mode == HQSAM){
      intermTensorValuesPre = std::vector<float>(intermShapePre[0] * intermShapePre[1] * intermShapePre[2] * intermShapePre[3] * intermShapePre[4]);
      outputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, intermTensorValuesPre.data(), intermTensorValuesPre.size(), intermShapePre.data(), intermShapePre.size()));
    }
    if(terminating){
      preprocessingEnd();
      return false;
    }
    runOptionsEncoder.UnsetTerminate();
    std::vector<const char*> inputNames = getInputNamesEncoder();
    std::vector<const char*> outputNames = getOutputNamesEncoder();
    sessionEncoder->Run(runOptionsEncoder, inputNames.data(), &inputTensor, 1, outputNames.data(), outputTensors.data(), outputTensors.size());
  }catch(Ort::Exception& e){
    preprocessingEnd();
    return false;
  }
  preprocessingEnd();
  return true;
}

void Sam::preprocessingStart(){
  preprocessing = true;
}

void Sam::preprocessingEnd(){
  preprocessing = false;
  terminating = false;
}

void setPointsLabels(const std::list<cv::Point>& points, const std::list<cv::Point>& negativePoints, const std::list<cv::Rect> &rects, std::vector<float> *inputPointValues, std::vector<float> *inputLabelValues){
  for(auto& point : points){
    (*inputPointValues).push_back((float)point.x);
    (*inputPointValues).push_back((float)point.y);
    (*inputLabelValues).push_back(1);
  }
  for(auto& point : negativePoints){
    (*inputPointValues).push_back((float)point.x);
    (*inputPointValues).push_back((float)point.y);
    (*inputLabelValues).push_back(0);
  }
  for(auto& roi : rects){
    (*inputPointValues).push_back((float)roi.x);
    (*inputPointValues).push_back((float)roi.y);
    (*inputLabelValues).push_back(2);
    (*inputPointValues).push_back((float)roi.br().x);
    (*inputPointValues).push_back((float)roi.br().y);
    (*inputLabelValues).push_back(3);
  }
}

std::vector<int64_t> Sam::getInputPointShape(int numPoints){
  if(mode == EfficientSAM){
    std::vector<int64_t> inputPointShape = {1, 1, numPoints, 2};
    return inputPointShape;
  }
  std::vector<int64_t> inputPointShape = {1, numPoints, 2};
  return inputPointShape;
}

std::vector<int64_t> Sam::getInputLabelShape(int numPoints){
  if(mode == EfficientSAM){
    std::vector<int64_t> inputLabelShape = {1, 1, numPoints};
    return inputLabelShape;
  }
  std::vector<int64_t> inputLabelShape = {1, numPoints};
  return inputLabelShape;
}

cv::Mat Sam::getMask(const std::list<cv::Point>& points, const std::list<cv::Point>& negativePoints, const std::list<cv::Rect> &rects, int previousMaskIdx, bool isNextGetMask){
  std::vector<float> inputPointValues, inputLabelValues;
  setPointsLabels(points, negativePoints, rects, &inputPointValues, &inputLabelValues);
  int numPoints = (int)inputLabelValues.size();
  std::vector<int64_t> inputPointShape = getInputPointShape(numPoints);
  std::vector<int64_t> inputLabelShape = getInputLabelShape(numPoints);
  std::vector<Ort::Value> inputTensors;
  inputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, (float*)outputTensorValuesEncoder.data(), outputTensorValuesEncoder.size(), outputShapeEncoder.data(), outputShapeEncoder.size()));
  if(mode == HQSAM){
    inputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, (float*)intermTensorValuesPre.data(), intermTensorValuesPre.size(), intermShapePre.data(), intermShapePre.size()));
  }
  inputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, inputPointValues.data(), 2 * numPoints, inputPointShape.data(), inputPointShape.size()));
  inputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, inputLabelValues.data(), numPoints, inputLabelShape.data(), inputLabelShape.size()));
  const size_t maskInputSize = 256 * 256;
  std::vector<float> previousMaskInputValues;
  resizePreviousMasks(previousMaskIdx);
  float maskInputValues[maskInputSize];
  memset(maskInputValues, 0, sizeof(maskInputValues));
  float hasMaskValues[] = {0};
  if(mode <= HQSAM){
    if(isNextGetMask){
    }else if(previousMaskIdx >= 0){
      hasMaskValues[0] = 1;
      previousMaskInputValues = previousMasks[previousMaskIdx];
    }
    std::vector<int64_t> maskInputShape = {1, 1, 256, 256},
    hasMaskInputShape = {1};
    if(hasMaskValues[0] == 1){
      inputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, previousMaskInputValues.data(), maskInputSize, maskInputShape.data(), maskInputShape.size()));
    }else{
      inputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, maskInputValues, maskInputSize, maskInputShape.data(), maskInputShape.size()));
    }
    inputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, hasMaskValues, 1, hasMaskInputShape.data(), hasMaskInputShape.size()));
  }
  float orig_im_size_values_float[] = {(float)inputShapeEncoder[2], (float)inputShapeEncoder[3]};
  int64_t orig_im_size_values_int64[] = {inputShapeEncoder[2], inputShapeEncoder[3]};
  if(mode <= HQSAM){
    std::vector<int64_t> origImSizeShape = {2};
    inputTensors.push_back(Ort::Value::CreateTensor<float>(memoryInfo, orig_im_size_values_float, 2, origImSizeShape.data(), origImSizeShape.size()));
  }else if(mode == EfficientSAM){
    std::vector<int64_t> origImSizeShape = {2};
    inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(memoryInfo, orig_im_size_values_int64, 2, origImSizeShape.data(), origImSizeShape.size()));
  }
  cv::Mat outputMask = cv::Mat((int)inputShapeEncoder[2], (int)inputShapeEncoder[3], CV_8UC1);
  try{
    Ort::RunOptions runOptionsDecoder;
    std::vector<const char*> inputNames = getInputNamesDecoder();
    std::vector<const char*> outputNames = getOutputNamesDecoder();
    int outputTensorsNum = 3;
    int outputMaskIdx = 0;
    if(mode == EfficientSAM){
      outputTensorsNum = 2;
    }else if(mode == EdgeSAM){
      outputTensorsNum = 2;
      outputMaskIdx = 1;
    }
    auto outputTensors = sessionDecoder->Run(runOptionsDecoder, inputNames.data(), inputTensors.data(), inputTensors.size(), outputNames.data(), outputTensorsNum);
    if(mode == EdgeSAM){
      auto& outputTensorMask = outputTensors[outputMaskIdx];
      auto maskShape = outputTensorMask.GetTensorTypeAndShapeInfo().GetShape();
      cv::Mat outputMaskImage((int)maskShape[2], (int)maskShape[3], CV_32FC1, outputTensorMask.GetTensorMutableData<float>());
      if (outputMaskImage.size() != outputMask.size()) {
        cv::resize(outputMaskImage, outputMaskImage, outputMask.size());
      }
      for (int i = 0; i < outputMask.rows; i++) {
        for (int j = 0; j < outputMask.cols; j++) {
          outputMask.at<uint8_t>(i, j) = outputMaskImage.at<float>(i, j) > 0 ? 255 : 0;
        }
      }
    }else{
      auto outputMaskValues = outputTensors[outputMaskIdx].GetTensorMutableData<float>();
      for (int i = 0; i < outputMask.rows; i++) {
        for (int j = 0; j < outputMask.cols; j++) {
          outputMask.at<uchar>(i, j) = outputMaskValues[i * outputMask.cols + j] > 0 ? 255 : 0;
        }
      }
    }
    if(mode <= HQSAM){
      previousMaskInputValues = std::vector<float>(maskInputSize);
      auto low_res_logits = outputTensors[2].GetTensorMutableData<float>();
      for (int i = 0; i < maskInputSize; i++) {
        previousMaskInputValues[i] = low_res_logits[i];
      }
    }else{
      previousMaskInputValues = std::vector<float>(maskInputSize);
    }
    previousMasks.push_back(previousMaskInputValues);
  }catch(Ort::Exception& e){
    return outputMask;
  }
  return outputMask;
}

std::vector<const char*> Sam::getInputNamesEncoder(){
  if(mode == EfficientSAM){
    return {
      "batched_images"
    };
  }else if(mode == EdgeSAM){
    return {
      "image"
    };
  }
  return {
    "input"
  };
}

std::vector<const char*> Sam::getOutputNamesEncoder(){
  if(mode == EfficientSAM ||
     mode == EdgeSAM){
    return {
      "image_embeddings"
    };
  }else if(mode == HQSAM){
    return {
      "output",
      "interm_embeddings"
    };
  }return {
    "output"
  };
}

std::vector<const char*> Sam::getInputNamesDecoder(){
  if(mode == EfficientSAM){
    return {
      "image_embeddings",
      "batched_point_coords",
      "batched_point_labels",
      "orig_im_size"
    };
  }else if(mode == EdgeSAM){
    return {
      "image_embeddings",
      "point_coords",
      "point_labels"
    };
  }
  else if(mode == HQSAM){
    return {
      "image_embeddings",
      "interm_embeddings",
      "point_coords",
      "point_labels",
      "mask_input",
      "has_mask_input",
      "orig_im_size"
    };
  }
  return {
    "image_embeddings",
    "point_coords",
    "point_labels",
    "mask_input",
    "has_mask_input",
    "orig_im_size"
  };
}

std::vector<const char*> Sam::getOutputNamesDecoder(){
  if(mode == EfficientSAM){
    return {
      "output_masks",
      "iou_predictions",
      "low_res_masks"
    };
  }else if(mode == EdgeSAM){
    return {
      "scores",
      "masks"
    };
  }
  return {
    "masks",
    "iou_predictions",
    "low_res_masks"
  };
}
