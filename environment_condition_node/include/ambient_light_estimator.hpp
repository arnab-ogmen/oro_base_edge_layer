#ifndef AMBIENT_LIGHT_ESTIMATOR_HPP
#define AMBIENT_LIGHT_ESTIMATOR_HPP

#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>

class AmbientLightEstimator {
public:
    AmbientLightEstimator(const std::string& device_path = "ipc:///tmp/video.sock",
                          double model_a = 0.05,
                          double model_b = 1.8,
                          double ema_alpha = 0.15,
                          int percentile = 50);
    ~AmbientLightEstimator();

    // Start background capture and processing thread
    void start();

    // Stop background capture and processing thread
    void stop();

    // Get the latest estimated lux and confidence score (thread-safe)
    bool get_latest_lux(double& out_lux, double& out_confidence, std::string& out_bucket);

    // Classify the estimated lux value into qualitative buckets
    static std::string classify_lux_bucket(double lux);

private:
    // Background thread capture and processing loop
    void run();

    // Core light estimation algorithm on a single frame
    double estimate_brightness(const cv::Mat& frame, double& out_confidence);

    std::string device_path_;
    double model_a_;
    double model_b_;
    double ema_alpha_;
    int percentile_;

    std::atomic<bool> running_;
    std::thread worker_thread_;
    std::mutex data_mutex_;

    double latest_lux_ = -1.0;
    double latest_confidence_ = 0.0;
    std::string latest_bucket_ = "unknown";

    // Temporal filter state
    double smoothed_brightness_ = -1.0;
};

#endif // AMBIENT_LIGHT_ESTIMATOR_HPP
