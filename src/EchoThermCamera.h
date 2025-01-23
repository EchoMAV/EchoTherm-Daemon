#pragma once
#include <string>
#include <mutex>
#include <cstdint>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <filesystem>
#include <deque>

namespace cv
{
    class Mat;
    class VideoWriter;
}

// forward reference
struct seekframe_t; 

class EchoThermCamera
{
public:
    EchoThermCamera();
    ~EchoThermCamera();
    // change the loopback device name (will cause camera session to restart)
    // example: /dev/video0
    void setLoopbackDeviceName(std::string loopbackDeviceName);
    // change the frame format (will cause camera session to restart)
    // FRAME_FORMAT_CORRECTED               = 0x04
    // FRAME_FORMAT_PRE_AGC                 = 0x08
    // FRAME_FORMAT_THERMOGRAPHY_FLOAT      = 0x10
    // FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 = 0x20
    // FRAME_FORMAT_GRAYSCALE               = 0x40
    // FRAME_FORMAT_COLOR_ARGB8888          = 0x80
    // FRAME_FORMAT_COLOR_RGB565            = 0x100
    // FRAME_FORMAT_COLOR_AYUV              = 0x200
    // FRAME_FORMAT_COLOR_YUY2              = 0x400
    void setFrameFormat(int frameFormat);
    // change the color palette
    // COLOR_PALETTE_WHITE_HOT =  0
    // COLOR_PALETTE_BLACK_HOT =  1
    // COLOR_PALETTE_SPECTRA   =  2
    // COLOR_PALETTE_PRISM     =  3
    // COLOR_PALETTE_TYRIAN    =  4
    // COLOR_PALETTE_IRON      =  5
    // COLOR_PALETTE_AMBER     =  6
    // COLOR_PALETTE_HI        =  7
    // COLOR_PALETTE_GREEN     =  8
    // COLOR_PALETTE_USER_0    =  9
    // COLOR_PALETTE_USER_1    = 10
    // COLOR_PALETTE_USER_2    = 11
    // COLOR_PALETTE_USER_3    = 12
    // COLOR_PALETTE_USER_4    = 13
    void setRadiometricFrameFormat(int radiometricFrameFormat);
    // SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT      = 16 = 0x10
	// SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 = 32 = 0x20
    void setColorPalette(int colorPalette);
    // change the shutter mode
    // negative = manual
    // zero     = auto
    // positive = number of seconds between shutter events
    void setShutterMode(int shutterMode);
    // set the sharpen filter
    // zero     = disabled
    // non-zero = enabled
    void setSharpenFilter(int sharpenFilterMode);
    // set the flat scene filter
    // zero     = disabled
    // non-zero = enabled
    void setFlatSceneFilter(int flatSceneFilterMode);
    // set the gradient filter
    // zero     = disabled
    // non-zero = enabled
    void setGradientFilter(int gradientFilterMode);
    // set the pipeline mode
    // PIPELINE_LITE       = 0,
    // PIPELINE_LEGACY     = 1,
    // PIPELINE_PROCESSED  = 2,
    // Note that in PIPELINE_PROCESSED, sharpen, flat scene, and gradient filters are disabled
    void setPipelineMode(int pipelineMode);
    // manually trigger the shutter, regardless of shuttermode
    void triggerShutter();
    // start the camera manager and wait for a camera to connect
    bool start();
    // stop the camera manager and disconnect the camera if it is connected
    void stop();
    // Get a string representing the status of the camera
    std::string getStatus() const;
    // Get a string representing the current zoom status
    std::string getZoom() const;
    // Set the zoom rate
    // 0 = stopped
    // positive = zooming in
    // negative = zooming out
    void setZoomRate(double zoomRate);
    //Instantly set the zoom
    void setZoom(double zoom);
    //set the maximum zoom
    void setMaxZoom(double maxZoom);
    //start recording to the file path
    //return a string indicating success or failure
    std::string startRecording(std::filesystem::path const& filePath);
    //take a screenshot of the current frame to the file path
    //return a string indicating success or failure
    std::string takeScreenshot(std::filesystem::path const& filePath);
    //stop recording
    //return a string indicating success or failure
    std::string stopRecording();
    //take a thermometic data screenshot of the current frame to the file path
    //return a string indicating success or failure
    std::string takeRadiometricScreenshot(std::filesystem::path const& filePath);

    void _closeSession();
    
private:
    void _updateFilterHelper(int filterType, int filterState);
    void _connect(void *p_camera);
    void _handleReadyToPair(void *p_camera);
    //void _closeSession();
    void _openSession(bool reconnect);
    void _openDevice(int width, int height);
    void _startShutterClickThread();
    void _stopShutterClickThread();
    void _startRecordingThread();
    void _stopRecordingThread();
    ssize_t _writeBytes(void* p_frameData, size_t frameDataSize);
    void _doContinuousZoom();
    void _pushFrame(int cvFrameType, void* p_frameData);
    std::string m_loopbackDeviceName;
    std::string m_chipId;
    int m_activeFrameFormat;
    int m_frameFormat;
    int m_colorPalette;
    int m_shutterMode;
    int m_sharpenFilterMode;
    int m_flatSceneFilterMode;
    int m_gradientFilterMode;
    int m_pipelineMode;
    void *mp_camera;
    void *mp_cameraManager;
    int m_loopbackDevice;
    double m_zoomRate;
    int m_width;
    int m_height;
    int m_roiX;
    int m_roiY;
    int m_roiWidth;
    int m_roiHeight;
    double m_currentZoom;
    double m_maxZoom;
    std::chrono::system_clock::time_point m_lastZoomTime;
    mutable std::recursive_mutex m_mut;
    std::thread m_shutterClickThread;
    std::condition_variable_any m_shutterClickCondition;
    std::atomic_bool m_shutterClickThreadRunning;
    std::filesystem::path m_screenshotFilePath;
    std::filesystem::path m_radiometricSreenshotFilePath;
    std::filesystem::path m_videoFilePath;
    std::string m_screenshotStatus;
    mutable std::mutex m_screenshotStatusReadyMut;
    std::condition_variable m_screenshotStatusReadyCondition;
    std::string m_recordingStatus;
    mutable std::mutex m_recordingStatusReadyMut;
    std::condition_variable m_recordingStatusReadyCondition;
    std::deque<cv::Mat> m_recordingFrameQueue;
    mutable std::mutex m_recordingFrameQueueMut;
    std::condition_variable m_recordingFramesReadyCondition;
    std::thread m_recordingThread;
    std::atomic_bool m_recordingThreadRunning;
    std::unique_ptr<cv::VideoWriter> mp_videoWriter;

    int m_frameNum;
    int m_radiometricFrameFormat;
    int m_radiometricFrameCapture;  
    int m_radiometricFrameCaptureBusy;
    std::filesystem::path m_radiometricScreenshotFilePath;
    int radiometricWrite(seekframe_t* frame);
};