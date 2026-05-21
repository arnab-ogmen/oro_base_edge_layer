#include "ambient_light_estimator.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

AmbientLightEstimator::AmbientLightEstimator(const std::string& device_path,
                                           double model_a,
                                           double model_b,
                                           double ema_alpha,
                                           int percentile)
    : device_path_(device_path),
      model_a_(model_a),
      model_b_(model_b),
      ema_alpha_(ema_alpha),
      percentile_(percentile),
      running_(false) {}

AmbientLightEstimator::~AmbientLightEstimator() {
    stop();
}

void AmbientLightEstimator::start() {
    if (running_.exchange(true)) {
        return; // Already running
    }
    worker_thread_ = std::thread(&AmbientLightEstimator::run, this);
    std::cout << "[LightEstimator] Background stream processor started on " << device_path_ << "\n";
}

void AmbientLightEstimator::stop() {
    if (!running_.exchange(false)) {
        return; // Already stopped
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    std::cout << "[LightEstimator] Background stream processor stopped.\n";
}

bool AmbientLightEstimator::get_latest_lux(double& out_lux, double& out_confidence, std::string& out_bucket) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (latest_lux_ < 0.0) {
        return false; // No reading available yet
    }
    out_lux = latest_lux_;
    out_confidence = latest_confidence_;
    out_bucket = latest_bucket_;
    return true;
}

std::string AmbientLightEstimator::classify_lux_bucket(double lux) {
    if (lux < 5.0) return "dark";
    if (lux < 50.0) return "dim";
    if (lux < 500.0) return "normal";
    if (lux < 2000.0) return "bright";
    return "direct_sunlight";
}

void AmbientLightEstimator::run() {
    cv::VideoCapture cap;
    
    while (running_) {
        // Try opening the V4L2 loopback device
        if (!cap.isOpened()) {
            // Using cv::CAP_V4L2 is required for stable embedded Linux capture
            cap.open(device_path_, cv::CAP_V4L2);
            if (!cap.isOpened()) {
                std::cerr << "[LightEstimator] Error: Failed to open V4L2 device " << device_path_ 
                          << ". Retrying in 2 seconds...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            
            // Set capture properties for standard efficient streaming
            cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        }

        cv::Mat frame;
        if (!cap.read(frame) || frame.empty()) {
            std::cerr << "[LightEstimator] Warning: Frame drop or empty capture. Reinitializing camera...\n";
            cap.release();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        double confidence = 1.0;
        double brightness = estimate_brightness(frame, confidence);

        if (brightness >= 0.0) {
            // Apply Exponential Moving Average (EMA) for temporal stabilization
            if (smoothed_brightness_ < 0.0) {
                smoothed_brightness_ = brightness; // Cold start initialization
            } else {
                smoothed_brightness_ = ema_alpha_ * brightness + (1.0 - ema_alpha_) * smoothed_brightness_;
            }

            // Power-law Lux Approximation Model: lux = a * brightness^b
            double lux = model_a_ * std::pow(smoothed_brightness_, model_b_);
            std::string bucket = classify_lux_bucket(lux);

            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                latest_lux_ = lux;
                latest_confidence_ = confidence;
                latest_bucket_ = bucket;
            }
        }

        // Target processing cadence: ~10 FPS is plenty for ambient light estimation, saving CPU cycles
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    cap.release();
}

double AmbientLightEstimator::estimate_brightness(const cv::Mat& frame, double& out_confidence) {
    // 1. Convert to grayscale/luminance
    cv::Mat gray;
    if (frame.channels() == 3) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else if (frame.channels() == 4) {
        cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
    } else {
        gray = frame.clone();
    }

    // 2. Center crop (60% region) to avoid edge artifacts/borders
    int crop_w = static_cast<int>(gray.cols * 0.60);
    int crop_h = static_cast<int>(gray.rows * 0.60);
    int crop_x = (gray.cols - crop_w) / 2;
    int crop_y = (gray.rows - crop_h) / 2;
    cv::Mat cropped = gray(cv::Rect(crop_x, crop_y, crop_w, crop_h));

    // 3. Downsample to 160x120 for extreme low CPU usage on embedded ARM
    cv::Mat small;
    cv::resize(cropped, small, cv::Size(160, 120), 0, 0, cv::INTER_AREA);

    int total_pixels = small.rows * small.cols;

    // 4. Calculate Histogram
    int histSize = 256;
    float range[] = { 0, 256 };
    const float* histRange = { range };
    cv::Mat hist;
    cv::calcHist(&small, 1, 0, cv::Mat(), hist, 1, &histSize, &histRange, true, false);

    // 5. Frame Quality Rejection & Outlier Detection
    // A. Extreme Glare/Saturation: Count fully saturated pixels (> 250)
    int saturated_pixels = 0;
    for (int i = 250; i < 256; ++i) {
        saturated_pixels += static_cast<int>(hist.at<float>(i));
    }
    double saturation_ratio = static_cast<double>(saturated_pixels) / total_pixels;
    if (saturation_ratio > 0.85) {
        // Severe saturation/glare - reject frame
        out_confidence = 0.1;
        return -1.0; 
    }

    // B. Severe darkness or lens cap
    double max_val = 0;
    cv::minMaxLoc(small, nullptr, &max_val);
    if (max_val < 3.0) {
        // Absolute darkness (e.g. lens cap is on)
        out_confidence = 0.95; // We are highly confident it is dark
        return 0.0;
    }

    // 6. Percentile-based luminance extraction & Histogram Trimming (P10 to P90)
    // We ignore the bottom 10% (deep shadows) and top 10% (bright spots/displays)
    int p10_count = (10 * total_pixels) / 100;
    int p90_count = (90 * total_pixels) / 100;

    double sum_trimmed = 0.0;
    int count_trimmed = 0;
    int cumulative_sum = 0;

    int p_target_count = (percentile_ * total_pixels) / 100;
    double p_value = 0.0;
    bool p_found = false;

    for (int i = 0; i < 256; ++i) {
        int bin_val = static_cast<int>(hist.at<float>(i));
        
        for (int j = 0; j < bin_val; ++j) {
            if (cumulative_sum >= p_target_count && !p_found) {
                p_value = i;
                p_found = true;
            }
            if (cumulative_sum >= p10_count && cumulative_sum <= p90_count) {
                sum_trimmed += i;
                count_trimmed++;
            }
            cumulative_sum++;
        }
    }

    double trimmed_mean = (count_trimmed > 0) ? (sum_trimmed / count_trimmed) : p_value;

    // Confidence scoring based on scene balance (closeness of median to trimmed mean)
    double deviation = std::abs(trimmed_mean - p_value) / 255.0;
    out_confidence = std::max(0.1, 1.0 - (deviation * 1.5) - (saturation_ratio * 0.5));

    // Combine percentile and trimmed mean for a highly stable brightness estimation
    return 0.7 * trimmed_mean + 0.3 * p_value;
}
