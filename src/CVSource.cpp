/// FicTrac http://rjdmoore.net/fictrac/
/// \file       CVSource.cpp
/// \brief      OpenCV frame sources.
/// \author     Richard Moore
/// \copyright  CC BY-NC-SA 3.0

#include "CVSource.h"

#include "Logger.h"
#include "timing.h"

/// OpenCV individual includes required by gcc?
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>  
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <exception>

using cv::Mat;


///
/// Constructor.
///
CVSource::CVSource(std::string input)
    : _is_image(false)
{
    LOG_DBG("Source is: %s", input.c_str());
    Mat test_frame;
    try {
        // try reading input as camera id
        LOG_DBG("Trying source as camera id...");
        if (input.size() > 2) { throw std::exception(); }
        int id = std::stoi(input);
        _cap = std::shared_ptr<cv::VideoCapture>(new cv::VideoCapture(id));
        if (!_cap->isOpened()) { throw 0; }
        *_cap >> test_frame;
        if (test_frame.empty()) { throw 0; }
        LOG("Using source type: camera id.");
        _open = true;
        _live = true;
    }
    catch (...) {
        try {
            // then try loading as video file
            LOG_DBG("Trying source as video file...");
            _cap = std::shared_ptr<cv::VideoCapture>(new cv::VideoCapture(input));
            if (!_cap->isOpened()) { throw 0; }
            *_cap >> test_frame;
            if (test_frame.empty()) { throw 0; }
            LOG("Using source type: video file.");
            _open = true;
            _live = false;
        }
        catch (...) {
            try {
                // then try loading as an image file
                LOG_DBG("Trying source as image file...");
                _frame_cap = cv::imread(input);
                if (_frame_cap.empty()) { throw 0; }
                LOG("Using source type: image file.");
                _open = true;
                _live = false;
                _is_image = true;
            }
            catch (...) {
                LOG_ERR("Could not interpret source type (%s)!", input.c_str());
                _open = false;
            }
        }
    }

	if( _open ) {
        if (_is_image) {
            _width = _frame_cap.cols;
            _height = _frame_cap.rows;
        }
        else {
            _width = static_cast<int>(_cap->get(cv::CAP_PROP_FRAME_WIDTH));
            _height = static_cast<int>(_cap->get(cv::CAP_PROP_FRAME_HEIGHT));
        }
        if (_live) {
            _fps = getFPS();    // don't init fps for video files - we might want to play them back as fast as possible

            LOG("OpenCV camera source initialised (%dx%d @ %.3f fps)!", _width, _height, _fps);
        }
        else if (_is_image) {
            LOG("OpenCV image source initialised (%dx%d)!", _width, _height);
        } else {
            LOG("OpenCV video source initialised (%dx%d)!", _width, _height);
        }
	}
}

///
/// Default destructor.
///
CVSource::~CVSource()
{}

///
/// Get input source fps.
///
double CVSource::getFPS()
{
    double fps = _fps;
    if (_open && _cap) {
        fps = _cap->get(cv::CAP_PROP_FPS);
    }
    return fps;
}

///
/// Set input source fps.
///
bool CVSource::setFPS(double fps)
{
    bool ret = false;
    if (_open && _cap && (fps > 0)) {
        if (!_cap->set(cv::CAP_PROP_FPS, fps)) {
            LOG_WRN("Warning! Failed to set device fps (attempted to set fps=%.2f).", fps);
            _fps = fps; // just set fps anyway for playback
            LOG("Playback frame rate is now %.2f", _fps);
        }
        else {
            _fps = getFPS();
            LOG("Device frame rate is now %.2f", _fps);
        }
    }
    return ret;
}

///
/// Set input width and height
/// 
bool CVSource::setWH(int width, int height)
{
	bool ret = false;
	if (_open && _cap && (width > 0) && (height > 0)){
        LOG("Backend is %s", _cap->getBackendName().c_str());
        
        // FIXME: This is PS3 Eye specific. 
        bool codec_set = _cap->set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('G', 'R', 'B', 'G'));
        _cap->set(cv::CAP_PROP_CONVERT_RGB, 0);
        setBayerType(BAYER_GRBG);

        bool width_set = _cap->set(cv::CAP_PROP_FRAME_WIDTH, width);
        bool height_set = _cap->set(cv::CAP_PROP_FRAME_HEIGHT, height);
        if (!( width_set && height_set )) {
            LOG_WRN("Warning! Failed to set the device width/heigth (attempted to set %dx%d", width, height);
            _width = width;
            _height = height;
            LOG("Playback dimensions are now %dx%d", _width, _height);
        }
	    else {
            _width = _cap->get(cv::CAP_PROP_FRAME_WIDTH);
            _height = _cap->get(cv::CAP_PROP_FRAME_HEIGHT);
            LOG("Device dimension is now %dx%d", _width, _height);
            if (_width == width && _height == height){
                ret = true;
            }
        }
    }
    return ret;
}

///
/// Rewind input source to beginning.
/// Ignored by non-file sources.
///
bool CVSource::rewind()
{
    bool ret = false;
	if (_open && _cap) {
        if (!_cap->set(cv::CAP_PROP_POS_FRAMES, 0)) {
            LOG_WRN("Warning! Failed to rewind source.");
        } else { ret = true; }
	}
    return ret;
}

///
/// Capture and retrieve frame from source.
///
bool CVSource::grab(cv::Mat& frame)
{
	if( !_open ) { return false; }
	if( !_is_image && !_cap->read(_frame_cap) ) {
		LOG_ERR("Error grabbing image frame!");
		return false;
	}
    double ts = ts_ms();    // backup, in case the device timestamp is junk
    _ms_since_midnight = ms_since_midnight();
	_timestamp = _cap->get(cv::CAP_PROP_POS_MSEC);
    LOG_DBG("Frame captured %dx%d[%d] @ %f (t_sys: %f ms, t_day: %f ms)", _frame_cap.cols, _frame_cap.rows, _frame_cap.channels(), _timestamp, ts, _ms_since_midnight);
    if (_timestamp <= 0) {
        _timestamp = ts;
    }
    if(_frame_cap.rows == 1){ // wrong cv::Mat returned from cv::read()
        _frame_cap = _frame_cap.reshape(_frame_cap.channels(), 240);
        LOG_DBG("reshape image returned from camera to %dx%d[%d]", _frame_cap.cols, _frame_cap.rows, _frame_cap.channels());
    }

	if( _frame_cap.channels() == 1 ) {
		switch( _bayerType ) {
			case BAYER_BGGR:
				cv::cvtColor(_frame_cap, _frame_flp, cv::COLOR_BayerBG2BGR);
				break;
			case BAYER_GBRG:
				cv::cvtColor(_frame_cap, _frame_flp, cv::COLOR_BayerGB2BGR);
				break;
			case BAYER_GRBG:
				cv::cvtColor(_frame_cap, _frame_flp, cv::COLOR_BayerGR2BGR);
				break;
			case BAYER_RGGB:
				cv::cvtColor(_frame_cap, _frame_flp, cv::COLOR_BayerRG2BGR);
				break;
			case BAYER_NONE:
			default:
				cv::cvtColor(_frame_cap, _frame_flp, cv::COLOR_GRAY2BGR);
				break;
		}
	} else {
        _frame_cap.copyTo(_frame_flp);
	}

    /// Correct average frame rate when reading from file.
    if (!_live && (_fps > 0)) {
        static double prev_ts = ts - (1000/_fps);
        static double av_fps = _fps;      // initially 40 Hz
        static double sleep_ms = 1000/_fps;
        av_fps = 0.15 * av_fps + 0.85 * (1000 / (ts - prev_ts));
        sleep_ms *= 0.25 * (av_fps / _fps) + 0.75;
        sleep(static_cast<long>(round(sleep_ms)));
        prev_ts = ts;
    }

    /// FIXME: This is specific to the new PS3 camera. Make it configurable?
    cv::flip(_frame_flp, frame, 0);


	return true;
}
