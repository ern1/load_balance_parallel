#ifndef __PERFCOUNTERS_PAPI_HPP__
#define __PERFCOUNTERS_PAPI_HPP__

#include <iostream>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <papi.h>
#include <linux/perf_event.h>

thread_local int retval;
thread_local int event_set = PAPI_NULL;

inline long long unsigned time_ns()
{
  timespec ts;

  /* We use monotonic to avoid any leap-problems generated by NTP. */
  if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
    exit(1);
  }

  return ((long long unsigned) ts.tv_sec) * 1000000000LLU
    + (long long unsigned) ts.tv_nsec;
}

inline void init_PAPI()
{
	if((retval = PAPI_library_init(PAPI_VER_CURRENT)) != PAPI_VER_CURRENT )
    printf("Failed to initialize papi: %s\n", PAPI_strerror(retval));

	if ((retval = PAPI_thread_init(&pthread_self)) != PAPI_OK)
		printf("Failed to init papi thread: %s\n", PAPI_strerror(retval));
}

inline void start_PAPI()
{
  int monitor_pid = syscall(SYS_gettid);

  if ((retval = PAPI_register_thread()) != PAPI_OK)
    printf("Failed to register thread: %s\n", PAPI_strerror(retval));

  if ((retval = PAPI_create_eventset(&event_set)) != PAPI_OK)
    printf("Failed to create eventset: %s\n", PAPI_strerror(retval));
  
  if ((retval = PAPI_add_event(event_set, PAPI_L3_TCM)) != PAPI_OK)
    printf("Failed to attach L3 misses: %s\n", PAPI_strerror(retval));
  
  if ((retval = PAPI_add_event(event_set, PAPI_PRF_DM)) != PAPI_OK)
    printf("Failed to attach Prefetch Cache Misses: %s\n", PAPI_strerror(retval));

  /* ----------------------- TEST ----------------------- */
  //std::cout << "---------> event_set: " << *event_set << std::endl;

  //if ((retval = PAPI_add_event(*event_set, PAPI_TLB_IM)) != PAPI_OK)
  //  printf("nooooo: %d\n", retval);

  //perf:PERF_COUNT_HW_BUS_CYCLES FUNKAR
  //perf_hw_id::PERF_COUNT_HW_BUS_CYCLES 
  //LONGEST_LAT_CACHE_MISS:MISS 
  //PAPI_PRF_DM

  //int teststtst = LLC_PREFETCH_MISSES;  //Något fel
  //int native = MEM_LOAD_UOPS_L3_HIT_RETIRED | 0x01;
  //PAPI_event_info_t info;
  //int native = 0x0;
  // int event_code;
  // if ((retval = PAPI_event_name_to_code("perf::PERF_COUNT_HW_BUS_CYCLES", &event_code)) != PAPI_OK){
  //   printf("nooooo0: %d\n", retval);
  // }

  // unsigned int native = 0xBB;

  // if ((retval = PAPI_add_event(*event_set, native)) != PAPI_OK)
  //   printf("Failed to attach Prefetch Cache Misses: %s\n", PAPI_strerror(retval));

  //printf("event_code = %d\n", event_code);
  
  // if (PAPI_get_event_info(event_code, &info) != PAPI_OK) {
  //   if ((retval = PAPI_enum_event(&event_code, 0)) != PAPI_OK) 
  //     printf("nooooo1: %d\n", retval);
  // }
  // std::cout << "info: " << info.long_descr << std::endl;
  // if ((retval = PAPI_add_event(*event_set, event_code)) != PAPI_OK)
  //   printf("nooooo2: %d\n", retval);
  /* ---------------------------------------------------- */


  if ((retval = PAPI_attach(event_set, monitor_pid)) != PAPI_OK)
    printf("Failed to attach tid to eventset: %s\n", PAPI_strerror(retval));
   
  if ((retval = PAPI_start(event_set)) != PAPI_OK)
    printf("Failed to start papi: %s\n", PAPI_strerror(retval));
}

inline void read_PAPI(long long* values)
{
  if ((retval = PAPI_read(event_set, values)) != PAPI_OK)
    printf("Failed to read the events: %s\n", PAPI_strerror(retval));
}

inline void stop_PAPI(long long* values)
{
  if ((retval = PAPI_stop(event_set, values)) != PAPI_OK)
		printf("Failed to stop eventset: %s\n", PAPI_strerror(retval));

  if ((retval = PAPI_cleanup_eventset(event_set)) != PAPI_OK)
		printf("Failed to cleanup eventset: %s\n", PAPI_strerror(retval));

  if ((retval = PAPI_destroy_eventset(&event_set)) != PAPI_OK)
		printf("Failed to destroy eventset: %s\n", PAPI_strerror(retval));
}

#endif