/// FicTrac http://rjdmoore.net/fictrac/
/// \file       configGui.cpp
/// \brief      Interactive GUI for configuring FicTrac.
/// \author     Richard Moore
/// \copyright  CC BY-NC-SA 3.0

//TODO: check config.is_open()
//TODO: Add support for fisheye camera model.
//TODO: Add support for edge clicks rather than square corner clicks.

#include "ConfigGui.h"

#include "typesvars.h"
#include "CameraModel.h"
#include "geometry.h"
#include "drawing.h"
#include "Logger.h"
#include "timing.h"

#include <iostream> // getline, stoi
#include <cstdio>   // getchar
#include <exception>

using cv::Mat;
using cv::Point2d;
using std::vector;
using std::string;

///
/// Constant variables.
///
const int       ZOOM_DIM    = 600;
const double    ZOOM_SCL    = 1.0 / 10.0;

const int NCOLOURS = 6;
cv::Scalar COLOURS[NCOLOURS] = {
    CV_RGB(255, 0,   0),
    CV_RGB(0,   255, 0),
    CV_RGB(0,   0,   255),
    CV_RGB(255, 255, 0),
    CV_RGB(0,   255, 255),
    CV_RGB(255, 0,   255)
};

///
/// Collect mouse events from config GUI window.
///
void onMouseEvent(int event, int x, int y, int f, void* ptr)
{
    ConfigGui::INPUT_DATA* pdata = static_cast<ConfigGui::INPUT_DATA*>(ptr);
    switch(event)
    {
        case CV_EVENT_LBUTTONDOWN:
            break;
            
        case CV_EVENT_LBUTTONUP:
            switch(pdata->mode)
            {
                case ConfigGui::CIRC_PTS:
                    pdata->circPts.push_back(Point2d(x,y));
                    pdata->newEvent = true;
                    break;
                    
                case ConfigGui::IGNR_PTS:
                    // ensure there is at least one active ignore region
                    if (pdata->ignrPts.empty()) { pdata->ignrPts.push_back(vector<Point2d>()); }
                    // add click to the active ignore region
                    pdata->ignrPts.back().push_back(Point2d(x,y));
                    pdata->newEvent = true;
                    break;
                    
                case ConfigGui::R_XY:
                case ConfigGui::R_YZ:
                case ConfigGui::R_XZ:
                    pdata->sqrPts.push_back(Point2d(x,y));
                    pdata->newEvent = true;
                    break;
                    
                default:
                    break;
            }
            break;
        
        case CV_EVENT_RBUTTONUP:
            switch(pdata->mode)
            {
                case ConfigGui::CIRC_PTS:
                    if (pdata->circPts.size() > 0) { pdata->circPts.pop_back(); }
                    pdata->newEvent = true;
                    break;
                    
                case ConfigGui::IGNR_PTS:
                    if (!pdata->ignrPts.empty()) {
                        // if the active ignore region is empty, remove it
                        if (pdata->ignrPts.back().empty()) { pdata->ignrPts.pop_back(); }
                        // otherwise remove points from the active ignore region
                        else { pdata->ignrPts.back().pop_back(); }
                    }
                    pdata->newEvent = true;
                    break;
                    
                case ConfigGui::R_XY:
                case ConfigGui::R_YZ:
                case ConfigGui::R_XZ:
                    if (pdata->sqrPts.size() > 0) { pdata->sqrPts.pop_back(); }
                    pdata->newEvent = true;
                    break;
                    
                default:
                    break;
            }
            break;
        
        case CV_EVENT_MOUSEMOVE:
            pdata->cursorPt.x = x;
            pdata->cursorPt.y = y;
            break;

        default:
            break;
    }
}

///
/// Create a zoomed ROI.
///
void createZoomROI(Mat& zoom_roi, const Mat& frame, const Point2d& pt, int orig_dim)
{
    int x = frame.cols/2;
    if (pt.x >= 0) { x = clamp(int(pt.x - orig_dim/2 + 0.5), int(orig_dim/2), frame.cols - 1 - orig_dim); }
    int y = frame.rows/2;
    if (pt.y >= 0) { y = clamp(int(pt.y - orig_dim/2 + 0.5), int(orig_dim/2), frame.rows - 1 - orig_dim); }
    Mat crop_rect = frame(cv::Rect(x, y, orig_dim, orig_dim));
    cv::resize(crop_rect, zoom_roi, zoom_roi.size());
}

///
/// Helper function to force getchar to take new key press.
///
int getchar_clean()
{
    double t1 = elapsed_secs();
    int ret;
    do {
        ret = std::getchar();
    } while ((elapsed_secs() - t1) < 0.1);
    return ret;
}

///
/// Constructor.
///
ConfigGui::ConfigGui(string config_fn)
: _open(false), _config_fn(config_fn)
{
    /// Load and parse config file.
    _open = (_cfg.read(_config_fn) > 0);

    /// Read source file name and load image.
    string input_fn;
    if (_open) {
        input_fn = _cfg("src_fn");
        if (input_fn.empty()) {
            _open = false;
        }
    }

    /// Load an image to use for annotation.
    Mat input_frame;
    if (_open) {
        try {
            // try loading as image file first
            LOG_DBG("Trying to load input %s as image ...", input_fn.c_str());
            input_frame = cv::imread(input_fn, CV_LOAD_IMAGE_GRAYSCALE);
            if (input_frame.empty()) { throw 0; }
            LOG("Input frame read from image file (%s).", input_fn.c_str());
        }
        catch (...) {
            try {
                // then try loading as camera id
                LOG_DBG("Trying to load input %s as camera id ...", input_fn.c_str());
                int id = stoi(input_fn);
                cv::VideoCapture cap(id);
                cap >> input_frame;
                if (input_frame.empty()) { throw 0; }
                LOG("Input frame read from camera (%d).", id);
            }
            catch (...) {
                try {
                    // then try loading as video file
                    LOG_DBG("Trying to load input %s as video file ...", input_fn.c_str());
                    cv::VideoCapture cap(input_fn);
                    cap >> input_frame;
                    if (input_frame.empty()) { throw 0; }
                    LOG("Input frame read from video file (%s).", input_fn.c_str());
                }
                catch (...) {
                    LOG_ERR("Could not read frame from input (%s)!", input_fn.c_str());
                    _open = false;
                }
            }
        }
        if (input_frame.empty()) {
            _open = false;
        }
    }

    /// Setup.
    if (_open) {
        _open = setFrame(input_frame);
    }
}

///
/// Destructor.
///
ConfigGui::~ConfigGui()
{}

///
/// Prepare input image for user input.
///
bool ConfigGui::setFrame(Mat& frame)
{
    /// Copy input frame.
    if (frame.channels() == 3) {
        cv::cvtColor(frame, _frame, CV_BGR2GRAY);
    } else if (frame.channels() == 1) {
        _frame = frame.clone();
    } else {
        // uh oh, shouldn't get here
        LOG_ERR("Unexpected number of image channels (%d)!", frame.channels());
        return false;
    }
    
    /// Stretch contrast for display
    histStretch(_frame);
    _w = _frame.cols;
    _h = _frame.rows;
    
    /// Load camera model.
    double vfov = 0;
    _cfg.getDbl("vfov", vfov);

	if (vfov <= 0) {
		LOG_ERR("vfov parameter must be > 0 (%f)!", vfov);
		return false;
	}

    LOG("Using vfov: %f deg", vfov);
    
    //FIXME: support also fisheye models!
    _cam_model = CameraModel::createRectilinear(static_cast<int>(_w), static_cast<int>(_h), vfov * CM_D2R);
    
    return true;
}

///
/// Write camera-animal transform to config file.
///
bool ConfigGui::saveC2ATransform(const Mat& R, const Mat& t)
{
	string sqr_type = "";
	switch (_input_data.mode) {
		case R_XY:
			sqr_type = "c2a_cnrs_xy";
			break;
		case R_YZ:
			sqr_type = "c2a_cnrs_yz";
			break;
		case R_XZ:
			sqr_type = "c2a_cnrs_xz";
			break;
	}

	// dump corner points to config file
	vector<int> cfg_pts;
	for (auto p : _input_data.sqrPts) {
		cfg_pts.push_back(static_cast<int>(p.x + 0.5));		// these are just ints in a double object anyway
		cfg_pts.push_back(static_cast<int>(p.y + 0.5));
	}

	// write to config file
	LOG("Adding %s to config file and writing to disk (%s) ...", sqr_type.c_str(), _config_fn.c_str());
	_cfg.add(sqr_type, cfg_pts);
	_cfg.add("c2a_src", sqr_type);

	// dump R to config file
	vector<double> cfg_r, cfg_t;
	CmPoint angleAxis = CmPoint64f::matrixToOmega(R);
	for (int i = 0; i < 3; i++) {
		cfg_r.push_back(angleAxis[i]);
		cfg_t.push_back(t.at<double>(i, 0));
	}

	// write to config file
	LOG("Adding c2a_r, c2a_t, and c2a_src to config file and writing to disk (%s) ...", _config_fn.c_str());
	_cfg.add("c2a_r", cfg_r);
	_cfg.add("c2a_t", cfg_t);

	if (_cfg.write() <= 0) {
		LOG_ERR("Bad write!");
		return false;
	}

	//// test read
	//LOG_DBG("Re-loading config file and reading %s, c2a_r, c2a_t ...", sqr_type.c_str());
	//_cfg.read(_config_fn);

	//if (!_cfg.getVecInt(sqr_type, cfg_pts) || !_cfg.getVecDbl("c2a_r", cfg_r) || !_cfg.getVecDbl("c2a_t", cfg_t)) {
	//	LOG_ERR("Bad read!");
	//	return false;
	//}

	return true;
}

///
/// Update animal coordinate frame estimate.
///
bool ConfigGui::updateC2ATransform(const Mat& ref_cnrs, Mat& R, Mat& t)
{
	bool ret = false;
	if (_input_data.newEvent) {
        //FIXME: also support edge clicks! e.g.:
        //  double x1 = click[2 * i + 0].x;     double y1 = click[2 * i + 0].y;
        //  double x2 = click[2 * i + 1].x;     double y2 = click[2 * i + 1].y;
        //  double x3 = click[2 * i + 2].x;     double y3 = click[2 * i + 2].y;
        //  double x4 = click[2 * i + 3].x;     double y4 = click[2 * i + 3].y;
        //  double px = ((x1*y2 - y1 * x2)*(x3 - x4) - (x1 - x2)*(x3*y4 - y3 * x4)) / ((x1 - x2)*(y3 - y4) - (y1 - y2)*(x3 - x4));
        //  double py = ((x1*y2 - y1 * x2)*(y3 - y4) - (y1 - y2)*(x3*y4 - y3 * x4)) / ((x1 - x2)*(y3 - y4) - (y1 - y2)*(x3 - x4));
		ret = computeRtFromSquare(_cam_model, ref_cnrs, _input_data.sqrPts, R, t);
		_input_data.newEvent = false;
	}
	return ret;
}

///
/// Draw animal coordinate frame axes.
///
void ConfigGui::drawC2ATransform(Mat& disp_frame, const Mat& ref_cnrs, const Mat& R, const Mat& t, const double& r, const CmPoint& c)
{
	// make x4 mat for projecting corners
	Mat T(3, 4, CV_64F);
	for (int i = 0; i < 4; i++) { t.copyTo(T.col(i)); }

	// project reference corners
	Mat p = R * ref_cnrs + T;

	// draw re-projected reference corners
	drawRectCorners(disp_frame, _cam_model, p, CV_RGB(0, 255, 0));

	// draw re-projected animal axes.
	if (r > 0) {
		double scale = 1.0 / tan(r);
		Mat so = (cv::Mat_<double>(3, 1) << c.x, c.y, c.z) * scale;
		drawAxes(disp_frame, _cam_model, R, so, CV_RGB(0, 0, 255));
	}
}

///
/// Utility function for changing state machine state.
///
void ConfigGui::changeState(INPUT_MODE new_state)
{
	_input_data.newEvent = true;
	LOG_DBG("New state: %s", INPUT_MODE_STR[static_cast<int>(new_state)]);
	_input_data.mode = new_state;
}

///
/// Run user input program or configuration.
///
bool ConfigGui::run()
{
    /// Interactive window.
    cv::namedWindow("configGUI", cv::WINDOW_AUTOSIZE);
    cv::setMouseCallback("configGUI", onMouseEvent, &_input_data);
    
    /// Display/input loop.
	Mat R, t;
    CmPoint c;
    double r = -1;
    char key = 0;
    string val;
	string cfg_r_src;
    vector<int> cfg_pts;
    vector<double> cfg_vec;
    vector<vector<int>> cfg_polys;
	changeState(CIRC_INIT);
    const char exit_key = 0x1b;
#ifdef WIN32
    const char enter_key = 0x0d;
#else
    const char enter_key = 0x0a;
#endif
    const int click_rad = std::max(int(_w/150+0.5), 5);
    Mat disp_frame, zoom_frame(ZOOM_DIM, ZOOM_DIM, CV_8UC1);
    const int scaled_zoom_dim = static_cast<int>(ZOOM_DIM * ZOOM_SCL + 0.5);
    while (_open && (key != exit_key)) {
        /// Create frame for drawing.
        cv::cvtColor(_frame, disp_frame, CV_GRAY2RGB);
        
        int in;
        string str;
        switch (_input_data.mode)
        {
            /// Check for existing circumference points.
            case CIRC_INIT:
            
                // test read
                cfg_pts.clear();
                if (_cfg.getVecInt("roi_circ", cfg_pts)) {
                    
                    /// Load circumference points from config file.
                    _input_data.circPts.clear();
                    for (unsigned int i = 1; i < cfg_pts.size(); i+=2) {
                        _input_data.circPts.push_back(Point2d(cfg_pts[i-1], cfg_pts[i]));
                    }
                    
                    /// Fit circular FoV to sphere.
                    if (_input_data.circPts.size() >= 3) {
                        circleFit_camModel(_input_data.circPts, _cam_model, c, r);
                        
                        /// Draw fitted circumference.
                        if (r > 0) {
                            drawCircle_camModel(disp_frame, _cam_model, c, r, CV_RGB(255,0,0), false);
                            
                            /// Display.
                            cv::imshow("configGUI", disp_frame);
                            cv::waitKey(100);   //FIXME: why do we have to wait so long to make sure the frame is drawn?
                            
                            printf("\n\n\n  Sphere circumference points were found in the config file.\n  You can discard these points and re-run config or keep the existing points.\n");
                            
                            // input loop
                            while (true) {
                                cv::waitKey(100);   //FIXME: dirty hack - sometimes image doesn't draw, at least with this line we can just mash keys until it does
                                printf("\n  Would you like to keep the existing circumference points ([y]/n)? ");
                                in = getchar();
                                switch (in)
                                {
                                    case 'y':
                                    case 'Y':
                                        getchar(); // discard \n
                                    case '\n':
                                        // advance state
										changeState(IGNR_INIT);
                                        break;
                                    case 'n':
                                    case 'N':
                                        getchar(); // discard \n
                                        break;
                                    default:
                                        LOG_WRN("Invalid input!");
                                        getchar(); // discard \n
                                        continue;
                                        break;
                                }
                                break;
                            }
                        }
                    }
                }
                
                if (_input_data.mode == CIRC_INIT) {
                    _input_data.circPts.clear();
                    printf("\n\n\n  Define the circumference of the track ball.\n\n  Use the left mouse button to add new points.\n  You must select at least 3 (but preferably 6+) points around the circumference of the track ball.\n  NOTE! Be careful to place points only on the circumference of the track ball,\nand not along the outline of the visible track ball where the actual circumference has been partially obscured.\n  You can use the right mouse button to remove the last added point.\n  The fitted circumference is drawn in red.\n\n  Press ENTER when you are satisfied with the fitted circumference, or press ESC to exit...\n\n");
					changeState(CIRC_PTS);
                }
                break;
            
            /// Input circumference points.
            case CIRC_PTS:

                /// Fit circular FoV to sphere.
                if (_input_data.newEvent) {
                    if (_input_data.circPts.size() >= 3) {
                        circleFit_camModel(_input_data.circPts, _cam_model, c, r);
                    } else {
                        r = -1;
                    }
                    _input_data.newEvent = false;
                }
                
                /// Draw previous clicks.
                for (auto click : _input_data.circPts) {
                    cv::circle(disp_frame, click, click_rad, CV_RGB(255,255,0), 1, CV_AA);
                }
                
                /// Draw fitted circumference.
                if (r > 0) { drawCircle_camModel(disp_frame, _cam_model, c, r, CV_RGB(255,0,0), false); }
                
                /// Draw cursor location.
                drawCursor(disp_frame, _input_data.cursorPt, CV_RGB(0,255,0));
                
                /// Create zoomed window.
                createZoomROI(zoom_frame, disp_frame, _input_data.cursorPt, scaled_zoom_dim);
                
                /// Display.
                cv::imshow("zoomROI", zoom_frame);
                cv::imshow("configGUI", disp_frame);
                key = cv::waitKey(5);
                
                /// State machine logic.
                if (key == enter_key) {
                    if (_input_data.circPts.size() >= 3) {
                        // dump circumference points to config file
                        cfg_pts.clear();
                        for (auto p : _input_data.circPts) {
                            cfg_pts.push_back(static_cast<int>(p.x + 0.5));
                            cfg_pts.push_back(static_cast<int>(p.y + 0.5));
                        }
                        
                        // write to config file
                        LOG("Adding roi_circ to config file and writing to disk (%s) ...", _config_fn.c_str());
                        _cfg.add("roi_circ", cfg_pts);
                        if (_cfg.write() <= 0) {
                            LOG_ERR("Error writing to config file (%s)!", _config_fn);
                            _open = false;  // will cause exit
                        }
                        
                        //// test read
                        //LOG_DBG("Re-loading config file and reading roi_circ ...");
                        //_cfg.read(_config_fn);
                        //assert(_cfg.getVecInt("roi_circ", cfg_pts));
                        
                        // advance state
                        cv::destroyWindow("zoomROI");
						changeState(IGNR_INIT);
                    } else {
                        LOG_WRN("You must select at least 3 circumference points (you have selected %d points)!", _input_data.circPts.size());
                    }
                }
                break;
            
            /// Check for existing ignore points.
            case IGNR_INIT:
                
                // test read
                cfg_polys.clear();
                if (_cfg.getVVecInt("roi_ignr", cfg_polys)) {
                    
                    /// Load ignore polys from config file.
                    _input_data.ignrPts.clear();
                    for (auto poly : cfg_polys) {
                        vector<cv::Point2d> tmp;
                        for (unsigned int i = 1; i < poly.size(); i+=2) {
                            tmp.push_back(cv::Point2d(poly[i-1],poly[i]));
                        }
                        if (!tmp.empty()) { _input_data.ignrPts.push_back(tmp); }
                    }
                    
                    /// Draw previous clicks.
                    for (unsigned int i = 0; i < _input_data.ignrPts.size(); i++) {
                        for (unsigned int j = 0; j < _input_data.ignrPts[i].size(); j++) {
                            if (i == _input_data.ignrPts.size()-1) {
                                cv::circle(disp_frame, _input_data.ignrPts[i][j], click_rad, COLOURS[i%NCOLOURS], 1, CV_AA);
                            }
                            cv::line(disp_frame, _input_data.ignrPts[i][j], _input_data.ignrPts[i][(j+1)%_input_data.ignrPts[i].size()], COLOURS[i%NCOLOURS], 1, CV_AA);
                        }
                    }
                    
                    /// Display.
                    cv::imshow("configGUI", disp_frame);
                    cv::waitKey(100);   //FIXME: why do we have to wait so long to make sure the frame is drawn?
                    
                    printf("\n\n\n  Ignore region points were found in the config file.\n  You can discard these points and re-run config or keep the existing points.\n");
                    
                    // input loop
                    while (true) {
                        cv::waitKey(100);   //FIXME: dirty hack - sometimes image doesn't draw, at least with this line we can just mash keys until it does
                        printf("\n  Would you like to keep the existing ignore regions ([y]/n)? ");
                        in = getchar();
                        switch (in)
                        {
                            case 'y':
                            case 'Y':
                                getchar(); // discard \n
                            case '\n':
                                // advance state
								changeState(R_INIT);
                                break;
                            case 'n':
                            case 'N':
                                getchar(); // discard \n
                                break;
                            default:
                                LOG_WRN("Invalid input!");
                                getchar(); // discard \n
                                continue;
                                break;
                        }
                        break;
                    }
                }
                
                if (_input_data.mode == IGNR_INIT) {
                    _input_data.ignrPts.clear();
                    printf("\n\n\n  Define ignore regions.\n\n  Use the left mouse button to add points to a new polygon.\n  Polygons can be drawn around objects (such as the animal) that block the view of the track ball.\n  You can use the right mouse button to remove the last added point.\n\n  Press ENTER to start a new polygon, or press ENTER twice when you are satisfied with the selected ignore regions, or press ESC to exit...\n\n");
					changeState(IGNR_PTS);
                }
                break;
            
            /// Input ignore regions.
            case IGNR_PTS:
                /// Draw previous clicks.
                for (unsigned int i = 0; i < _input_data.ignrPts.size(); i++) {
                    for (unsigned int j = 0; j < _input_data.ignrPts[i].size(); j++) {
                        if (i == _input_data.ignrPts.size()-1) {
                            cv::circle(disp_frame, _input_data.ignrPts[i][j], click_rad, COLOURS[i%NCOLOURS], 1, CV_AA);
                        }
                        cv::line(disp_frame, _input_data.ignrPts[i][j], _input_data.ignrPts[i][(j+1)%_input_data.ignrPts[i].size()], COLOURS[i%NCOLOURS], 1, CV_AA);
                    }
                }
                
                /// Draw fitted circumference.
                if (r > 0) { drawCircle_camModel(disp_frame, _cam_model, c, r, CV_RGB(255,0,0), false); }
                
                /// Draw cursor location.
                drawCursor(disp_frame, _input_data.cursorPt, CV_RGB(0,255,0));
                
                /// Create zoomed window.
                createZoomROI(zoom_frame, disp_frame, _input_data.cursorPt, scaled_zoom_dim);
                
                /// Display.
                cv::imshow("zoomROI", zoom_frame);
                cv::imshow("configGUI", disp_frame);
                key = cv::waitKey(5);
                
                /// State machine logic.
                if (key == enter_key) {
                    // if current poly is empty, assume we've finished
                    if (_input_data.ignrPts.empty() || _input_data.ignrPts.back().empty()) {
                        if (!_input_data.ignrPts.empty()) { _input_data.ignrPts.pop_back(); }
                        
                        // dump ignore region polys to config file
                        cfg_polys.clear();
                        for (auto poly : _input_data.ignrPts) {
                            cfg_polys.push_back(vector<int>());
                            for (auto pt : poly) {
                                cfg_polys.back().push_back(static_cast<int>(pt.x + 0.5));
                                cfg_polys.back().push_back(static_cast<int>(pt.y + 0.5));
                            }
                        }
                        
                        // write to config file
                        LOG("Adding roi_ignr to config file and writing to disk (%s) ...", _config_fn.c_str());
                        _cfg.add("roi_ignr", cfg_polys);
                        if (_cfg.write() <= 0) {
                            LOG_ERR("Error writing to config file (%s)!", _config_fn);
                            _open = false;      // will cause exit
                        }
                        
                        //// test read
                        //LOG_DBG("Re-loading config file and reading roi_ignr ...");
                        //_cfg.read(_config_fn);
                        //assert(_cfg.getVVecInt("roi_ignr", cfg_polys));
                        
                        // advance state
                        cv::destroyWindow("zoomROI");
						changeState(R_INIT);
                    }
                    // otherwise, start a new poly
                    else {
                        _input_data.addPoly();
                        LOG("New ignore region added!");
                    }
                }
                break;
            
            /// Choose method for defining animal frame.
            case R_INIT:
				// test read
				if (_cfg.getStr("c2a_src", cfg_r_src)) {

					LOG_DBG("Found c2a_src: %s", cfg_r_src);

					/// Load square corners from config file.
					cfg_pts.clear();
					if (!_cfg.getVecInt(cfg_r_src, cfg_pts)) {
						LOG_DBG("Error reading %s from config file! Re-running configuration ...", cfg_r_src);
						changeState(R_SLCT);
						break;
					}

					_input_data.sqrPts.clear();
					for (unsigned int i = 1; i < cfg_pts.size(); i += 2) {
						_input_data.sqrPts.push_back(cv::Point2d(cfg_pts[i - 1], cfg_pts[i]));
					}

					/// Draw previous clicks.
					for (auto click : _input_data.sqrPts) {
						cv::circle(disp_frame, click, click_rad, CV_RGB(255, 255, 0), 1, CV_AA);
					}

					/// Load transform from config file.
					cfg_vec.clear();
					if (_cfg.getVecDbl("c2a_r", cfg_vec)) {
						R = CmPoint64f::omegaToMatrix(CmPoint(cfg_vec[0], cfg_vec[1], cfg_vec[2]));
					} else {
						LOG_DBG("Error reading c2a_r from config file! Re-running configuration ...");
						changeState(R_SLCT);
						break;
					}
					
					cfg_vec.clear();
					if (_cfg.getVecDbl("c2a_t", cfg_vec)) {
						t = (cv::Mat_<double>(3, 1) << cfg_vec[0], cfg_vec[1], cfg_vec[2]);
					} else {
						LOG_DBG("Error reading c2a_t from config file! Re-running configuration ...");
						changeState(R_SLCT);
						break;
					}

					/// Draw axes.
					if (_input_data.sqrPts.size() == 4) {
						if (cfg_r_src == "c2a_cnrs_xy") {
							drawC2ATransform(disp_frame, XY_CNRS, R, t, r, c);
						} else if (cfg_r_src == "c2a_cnrs_yz") {
							drawC2ATransform(disp_frame, YZ_CNRS, R, t, r, c);
						} else if (cfg_r_src == "c2a_cnrs_xz") {
							drawC2ATransform(disp_frame, XZ_CNRS, R, t, r, c);
						}
					}

					/// Display.
					cv::imshow("configGUI", disp_frame);
					cv::waitKey(100);   //FIXME: why do we have to wait so long to make sure the frame is drawn?

					printf("\n\n\n  A camera-animal transform was found in the config file.\n  You can keep the existing transform, or discard and re-run config.\n");

					// input loop
					while (true) {
						cv::waitKey(100);   //FIXME: dirty hack - sometimes image doesn't draw, at least with this line we can just mash keys until it does
						printf("\n  Would you like to keep the existing transform ([y]/n)? ");
						in = getchar();
						switch (in)
						{
							case 'y':
							case 'Y':
								getchar(); // discard \n
							case '\n':
								// advance state
								changeState(EXIT);
								break;
							case 'n':
							case 'N':
								getchar(); // discard \n
								break;
							default:
								LOG_WRN("Invalid input!");
								getchar(); // discard \n
								continue;
								break;
						}
						break;
					}
				}

				if (_input_data.mode == R_INIT) {
					changeState(R_SLCT);
				}
				break;
            
            /// Choose method for defining animal frame.
            case R_SLCT:
                printf("\n\n\n  Define the animal's coordinate frame.\n\n  You must now define the reference frame of the animal, from the perspective of the camera.\n  This allows FicTrac to convert rotations of the ball into walking and turning motions for the animal.\n");
                printf("  The camera's reference frame is defined as: X = image right (cols); Y = image down (rows); Z = into image (out from camera)\n");
                printf("  The animal's reference frame is defined as: X = forward; Y = right; Z = down\n");
                
                printf("\n  There are 5 possible methods for defining the animal's coordinate frame:\n");
                printf("\n\t 1 (XY square) : [Default] Click the four corners of a square shape that is aligned with the animal's X-Y axes. This method is recommended when the camera is above/below the animal.\n");
                printf("\n\t 2 (YZ square) : Click the four corners of a square shape that is aligned with the animal's Y-Z axes. This method is recommended when the camera is in front/behind the animal.\n");
                printf("\n\t 3 (XZ square) : Click the four corners of a square shape that is aligned with the animal's X-Z axes. This method is recommended when the camera is to the animal's left/right.\n");
                // printf("\n\t 4 (manual)    : Rotate a visualisation of the animal's coordinate frame to align with the orientation of the animal. This method is not recommended as it is inaccurate.\n");
                printf("\n\t 5 (external)  : The transform between the camera and animal reference frames can also be defined by hand by editing the appropriate variables in the config file. This method is only recommended when the transform is known by some other means.\n");
                
                // input loop
                while (true) {
                    printf("\n\n  Please enter your preferred method [1]: ");
                    std::getline(std::cin, str);
                    if (str.empty()) {
                        in = 1;
                    } else {
                        try { in = std::stoi(str); }
                        catch(...) {
                            LOG_WRN("Invalid input!");
                            continue;
                        }
                    }
                    switch (in)
                    {
                        case 1:
                            printf("\n\n\n  XY-square method.\n\n  Please click on the four corners of a square shape that is aligned with the animal's X-Y axes. The corners must be clicked in the following order: (+X,-Y), (+X,+Y), (-X,+Y), (-X,-Y). If your camera is looking down on the animal from above, then the four corners are (in order): TL, TR, BR, BL from the camera's perspective. If your camera is below the animal, then the order is TR, TL, BL, BR.\n\n  Make sure the displayed axis is the correct right-handed coordinate frame!!\n\n  You can hold F to mirror the axis if the handedness is incorrect.\n\n  Press ENTER when you are satisfied with the animal's axis, or press ESC to exit...\n\n");
                            // advance state
							changeState(R_XY);
                            break;
                            
                        case 2:
                            printf("\n\n\n  YZ-square method.\n\n  Please click on the four corners of a square shape that is aligned with the animal's Y-Z axes. The corners must be clicked in the following order: (-Y,-Z), (+Y,-Z), (+Y,+Z), (-Y,+Z). If your camera is behind the animal, then the four corners are (in order): TL, TR, BR, BL from the camera's perspective. If your camera is in front of the animal, then the order is TR, TL, BL, BR.\n\n  Make sure the displayed axis is the correct right-handed coordinate frame!!\n\n  You can hold F to mirror the axis if the handedness is incorrect.\n\n  Press ENTER when you are satisfied with the animal's axis, or press ESC to exit...\n\n");
                            // advance state
							changeState(R_YZ);
                            break;
                            
                        case 3:
                            printf("\n\n\n  XZ-square method.\n\n  Please click on the four corners of a square shape that is aligned with the animal's X-Z axes. The corners must be clicked in the following order: (+X,-Z), (-X,-Z), (-X,+Z), (+X,+Z). If your camera is to the animal's left side, then the four corners are (in order): TL, TR, BR, BL from the camera's perspective. If your camera is to the animal's right side, then the order is TR, TL, BL, BR.\n\n  Make sure the displayed axis is the correct right-handed coordinate frame!!\n\n  You can hold F to mirror the axis if the handedness is incorrect.\n\n  Press ENTER when you are satisfied with the animal's axis, or press ESC to exit...\n\n");
                            // advance state
							changeState(R_XZ);
                            break;
                            
                        // case 4:
                            // // advance state
                            // BOOST_LOG_TRIVIAL(debug) << "New state: R_MAN";
                            // _input_data.mode = R_MAN;
                            // break;
                            
                        case 5:
                            // advance state
							changeState(R_EXT);
                            break;
                            
                        default:
                            LOG_WRN("Invalid input!");
                            continue;
                            break;
                    }
                    break;
                }
                break;
            
            /// Define animal coordinate frame.
            case R_XY:
            
                /// Draw previous clicks.
                for (auto click : _input_data.sqrPts) {
                    cv::circle(disp_frame, click, click_rad, CV_RGB(255,255,0), 1, CV_AA);
                }
                
                /// Draw axes.
                if (_input_data.sqrPts.size() == 4) {
					updateC2ATransform(XY_CNRS, R, t);
					drawC2ATransform(disp_frame, XY_CNRS, R, t, r, c);
                }
                
                /// Draw cursor location.
                drawCursor(disp_frame, _input_data.cursorPt, CV_RGB(0,255,0));
                
                /// Create zoomed window.
                createZoomROI(zoom_frame, disp_frame, _input_data.cursorPt, scaled_zoom_dim);
                
                /// Display.
                cv::imshow("zoomROI", zoom_frame);
                cv::imshow("configGUI", disp_frame);
                key = cv::waitKey(5);
                
                /// State machine logic.
                if (key == enter_key) {
                    if ((_input_data.sqrPts.size() == 4) && !R.empty()) {
                        // dump corner points to config file
						if (!saveC2ATransform(R, t)) {
							LOG_ERR("Error writing coordinate transform to config file!");
                            _open = false;      // will cause exit
						}
                        
                        // advance state
                        cv::destroyWindow("zoomROI");
						changeState(EXIT);
                    } else {
                        LOG_WRN("You must select exactly 4 corners (you have selected %d points)!", _input_data.sqrPts.size());
                    }
                } else if (key == 0x66) {   // f
                    /// Reflect R and re-minimise.
                    if (!R.empty()) {
                        R.col(2) *= -1;
                        _input_data.newEvent = true;
                    }
                }
                break;
            
            /// Define animal coordinate frame.
            case R_YZ:
                
                /// Draw previous clicks.
                for (auto click : _input_data.sqrPts) {
                    cv::circle(disp_frame, click, click_rad, CV_RGB(255,255,0), 1, CV_AA);
                }
                
                /// Draw axes.
                if (_input_data.sqrPts.size() == 4) {
					updateC2ATransform(YZ_CNRS, R, t);
					drawC2ATransform(disp_frame, YZ_CNRS, R, t, r, c);
                }
                
                /// Draw cursor location.
                drawCursor(disp_frame, _input_data.cursorPt, CV_RGB(0,255,0));
                
                /// Create zoomed window.
                createZoomROI(zoom_frame, disp_frame, _input_data.cursorPt, scaled_zoom_dim);
                
                /// Display.
                cv::imshow("zoomROI", zoom_frame);
                cv::imshow("configGUI", disp_frame);
                key = cv::waitKey(5);
                
                /// State machine logic.
                if (key == enter_key) {
					if ((_input_data.sqrPts.size() == 4) && !R.empty()) {
                        // dump corner points to config file
						if (!saveC2ATransform(R, t)) {
							LOG_ERR("Error writing coordinate transform to config file!");
                            _open = false;      // will cause exit
						}
                        
                        // advance state
                        cv::destroyWindow("zoomROI");
						changeState(EXIT);
                    } else {
                        LOG_WRN("You must select exactly 4 corners (you have selected %d points)!", _input_data.sqrPts.size());
                    }
                } else if (key == 0x66) {   // f
                    /// Reflect R and re-minimise.
                    if (!R.empty()) {
                        R.col(2) *= -1;
                        _input_data.newEvent = true;
                    }
                }
                break;
            
            /// Define animal coordinate frame.
            case R_XZ:
                
                /// Draw previous clicks.
                for (auto click : _input_data.sqrPts) {
                    cv::circle(disp_frame, click, click_rad, CV_RGB(255,255,0), 1, CV_AA);
                }
                
                /// Draw axes.
                if (_input_data.sqrPts.size() == 4) {
					updateC2ATransform(XZ_CNRS, R, t);
					drawC2ATransform(disp_frame, XZ_CNRS, R, t, r, c);
                }
                
                /// Draw cursor location.
                drawCursor(disp_frame, _input_data.cursorPt, CV_RGB(0,255,0));
                
                /// Create zoomed window.
                createZoomROI(zoom_frame, disp_frame, _input_data.cursorPt, scaled_zoom_dim);
                
                /// Display.
                cv::imshow("zoomROI", zoom_frame);
                cv::imshow("configGUI", disp_frame);
                key = cv::waitKey(5);
                
                /// State machine logic.
                if (key == enter_key) {
					if ((_input_data.sqrPts.size() == 4) && !R.empty()) {
                        // dump corner points to config file
						if (!saveC2ATransform(R, t)) {
							LOG_ERR("Error writing coordinate transform to config file!");
                            _open = false;      // will cause exit
						}
                        
                        // advance state
                        cv::destroyWindow("zoomROI");
						changeState(EXIT);
                    } else {
                        LOG_WRN("You must select exactly 4 corners (you have selected %d points)!", _input_data.sqrPts.size());
                    }
                } else if (key == 0x66) {   // f
                    /// Reflect R and re-minimise.
                    if (!R.empty()) {
                        R.col(2) *= -1;
                        _input_data.newEvent = true;
                    }
                }
                break;
            
            // /// Define animal coordinate frame.
            // case R_MAN:
            
                // /// Draw axes.
                
                
                // // draw re-projected animal axes.
                // if (r > 0) {
                    // double scale = 1.0/tan(r);
                    // Mat so = (cv::Mat_<double>(3,1) << c.x, c.y, c.z) * scale;
                    // drawAxes(disp_frame, _cam_model, R, so, CV_RGB(0,0,255));
                // }
                
                // // advance state
                // BOOST_LOG_TRIVIAL(debug) << "New state: EXIT";
                // _input_data.mode = EXIT;
                // break;
            
            /// Define animal coordinate frame.
            case R_EXT:

                // ensure c2a_r exists in config file
                if (!_cfg.getStr("c2a_r", val)) {
                    cfg_vec.clear();
                    cfg_vec.resize(3, 0);

                    // write to config file
                    LOG("Adding c2a_r to config file and writing to disk (%s) ...", _config_fn.c_str());
                    _cfg.add("c2a_r", cfg_vec);
                }
                _cfg.add("c2a_src", string("ext"));

                if (_cfg.write() <= 0) {
                    LOG_ERR("Error writing to config file (%s)!", _config_fn);
                    _open = false;      // will cause exit
                }
            
                // advance state
				changeState(EXIT);
                break;
            
            default:
                LOG_WRN("Unexpected state encountered!");
                _input_data.mode = EXIT;
                // break;
            
            /// Exit config.
            case EXIT:
                key = exit_key;
                break;
        }
    }

	cv::destroyAllWindows();

	/// Save config image
	cv::cvtColor(_frame, disp_frame, CV_GRAY2RGB);

	// draw fitted circumference
	if (r > 0) {
		drawCircle_camModel(disp_frame, _cam_model, c, r, CV_RGB(255, 0, 0), false);
	}

	// draw ignore regions
	for (unsigned int i = 0; i < _input_data.ignrPts.size(); i++) {
		for (unsigned int j = 0; j < _input_data.ignrPts[i].size(); j++) {
			if (i == _input_data.ignrPts.size() - 1) {
				cv::circle(disp_frame, _input_data.ignrPts[i][j], click_rad, COLOURS[i%NCOLOURS], 1, CV_AA);
			}
			cv::line(disp_frame, _input_data.ignrPts[i][j], _input_data.ignrPts[i][(j + 1) % _input_data.ignrPts[i].size()], COLOURS[i%NCOLOURS], 1, CV_AA);
		}
	}

	// draw animal axes
	if (_input_data.sqrPts.size() == 4) {
		if (cfg_r_src == "c2a_cnrs_xy") {
			drawC2ATransform(disp_frame, XY_CNRS, R, t, r, c);
		} else if (cfg_r_src == "c2a_cnrs_yz") {
			drawC2ATransform(disp_frame, YZ_CNRS, R, t, r, c);
		} else if (cfg_r_src == "c2a_cnrs_xz") {
			drawC2ATransform(disp_frame, XZ_CNRS, R, t, r, c);
		}
	}

	// write image to disk
	string cfg_img_fn = _config_fn.substr(0, std::max(static_cast<int>(_config_fn.size() - 4), 0)) + "-configImg.png";
    LOG("Writing config image to disk (%s)..", cfg_img_fn.c_str());
	if (!cv::imwrite(cfg_img_fn, disp_frame)) {
		LOG_ERR("Error writing config image to disk!");
	}

    if (_open) {
        LOG("Configuration complete!");
        PRINT("\n\nPress any key to exit..");
    }
    else {
        LOG_WRN("\n\nWarning! There were errors and the configuration file may not have been properly updated. Please run configuration again.");
        PRINT("\n\nPress any key to exit..");
    }
    getchar_clean();
    
    LOG("Exiting configuration!");
    return _open;
}
