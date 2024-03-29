#ifndef __BANDWIDTH_HPP__
#define __BANDWIDTH_HPP__

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <numeric>

#define STR_SIZE 1000
#define BW_VAL_THRESHOLD 5 // For WMA used in partition_bandwidth()

int cache_line_size = sysconf(_SC_LEVEL3_CACHE_LINESIZE);

struct ThreadInfo {
	int core_id;
	double execution_time; 		// in seconds
	double tot_exec_time;		// in seconds
	double tot_used_bw; 		// temp (for experiments)

	double guaranteed_bw; 		// a fraction of max_bw
	double used_bw;				// a fraction of max_bw

	std::vector<double> prev_used_bw;
	std::vector<double> prev_used_exec_time;

	ThreadInfo() : guaranteed_bw(0.0), tot_exec_time(0.0), tot_used_bw(0.0) {}
};

inline void print_thread_info(const ThreadInfo* th, int size){
  std::cout << std::setw(10) << "Core id" << std::setw(22) << "Execution time (ms)" << std::setw(22)
		<< "Total exec time (s)" << std::setw(20) << "Guaranteed bw (%)" << "Used bw (%)" <<'\n';

  for (int i = 0; i < size; i++)
    std::cout << std::setw(10) << th[i].core_id << std::setw(22) << th[i].execution_time * 1000 << std::setw(22)
			<< th[i].tot_exec_time << std::setw(20) << th[i].guaranteed_bw * 100 << th[i].used_bw * 100 << '\n';
}

double measure_max_bw(){
	if (!system(NULL))
		exit(EXIT_FAILURE);

	int status = system("mbw 500 -t 2 | grep 'AVG' | grep -oE '\\-?[0-9]+\\.[0-9]+' | tail -1 > temp");
	if(status < 0)
		printf("%s\n",strerror(errno));

	double measured_bw;
	std::ifstream f;

	f.open("temp");
	f >> measured_bw;
	f.close();

	std::cout << "max_bw: "<< measured_bw << '\n';

	return measured_bw;
} 

// TODO: Doesn't work as intented. Remove.
void get_bw_from_memguard(double* bw)
{
	if (!system(NULL))
		exit(EXIT_FAILURE);

	int status = system("tail -3 /sys/kernel/debug/memguard/usage > temp");
	if(status < 0)
		printf("%s\n",strerror(errno));

	std::ifstream f;
	double value;
	int i = 0;
	
	f.open("temp");
	while(f >> value){
		bw[i++] = value / 100.0;
	}
	f.close();
}

// Used to enable/disable best-effort etc
void set_exclusive_mode(int mode)
{
	if (!system(NULL))
		exit(EXIT_FAILURE);

	char script_str[STR_SIZE] = {0};
	snprintf(script_str, sizeof(script_str), "%s %d %s",
		"echo exclusive", mode, "> /sys/kernel/debug/memguard/control");

	int status = system(script_str);
	if (status < 0)
		printf("%s\n", strerror(errno));
}

// Assign bandwidth using percentages
void assign_bw(double bw, double core1_bw, double core2_bw, double core3_bw, double core4_bw)
{
	if (!system(NULL))
		exit(EXIT_FAILURE);

	char script_str[STR_SIZE] = {0};
	snprintf(script_str, sizeof(script_str), "%s %f %f %f %f %s",
		"echo mb", (core1_bw * bw), (core2_bw * bw), (core3_bw * bw), (core4_bw * bw), "> /sys/kernel/debug/memguard/limit");

	int status = system(script_str);
	if (status < 0)
		printf("%s\n", strerror(errno));
}

// Assign bandwidth by specifying bandwidth in MB
void assign_bw_MB(double core1_bw, double core2_bw, double core3_bw, double core4_bw)
{
	if (!system(NULL))
		exit(EXIT_FAILURE);

	char script_str[STR_SIZE] = {0};
	snprintf(script_str, sizeof(script_str), "%s %f %f %f %f %s",
		"echo mb", core1_bw, core2_bw, core3_bw, core4_bw, "> /sys/kernel/debug/memguard/limit");

	int status = system(script_str);
	if (status < 0)
		printf("%s\n", strerror(errno));
}

// Calculate used bandwidth by using performance counters
inline double calculate_bandwidth_MBs(unsigned long long l3_misses, double execution_time)
{
	unsigned long long bw_b = (double)l3_misses * cache_line_size;
	
	//return (double)(bw_b / 1024 / 1024) / execution_time; // Same result as below (blev fel med denna då vi även delade med 8)
	
	double divisor = execution_time * 1024.0 * 1024.0;
	return (bw_b + (divisor - 1.0)) / divisor;
}

// TODO: used_bw, used_wma_bw etc are not used and can be removed.
// Parition bandwidth between different cores
void partition_bandwidth(ThreadInfo* th, double bw, int num_threads)
{
	double used_wma_bw[num_threads] = {0};
	double used_wma_exec_time[num_threads] = {0};
	double used_bw[num_threads];
	double tot_bw = 0, tot_exec_time = 0;	// 100 percent of bw/exection time

	/* Calculate bandwidth used by each thread */
	for (int i = 0; i < num_threads; i++){
		//Add current used_bw to prev_used_bw and delete oldest
		th[i].prev_used_bw.push_back(used_bw[i]);
		th[i].prev_used_exec_time.push_back(th[i].execution_time);

		if(th[i].prev_used_bw.size() >= BW_VAL_THRESHOLD)
		{
			th[i].prev_used_bw.erase(th[i].prev_used_bw.begin());
			th[i].prev_used_exec_time.erase(th[i].prev_used_exec_time.begin());
		}

		//Calculate WMA (Weighted Moving Average BW) for each core
		int tot_weight = 0;
		for(int j = 0; j < th[i].prev_used_bw.size(); j++)
		{
			used_wma_bw[i] += th[i].prev_used_bw[j] * (j + 1);
			used_wma_exec_time[i] += th[i].prev_used_exec_time[j] * (j +1);
			tot_weight += (j + 1);
		}

		used_wma_bw[i] /= tot_weight;
		used_wma_exec_time[i] /= tot_weight;
		//std::cout << "wma bw: " << used_wma_bw[i] << "\n";
		
		tot_bw += used_wma_bw[i];
		tot_exec_time += used_wma_exec_time[i];

		// EWMA?
		// int n = th[i].runs < BW_VAL_THRESHOLD ? h[i].runs : BW_VAL_THRESHOLD;
		// int m = 2 / (1 + n);
		// th[i].ewma_bw = m * used_bw + (1 - m) * th[i].ewma_bw;
	}
	std::cout << '\n';
	
	/* Calculate how to partition bandwidth between different cores */
	for(int i = 0; i < num_threads; i++)
	{
		double new_bw = used_wma_exec_time[i] / tot_exec_time;

		th[i].guaranteed_bw = new_bw;
		//th[i].guaranteed_bw = new_bw < 0.30 ? new_bw : 0.30; // Give one core maximum 30% of max bandwidth
	}
	
	/* Partition bandwidth */
	assign_bw(bw, th[0].guaranteed_bw, th[1].guaranteed_bw, th[2].guaranteed_bw, th[3].guaranteed_bw);
}

#endif
