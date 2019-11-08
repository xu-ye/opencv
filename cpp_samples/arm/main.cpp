#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <librealsense2/rsutil.h>
#include <opencv2/opencv.hpp>   // Include OpenCV API

#if (defined _WIN32 || defined WINCE || defined __CYGWIN__)
#define OPENCV_VERSION "343"
#define REALSENSE_VERSION "2290"
#pragma comment(lib, "opencv_world" OPENCV_VERSION ".lib")
#pragma comment(lib, "realsense2_" REALSENSE_VERSION ".lib")
#endif

#define COLOR_COLS		640
#define COLOR_ROWS		480
#define DEPTH_COLS		640
#define DEPTH_ROWS		480


using pixel = std::pair<int, int>;

// Distance 3D is used to calculate real 3D distance between two pixels
float dist_3d(const rs2::depth_frame& frame, pixel u, pixel v);
void get3d_from2d(const rs2::depth_frame& frame, pixel u, float *upoint);


int main(int argc, char * argv[]) try
{
	// Declare depth colorizer for pretty visualization of depth data
	rs2::colorizer color_map;

	// Declare RealSense pipeline, encapsulating the actual device and sensors
	rs2::pipeline pipe;

	rs2::config cfg;

	cfg.enable_stream(RS2_STREAM_DEPTH, DEPTH_COLS, DEPTH_ROWS, RS2_FORMAT_Z16, 30);
	cfg.enable_stream(RS2_STREAM_COLOR, COLOR_COLS, COLOR_ROWS, RS2_FORMAT_BGR8, 30);

	//  Load from file
	//cfg.enable_device_from_file("D:/realsense/bag/20191106_085553.bag");
	// Start pipeline with chosen configuration
	pipe.start(cfg);

	const auto window_name = "Display Image";
	cv::namedWindow(window_name, cv::WINDOW_AUTOSIZE);

	const std::string input_file = "../../images/template/";

	std::vector<cv::String> imgnames;
	if (input_file[input_file.size() - 1] == '/' || input_file[input_file.size() - 1] == '\\') {
		cv::glob(input_file + "*g", imgnames);
	}
	else {
		cv::glob(input_file + "/*g", imgnames);
	}


	while (cv::waitKey(100) < 0 && cv::getWindowProperty(window_name, cv::WND_PROP_AUTOSIZE) >= 0)
	{

		rs2::frameset data = pipe.wait_for_frames(); // Wait for next set of frames from the camera
//		rs2::frame depth = data.get_depth_frame().apply_filter(color_map);
		rs2::video_frame color = data.get_color_frame();
		auto depth = data.get_depth_frame();

		// Query frame size (width and height)
//		const int w_depth = depth.as<rs2::video_frame>().get_width();
//		const int h_depth = depth.as<rs2::video_frame>().get_height();

		// Query frame size (width and height)
		const int w_color = color.get_width();
		const int h_color = color.get_height();


		// Create OpenCV matrix of size (w,h) from the colorized depth data
		cv::Mat image(cv::Size(w_color, h_color), CV_8UC3, (void*)color.get_data(), cv::Mat::AUTO_STEP);

		double final_min = 1;
		double final_max = 0;
		cv::Point final_Loc(0,0);
		int final_w = 0;
		int final_h = 0;

		for (const auto &it : imgnames)
		{
			auto temp = imread(it, 1);
			if (temp.empty()) 
			{
				std::cout << "imread failed.  fname:" << it << std::endl;
				continue;
			}



			int width = image.cols - temp.cols + 1;
			int height = image.rows - temp.rows + 1;
			int match_method = cv::TM_SQDIFF;

			cv::Mat result(width, height, CV_32FC1);
			result.setTo(0);

			matchTemplate(image, temp, result, match_method, cv::Mat());

			normalize(result, result, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());

			cv::Point minLoc;
			cv::Point maxLoc;
			double min, max;

			minMaxLoc(result, &min, &max, &minLoc, &maxLoc, cv::Mat());

			if (match_method == cv::TM_SQDIFF || match_method == cv::TM_SQDIFF_NORMED) 
			{
				if (final_min > min)
				{
					final_min = min;
					final_Loc = minLoc;
					final_w = temp.cols;
					final_h = temp.rows;
				}
			}
			else 
			{
				if (final_max < max)
				{
					final_max = max;
					final_Loc = maxLoc;
					final_w = temp.cols;
					final_h = temp.rows;
				}
			}
		}
		uint center_x = final_Loc.x + final_w / 2;
		uint center_y = final_Loc.y + final_h / 2;

		float cpoint[3] = { 0, 0, 0 };
		get3d_from2d(depth, pixel(center_x, center_y), cpoint);

		std::cout << cpoint[0] << " " << cpoint[1] << " " << cpoint[2] << std::endl;
		// 绘制矩形
		cv::rectangle(image, cv::Rect(final_Loc.x, final_Loc.y, final_w, final_h), cv::Scalar(0, 0, 255), 2, 8);
	
		//cv::putText(image, "text", cv::Point(center_x, center_y), cv::FONT_HERSHEY_SIMPLEX, 1, (0, 0, 255), 2);
		// Update the window with new data
		cv::imshow(window_name, image);
	}

	return EXIT_SUCCESS;
}
catch (const rs2::error & e)
{
	std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
	return EXIT_FAILURE;
}
catch (const std::exception& e)
{
	std::cerr << e.what() << std::endl;
	return EXIT_FAILURE;
}

//计算三维空间中两个点的距离
float dist_3d(const rs2::depth_frame& frame, pixel u, pixel v)
{
	float upixel[2]; // From pixel
	float upoint[3]; // From point (in 3D)

	float vpixel[2]; // To pixel
	float vpoint[3]; // To point (in 3D)

					 // Copy pixels into the arrays (to match rsutil signatures)
	upixel[0] = u.first;
	upixel[1] = u.second;
	vpixel[0] = v.first;
	vpixel[1] = v.second;

	// Query the frame for distance
	// Note: this can be optimized
	// It is not recommended to issue an API call for each pixel
	// (since the compiler can't inline these)
	// However, in this example it is not one of the bottlenecks
	auto udist = frame.get_distance(upixel[0], upixel[1]);
	auto vdist = frame.get_distance(vpixel[0], vpixel[1]);

	// Deproject from pixel to point in 3D
	rs2_intrinsics intr = frame.get_profile().as<rs2::video_stream_profile>().get_intrinsics(); // Calibration data
	rs2_deproject_pixel_to_point(upoint, &intr, upixel, udist);
	rs2_deproject_pixel_to_point(vpoint, &intr, vpixel, vdist);

	// Calculate euclidean distance between the two points
	return sqrt(pow(upoint[0] - vpoint[0], 2) +
		pow(upoint[1] - vpoint[1], 2) +
		pow(upoint[2] - vpoint[2], 2));
}

//根据像素点坐标获取相机坐标
void get3d_from2d(const rs2::depth_frame& frame, pixel u, float *upoint)
{
	float upixel[2]; // From pixel

	upixel[0] = u.first;
	upixel[1] = u.second;


	// Query the frame for distance
	// Note: this can be optimized
	// It is not recommended to issue an API call for each pixel
	// (since the compiler can't inline these)
	// However, in this example it is not one of the bottlenecks
	auto udist = frame.get_distance(upixel[0], upixel[1]);

	// Deproject from pixel to point in 3D
	rs2_intrinsics intr = frame.get_profile().as<rs2::video_stream_profile>().get_intrinsics(); // Calibration data
	rs2_deproject_pixel_to_point(upoint, &intr, upixel, udist);
}

