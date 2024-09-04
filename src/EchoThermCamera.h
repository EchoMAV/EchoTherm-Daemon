#pragma once
#include <string>
#include <mutex>
#include <cstdint>
#include <condition_variable>
#include <thread>
#include <atomic>

class EchoThermCamera
{
    public:
        EchoThermCamera();
        ~EchoThermCamera();
        // change the loopback device name (will cause camera session to restart)
        // example: /dev/video0
        void setLoopbackDeviceName(std::string loopbackDeviceName);
        // change the frame format (will cause camera session to restart)
        // SEEKCAMERA_FRAME_FORMAT_CORRECTED               = 0x04
	    // SEEKCAMERA_FRAME_FORMAT_PRE_AGC                 = 0x08
	    // SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT      = 0x10
	    // SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6 = 0x20
	    // SEEKCAMERA_FRAME_FORMAT_GRAYSCALE               = 0x40
	    // SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888          = 0x80
	    // SEEKCAMERA_FRAME_FORMAT_COLOR_RGB565            = 0x100
	    // SEEKCAMERA_FRAME_FORMAT_COLOR_AYUV              = 0x200
	    // SEEKCAMERA_FRAME_FORMAT_COLOR_YUY2              = 0x400
        void setFrameFormat(int frameFormat);
        // change the color palette
        // SEEKCAMERA_COLOR_PALETTE_WHITE_HOT =  0
	    // SEEKCAMERA_COLOR_PALETTE_BLACK_HOT =  1
	    // SEEKCAMERA_COLOR_PALETTE_SPECTRA   =  2
	    // SEEKCAMERA_COLOR_PALETTE_PRISM     =  3
	    // SEEKCAMERA_COLOR_PALETTE_TYRIAN    =  4
	    // SEEKCAMERA_COLOR_PALETTE_IRON      =  5
	    // SEEKCAMERA_COLOR_PALETTE_AMBER     =  6
	    // SEEKCAMERA_COLOR_PALETTE_HI        =  7
	    // SEEKCAMERA_COLOR_PALETTE_GREEN     =  8
	    // SEEKCAMERA_COLOR_PALETTE_USER_0    =  9
	    // SEEKCAMERA_COLOR_PALETTE_USER_1    = 10
	    // SEEKCAMERA_COLOR_PALETTE_USER_2    = 11
	    // SEEKCAMERA_COLOR_PALETTE_USER_3    = 12
	    // SEEKCAMERA_COLOR_PALETTE_USER_4    = 13
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
        // SEEKCAMERA_IMAGE_LITE       = 0,
	    // SEEKCAMERA_IMAGE_LEGACY     = 1,
	    // SEEKCAMERA_IMAGE_SEEKVISION = 2,
        // Note that in SEEKCAMERA_IMAGE_SEEKVISION, sharpen, flat scene, and gradient filters are disabled
        void setPipelineMode(int pipelineMode);
        //manually trigger the shutter, regardless of shuttermode
        void triggerShutter();
        //start the camera manager and wait for a camera to connect
        bool start();
        //stop the camera manager and disconnect the camera if it is connected
        void stop();
    private:

        void _updateFilterHelper(int filterType, int filterState);
        void _connect(void* p_camera);
        void _handleReadyToPair(void* p_camera);
        void _closeSession();
        void _openSession(bool reconnect);
        void _openDevice(int width, int height);
        void _startShutterClickThread();
        void _stopShutterClickThread();

        std::string m_loopbackDeviceName;
        std::string m_chipId;
        int m_frameFormat;
        int m_colorPalette;
        int m_shutterMode;
        int m_sharpenFilterMode;
        int m_flatSceneFilterMode;
        int m_gradientFilterMode;
        int m_pipelineMode;
        void* mp_camera;
        void* mp_cameraManager;
        int m_loopbackDevice;
        std::recursive_mutex m_mut;
        //std::mutex m_shutterClickMut;
        std::thread m_shutterClickThread;
        std::condition_variable_any m_shutterClickCondition; 
        std::atomic_bool m_shutterClickThreadRunning;
};