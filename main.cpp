#include <opencv2/opencv.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/xfeatures2d/nonfree.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <string>
#include "perfCounter.hpp"
#include "bandwidth.hpp"

#define STR_SIZE 1000

#define USE_MEMGUARD 1
#define USE_AMBP 1
#define NUM_THREADS 4
#define WRITE_STATS_TO_FILE 0

double max_bw;
int global_width, global_height;
cv::Mat frame;
cv::Mat* roi;
char program_start_time[STR_SIZE] = {0};

double tot_tot_used_bw = 0; // total bw for all threads and every frame, used to calculate average bw usage (temp)

// For cv::drawKeypoints
const cv::Scalar COLORS[4] = {
	cv::Scalar(255, 0, 0),
	cv::Scalar(0, 255, 0),
	cv::Scalar(0, 0, 255),
	cv::Scalar(255, 0, 255)
};

void send_thread_exec_time_to_file(double exec_time0, double exec_time1, double exec_time2, double exec_time3)
{
	if (!system(NULL))
		exit(EXIT_FAILURE);

	char values_str[STR_SIZE] = {0};
	snprintf(values_str, sizeof(values_str), "%f,%f,%f,%f\n",
		 exec_time0, exec_time1, exec_time2, exec_time3);

	char file_name[STR_SIZE] = {0};
	snprintf(file_name, sizeof(file_name), "%s%s%s", "thread_exec_", program_start_time, ".csv");

	std::ofstream file;
	file.open(file_name, std::ios::app);
	file << values_str;
	file.close();
}

void send_frame_exec_time_to_file(double exec_time)
{
	if (!system(NULL))
		exit(EXIT_FAILURE);

	char file_name[STR_SIZE] = {0};
	snprintf(file_name, sizeof(file_name), "%s%s%s", "frame_exec_", program_start_time, ".csv");

	std::ofstream file;
	file.open(file_name, std::ios::app);
	file << exec_time << '\n';
	file.close();
}

void send_total_exec_time_to_file(double exec_time)
{
	if (!system(NULL))
		exit(EXIT_FAILURE);

	std::ofstream file;
	file.open("total_exec_time.csv", std::ios::app);
	file << program_start_time << " - Execution time: " << exec_time << ", Average total used bw (all cores): " << tot_tot_used_bw << ", Max bandwidth: " << max_bw << '\n';
	file.close();
}

int stick_this_thread_to_core(int core_id)
{
  int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (core_id < 0 || core_id >= num_cores)
  {
    printf("Core out of index");
    return 0;
  }

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);

  pthread_t current_thread = pthread_self();    
  return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

void* feature_thread(void* threadArg)
{
	ThreadInfo* thread_info = (ThreadInfo*)threadArg;
	stick_this_thread_to_core(thread_info->core_id);
	reset_counter(thread_info->core_id);

	// Start timer
	long long start_cycle = cv::getTickCount();

	// Set ROI
	int local_x = 0, local_y;
	if (thread_info->core_id == 0)
		local_y = 0;
	else
		local_y = (int)global_height / NUM_THREADS * thread_info->core_id - 1;
	int local_width = global_width;
	int local_height = (int)global_height / NUM_THREADS;
	cv::Rect roi_rect = cv::Rect(local_x, local_y, local_width, local_height);
	roi[thread_info->core_id] = frame(roi_rect);

	// Process ROI of frame
	std::vector<cv::KeyPoint> keypoints;
	cv::Ptr<cv::xfeatures2d::SIFT> detector = cv::xfeatures2d::SIFT::create(1000);
	detector->detect(roi[thread_info->core_id], keypoints, cv::Mat());
	cv::drawKeypoints(roi[thread_info->core_id], keypoints, roi[thread_info->core_id], COLORS[thread_info->core_id], cv::DrawMatchesFlags::DEFAULT);

	// End timer
	thread_info->execution_time = (double)(cv::getTickCount() - start_cycle) / cv::getTickFrequency();
	thread_info->tot_exec_time += thread_info->execution_time;
	
	// Read performance counters and calculate used bandwidth
	unsigned long long cache_misses = read_counter(thread_info->core_id);
	thread_info->used_bw = calculate_bandwidth_MBs(cache_misses, thread_info->execution_time) / max_bw;
	thread_info->tot_used_bw += thread_info->used_bw;
	//std::cout << "core_id: " << thread_info->core_id << ", cache misses: " << cache_misses << std::endl;
}

int main(int argc, char** argv)
{
	// Check if user is root
	if (getuid()) {
		std::cout << "User is not root" << std::endl;
		return 0;
	}

	{
		time_t now = time(0);
		struct tm *ltm = localtime(&now);
		snprintf(program_start_time, sizeof(program_start_time), "%d_%d_%d", ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
	}
	
	umask(0777);
	init_counters(NUM_THREADS);
	std::cout << std::fixed << std::setprecision(3) << std::left;
	
	void* status;
	roi = new cv::Mat[NUM_THREADS];
	ThreadInfo* thread_info = new ThreadInfo[NUM_THREADS];
	double total_tick_count = 0;
	int num_frames = 0;
	cv::VideoCapture cap(argv[1]);
  	cap.set(CV_CAP_PROP_FOURCC, CV_FOURCC('H', '2', '6', '4'));

	// Check if camera opened successfully
	if(!cap.isOpened()) {
		std::cout << "Error opening video stream or file" << std::endl;
		return 0;
	}

#if USE_MEMGUARD
	// Disable best-effort
	//set_exclusive_mode(0);

	// Use best-effort
	set_exclusive_mode(2);
	
	// Increase bw for each core to not interfere with the bandwidth measurement
	assign_bw_MB(100000.0, 100000.0, 100000.0, 100000.0);
#endif

	// Measure and set max_bw
	max_bw = measure_max_bw();

#if USE_MEMGUARD
	{
		// Distribute bandwidth evenly among cores
		double new_bw = max_bw / 4.0;
		assign_bw_MB(new_bw, new_bw, new_bw, new_bw);
		for(int i = 0; i < NUM_THREADS; i++)
			thread_info[i].guaranteed_bw = new_bw / max_bw;

		// För Experiment 1 (point of saturation)
		// const double new_bw0 = 0.25;
		// const double other_new = (1 - new_bw0) / 3.0;
		// assign_bw(max_bw, other_new, other_new, new_bw0, other_new);

		// För experiment 3 (isolation)
		//assign_bw(max_bw, 0.3, 0.3, 0.3, 0.1);
	}
#endif

	while((cv::waitKey(25) != 27) && (num_frames < 100)) {
		cap >> frame;
		if(frame.empty())
			break;
		
		global_width = frame.size().width;
		global_height = frame.size().height;
		int size = frame.elemSize();
		int cols = frame.cols;
		int rows = frame.rows;
		int total_size = size * cols * rows;
		
		// Create threads
		pthread_t threads[NUM_THREADS];
  		pthread_attr_t attr;

		int64 start = cv::getTickCount();

		for (int i = 0; i < NUM_THREADS; i++) {
			thread_info[i].core_id = i;

			if(pthread_create(&threads[i], NULL, feature_thread, (void*)&thread_info[i]) != 0) {
				perror("thread create error");
				exit(1);
			}
		}

		// Join threads
		for (int i = 0; i < NUM_THREADS; i++) {
			if(pthread_join(threads[i], &status) != 0) {
				perror("thread join error");
				exit(1);
			}
		}

		// Print stats
		double ticks = cv::getTickCount() - start;
		double fps = cv::getTickFrequency() / ticks;
		print_thread_info(thread_info, NUM_THREADS);
		std::cout << "FPS: " << fps << "\n\n";
		
		double tot_used_bw = 0; // Used bw for all threads	
		for(int i = 0; i < NUM_THREADS; i++)
			tot_used_bw += thread_info[i].used_bw;
		std::cout << "Total used bw: " << tot_used_bw * 100 << "%\n";
		
		tot_tot_used_bw += tot_used_bw;
		total_tick_count += ticks;
		num_frames++;

		// Create image
		std::vector<cv::Mat> result_mat;
		for (int i = 0; i < NUM_THREADS; i++)
			result_mat.push_back(roi[i].clone());
		
		cv::Mat out;
		cv::vconcat(result_mat, out);
		cv::imshow("Video", out);
		
		// För att spara alla bilder som png
		/*std::stringstream fn_out;
		fn_out << "keypoints" << "_" << num_frames << ".png";
		cv::imwrite(fn_out.str(), out);*/

#if WRITE_STATS_TO_FILE
		send_frame_exec_time_to_file(ticks / cv::getTickFrequency());
		send_thread_exec_time_to_file(thread_info[0].execution_time, thread_info[1].execution_time,
					    				thread_info[2].execution_time, thread_info[3].execution_time);
#endif

#if USE_MEMGUARD && USE_AMBP
		partition_bandwidth(thread_info, max_bw, NUM_THREADS);
#endif

	}

	stop_counters();
	cap.release();
  	cv::destroyAllWindows();

	// Calculate total execution time (time from fork to join for all frames)
	double exec_time = total_tick_count / cv::getTickFrequency();

	// Print stats
	std::cout << "Execution time: " << exec_time << std::endl;
	//std::cout << "Thread 2 - Avg exec time: " << thread_info[2].tot_exec_time / 100 << ", Avg used bw: " << thread_info[2].tot_used_bw << ", Avg tot used bw (all cores): " << tot_tot_used_bw << ", max_bw: " << max_bw << std::endl;
	std::cout << "Avg tot used bw (all cores): " << tot_tot_used_bw << ", max_bw: " << max_bw << std::endl;

#if WRITE_STATS_TO_FILE
	send_total_exec_time_to_file(exec_time);
#endif

	return 0;
}