#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fftw3.h>
#include <getopt.h>

#include "web.h"
#include "timing.h"
#include "fft.h"
#include "lime.h"
#include "buffer/buffer_circular.h"

static bool app_exit = false;

void sigint_handler(int sig)
{
  (void)sig;
  app_exit = true;
}

void _print_usage(void)
{
    printf(
        "\n"
        "Usage: txrx [options]\n"
        "\n"
        "  -d, --downconversion <number>  Set the RX LO  Default: 9750000\n"
        "\n"
    );
}

static pthread_t web_thread_obj;
static pthread_t fft_thread_obj;
static pthread_t lime_thread_obj;

int main(int argc, char* argv[])
{
  (void) argc;
  (void) argv;

  signal(SIGINT, sigint_handler);
  signal(SIGTERM, sigint_handler);

  static const struct option long_options[] = {
    { "downconversion",    required_argument, 0, 'd' },
    { 0,                   0,                 0,  0  }
  };

  int c, opt;
  while((c = getopt_long(argc, argv, "d:", long_options, &opt)) != -1)
  {
    switch(c)
    {        
    case 'd': /* --downconversion <number> */
      //frequency_downconversion = atof(optarg);
      break;

    case '?':
      _print_usage();
      return(0);
    }
  }

  printf("Profiling FFTs..\n");
  fftwf_import_wisdom_from_filename(".fftwf_wisdom");
  printf(" - Main Band FFT\n");
  main_fft_init();
  fftwf_export_wisdom_to_filename(".fftwf_wisdom");
  printf("FFTs Done.\n");

  /* Setting up buffers */
  buffer_circular_init(&buffer_circular_iq_main, sizeof(buffer_iqsample_t), 4096*1024);

  /* IF Subsample Thread */
  if(pthread_create(&web_thread_obj, NULL, web_thread, &app_exit))
  {
      fprintf(stderr, "Error creating %s pthread\n", "Web");
      return 1;
  }
  pthread_setname_np(web_thread_obj, "Web");

  /* FFT Thread */
  if(pthread_create(&fft_thread_obj, NULL, fft_thread, &app_exit))
  {
      fprintf(stderr, "Error creating %s pthread\n", "FFT");
      return 1;
  }
  pthread_setname_np(fft_thread_obj, "FFT");

  /* LimeSDR Thread */
  if(pthread_create(&lime_thread_obj, NULL, lime_thread, &app_exit))
  {
      fprintf(stderr, "Error creating %s pthread\n", "Lime");
      return 1;
  }
  pthread_setname_np(lime_thread_obj, "Lime");

  while(!app_exit)
  {
    sleep_ms(10);
  }

  printf("Got SIGTERM/INT..\n");
  app_exit = true;
  /* Interrupt lws service worker */
  pthread_kill(web_thread_obj, SIGINT);

  /* TODO: Add validity flag and appropriate bomb-out logic for buffer-consumer threads so we don't segfault on exit half the time */


  printf("Waiting for FFTs Thread to exit..\n");
  pthread_join(fft_thread_obj, NULL);
  printf("Waiting for Lime Thread to exit..\n");
  pthread_join(lime_thread_obj, NULL);
  printf("Waiting for Web Thread to exit..\n");
  pthread_join(web_thread_obj, NULL);

  printf("All threads caught, exiting..\n");
}
