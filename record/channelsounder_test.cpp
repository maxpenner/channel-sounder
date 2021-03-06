//
// Copyright 2011-2015 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <uhd/convert.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <atomic>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "ringbuffer_rx.h"
#include "fifo_ch_measurement.h"

#define DURATION_SEC            120             // actual execution time of this test programm
#define N_CHANNELS              4               // number of channels/antennas
#define N_BYTES_PER_ITEM        4               // 4 for complex int16_t and 8 for float
#define N_MIN_SAMPLES           2000            // minimum number of samples passed to ringbuffer
#define N_MAX_SAMPLES           10000           // maximum number of samples passed to ringbuffer
#define RX_RATE                 200000000       // target samp_rate, test programm likely much slower
#define ITEM_CNT_MAX            1000

/***********************************************************************
 * Test result variables
 **********************************************************************/
// --

/***********************************************************************
 * Benchmark RX Rate
 **********************************************************************/
void benchmark_RX_RATE(std::atomic<bool>& burst_timer_elapsed)
{
    unsigned long long n_new_samples = 0;       // number of samples passed to ringbuffer
    unsigned long long num_rx_samps = 0;        // uhd counter of all samples
    unsigned long long item_cnt = 0;
    
    std::vector<void*> buffs = channelsounder::get_ringbuffer_rx_pointers(0);
    
    while (burst_timer_elapsed == false){
        
        // we measure the execution time to follow sampling rate as close as possible
        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
        
        // the number of samples generated by uhd can vary
        n_new_samples = rand() % (N_MAX_SAMPLES - N_MIN_SAMPLES) + N_MIN_SAMPLES;

        if (N_BYTES_PER_ITEM == 4){

            std::vector<int16_t*> buff_s16;

            // set samples consecutively (2 -> complex samples)
            for(int i=0; i<N_CHANNELS; i++){
                
                buff_s16.push_back(static_cast<int16_t*>(buffs[i]));
                
                for(int j=0; j<n_new_samples*2; j=j+2){
                    buff_s16[i][j] = (int16_t) item_cnt;       // real
                    buff_s16[i][j+1] = (int16_t) item_cnt;     // imag
                    
                    item_cnt++;
                    item_cnt = item_cnt % ITEM_CNT_MAX;
                }
            }
        }
        else if(N_BYTES_PER_ITEM == 8){
            
            std::vector<float*> buff_f32;

            // set samples consecutively (2 -> complex samples)
            for(int i=0; i<N_CHANNELS; i++){
                
                buff_f32.push_back(static_cast<float*>(buffs[i]));
                
                for(int j=0; j<n_new_samples*2; j=j+2){
                    buff_f32[i][j] = (float) item_cnt;          // real
                    buff_f32[i][j+1] = (float) item_cnt;        // imag
                    
                    item_cnt++;
                    item_cnt = item_cnt % ITEM_CNT_MAX;
                }
            }
        }
        else{
            std::cerr << "Unknown data type." << std::endl;
        }
        num_rx_samps += n_new_samples * N_CHANNELS;

        // refresh pointers for next call of rx_stream->recv()
        buffs = channelsounder::get_ringbuffer_rx_pointers(n_new_samples);
        
        // try to follow RX_RATE, but probably mush slower
        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        double time_it_took_us = 1e6 * (double) std::chrono::duration_cast<std::chrono::seconds>(t2 - t1).count();
        double time_it_should_have_taken_us = 1e6 * (double) n_new_samples/ (double) RX_RATE;
        double time_difference_us = time_it_should_have_taken_us - time_it_took_us;
        if(time_difference_us > 0)
            boost::this_thread::sleep_for(boost::chrono::microseconds((unsigned int) time_difference_us));
    }
}

/***********************************************************************
 * Benchmark TX Rate
 **********************************************************************/
void benchmark_tx_rate(std::atomic<bool>& burst_timer_elapsed)
{
    while (burst_timer_elapsed == false)
        boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
}

void benchmark_tx_rate_async_helper(std::atomic<bool>& burst_timer_elapsed)
{
    while (burst_timer_elapsed == false)
        boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
}

/***********************************************************************
 * Main code + dispatcher
 **********************************************************************/
int UHD_SAFE_MAIN(int argc, char* argv[])
{
    double duration = DURATION_SEC;
    std::atomic<bool> burst_timer_elapsed(false);

    boost::thread_group thread_group;   

    // spawn the receive test thread
    if (1==1) {
       
        // initialize save and send fifo
        channelsounder::init_fifo_ch_measurement(N_CHANNELS, N_BYTES_PER_ITEM, RX_RATE);
        auto save_thread = thread_group.create_thread([=, &burst_timer_elapsed]() {channelsounder::send_save_ch_measurements(burst_timer_elapsed);});
        boost::this_thread::sleep_for(boost::chrono::milliseconds(100));

        // initialize ring buffer
        channelsounder::init_ringbuffer_rx(N_CHANNELS, N_BYTES_PER_ITEM, N_MAX_SAMPLES);
        auto process_thread = thread_group.create_thread([=, &burst_timer_elapsed]() {channelsounder::process_ringbuffer_rx(burst_timer_elapsed);});
        boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
        
        auto rx_thread = thread_group.create_thread([=, &burst_timer_elapsed]() {
            benchmark_RX_RATE(burst_timer_elapsed);
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // spawn the transmit test thread
    if (1==1) {
        auto tx_thread = thread_group.create_thread([=, &burst_timer_elapsed]() {
            benchmark_tx_rate(burst_timer_elapsed);
        });
        
        auto tx_async_thread = thread_group.create_thread([=, &burst_timer_elapsed]() {
            benchmark_tx_rate_async_helper(burst_timer_elapsed);
        });
    }

    // sleep for the required duration (add any initial delay)
    const int64_t secs  = int64_t(duration);
    const int64_t usecs = int64_t((duration - secs) * 1e6);
    std::this_thread::sleep_for(
        std::chrono::seconds(secs) + std::chrono::microseconds(usecs));    

    // interrupt and join the threads
    burst_timer_elapsed = true;
    thread_group.join_all();
    
    channelsounder::show_debug_information_ringbuffer_rx();
    channelsounder::show_debug_information_fifo();
    
    std::cout << "Test samples generated and written to file. Switch to MATLAB to finish testing." << std::endl;
    
    return EXIT_SUCCESS;
}

