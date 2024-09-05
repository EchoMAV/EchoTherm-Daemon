#include "EchoThermCamera.h"
#include "seekcamera/seekcamera.h"
#include "seekcamera/seekcamera_manager.h"
#include <syslog.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>

EchoThermCamera::EchoThermCamera()
    : m_loopbackDeviceName{},
      m_chipId{},
      m_frameFormat{int(SEEKCAMERA_FRAME_FORMAT_COLOR_YUY2)},
      m_colorPalette{int(SEEKCAMERA_COLOR_PALETTE_WHITE_HOT)},
      m_shutterMode{int(SEEKCAMERA_SHUTTER_MODE_AUTO)},
      m_sharpenFilterMode{int(SEEKCAMERA_FILTER_STATE_DISABLED)},
      m_flatSceneFilterMode{int(SEEKCAMERA_FILTER_STATE_DISABLED)},
      m_gradientFilterMode{int(SEEKCAMERA_FILTER_STATE_DISABLED)},
      m_pipelineMode{int(SEEKCAMERA_IMAGE_SEEKVISION)},
      mp_camera{nullptr},
      mp_cameraManager{nullptr},
      m_loopbackDevice{-1},
      m_mut{},
      m_shutterClickThread{},
      m_shutterClickCondition{},
      m_shutterClickThreadRunning{false}
{
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::EchoThermCamera()");
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::EchoThermCamera()");
}

EchoThermCamera::~EchoThermCamera()
{
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::~EchoThermCamera()");
    stop();
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::~EchoThermCamera()");
}

void EchoThermCamera::setLoopbackDeviceName(std::string loopbackDeviceName)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setLoopbackDeviceName(%s)", loopbackDeviceName.c_str());
    if (loopbackDeviceName != m_loopbackDeviceName)
    {
        m_loopbackDeviceName = std::move(loopbackDeviceName);
    }
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setLoopbackDeviceName(%s)", loopbackDeviceName.c_str());
}

void EchoThermCamera::setFrameFormat(int frameFormat)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setFrameFormat(%d)", frameFormat);
    if (frameFormat != m_frameFormat)
    {
        switch (frameFormat)
        {
        case SEEKCAMERA_FRAME_FORMAT_COLOR_YUY2:
        case SEEKCAMERA_FRAME_FORMAT_COLOR_AYUV:
        case SEEKCAMERA_FRAME_FORMAT_COLOR_RGB565:
        case SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888:
        case SEEKCAMERA_FRAME_FORMAT_GRAYSCALE:
            m_frameFormat = frameFormat;
            break;
        // TODO support these as well as bit-wise ORed frame formats
        case SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FIXED_10_6:
        case SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT:
        case SEEKCAMERA_FRAME_FORMAT_PRE_AGC:
        case SEEKCAMERA_FRAME_FORMAT_CORRECTED:
        default:
            syslog(LOG_WARNING, "The frame format %d is invalid.", frameFormat);
            break;
        }
    }
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setFrameFormat(%d)", frameFormat);
}

void EchoThermCamera::setColorPalette(int colorPalette)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setColorPalette(%d)", colorPalette);
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
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setColorPalette(%d)", colorPalette);
}

void EchoThermCamera::setShutterMode(int shutterMode)
{
    std::unique_lock<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setShutterMode(%d)", shutterMode);
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
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setShutterMode(%d)", shutterMode);
}

void EchoThermCamera::_updateFilterHelper(int filterType, int filterState)
{
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_updateFilterHelper(%d, %d)", filterType, filterState);
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
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_updateFilterHelper(%d, %d)", filterType, filterState);
}

void EchoThermCamera::setSharpenFilter(int sharpenFilterMode)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setSharpenFilter(%d)", sharpenFilterMode);
    if (m_sharpenFilterMode != sharpenFilterMode)
    {
        if (sharpenFilterMode != (int)SEEKCAMERA_FILTER_STATE_DISABLED)
        {
            sharpenFilterMode = (int)SEEKCAMERA_FILTER_STATE_ENABLED;
        }
        m_sharpenFilterMode = sharpenFilterMode;
        _updateFilterHelper(SEEKCAMERA_FILTER_SHARPEN_CORRECTION, m_sharpenFilterMode);
    }
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setSharpenFilter(%d)", sharpenFilterMode);
}

void EchoThermCamera::setFlatSceneFilter(int flatSceneFilterMode)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setFlatSceneFilter(%d)", flatSceneFilterMode);
    if (m_flatSceneFilterMode != flatSceneFilterMode)
    {
        if (flatSceneFilterMode != (int)SEEKCAMERA_FILTER_STATE_DISABLED)
        {
            flatSceneFilterMode = (int)SEEKCAMERA_FILTER_STATE_ENABLED;
        }
        m_flatSceneFilterMode = flatSceneFilterMode;
        _updateFilterHelper(SEEKCAMERA_FILTER_FLAT_SCENE_CORRECTION, m_flatSceneFilterMode);
    }
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setFlatSceneFilter(%d)", flatSceneFilterMode);
}

void EchoThermCamera::setGradientFilter(int gradientFilterMode)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setGradientFilter(%d)", gradientFilterMode);
    if (m_gradientFilterMode != gradientFilterMode)
    {
        if (gradientFilterMode != (int)SEEKCAMERA_FILTER_STATE_DISABLED)
        {
            gradientFilterMode = (int)SEEKCAMERA_FILTER_STATE_ENABLED;
        }
        m_gradientFilterMode = gradientFilterMode;
        _updateFilterHelper(SEEKCAMERA_FILTER_GRADIENT_CORRECTION, m_gradientFilterMode);
    }
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setGradientFilter(%d)", gradientFilterMode);
}

void EchoThermCamera::setPipelineMode(int pipelineMode)
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::setPipelineMode(%d)", pipelineMode);
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
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::setPipelineMode(%d)", pipelineMode);
}

void EchoThermCamera::triggerShutter()
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::triggerShutter()");
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
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::triggerShutter()");
}

bool EchoThermCamera::start()
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::start()");
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
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::start()");
    return returnVal;
}

void EchoThermCamera::stop()
{
    std::lock_guard<decltype(m_mut)> lock{m_mut};
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::stop()");
    _closeSession();
    if (mp_cameraManager)
    {
        seekcamera_manager_destroy((seekcamera_manager_t **)&mp_cameraManager);
        mp_cameraManager = nullptr;
    }
    m_chipId.clear();
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::stop()");
}

void EchoThermCamera::_connect(void *p_camera)
{
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_connect()");
    _closeSession();
    mp_camera = p_camera;
    _openSession(false);
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_connect()");
}

void EchoThermCamera::_handleReadyToPair(void *p_camera)
{
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_handleReadyToPair()");
    // Attempt to pair the camera automatically.
    // Pairing refers to the process by which the sensor is associated with the host and the embedded processor.
    auto const status = seekcamera_store_calibration_data((seekcamera_t *)p_camera, nullptr, nullptr, nullptr);
    if (status != SEEKCAMERA_SUCCESS)
    {
        syslog(LOG_ERR, "Failed to pair device: %s.", seekcamera_error_get_str(status));
    }
    // Start imaging.
    _connect(p_camera);
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_handleReadyToPair()");
}

void EchoThermCamera::_closeSession()
{
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_closeSession()");
    _stopShutterClickThread();
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
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_closeSession()");
}

void EchoThermCamera::_openSession(bool reconnect)
{
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_openSession(%d)", reconnect);
    // Register a frame available callback function.
    auto status = SEEKCAMERA_SUCCESS;
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
                                                                          ssize_t written = write(p_this->m_loopbackDevice, p_frameData, frameDataSize);
                                                                          if (written < 0)
                                                                          {
                                                                              syslog(LOG_ERR, "Error writing %zu bytes to v4l2 device %s: %m", frameDataSize, p_this->m_loopbackDeviceName.c_str());
                                                                          }
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
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_openSession(%d)", reconnect);
}

void EchoThermCamera::_openDevice(int width, int height)
{
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_openDevice(%d, %d)", width, height);
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
            case SEEKCAMERA_FRAME_FORMAT_COLOR_YUY2:
                v.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
                v.fmt.pix.sizeimage = width * height * 2;
                break;
            case SEEKCAMERA_FRAME_FORMAT_COLOR_AYUV:
                // note: probably not supported by gstreamer v4l2src
                v.fmt.pix.pixelformat = V4L2_PIX_FMT_AYUV32;
                v.fmt.pix.sizeimage = width * height * 4;
                break;
            case SEEKCAMERA_FRAME_FORMAT_COLOR_RGB565:
                v.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
                v.fmt.pix.sizeimage = width * height * 2;
                break;
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
            default:
                syslog(LOG_ERR, "Unknown frame format %d.", m_frameFormat);
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
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_openDevice(%d, %d)", width, height);
}

void EchoThermCamera::_startShutterClickThread()
{
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_startShutterClickThread()");
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
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_startShutterClickThread()");
}

void EchoThermCamera::_stopShutterClickThread()
{
    syslog(LOG_DEBUG, "ENTER EchoThermCamera::_stopShutterClickThread()");
    m_shutterClickThreadRunning = false;
    m_shutterClickCondition.notify_one();
    if (m_shutterClickThread.joinable())
    {
        m_shutterClickThread.join();
    }
    syslog(LOG_DEBUG, "EXIT  EchoThermCamera::_stopShutterClickThread()");
}