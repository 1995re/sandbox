#include "stdafx.h"

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include <opencv/cv.h>
#include <opencv/highgui.h>

#include <stdint.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#include <sstream>

using namespace cv;
using namespace std;

//
//-- Constants
//

struct Settings {
   // Command line settings
   int monitor;
   bool fullscreen;
   int sandPlaneDistanceInMM;

	// Derived from settings
	RECT monitorRect;

	int beamerXres;
	int beamerYres;

	int maxSandDepthInMM;
	int maxSandHeightInMM;

	uint16_t boxBottomDistanceInMM;

} settings;


#ifdef _WIN32

BOOL CALLBACK MonitorEnumProc(
   __in  HMONITOR hMonitor,
   __in  HDC hdcMonitor,
   __in  LPRECT lprcMonitor,
   __in  LPARAM dwData
   )
{
   std::vector<RECT>* monitors = (std::vector<RECT>*)dwData;

   monitors->push_back(*lprcMonitor);

   return TRUE;
}

bool enumMonitors(std::vector<RECT> &rect)
{
   cout << "Enumerating monitors...";
   if(EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM) &rect) == 0)
   {
       cout << "failed" << endl;
       return false;
   }
   cout << "ok" << endl;
   return true;
}

bool fullScreen(RECT &monitor, const std::string windowName)
{
   HWND hwnd = FindWindowA(NULL, windowName.c_str());
   if(hwnd == NULL) {
       return false;
   }

   SetWindowLongPtr(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);
   SetWindowLongPtr(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
   
   SetWindowPos(hwnd, HWND_TOPMOST, monitor.left, monitor.top, monitor.right - monitor.left, monitor.bottom - monitor.top, SWP_SHOWWINDOW);
   
   ShowWindow(hwnd, SW_MAXIMIZE);

   return true;
}

bool getMonitorRect(int monitor, RECT &monitorRect)
{
   std::vector<RECT> rect;
   if(!enumMonitors(rect))
       return false;

   if (monitor < 0 || monitor >= rect.size())
       return false;

   monitorRect = rect[monitor];

   return true;
}

bool printMonitors()
{
   std::vector<RECT> monitors;
   if(!enumMonitors(monitors))
   {
       return false;
   }

   cout << endl;

   size_t num = 0;
   for (std::vector<RECT>::iterator it = monitors.begin();
       it != monitors.end();
       ++it)
   {
       RECT *monitor = &(*it);

       cout << "Monitor " << num << ":" << endl;
       cout << "Left/Right: " << monitor->left << " - " << monitor->right << endl;
       cout << "Top/Bottom: " << monitor->top << " - " << monitor->bottom << endl << endl;

       ++num;
   }

   return true;
}
#else

bool enumMonitors(std::vector<RECT> &rect) {
	/* Not implemented for other platforms */
	return false;
}

bool fullScreen(RECT &monitor, const std::string windowName) {
	/* Not implemented for other platforms */
	return false;
}

bool printMonitors() {
	/* Not implemented for other platforms */ 
	return false;
}

bool getMonitorRect(int monitor, RECT &monitorRect) {
	/* Not implemented for other platforms */ 
	return false;
}

#endif



static void colorizeDisparity( const Mat& gray, Mat& rgb, double maxDisp=-1.f, float S=1.f, float V=1.f )
{
    CV_Assert( !gray.empty() );
    CV_Assert( gray.type() == CV_8UC1 );

    if( maxDisp <= 0 )
    {
        maxDisp = 0;
        minMaxLoc( gray, 0, &maxDisp );
    }

    rgb.create( gray.size(), CV_8UC3 );
    rgb = Scalar::all(0);
    if( maxDisp < 1 )
        return;

    for( int y = 0; y < gray.rows; y++ )
    {
        for( int x = 0; x < gray.cols; x++ )
        {
            uchar d = gray.at<uchar>(y,x);
            unsigned int H = ((uchar)maxDisp - d) * 240 / (uchar)maxDisp;

            unsigned int hi = (H/60) % 6;
            float f = H/60.f - H/60;
            float p = V * (1 - S);
            float q = V * (1 - f * S);
            float t = V * (1 - (1 - f) * S);

            Point3f res;

            if( hi == 0 ) //R = V,  G = t,  B = p
                res = Point3f( p, t, V );
            if( hi == 1 ) // R = q, G = V,  B = p
                res = Point3f( p, V, q );
            if( hi == 2 ) // R = p, G = V,  B = t
                res = Point3f( t, V, p );
            if( hi == 3 ) // R = p, G = q,  B = V
                res = Point3f( V, q, p );
            if( hi == 4 ) // R = t, G = p,  B = V
                res = Point3f( V, p, t );
            if( hi == 5 ) // R = V, G = p,  B = q
                res = Point3f( q, p, V );

            uchar b = (uchar)(std::max(0.f, std::min (res.x, 1.f)) * 255.f);
            uchar g = (uchar)(std::max(0.f, std::min (res.y, 1.f)) * 255.f);
            uchar r = (uchar)(std::max(0.f, std::min (res.z, 1.f)) * 255.f);

            rgb.at<Point3_<uchar> >(y,x) = Point3_<uchar>(b, g, r);
        }
    }
}

static float getMaxDisparity( VideoCapture& capture )
{
    const int minDistance = 400; // mm
    float b = (float)capture.get( CV_CAP_OPENNI_DEPTH_GENERATOR_BASELINE ); // mm
    float F = (float)capture.get( CV_CAP_OPENNI_DEPTH_GENERATOR_FOCAL_LENGTH ); // pixels
    return b * F / minDistance;
}

bool initializeCapture(VideoCapture &capture)
{
	cout << "Opening device...";
	capture.open(CV_CAP_OPENNI_ASUS);
	if (!capture.isOpened())
	{
		cout << "failed" << endl;
		return false;
	}
	cout << "ok" << endl;

	cout << "Setting capture mode...";
	if(!capture.set( CV_CAP_OPENNI_IMAGE_GENERATOR_OUTPUT_MODE, CV_CAP_OPENNI_VGA_30HZ ))
	{
		cout << "failed" << endl;
		return false;
	}

	cout << "ok" << endl;
	cout << "Settings: " << endl <<
            left << setw(20) << "FRAME_WIDTH" << capture.get( CV_CAP_PROP_FRAME_WIDTH ) << endl <<
            left << setw(20) << "FRAME_HEIGHT" << capture.get( CV_CAP_PROP_FRAME_HEIGHT ) << endl <<
            left << setw(20) << "FRAME_MAX_DEPTH" << capture.get( CV_CAP_PROP_OPENNI_FRAME_MAX_DEPTH ) << " mm" << endl <<
            left << setw(20) << "FPS" << capture.get( CV_CAP_PROP_FPS ) << endl;

	return true;
}

bool grabAndStore(VideoCapture &capture, const std::string &prefix = std::string())
{
	if(!capture.grab())
		return false;

	Mat img;
	if(!capture.retrieve(img, CV_CAP_OPENNI_BGR_IMAGE))
		return false;

	if(!imwrite(prefix + "bgr.png", img))
		return false;

	if(!capture.retrieve(img, CV_CAP_OPENNI_GRAY_IMAGE))
		return false;

	if(!imwrite(prefix + "gray.png", img))
		return false;

	if(!capture.retrieve(img, CV_CAP_OPENNI_DEPTH_MAP))
		return false;

	if(!imwrite(prefix + "depth.png", img))
		return false;

	return true;
}

bool grabAndStoreMany(VideoCapture &capture, const int n = 30, const std::string &prefix = std::string())
{
	for (int i = 0; i < n; ++i)
	{
		cout << "Snap " << i << endl;
		stringstream ss;
		ss << prefix << i << "_";
		if(!grabAndStore(capture, ss.str()))
		{
			cout << "Failed" << endl;
			return false;
		}

		if( waitKey( 30 ) >= 0 )
			return false;
	}

	return true;
}



void calibrationMouseClickHandler(int event, int x, int y, int flags, void* ptr)
{
	if (event == CV_EVENT_LBUTTONDOWN)
	{
		vector<Point2f> *calibPoints = (vector<Point2f>*)ptr;
		cout << "Calibration point at x: " << x << " y: " << y << endl;
		calibPoints->push_back(Point2f(x,y));
	}
}

bool getManualCalibrationRectangleCorners(VideoCapture &capture, vector<Point2f> &calibPoints)
{
	calibPoints.clear();
	const std::string CALIB_BGR_WND = "Calibration (please click the edges of the beamer area clockwise starting top left)";

	namedWindow(CALIB_BGR_WND, CV_WINDOW_KEEPRATIO);
	setMouseCallback(CALIB_BGR_WND, calibrationMouseClickHandler, &calibPoints);

	cout << "Get calibration points" << endl;
	while (calibPoints.size() < 4)
	{
		Mat bgrImage;
		if (!capture.grab())
		{
			cerr << "Failed to grab frame" << endl;
			return false;
		}

		if (!capture.retrieve(bgrImage, CV_CAP_OPENNI_BGR_IMAGE))
		{
			cerr << "Failed to retrieve" << endl;
			return false;
		}

		
		for (vector<Point2f>::iterator it = calibPoints.begin();
			it != calibPoints.end();
			++it)
		{
			circle(bgrImage, Point( it->x, it->y ), 5,  Scalar(0), 2, 8, 0 );
		}

		imshow(CALIB_BGR_WND, bgrImage);

		if( waitKey( 30 ) >= 0 )
			break;

	}

	cv::destroyWindow(CALIB_BGR_WND);

	if (calibPoints.size() < 4)
	{
		cout << "Aborted calibration" << endl;
		return false;
	}

	return true;
}

bool getHomography(VideoCapture &capture, Mat &homography)
{
	vector<Point2f> calibPoints;
	if(!getManualCalibrationRectangleCorners(capture, calibPoints))
	{
		cerr << "Calibration failed" << endl;
		return false;
	}

	vector<Point2f> realPoints;

	realPoints.push_back(Point2f(0,0)); // Top left
	realPoints.push_back(Point2f(settings.beamerXres,0)); // Top right
	realPoints.push_back(Point2f(settings.beamerXres, settings.beamerYres)); // Bottom right
	realPoints.push_back(Point2f(0, settings.beamerYres)); // Bottom left

	homography = getPerspectiveTransform(calibPoints, realPoints);
	return true;
}

bool getDepthCorrection(VideoCapture &capture, Mat &homography, uint16_t &boxBottomDistanceInMM)
{
	if (settings.sandPlaneDistanceInMM >= 0) {
		cout << "Using manual settings for depth correction" << endl;
		cout << "Sandbox sand level set to: " << settings.sandPlaneDistanceInMM << "mm" << endl;
		boxBottomDistanceInMM = settings.sandPlaneDistanceInMM + settings.maxSandDepthInMM;
		cout << "Sandbox box bottom level estimated at: " << boxBottomDistanceInMM << "mm" << endl;
		return true; // Depth correction already set manually
	}

	if(!capture.grab())
		return false;

	Mat rawDepthInMM;
	if(!capture.retrieve(rawDepthInMM, CV_CAP_OPENNI_DEPTH_MAP))
		return false;

	Mat depthWarpedInMM;
	warpPerspective(rawDepthInMM, depthWarpedInMM, homography, Size(settings.beamerXres, settings.beamerYres));
	
	uint64_t val = 0;
	unsigned int num = 0;
	// Sample a few points to estimate ground level
	for (int x = 0; x < settings.beamerXres; x += settings.beamerXres/16)
	{
		for (int y = 0; y < settings.beamerYres; y += settings.beamerYres/16)
		{
			val += depthWarpedInMM.at<unsigned short>(Point(x,y));
			++num;
		}
	}

	val = val / num;
	cout << "Sandbox sand level estimated at: " << val << "mm" << endl;
	val += settings.maxSandDepthInMM;
	cout << "Sandbox box bottom level estimated at: " << val << "mm" << endl;
	
	boxBottomDistanceInMM = static_cast<uint16_t>(val);

	//TODO: Perspective correction doesn't help with 3D issues. Should use 4 edge points for reconstructing 3d base plane but that can wait.

	// Create depth correction as level
	//Mat baseLayer = Mat(settings.beamerXres, settings.beamerYres, depthWarpedInMM.type(), val); // Idealized sandbox base
	//cv::subtract(baseLayer, depthWarpedInMM, depthCorrection);
	return true;
}

bool sandboxNormalize(Mat &depthWarped, Mat& depthWarpedNormalized, uint16_t boxBottomDistanceInMM)
{
	const size_t rows = depthWarped.rows;
	const size_t cols = depthWarped.cols;
	depthWarpedNormalized = Mat(rows, cols, depthWarped.type());

	const uint16_t topOrig = boxBottomDistanceInMM - settings.maxSandDepthInMM - settings.maxSandHeightInMM;
	const uint16_t range = boxBottomDistanceInMM - topOrig;

	for (size_t row = 0; row < rows; ++row)
	{
		for (size_t col = 0; col < cols; ++col)
		{
			uint16_t val = depthWarped.at<uint16_t>(Point(col, row));
			// Clip
			if (val > boxBottomDistanceInMM) val = boxBottomDistanceInMM;
			else if (val < topOrig) val = topOrig;

			// Shift and invert
			val = boxBottomDistanceInMM - val;

			// Scale and save
			const uint16_t fac = numeric_limits<uint16_t>::max() / range;
			const uint16_t color = val * fac;
			depthWarpedNormalized.at<uint16_t>(Point(col, row)) = color;
		}
	}


	return true;
}

//
// Snippets
//

	/*if(!grabAndStore(capture))
	{
		cout << "Failed to store set of images" << endl;
		return 1;
	}*/


/*
// 1024x768 Beamer resolution
settings.beamerXres = 1024;
settings.beamerYres = 768;

// Sandbox constants
settings.maxSandDepthInMM = 90;
settings.maxSandHeightInMM = 200;

*/


bool parseSettingsFromCommandline(int argc, char **argv, bool &quit)
{
	const char *keys =
		"{f|fullscreen|true|If true fullscreen is used for output window}"
		"{m|monitor|0|Monitor to use for fullscreen}"
		"{e|enumerate|false|Enumerate monitors and quit}"
		"{d|depth|90|Maximum sand depth below plane in mm}"
		"{t|top|200|Maximum sand height above plane in mm}"
		"{g|ground|-1|Distance of the sand plane to the sensor. (-1 for automatic calibration.)}"
		"{h|help|false|Print help}";

	CommandLineParser clp(argc, argv, keys);

	if (clp.get<bool>("h"))
	{
		cout << "Usage: " << argv[0] << " [options]" << endl;
		clp.printParams();
		quit = true;
		return true;
	}

	if (clp.get<bool>("e"))
	{
		if (printMonitors())
		{
			quit = true;
			return true;
		}
		else
		{
			quit = true;
			return false;
		}
	}

	settings.fullscreen = clp.get<bool>("f");
	settings.monitor = clp.get<int>("m");

	settings.sandPlaneDistanceInMM = clp.get<int>("g");
	settings.maxSandDepthInMM = clp.get<int>("d");
	settings.maxSandHeightInMM = clp.get<int>("t");


	if (!getMonitorRect(settings.monitor, settings.monitorRect))
	{
		cerr << "Failed to get information on monitor " << settings.monitor << endl;
		cerr << "Use the -e option to enumerate available monitors" << endl;
		quit = true;
		return false;
	}

	settings.beamerXres = settings.monitorRect.right - settings.monitorRect.left;
	settings.beamerYres = settings.monitorRect.bottom - settings.monitorRect.top;

	quit = false;
	return true;
}

int main( int argc, char* argv[] )
{
	bool quit;
	if(!parseSettingsFromCommandline(argc, argv, quit))
		return 1;
	
	if (quit)
		return 0;
	
	VideoCapture capture;
	if (!initializeCapture(capture))
		return 1;

	Mat homography;
	if(!getHomography(capture, homography))
		return 1;

	if(!getDepthCorrection(capture, homography, settings.boxBottomDistanceInMM))
		return 1;

	const std::string BGR_IMAGE = "Bgr Image";
	const std::string DEPTH_MAP = "Depth Map";
	const std::string BGR_WARPED = "Warped BGR Image";
	const std::string DEPTH_WARPED = "Warped Depth Image";
	const std::string SAND_NORMALIZED = "Normalized Sand";

	namedWindow(BGR_IMAGE, CV_WINDOW_KEEPRATIO | CV_GUI_EXPANDED);
	namedWindow(DEPTH_MAP, CV_WINDOW_KEEPRATIO | CV_GUI_EXPANDED);
	namedWindow(BGR_WARPED, CV_WINDOW_KEEPRATIO | CV_GUI_EXPANDED);
	namedWindow(DEPTH_WARPED, CV_WINDOW_KEEPRATIO | CV_GUI_EXPANDED);
	namedWindow(SAND_NORMALIZED);

	if (settings.fullscreen)
	{
		if(!fullScreen(settings.monitorRect, SAND_NORMALIZED))
		{
			cerr << "Failed to fullscreen output window on monitor " << settings.monitor << endl;
		}
		else
		{
			cout << "Entered fullscreen on monitor " << settings.monitor << endl;
		}
	}


	cout << "Enter mainloop" << endl;

	for (;;)
	{
		Mat bgrImage;
		Mat depthMap;

		if (!capture.grab())
		{
			cerr << "Failed to grab frame" << endl;
			return 1;
		}

		if (!capture.retrieve(bgrImage, CV_CAP_OPENNI_BGR_IMAGE))
		{
			cerr << "Failed to retrieve" << endl;
			return 1;
		}

		if (!capture.retrieve(depthMap, CV_CAP_OPENNI_DEPTH_MAP))
		{
			cerr << "Failed to retrieve valid depth mask" << endl;
			return 1;
		}

		Mat bgrWarped;		
		warpPerspective(bgrImage, bgrWarped, homography, Size(settings.beamerXres, settings.beamerYres));

		Mat depthWarped;
		warpPerspective(depthMap, depthWarped, homography, Size(settings.beamerXres, settings.beamerYres));

		Mat depthWarpedNormalized;
		sandboxNormalize(depthWarped, depthWarpedNormalized, settings.boxBottomDistanceInMM);

		imshow(SAND_NORMALIZED, depthWarpedNormalized);
		//unsigned short centerval = depthWarped.at<unsigned short>(Point(settings.beamerXres/2, settings.beamerYres/2));
		//cout << centerval << endl;

		imshow(BGR_WARPED, bgrWarped);
		imshow(DEPTH_WARPED, depthWarped);
		imshow(BGR_IMAGE, bgrImage);
		imshow(DEPTH_MAP, depthMap);

		if( waitKey( 30 ) >= 0 )
			break;
	}

	return 0;
}



