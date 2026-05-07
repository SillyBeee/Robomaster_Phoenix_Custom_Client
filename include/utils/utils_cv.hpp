#ifndef UTILS_CV_HPP
#define UTILS_CV_HPP


#include <opencv2/opencv.hpp>
#include <slint.h>
#include <slint_image.h>




slint::Image MatToSlintImage(const cv::Mat &mat);









#endif // UTILS_CV_HPP