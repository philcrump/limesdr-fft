#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>

#include <pthread.h>
#include <math.h>

// sudo apt install libfftw3-dev
#include <fftw3.h>

#include "timing.h"
#include "lime.h"
#include "web.h"

/* Input from lime.c */
extern lime_fft_buffer_t lime_fft_buffer;

#define FFT_SIZE    1024 //2048
#define FFT_MAXAVERAGE  5000

static float hanning_window_const[FFT_SIZE] __attribute__ ((aligned(8)));
static float hamming_window_const[FFT_SIZE] __attribute__ ((aligned(8)));

static fftwf_complex* fft_in;
static fftwf_complex* fft_out;
static fftwf_plan fft_plan;

static uint32_t config_fft_average = 500;

static float fft_current_buffer[FFT_SIZE] __attribute__ ((aligned(8)));
static uint32_t fft_averaging_index = 0;
static float fft_averaging_buffer[FFT_MAXAVERAGE][FFT_SIZE] __attribute__ ((aligned(8)));
static float fft_average_buffer[FFT_SIZE] __attribute__ ((aligned(8)));



static float fft_scaled_data[FFT_SIZE] __attribute__ ((aligned(8)));
static uint8_t fft_data_output[FFT_SIZE] __attribute__ ((aligned(8)));



void main_fft_init(void)
{
    /* Set up windowing functions */
    for(int i=0; i<FFT_SIZE; i++)
    {
        /* Hanning */
        hanning_window_const[i] = 0.5 * (1.0 - cos(2*M_PI*(((float)i)/FFT_SIZE)));

        /* Hamming */
        hamming_window_const[i] = 0.54 - (0.46 * cos(2*M_PI*(0.5+((2.0*((float)i/(FFT_SIZE-1))+1.0)/2))));
    }

    /* Set up FFTW */
    fft_in = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
    fft_out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
    fft_plan = fftwf_plan_dft_1d(FFT_SIZE, fft_in, fft_out, FFTW_FORWARD, FFTW_PATIENT);
    printf(" "); fftwf_print_plan(fft_plan); printf("\n");
}

static void fft_fftw_close(void)
{
    /* De-init fftw */
    fftwf_free(fft_in);
    fftwf_free(fft_out);
    fftwf_destroy_plan(fft_plan);
    fftwf_forget_wisdom();
}

/* FFT Thread */
void *fft_thread(void *arg)
{
    bool *exit_requested = (bool *)arg;

    uint32_t i, offset;
    fftw_complex pt;
    double pwr_scale = 1.0 / ((float)FFT_SIZE * (float)FFT_SIZE);
    float *fft_average_output_ptr;

    struct timespec ts;

    //uint64_t last_output = monotonic_ms();

    /* Set pthread timer on .signal to use monotonic clock */
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init (&lime_fft_buffer.signal, &attr);
    pthread_condattr_destroy(&attr);

    while(false == *exit_requested)
    {
        /* Lock input buffer */
        pthread_mutex_lock(&lime_fft_buffer.mutex);

        while(lime_fft_buffer.index >= (lime_fft_buffer.size/(FFT_SIZE * sizeof(float) * 2))
            && false == *exit_requested)
        {
            /* Set timer for 100ms */
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_nsec += 10 * 1000000;

            pthread_cond_timedwait(&lime_fft_buffer.signal, &lime_fft_buffer.mutex, &ts);
        }

        if(*exit_requested)
        {
            break;
        }

        offset = (lime_fft_buffer.index * FFT_SIZE * 2) / 2;

        /* Copy data out of rf buffer into fft_input buffer */
        for (i = 0; i < FFT_SIZE; i++)
        {
            fft_in[i][0] = (((float*)lime_fft_buffer.data)[offset+(2*i)]+0.00048828125) * hanning_window_const[i];
            fft_in[i][1] = (((float*)lime_fft_buffer.data)[offset+(2*i)+1]+0.00048828125) * hanning_window_const[i];
        }

        lime_fft_buffer.index++;

        /* Unlock input buffer */
        pthread_mutex_unlock(&lime_fft_buffer.mutex);

        /* Run FFT */
        fftwf_execute(fft_plan);

        float int_max = -9999.0;
        float int_min = 9999.0;

        for (i = 0; i < FFT_SIZE; i++)
        {
            /* shift, normalize and convert to dBFS */
            if (i < FFT_SIZE / 2)
            {
                pt[0] = fft_out[FFT_SIZE / 2 + i][0] / FFT_SIZE;
                pt[1] = fft_out[FFT_SIZE / 2 + i][1] / FFT_SIZE;
            }
            else
            {
                pt[0] = fft_out[i - FFT_SIZE / 2][0] / FFT_SIZE;
                pt[1] = fft_out[i - FFT_SIZE / 2][1] / FFT_SIZE;
            }
            
            fft_current_buffer[i] = 10.f * log10((pwr_scale * (pt[0] * pt[0]) + (pt[1] * pt[1])) + 1.0e-20);
        }

        /* Perform averaging if configured */
        if(config_fft_average > 0)
        {
            for (i = 0; i < FFT_SIZE; i++)
            {
                fft_average_buffer[i] -= fft_averaging_buffer[fft_averaging_index % config_fft_average][i] / config_fft_average;
                fft_average_buffer[i] += fft_current_buffer[i] / config_fft_average;
            }
            memcpy(fft_averaging_buffer[fft_averaging_index % config_fft_average], fft_current_buffer, sizeof(float) * FFT_SIZE);
            fft_averaging_index++;

            /* Set output pointer */
            fft_average_output_ptr = fft_average_buffer;
        }
        else
        {
            /* Set output pointer */
            fft_average_output_ptr = fft_current_buffer;
        }
        
        /* Scale output to 8-bit */
        for (i = 0; i < FFT_SIZE; i++)
        {
            fft_scaled_data[i] = 8.0 * (fft_average_output_ptr[i] + 60);

            if(fft_scaled_data[i] > int_max) int_max = fft_scaled_data[i];
            if(fft_scaled_data[i] < int_min) int_min = fft_scaled_data[i];

            if(fft_scaled_data[i] < 0) fft_scaled_data[i] = 0;
            if(fft_scaled_data[i] > 255) fft_scaled_data[i] = 255;

            fft_data_output[i] = (uint8_t)(fft_scaled_data[i]);
        }
        printf("Max: %f, Min %f\n", int_max, int_min);

        //printf("%x%x%x%x", fft_data_output[0], fft_data_output[510], fft_data_output[720], fft_data_output[613]);
        web_submit_fft_main(fft_data_output, FFT_SIZE);
    }

    fft_fftw_close();

    return NULL;
}
