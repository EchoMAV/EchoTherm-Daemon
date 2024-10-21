#include "EchoThermCamera.h"
#include "seekcamera/seekcamera.h"
#include "seekcamera/seekcamera_manager.h"
#include <syslog.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <sstream>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

// Define the DEBUG macro to enable debug code
// #define DEBUG

namespace
{
    constexpr static inline auto const n_minZoom = 1.0;
    constexpr static inline auto const n_defaultMaxZoom = 16.0;
    constexpr static inline auto const n_frameRate = 27.0;
}

EchoThermCamera::EchoThermCamera()
    : m_loopbackDeviceName{},
      m_chipId{},
      m_frameFormat{int(SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888)},
      m_colorPalette{int(SEEKCAMERA_COLOR_PALETTE_WHITE_HOT)},
      m_shutterMode{int(SEEKCAMERA_SHUTTER_MODE_AUTO)},
      m_sharpenFilterMode{int(SEEKCAMERA_FILTER_STATE_DISABLED)},
      m_flatSceneFilterMode{int(SEEKCAMERA_FILTER_STATE_DISABLED)},
      m_gradientFilterMode{int(SEEKCAMERA_FILTER_STATE_DISABLED)},
      m_pipelineMode{int(SEEKCAMERA_IMAGE_SEEKVISION)},
      mp_camera{nullptr},
      mp_cameraManager{nullptr},
      m_loopbackDevice{-1},
      m_zoomRate{0.0},
      m_width{0},
      m_height{0},
      m_roiX{0},
      m_roiY{0},
      m_roiWidth{0},
      m_roiHeight{0},
      m_currentZoom{n_minZoom},
      m_maxZoom{n_defaultMaxZoom},
      m_lastZoomTime{},
      m_mut{},
      m_shutterClickThread{},
      m_shutterClickCondition{},
      m_shutterClickThreadRunning{false},
      m_screenshotFilePath{},
      m_videoFilePath{},
      m_screenshotStatus{},
      m_screenshotStatusReadyMut{},
      m_screenshotStatusReadyCondition{},
      m_recordingStatus{},
      m_recordingStatusReadyMut{},
      m_recordingStatusReadyCondition{},
      m_recordingFrameQueue{},
      m_recordingFrameQueueMut{},
      m_recordingFramesReadyCondition{},
      m_recordingThread{},
      m_recordingThreadRunning{false},
      mp_videoWriter{}
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::EchoThermCamera()");
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::EchoThermCamera()");
#endif
}

EchoThermCamera::~EchoThermCamera()
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::~EchoThermCamera()");
#endif
    stop();
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::~EchoThermCamera()");
#endif
}

void EchoThermCamera::setLoopbackDeviceName(std::string loopbackDeviceName)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setLoopbackDeviceName(%s)", loopbackDeviceName.c_str());
#endif
    if (loopbackDeviceName != m_loopbackDeviceName)
    {
        m_loopbackDeviceName = std::move(loopbackDeviceName);
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setLoopbackDeviceName(%s)", loopbackDeviceName.c_str());
#endif
}

void EchoThermCamera::setFrameFormat(int frameFormat)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setFrameFormat(%d)", frameFormat);
#endif
    if (frameFormat != m_frameFormat)
    {
        switch (frameFormat)
        {

        case SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888:
        case SEEKCAMERA_FRAME_FORMAT_GRAYSCALE:
            m_frameFormat = frameFormat;
            break;
        // TODO support these as well as bit-wise ORed frame formats
        case SEEKCAMERA_FRAME_FORMAT_COLOR_RGB565:
        case SEEKCAMERA_FRAME_FORMAT_COLOR_YUY2:
        case SEEKCAMERA_FRAME_FORMAT_COLOR_AYUV:
        case SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6:
        case SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT:
        case SEEKCAMERA_FRAME_FORMAT_PRE_AGC:
        case SEEKCAMERA_FRAME_FORMAT_CORRECTED:
        default:
            syslog(LOG_WARNING, "The frame format %d is invalid.", frameFormat);
            break;
        }
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setFrameFormat(%d)", frameFormat);
#endif
}

void EchoThermCamera::setColorPalette(int colorPalette)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setColorPalette(%d)", colorPalette);
#endif
    if (colorPalette != m_colorPalette)
    {
        std::string colorPaletteName;
        switch (colorPalette)
        {
        case SEEKCAMERA_COLOR_PALETTE_WHITE_HOT:
        case SEEKCAMERA_COLOR_PALETTE_BLACK_HOT:
        case SEEKCAMERA_COLOR_PALETTE_SPECTRA:
        case SEEKCAMERA_COLOR_PALETTE_PRISM:
        case SEEKCAMERA_COLOR_PALETTE_TYRIAN:
        case SEEKCAMERA_COLOR_PALETTE_IRON:
        case SEEKCAMERA_COLOR_PALETTE_AMBER:
        case SEEKCAMERA_COLOR_PALETTE_HI:
        case SEEKCAMERA_COLOR_PALETTE_GREEN:
        case SEEKCAMERA_COLOR_PALETTE_USER_0:
        case SEEKCAMERA_COLOR_PALETTE_USER_1:
        case SEEKCAMERA_COLOR_PALETTE_USER_2:
        case SEEKCAMERA_COLOR_PALETTE_USER_3:
        case SEEKCAMERA_COLOR_PALETTE_USER_4:
        {
            m_colorPalette = colorPalette;
            auto result = SEEKCAMERA_SUCCESS;
            if (mp_camera)
            {
                result = seekcamera_set_color_palette((seekcamera_t *)mp_camera, (seekcamera_color_palette_t)m_colorPalette);
            }
            if (result == SEEKCAMERA_SUCCESS)
            {
                syslog(LOG_NOTICE, "Switched camera color palette to %s.", seekcamera_color_palette_get_str((seekcamera_color_palette_t)m_colorPalette));
            }
            else
            {
                syslog(LOG_NOTICE, "Failed to update color palette to %s: %s.", seekcamera_color_palette_get_str((seekcamera_color_palette_t)m_colorPalette), seekcamera_error_get_str(result));
            }
            break;
        }
        default:
        {
            syslog(LOG_WARNING, "The color palette %d is invalid.", colorPalette);
            break;
        }
        }
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setColorPalette(%d)", colorPalette);
#endif
}

void EchoThermCamera::setShutterMode(int shutterMode)
{
    std::unique_lock<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setShutterMode(%d)", shutterMode);
#endif
    if (m_shutterMode != shutterMode)
    {
        auto const newShutterMode = m_shutterMode = shutterMode;
        auto result = SEEKCAMERA_SUCCESS;
        if (mp_camera)
        {
            if (m_shutterMode == 0)
            {
                result = seekcamera_set_shutter_mode((seekcamera_t *)mp_camera, (seekcamera_shutter_mode_t)m_shutterMode);
            }
            else
            {
                result = seekcamera_set_shutter_mode((seekcamera_t *)mp_camera, SEEKCAMERA_SHUTTER_MODE_MANUAL);
            }
        }
        // unlock the mutex to give the shutter timer thread a chance to loop again
        lock.unlock();
        _stopShutterClickThread();
        _startShutterClickThread();
        if (result == SEEKCAMERA_SUCCESS)
        {
            if (newShutterMode > 0)
            {
                syslog(LOG_NOTICE, "Camera is set to trigger shutter every %d seconds.", newShutterMode);
            }
            else if (newShutterMode == 0)
            {
                syslog(LOG_NOTICE, "Camera is set to trigger shutter automatically.");
            }
            else
            {
                syslog(LOG_NOTICE, "Camera is set to not trigger the shutter unless specifically requested.");
            }
        }
        else
        {
            if (newShutterMode == 0)
            {
                syslog(LOG_ERR, "Failed to switch camera shutter mode to auto: %s.", seekcamera_error_get_str(result));
            }
            else
            {
                syslog(LOG_ERR, "Failed to switch camera shutter mode to manual: %s.", seekcamera_error_get_str(result));
            }
        }
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setShutterMode(%d)", shutterMode);
#endif
}

void EchoThermCamera::_updateFilterHelper(int filterType, int filterState)
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_updateFilterHelper(%d, %d)", filterType, filterState);
#endif
    auto result = SEEKCAMERA_SUCCESS;
    if (mp_camera)
    {
        result = seekcamera_set_filter_state((seekcamera_t *)mp_camera, (seekcamera_filter_t)filterType, (seekcamera_filter_state_t)filterState);
    }
    if (result == SEEKCAMERA_SUCCESS)
    {
        syslog(LOG_NOTICE, "Filter state updated to %s.", seekcamera_get_filter_state_str((seekcamera_filter_t)filterType, (seekcamera_filter_state_t)filterState));
    }
    else
    {
        syslog(LOG_ERR, "Failed to update filter state to %s: %s.", seekcamera_get_filter_state_str((seekcamera_filter_t)filterType, (seekcamera_filter_state_t)filterState), seekcamera_error_get_str(result));
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_updateFilterHelper(%d, %d)", filterType, filterState);
#endif
}

void EchoThermCamera::setSharpenFilter(int sharpenFilterMode)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setSharpenFilter(%d)", sharpenFilterMode);
#endif
    if (m_sharpenFilterMode != sharpenFilterMode)
    {
        if (sharpenFilterMode != (int)SEEKCAMERA_FILTER_STATE_DISABLED)
        {
            sharpenFilterMode = (int)SEEKCAMERA_FILTER_STATE_ENABLED;
        }
        m_sharpenFilterMode = sharpenFilterMode;
        _updateFilterHelper(SEEKCAMERA_FILTER_SHARPEN_CORRECTION, m_sharpenFilterMode);
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setSharpenFilter(%d)", sharpenFilterMode);
#endif
}

void EchoThermCamera::setFlatSceneFilter(int flatSceneFilterMode)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setFlatSceneFilter(%d)", flatSceneFilterMode);
#endif
    if (m_flatSceneFilterMode != flatSceneFilterMode)
    {
        if (flatSceneFilterMode != (int)SEEKCAMERA_FILTER_STATE_DISABLED)
        {
            flatSceneFilterMode = (int)SEEKCAMERA_FILTER_STATE_ENABLED;
        }
        m_flatSceneFilterMode = flatSceneFilterMode;
        _updateFilterHelper(SEEKCAMERA_FILTER_FLAT_SCENE_CORRECTION, m_flatSceneFilterMode);
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setFlatSceneFilter(%d)", flatSceneFilterMode);
#endif
}

void EchoThermCamera::setGradientFilter(int gradientFilterMode)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setGradientFilter(%d)", gradientFilterMode);
#endif
    if (m_gradientFilterMode != gradientFilterMode)
    {
        if (gradientFilterMode != (int)SEEKCAMERA_FILTER_STATE_DISABLED)
        {
            gradientFilterMode = (int)SEEKCAMERA_FILTER_STATE_ENABLED;
        }
        m_gradientFilterMode = gradientFilterMode;
        _updateFilterHelper(SEEKCAMERA_FILTER_GRADIENT_CORRECTION, m_gradientFilterMode);
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setGradientFilter(%d)", gradientFilterMode);
#endif
}

void EchoThermCamera::setPipelineMode(int pipelineMode)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setPipelineMode(%d)", pipelineMode);
#endif
    if (m_pipelineMode != pipelineMode)
    {
        switch (pipelineMode)
        {
        case int(SEEKCAMERA_IMAGE_LITE):
        case int(SEEKCAMERA_IMAGE_LEGACY):
        case int(SEEKCAMERA_IMAGE_SEEKVISION):
        {
            m_pipelineMode = pipelineMode;
            auto result = SEEKCAMERA_SUCCESS;
            if (mp_camera)
            {
                result = seekcamera_set_pipeline_mode((seekcamera_t *)mp_camera, (seekcamera_pipeline_mode_t)m_pipelineMode);
                if (result == SEEKCAMERA_SUCCESS)
                {
                    syslog(LOG_NOTICE, "Pipeline mode updated to %s.", seekcamera_pipeline_mode_get_str((seekcamera_pipeline_mode_t)m_pipelineMode));
                    if (m_pipelineMode != SEEKCAMERA_IMAGE_SEEKVISION)
                    {
                        {
                            auto state = SEEKCAMERA_FILTER_STATE_DISABLED;
                            (void)seekcamera_get_filter_state((seekcamera_t *)mp_camera, SEEKCAMERA_FILTER_FLAT_SCENE_CORRECTION, &state);
                            if (state != m_flatSceneFilterMode)
                            {
                                result = seekcamera_set_filter_state((seekcamera_t *)mp_camera, SEEKCAMERA_FILTER_FLAT_SCENE_CORRECTION, (seekcamera_filter_state_t)m_flatSceneFilterMode);
                            }
                            if (result == SEEKCAMERA_SUCCESS)
                            {
                                syslog(LOG_NOTICE, "Filter state updated to %s.", seekcamera_get_filter_state_str(SEEKCAMERA_FILTER_FLAT_SCENE_CORRECTION, (seekcamera_filter_state_t)m_flatSceneFilterMode));
                            }
                            else
                            {
                                syslog(LOG_ERR, "Failed to update filter state to %s: %s.", seekcamera_get_filter_state_str(SEEKCAMERA_FILTER_FLAT_SCENE_CORRECTION, (seekcamera_filter_state_t)m_flatSceneFilterMode), seekcamera_error_get_str(result));
                            }
                            result = SEEKCAMERA_SUCCESS;
                        }
                        {
                            auto state = SEEKCAMERA_FILTER_STATE_DISABLED;
                            (void)seekcamera_get_filter_state((seekcamera_t *)mp_camera, SEEKCAMERA_FILTER_GRADIENT_CORRECTION, &state);
                            if (state != m_gradientFilterMode)
                            {
                                result = seekcamera_set_filter_state((seekcamera_t *)mp_camera, SEEKCAMERA_FILTER_GRADIENT_CORRECTION, (seekcamera_filter_state_t)m_gradientFilterMode);
                            }
                            if (result == SEEKCAMERA_SUCCESS)
                            {
                                syslog(LOG_NOTICE, "Filter state updated to %s.", seekcamera_get_filter_state_str(SEEKCAMERA_FILTER_GRADIENT_CORRECTION, (seekcamera_filter_state_t)m_gradientFilterMode));
                            }
                            else
                            {
                                syslog(LOG_ERR, "Failed to update filter state to %s: %s.", seekcamera_get_filter_state_str(SEEKCAMERA_FILTER_GRADIENT_CORRECTION, (seekcamera_filter_state_t)m_gradientFilterMode), seekcamera_error_get_str(result));
                            }
                            result = SEEKCAMERA_SUCCESS;
                        }
                        {
                            auto state = SEEKCAMERA_FILTER_STATE_DISABLED;
                            (void)seekcamera_get_filter_state((seekcamera_t *)mp_camera, SEEKCAMERA_FILTER_SHARPEN_CORRECTION, &state);
                            if (state != m_sharpenFilterMode)
                            {
                                result = seekcamera_set_filter_state((seekcamera_t *)mp_camera, SEEKCAMERA_FILTER_SHARPEN_CORRECTION, (seekcamera_filter_state_t)m_sharpenFilterMode);
                            }
                            if (result == SEEKCAMERA_SUCCESS)
                            {
                                syslog(LOG_NOTICE, "Filter state updated to %s.", seekcamera_get_filter_state_str(SEEKCAMERA_FILTER_SHARPEN_CORRECTION, (seekcamera_filter_state_t)m_sharpenFilterMode));
                            }
                            else
                            {
                                syslog(LOG_ERR, "Failed to update filter state to %s: %s.", seekcamera_get_filter_state_str(SEEKCAMERA_FILTER_SHARPEN_CORRECTION, (seekcamera_filter_state_t)m_sharpenFilterMode), seekcamera_error_get_str(result));
                            }
                            result = SEEKCAMERA_SUCCESS;
                        }
                    }
                }
                else
                {
                    syslog(LOG_ERR, "Failed to update pipeline mode to %s: %s.", seekcamera_pipeline_mode_get_str((seekcamera_pipeline_mode_t)m_pipelineMode), seekcamera_error_get_str(result));
                }
            }
            break;
        }
        default:
        {
            syslog(LOG_WARNING, "The pipeline mode %d is invalid.", pipelineMode);
            break;
        }
        }
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setPipelineMode(%d)", pipelineMode);
#endif
}

void EchoThermCamera::triggerShutter()
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::triggerShutter()");
#endif
    if (mp_camera)
    {
        auto const result = seekcamera_shutter_trigger((seekcamera_t *)mp_camera);
        if (result == SEEKCAMERA_SUCCESS)
        {
            syslog(LOG_NOTICE, "Camera shutter manually triggered.");
        }
        else
        {
            syslog(LOG_ERR, "Failed to manually trigger camera shutter: %s.", seekcamera_error_get_str(result));
        }
    }
    else
    {
        syslog(LOG_ERR, "Cannot trigger shutter because no capture session is active.");
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::triggerShutter()");
#endif
}

bool EchoThermCamera::start()
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::start()");
#endif
    stop();
    bool returnVal = true;
    auto status = seekcamera_manager_create((seekcamera_manager_t **)&mp_cameraManager, SEEKCAMERA_IO_TYPE_USB);
    if (status == SEEKCAMERA_SUCCESS)
    {
        status = seekcamera_manager_register_event_callback((seekcamera_manager_t *)mp_cameraManager,
                                                            [](seekcamera_t *p_camera, seekcamera_manager_event_t event, seekcamera_error_t eventStatus, void *p_userData)
                                                            {
                                                                seekcamera_chipid_t cid{};
                                                                seekcamera_get_chipid(p_camera, &cid);
                                                                syslog(LOG_NOTICE, "%s (CID: %s)", seekcamera_manager_get_event_str(event), cid);
                                                                auto *const p_this = (EchoThermCamera *)p_userData;
                                                                std::string chipId((char *)&cid);
                                                                if (p_this->m_chipId.empty())
                                                                {
                                                                    syslog(LOG_NOTICE, "Camera manager is taking ownership of device %s.", cid);
                                                                    p_this->m_chipId = chipId;
                                                                }
                                                                if (p_this->m_chipId == chipId)
                                                                {
                                                                    switch (event)
                                                                    {
                                                                    case SEEKCAMERA_MANAGER_EVENT_CONNECT:
                                                                        p_this->_connect(p_camera);
                                                                        break;
                                                                    case SEEKCAMERA_MANAGER_EVENT_DISCONNECT:
                                                                        p_this->_closeSession();
                                                                        break;
                                                                    case SEEKCAMERA_MANAGER_EVENT_ERROR:
                                                                        syslog(LOG_ERR, "Unhandled camera error: (CID: %s) %s.", chipId.c_str(), seekcamera_error_get_str(eventStatus));
                                                                        break;
                                                                    case SEEKCAMERA_MANAGER_EVENT_READY_TO_PAIR:
                                                                        p_this->_handleReadyToPair(p_camera);
                                                                        break;
                                                                    default:
                                                                        break;
                                                                    }
                                                                }
                                                                else
                                                                {
                                                                    syslog(LOG_NOTICE, "Encountered camera with unknown chip ID %s.", chipId.c_str());
                                                                }
                                                            },
                                                            (void *)this);
        if (status != SEEKCAMERA_SUCCESS)
        {
            returnVal = false;
            syslog(LOG_ERR, "Failed to register camera event callback: %s.", seekcamera_error_get_str(status));
        }
    }
    else
    {
        returnVal = false;
        syslog(LOG_ERR, "Failed to create camera manager: %s.", seekcamera_error_get_str(status));
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::start()");
#endif
    return returnVal;
}

void EchoThermCamera::stop()
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::stop()");
#endif
    _closeSession();
    if (mp_cameraManager)
    {
        seekcamera_manager_destroy((seekcamera_manager_t **)&mp_cameraManager);
        mp_cameraManager = nullptr;
    }
    m_chipId.clear();
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::stop()");
#endif
}

std::string EchoThermCamera::getStatus() const
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::getStatus()");
#endif
    std::string statusStr;
    if (mp_camera && seekcamera_is_active((seekcamera_t *)mp_camera))
    {
        statusStr = "echotherm camera connected";
    }
    else
    {
        statusStr = "waiting for echotherm camera";
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::getStatus() with %s", statusStr.c_str());
#endif
    return statusStr;
}

std::string EchoThermCamera::getZoom() const
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::getZoomRate()");
#endif
    std::stringstream ss;
    ss << "{";
    ss << "zoom=" << m_currentZoom;
    ss << ", zoomRate=" << m_zoomRate;
    ss << ", maxZoom=" << m_maxZoom;
    ss << ", roiSize={" << m_roiWidth << ", " << m_roiHeight << "}";
    ss << ", roiOffset={" << m_roiX << ", " << m_roiY << "}";
    ss << "}";
    std::string zoomStatus = ss.str();
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::getZoomRate() with %s", zoomStatus.c_str());
#endif
    return zoomStatus;
}

void EchoThermCamera::setZoomRate(double zoomRate)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setZoomRate(%f)", zoomRate);
#endif
    if (zoomRate != zoomRate)
    {
        zoomRate = 0;
    }
    m_zoomRate = zoomRate;
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setZoomRate(%f)", zoomRate);
#endif
}

void EchoThermCamera::setMaxZoom(double maxZoom)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setMaxZoom(%f)", maxZoom);
#endif
    if (maxZoom != maxZoom || maxZoom < n_minZoom)
    {
        maxZoom = n_defaultMaxZoom;
    }
    m_maxZoom = maxZoom;
    if (m_currentZoom > m_maxZoom)
    {
        m_currentZoom = m_maxZoom;
        m_zoomRate = 0;
        m_roiWidth = std::min<int>(m_width, std::max<int>(1, (int)std::rint(m_width / m_currentZoom)));
        m_roiHeight = std::min<int>(m_height, std::max<int>(1, (int)std::rint(m_height / m_currentZoom)));
        m_roiX = (m_width - m_roiWidth) >> 1;
        m_roiY = (m_height - m_roiHeight) >> 1;
        if (m_roiWidth >= m_width || m_roiHeight >= m_height || m_currentZoom <= n_minZoom)
        {
            m_currentZoom = n_minZoom;
            m_roiWidth = m_width;
            m_roiHeight = m_height;
            m_roiX = 0;
            m_roiY = 0;
        }
        else if (m_roiWidth <= 1 || m_roiHeight <= 1 || m_currentZoom >= m_maxZoom)
        {
            m_currentZoom = m_maxZoom;
        }
#ifdef DEBUG
        syslog(LOG_DEBUG, "setMaxZoom m_currentZoom=%f, m_zoomRate=%f, m_roiWidth=%d, m_roiHeight=%d, m_roiX=%d, m_roiY=%d", m_currentZoom, m_zoomRate, m_roiWidth, m_roiHeight, m_roiX, m_roiY);
#endif
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setMaxZoom(%f)", maxZoom);
#endif
}

void EchoThermCamera::setZoom(double zoom)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setZoom(%f)", zoom);
#endif
    if (zoom != zoom || zoom < n_minZoom)
    {
        zoom = n_minZoom;
    }
    else if (zoom > m_maxZoom)
    {
        zoom = m_maxZoom;
    }
    m_currentZoom = zoom;
    m_zoomRate = 0;
    m_roiWidth = std::min<int>(m_width, std::max<int>(1, (int)std::rint(m_width / m_currentZoom)));
    m_roiHeight = std::min<int>(m_height, std::max<int>(1, (int)std::rint(m_height / m_currentZoom)));
    m_roiX = (m_width - m_roiWidth) >> 1;
    m_roiY = (m_height - m_roiHeight) >> 1;
    if (m_roiWidth >= m_width || m_roiHeight >= m_height || m_currentZoom <= n_minZoom)
    {
        m_currentZoom = n_minZoom;
        m_roiWidth = m_width;
        m_roiHeight = m_height;
        m_roiX = 0;
        m_roiY = 0;
    }
    else if (m_roiWidth <= 1 || m_roiHeight <= 1 || m_currentZoom >= m_maxZoom)
    {
        m_currentZoom = m_maxZoom;
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "setZoom m_currentZoom=%f, m_zoomRate=%f, m_roiWidth=%d, m_roiHeight=%d, m_roiX=%d, m_roiY=%d", m_currentZoom, m_zoomRate, m_roiWidth, m_roiHeight, m_roiX, m_roiY);
#endif
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setZoom(%f)", zoom);
#endif
}

std::string EchoThermCamera::startRecording(std::filesystem::path const &filePath)
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::startRecording(%s)", filePath.string().c_str());
#endif
    std::string status;
    if (mp_videoWriter && mp_videoWriter->isOpened())
    {
        status = "Already recording to video file " + m_videoFilePath.string();
    }
    else
    {
        if(!m_recordingStatus.empty())
        {
            status = "Previous recording session stopped unexpectedly: "+m_recordingStatus+"; ";
            m_recordingStatus.clear();
        }
        auto extension = filePath.extension().string();
        std::transform(std::begin(extension), std::end(extension), std::begin(extension), [](auto const c)
                       { return (char)std::tolower(c); });
        if (extension == ".mp4")
        {
            std::unique_lock<std::mutex> recordingLock(m_recordingFrameQueueMut);
            m_recordingFrameQueue.clear();
            m_videoFilePath = filePath;
            auto const fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
            double fps = n_frameRate; // 27 fps assumed
            try
            {
                mp_videoWriter = std::make_unique<cv::VideoWriter>(m_videoFilePath.string(), fourcc, fps, cv::Size(m_width, m_height), m_frameFormat != SEEKCAMERA_FRAME_FORMAT_GRAYSCALE);
                if (mp_videoWriter->isOpened())
                {
                    status += "Video file " + m_videoFilePath.string() + " opened for writing";
                }
                else
                {
                    status += "Failed to open file " + m_videoFilePath.string() + " opened for writing";
                }
            }
            catch (cv::Exception const &e)
            {
                status += "Failed to open file " + m_videoFilePath.string() + " opened for writing : " + e.msg;
            }
            recordingLock.unlock();
            m_recordingFramesReadyCondition.notify_one();
        }
        else
        {
            status += "Extension must be '.mp4'";
        }
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::startRecording(%s) with %s", filePath.string().c_str(), status.c_str());
#endif
    return status;
}

std::string EchoThermCamera::takeScreenshot(std::filesystem::path const &filePath)
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::takeScreenshot(%s)", filePath.string().c_str());
#endif
    std::string status;
    // set the path so that the next frame will see the path and write it
    // then wait for the status to update
    m_screenshotFilePath = filePath;
    std::unique_lock<std::mutex> screenshotStatusLock(m_screenshotStatusReadyMut);
    m_screenshotStatusReadyCondition.wait(screenshotStatusLock, [this]()
                                          { return !m_screenshotStatus.empty() || !m_recordingThreadRunning; });
    if (m_screenshotStatus.empty())
    {
        status = "Failed to take screenshot on file path " + filePath.string() + " because the screenshot thread was stopped";
    }
    else
    {
        status = std::move(m_screenshotStatus);
        m_screenshotStatus.clear();
    }
    screenshotStatusLock.unlock();
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::takeScreenshot(%s) with %s", filePath.string().c_str(), status.c_str());
#endif
    return status;
}

std::string EchoThermCamera::stopRecording()
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::stopRecording()");
#endif
    std::string status;
    if (mp_videoWriter && mp_videoWriter->isOpened())
    {
        std::lock_guard<std::mutex> recordingLock(m_recordingFrameQueueMut);
        try
        {
            //flush the frames
            while (!m_recordingFrameQueue.empty())
            {
                cv::Mat queueFrame=std::move(m_recordingFrameQueue.front());
                m_recordingFrameQueue.pop_front();
                 cv::Mat frameToWrite;
                if(queueFrame.channels()==4)
                {
                    assert(m_frameFormat == SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888);
                    cv::cvtColor(queueFrame,frameToWrite, cv::COLOR_BGRA2BGR);
                }
                else
                {
                    frameToWrite = std::move(queueFrame);
                }
                 mp_videoWriter->write(frameToWrite);
            }
            mp_videoWriter->release();
            status = "Successfully finished writing video file "+m_videoFilePath.string();
        }
        catch (cv::Exception const &e)
        {
            status = "Exception occurred while writing video frame to " + m_videoFilePath.string() + " : " + e.msg;
            mp_videoWriter->release();
        }
    }
    else
    {
        std::lock_guard<std::mutex> recordingStatusLock(m_recordingStatusReadyMut);
        if (m_recordingStatus.empty())
        {
            status = "Recording was not in progress";
        }
        else
        {

            status = std::move(m_recordingStatus);
            m_recordingStatus.clear();
        }
    }
    mp_videoWriter.release();
    m_videoFilePath.clear();
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::stopRecording() with %s", status.c_str());
#endif
    return status;
}

void EchoThermCamera::_connect(void *p_camera)
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_connect()");
#endif
    _closeSession();
    mp_camera = p_camera;
    _openSession(false);
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_connect()");
#endif
}

void EchoThermCamera::_handleReadyToPair(void *p_camera)
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_handleReadyToPair()");
#endif
    // Attempt to pair the camera automatically.
    // Pairing refers to the process by which the sensor is associated with the host and the embedded processor.
    auto const status = seekcamera_store_calibration_data((seekcamera_t *)p_camera, nullptr, nullptr, nullptr);
    if (status != SEEKCAMERA_SUCCESS)
    {
        syslog(LOG_ERR, "Failed to pair device: %s.", seekcamera_error_get_str(status));
    }
    // Start imaging.
    _connect(p_camera);
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_handleReadyToPair()");
#endif
}

void EchoThermCamera::_closeSession()
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_closeSession()");
#endif
    _stopShutterClickThread();
    _stopRecordingThread();
    seekcamera_error_t status = SEEKCAMERA_SUCCESS;
    if (mp_camera)
    {
        syslog(LOG_NOTICE, "Calling seekcamera_capture_session_stop");
        status = seekcamera_capture_session_stop((seekcamera_t *)mp_camera);
        if (status != SEEKCAMERA_SUCCESS)
        {
            syslog(LOG_ERR, "Failed to stop capture session: %s.", seekcamera_error_get_str(status));
        }
        mp_camera = nullptr;
    }
    if (m_loopbackDevice >= 0)
    {
        syslog(LOG_NOTICE, "Closing loopback device %d", m_loopbackDevice);
        close(m_loopbackDevice);
        m_loopbackDevice = -1;
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_closeSession()");
#endif
}

void EchoThermCamera::_openSession(bool reconnect)
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_openSession(%d)", reconnect);
#endif
    // Register a frame available callback function.
    auto status = SEEKCAMERA_SUCCESS;
    m_screenshotFilePath.clear();
    m_screenshotStatus.clear();
    m_videoFilePath.clear();
    m_recordingStatus.clear();
    m_recordingFrameQueue.clear();
    if (!reconnect)
    {
        status = seekcamera_register_frame_available_callback((seekcamera_t *)mp_camera,
                                                              [](seekcamera_t *, seekcamera_frame_t *p_cameraFrame, void *p_userData)
                                                              {
                                                                  auto *p_this = (EchoThermCamera *)p_userData;
                                                                  std::lock_guard<decltype(p_this->m_mut)> lock{p_this->m_mut};
                                                                  seekframe_t *p_frame = nullptr;
                                                                  // TODO: support the ability to capture multiple formats
                                                                  // For example, you can pull YUY2 data AND thermography data
                                                                  // You'd write the YUY2 data to the frame and you'd write the thermography data to a CSV
                                                                  auto const status = seekcamera_frame_get_frame_by_format(p_cameraFrame, (seekcamera_frame_format_t)p_this->m_frameFormat, &p_frame);
                                                                  if (status == SEEKCAMERA_SUCCESS)
                                                                  {
                                                                      if (p_this->m_loopbackDevice < 0)
                                                                      {
                                                                          int const frameWidth = (int)seekframe_get_width(p_frame);
                                                                          int const frameHeight = (int)seekframe_get_height(p_frame);
                                                                          p_this->_openDevice(frameWidth, frameHeight);
                                                                      }
                                                                      if (p_this->m_loopbackDevice >= 0)
                                                                      {
                                                                          void *const p_frameData = seekframe_get_data(p_frame);
                                                                          size_t const frameDataSize = seekframe_get_data_size(p_frame);
                                                                          ssize_t const written = p_this->_writeBytes(p_frameData, frameDataSize);
                                                                          if (written < 0)
                                                                          {
                                                                              syslog(LOG_ERR, "Error writing %zu bytes to v4l2 device %s: %m", frameDataSize, p_this->m_loopbackDeviceName.c_str());
                                                                          }
                                                                          p_this->_doContinuousZoom();
                                                                      }
                                                                  }
                                                                  else
                                                                  {
                                                                      syslog(LOG_ERR, "Failed to get frame: %s.", seekcamera_error_get_str(status));
                                                                  }
                                                              },
                                                              (void *)this);
    }
    if (status == SEEKCAMERA_SUCCESS)
    {
        status = seekcamera_set_pipeline_mode((seekcamera_t *)mp_camera, (seekcamera_pipeline_mode_t)m_pipelineMode);
        if (status == SEEKCAMERA_SUCCESS)
        {
            // Start the capture session.
            status = seekcamera_capture_session_start((seekcamera_t *)mp_camera, m_frameFormat);
            if (status == SEEKCAMERA_SUCCESS)
            {
                if (auto const shutterStatus = seekcamera_set_shutter_mode((seekcamera_t *)mp_camera, (seekcamera_shutter_mode_t)m_shutterMode); shutterStatus != SEEKCAMERA_SUCCESS)
                {
                    syslog(LOG_ERR, "Failed ot set shutter mode to %d.", m_shutterMode);
                }
                if (auto const paletteStatus = seekcamera_set_color_palette((seekcamera_t *)mp_camera, (seekcamera_color_palette_t)m_colorPalette);
                    paletteStatus != SEEKCAMERA_SUCCESS)
                {
                    syslog(LOG_ERR, "Failed to set color palette to %s.", seekcamera_color_palette_get_str((seekcamera_color_palette_t)m_colorPalette));
                }
                if ((seekcamera_pipeline_mode_t)m_pipelineMode != SEEKCAMERA_IMAGE_SEEKVISION)
                {
                    if (auto const filterStatus = seekcamera_set_filter_state((seekcamera_t *)mp_camera, SEEKCAMERA_FILTER_SHARPEN_CORRECTION, (seekcamera_filter_state_t)m_sharpenFilterMode); filterStatus != SEEKCAMERA_SUCCESS)
                    {
                        syslog(LOG_ERR, "Failed to set filter state to %s: %s.", seekcamera_get_filter_state_str(SEEKCAMERA_FILTER_SHARPEN_CORRECTION, (seekcamera_filter_state_t)m_sharpenFilterMode), seekcamera_error_get_str(filterStatus));
                    }
                    if (auto const filterStatus = seekcamera_set_filter_state((seekcamera_t *)mp_camera, SEEKCAMERA_FILTER_FLAT_SCENE_CORRECTION, (seekcamera_filter_state_t)m_flatSceneFilterMode); filterStatus != SEEKCAMERA_SUCCESS)
                    {
                        syslog(LOG_ERR, "Failed to set filter state to %s: %s", seekcamera_get_filter_state_str(SEEKCAMERA_FILTER_FLAT_SCENE_CORRECTION, (seekcamera_filter_state_t)m_flatSceneFilterMode), seekcamera_error_get_str(filterStatus));
                    }
                    if (auto const filterStatus = seekcamera_set_filter_state((seekcamera_t *)mp_camera, SEEKCAMERA_FILTER_GRADIENT_CORRECTION, (seekcamera_filter_state_t)m_gradientFilterMode); filterStatus != SEEKCAMERA_SUCCESS)
                    {
                        syslog(LOG_ERR, "Failed to set filter state to %s: %s", seekcamera_get_filter_state_str(SEEKCAMERA_FILTER_GRADIENT_CORRECTION, (seekcamera_filter_state_t)m_gradientFilterMode), seekcamera_error_get_str(filterStatus));
                    }
                }
            }
            else
            {
                syslog(LOG_ERR, "Failed to start capture session: %s.", seekcamera_error_get_str(status));
            }
        }
        else
        {
            syslog(LOG_ERR, "Failed to set image pipeline mode to %s: %s.", seekcamera_pipeline_mode_get_str((seekcamera_pipeline_mode_t)m_pipelineMode), seekcamera_error_get_str(status));
        }
    }
    else
    {
        syslog(LOG_ERR, "Failed to register frame callback: %s.", seekcamera_error_get_str(status));
    }
    _startShutterClickThread();
    _startRecordingThread();
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_openSession(%d)", reconnect);
#endif
}

void EchoThermCamera::_openDevice(int width, int height)
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_openDevice(%d, %d)", width, height);
#endif
    // TODO find a way to detect the format automatically
    m_loopbackDevice = open(m_loopbackDeviceName.c_str(), O_RDWR);
    if (m_loopbackDevice < 0)
    {
        syslog(LOG_ERR, "Error opening loopback device %s: %m", m_loopbackDeviceName.c_str());
    }
    else
    {
        struct v4l2_format v;
        v.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        auto deviceOpenResult = ioctl(m_loopbackDevice, VIDIOC_G_FMT, &v);
        if (deviceOpenResult < 0)
        {
            syslog(LOG_ERR, "VIDIOC_G_FMT error on device %s: %m", m_loopbackDeviceName.c_str());
        }
        else
        {
            v.fmt.pix.width = width;
            v.fmt.pix.height = height;
            switch (m_frameFormat)
            {

           
            case SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888:
                v.fmt.pix.pixelformat = V4L2_PIX_FMT_ARGB32;
                v.fmt.pix.sizeimage = width * height * 4;
                break;
            case SEEKCAMERA_FRAME_FORMAT_GRAYSCALE:
                v.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
                v.fmt.pix.sizeimage = width * height;
                break;
            case SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6:
            case SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT:
            case SEEKCAMERA_FRAME_FORMAT_PRE_AGC:
            case SEEKCAMERA_FRAME_FORMAT_CORRECTED:
            case SEEKCAMERA_FRAME_FORMAT_COLOR_YUY2:
            case SEEKCAMERA_FRAME_FORMAT_COLOR_RGB565:
                //v.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
                //v.fmt.pix.sizeimage = width * height * 2;
                //break;
                // v.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
                // v.fmt.pix.sizeimage = width * height * 2;
                // break;
            case SEEKCAMERA_FRAME_FORMAT_COLOR_AYUV:
                // note: probably not supported by gstreamer v4l2src
                // v.fmt.pix.pixelformat = V4L2_PIX_FMT_AYUV32;
                // v.fmt.pix.sizeimage = width * height * 4;
                // break;
            default:
                syslog(LOG_ERR, "Unsupported frame format %d.", m_frameFormat);
                deviceOpenResult = -1;
                break;
            }
            if (deviceOpenResult >= 0)
            {
                deviceOpenResult = ioctl(m_loopbackDevice, VIDIOC_S_FMT, &v);
                if (deviceOpenResult < 0)
                {
                    syslog(LOG_ERR, "VIDIOC_S_FMT error on device %s: %m", m_loopbackDeviceName.c_str());
                }
                else
                {
                    syslog(LOG_NOTICE, "Opened loopback device with path %s.", m_loopbackDeviceName.c_str());
                }
            }
        }
    }
    m_zoomRate = 0.0;
    m_currentZoom = n_minZoom;
    m_width = width;
    m_height = height;
    m_roiX = 0;
    m_roiY = 0;
    m_roiWidth = width;
    m_roiHeight = height;
    m_lastZoomTime = std::chrono::system_clock::time_point();
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_openDevice(%d, %d)", width, height);
#endif
}

void EchoThermCamera::_startShutterClickThread()
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_startShutterClickThread()");
#endif
    if (m_shutterMode > 0)
    {
        m_shutterClickThreadRunning = true;
        m_shutterClickThread = std::thread([this]()
                                           {
                                               auto const interval = std::chrono::seconds(m_shutterMode);
                                               for (;;)
                                               {
                                                   std::unique_lock<decltype(m_mut)> lock(m_mut);
                                                   if (mp_camera)
                                                   {
                                                       if (auto const shutterClickResult = seekcamera_shutter_trigger((seekcamera_t *)mp_camera); shutterClickResult != SEEKCAMERA_SUCCESS)
                                                       {
                                                           syslog(LOG_ERR, "Failed to manually trigger camera shutter: %s.", seekcamera_error_get_str(shutterClickResult));
                                                       }
                                                   }
                                                   m_shutterClickCondition.wait_for(lock, interval, [this]()
                                                                                    { return !m_shutterClickThreadRunning.load(); });
                                                   if (!m_shutterClickThreadRunning)
                                                   {
                                                       break;
                                                   }
                                               } });
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_startShutterClickThread()");
#endif
}

void EchoThermCamera::_stopShutterClickThread()
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_stopShutterClickThread()");
#endif
    m_shutterClickThreadRunning = false;
    m_shutterClickCondition.notify_one();
    if (m_shutterClickThread.joinable())
    {
        m_shutterClickThread.join();
    }
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_stopShutterClickThread()");
#endif
}

void EchoThermCamera::_startRecordingThread()
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_startRecordingThread()");
#endif
    m_recordingThreadRunning = true;
    m_recordingThread = std::thread([this]()
                                    {
        for (;;)
        {
            std::unique_lock<decltype(m_recordingFrameQueueMut)> lock(m_recordingFrameQueueMut);
            m_recordingFramesReadyCondition.wait(lock,[this](){return (!m_recordingFrameQueue.empty() && (!m_screenshotFilePath.empty()||(mp_videoWriter && mp_videoWriter->isOpened()))) || !m_recordingThreadRunning.load();});
            if (!m_recordingThreadRunning)
            {
                if(mp_videoWriter)
                {
                    mp_videoWriter->release();
                    mp_videoWriter.release();
                }
                break;
            }
            assert(!m_recordingFrameQueue.empty());
            cv::Mat queueFrame=std::move(m_recordingFrameQueue.front());
            m_recordingFrameQueue.pop_front();
            if(!m_screenshotFilePath.empty())
            {
                try
                {
                    if(cv::imwrite(m_screenshotFilePath.string(),queueFrame))
                    {
                        m_screenshotStatus = "Wrote screenshot to "+m_screenshotFilePath.string();
                    }
                    else
                    {
                        m_screenshotStatus = "Failed to write screenshot to "+m_screenshotFilePath.string()+" because of an unspecified error";
                    }
                }
                catch( cv::Exception const & e)
                {
                    m_screenshotStatus = "Exception occurred while writing screenshot to "+m_screenshotFilePath.string()+" : "+e.msg;
                }
                m_screenshotFilePath.clear();
                m_screenshotStatusReadyCondition.notify_one();
            }
            if(mp_videoWriter && mp_videoWriter->isOpened())
            {
                cv::Mat frameToWrite;
                if(queueFrame.channels()==4)
                {
                    assert(m_frameFormat == SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888);
                    cv::cvtColor(queueFrame,frameToWrite, cv::COLOR_BGRA2BGR);
                }
                else
                {
                    frameToWrite = std::move(queueFrame);
                }
                try
                {
                    mp_videoWriter->write(frameToWrite);
                }
                catch(cv::Exception const& e)
                {
                    m_recordingStatus = "Exception occurred while writing video frame to "+m_videoFilePath.string()+" : "+e.msg;
                    mp_videoWriter->release();
                    mp_videoWriter.release();
                    m_recordingStatusReadyCondition.notify_one();
                }
            }
            lock.unlock();
            
        } });

#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_startScreenshotThread()");
#endif
}

void EchoThermCamera::_stopRecordingThread()
{
#ifdef DEBUG
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_stopRecordingThread()");
#endif
    m_recordingThreadRunning = false;
    m_recordingFramesReadyCondition.notify_one();
    if (m_recordingThread.joinable())
    {
        m_recordingThread.join();
    }
    m_recordingFrameQueue.clear();
    m_screenshotFilePath.clear();
    m_videoFilePath.clear();
    m_recordingStatus.clear();
    m_screenshotStatus.clear();
#ifdef DEBUG
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_stopRecordingThread()");
#endif
}

void EchoThermCamera::_doContinuousZoom()
{
    auto const currentTime = std::chrono::system_clock::now();
    if (m_zoomRate > 0)
    {
        // zooming in
        if (m_roiWidth > 1 && m_roiHeight > 1 && m_currentZoom < m_maxZoom)
        {
            if (m_lastZoomTime != std::chrono::system_clock::time_point())
            {
                auto const elapsedTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - m_lastZoomTime).count();
                auto const deltaZoom = double(double(1 + m_zoomRate) * elapsedTimeMs) / 1000.0;
                m_currentZoom = std::min(m_maxZoom, m_currentZoom + deltaZoom);
                m_roiWidth = std::max<int>(1, (int)std::rint(m_width / m_currentZoom));
                m_roiHeight = std::max<int>(1, (int)std::rint(m_height / m_currentZoom));
                m_roiX = (m_width - m_roiWidth) >> 1;
                m_roiY = (m_height - m_roiHeight) >> 1;
#ifdef DEBUG
                syslog(LOG_DEBUG, "zooming in  m_currentZoom=%f, m_zoomRate=%f, m_roiWidth=%d, m_roiHeight=%d, m_roiX=%d, m_roiY=%d, elapsedTime(ms) = %d, deltaZoom=%f", m_currentZoom, m_zoomRate, m_roiWidth, m_roiHeight, m_roiX, m_roiY, (int)elapsedTimeMs, deltaZoom);
#endif
            }
        }
        if (m_roiWidth <= 1 || m_roiHeight <= 1 || m_currentZoom >= m_maxZoom)
        {
            // stop zooming in
            m_zoomRate = 0;
            m_currentZoom = m_maxZoom;
        }
    }
    else if (m_zoomRate < 0)
    {
        if (m_roiWidth < m_width && m_roiHeight < m_height && m_currentZoom > n_minZoom)
        {
            if (m_lastZoomTime != std::chrono::system_clock::time_point())
            {
                auto const elapsedTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - m_lastZoomTime).count();
                auto const deltaZoom = double(double(1 - m_zoomRate) * elapsedTimeMs) / 1000.0;
                m_currentZoom = std::max(n_minZoom, m_currentZoom - deltaZoom);
                m_roiWidth = std::min<int>(m_width, (int)std::rint(m_width / m_currentZoom));
                m_roiHeight = std::min<int>(m_height, (int)std::rint(m_height / m_currentZoom));
                m_roiX = (m_width - m_roiWidth) >> 1;
                m_roiY = (m_height - m_roiHeight) >> 1;
#ifdef DEBUG
                syslog(LOG_DEBUG, "zooming out m_currentZoom=%f, m_zoomRate=%f, m_roiWidth=%d, m_roiHeight=%d, m_roiX=%d, m_roiY=%d, elapsedTime(ms) = %d, deltaZoom=%f", m_currentZoom, m_zoomRate, m_roiWidth, m_roiHeight, m_roiX, m_roiY, (int)elapsedTimeMs, deltaZoom);
#endif
            }
        }
        if (m_roiWidth >= m_width || m_roiHeight >= m_height || m_currentZoom <= n_minZoom)
        {
            // stop zooming out
            m_zoomRate = 0;
            m_currentZoom = n_minZoom;
            m_roiX = 0;
            m_roiY = 0;
            m_roiWidth = m_width;
            m_roiHeight = m_height;
        }
    }
    m_lastZoomTime = currentTime;
}

void EchoThermCamera::_pushFrame(int cvFrameType, void *p_frameData)
{
    ;
    if (!m_screenshotFilePath.empty() || (mp_videoWriter && mp_videoWriter->isOpened()))
    {
        {
            std::lock_guard lock(m_recordingFrameQueueMut);
            m_recordingFrameQueue.push_back(cv::Mat(m_height, m_width, cvFrameType, p_frameData).clone());
        }
        m_recordingFramesReadyCondition.notify_one();
    }
}

ssize_t EchoThermCamera::_writeBytes(void *p_frameData, size_t frameDataSize)
{
    ssize_t bytesWritten = -1;
    cv::Mat cvFrame;
    if (m_roiX == 0 && m_roiY == 0 && m_roiWidth == m_width && m_roiHeight == m_height)
    {
        bytesWritten = write(m_loopbackDevice, p_frameData, frameDataSize);
        if (!m_screenshotFilePath.empty() || (mp_videoWriter && mp_videoWriter->isOpened()))
        {
            switch (m_frameFormat)
            {
            case SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888:
            {
                _pushFrame(CV_8UC4, p_frameData);
                break;
            }
            case SEEKCAMERA_FRAME_FORMAT_GRAYSCALE:
            {
                _pushFrame(CV_8U, p_frameData);
                break;
            }
            case SEEKCAMERA_FRAME_FORMAT_COLOR_RGB565:
            case SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6:
            case SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT:
            case SEEKCAMERA_FRAME_FORMAT_PRE_AGC:
            case SEEKCAMERA_FRAME_FORMAT_CORRECTED:
            case SEEKCAMERA_FRAME_FORMAT_COLOR_AYUV:
            case SEEKCAMERA_FRAME_FORMAT_COLOR_YUY2:
            default:
            {
                std::lock_guard screenshotLock(m_screenshotStatusReadyMut);
                m_screenshotStatus = "Could not write screenshot to " + m_screenshotFilePath.string() + " because the frame format is not supported";
                m_screenshotFilePath.clear();
            }
                m_screenshotStatusReadyCondition.notify_one();
                break;
            }
        }
    }
    else
    {
        switch (m_frameFormat)
        {
        case SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888:
        {
            cv::Mat srcMat(m_height, m_width, CV_8UC4, p_frameData);
            cv::Mat srcROI(srcMat, cv::Rect(m_roiX, m_roiY, m_roiWidth, m_roiHeight));
            cv::Mat dstMat;
            cv::resize(srcROI, dstMat, cv::Size(m_width, m_height), 0, 0, cv::INTER_LINEAR);
            bytesWritten = write(m_loopbackDevice, dstMat.data, dstMat.total() * dstMat.elemSize());
            _pushFrame(dstMat.type(), dstMat.data);
            break;
        }
        case SEEKCAMERA_FRAME_FORMAT_GRAYSCALE:
        {
            cv::Mat srcMat(m_height, m_width, CV_8U, p_frameData);
            cv::Mat srcROI(srcMat, cv::Rect(m_roiX, m_roiY, m_roiWidth, m_roiHeight));
            cv::Mat dstMat;
            cv::resize(srcROI, dstMat, cv::Size(m_width, m_height), 0, 0, cv::INTER_LINEAR);
            bytesWritten = write(m_loopbackDevice, dstMat.data, dstMat.total() * dstMat.elemSize());
            _pushFrame(dstMat.type(), dstMat.data);
            break;
        }
        case SEEKCAMERA_FRAME_FORMAT_COLOR_RGB565:
        case SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6:
        case SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT:
        case SEEKCAMERA_FRAME_FORMAT_PRE_AGC:
        case SEEKCAMERA_FRAME_FORMAT_CORRECTED:
        case SEEKCAMERA_FRAME_FORMAT_COLOR_AYUV:
        default:
        {
            if (!m_screenshotFilePath.empty())
            {
                {
                    std::lock_guard screenshotLock(m_screenshotStatusReadyMut);
                    m_screenshotStatus = "Could not write screenshot to " + m_screenshotFilePath.string() + " because the frame format is not supported";
                    m_screenshotFilePath.clear();
                }
                m_screenshotStatusReadyCondition.notify_one();
                break;
            }
        }
        }
    }
    return bytesWritten;
}