// amy-example.c
// a simple C example that plays audio using AMY out your speaker 

#if !defined(ESP_PLATFORM) && !defined(PICO_ON_DEVICE) &&!defined(ARDUINO)
#include "amy.h"
#include "examples.h"
#include "miniaudio.h"
#include "libminiaudio-audio.h"

// If you want PCM support smaller than 67 samples you have to include one of pcm_{small,tiny}.h
#include "pcm_small.h"


int main(int argc, char ** argv) {
    char *output_filename = NULL;
    //fprintf(stderr, "main init. pcm is %p pcm_map is %p\n",  pcm, pcm_map);

    int opt;
    while((opt = getopt(argc, argv, ":d:o:lh")) != -1) 
    { 
        switch(opt) 
        { 
            case 'd': 
                amy_device_id = atoi(optarg);
                break;
            case 'l':
                amy_print_devices();
                return 0;
                break;
            case 'o':
                output_filename = strdup(optarg);
                break;
            case 'h':
                printf("usage: amy-example\n");
                printf("\t[-d sound device id, use -l to list, default, autodetect]\n");
                printf("\t[-l list all sound devices and exit]\n");
                printf("\t[-o filename.wav - write to filename.wav instead of playing live]\n");
                printf("\t[-h show this help and exit]\n");
                return 0;
                break;
            case ':': 
                printf("option needs a value\n"); 
                break; 
            case '?': 
                printf("unknown option: %c\n", optopt);
                break; 
        } 
    }
    uint32_t start = amy_sysclock();

    float test_vals[] = {1.0, 1.5, 1.99, 2.0, 2.01, 2.5, 0.01};
    for (unsigned int i = 0; i < sizeof(test_vals) / sizeof(float); ++i) {
        float f = test_vals[i];
        SAMPLE logf = log2_lut(F2S(f));
        printf("log2(%f)=%f, exp2(%f)=%f\n", f, S2F(logf), S2F(logf), S2F(exp2_lut(logf)));
    }



    amy_start(1, 1, 1);


    ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, AMY_NCHANS, AMY_SAMPLE_RATE);
    ma_encoder encoder;
    ma_result result;
    if (output_filename) {
        result = ma_encoder_init_file(output_filename, &config, &encoder);
        if (result != MA_SUCCESS) {
            fprintf(stderr, "Error : could not open file : %s (%d)\n", output_filename, result) ;
            exit (1) ;
        }
    } else {
        amy_live_start();
    }

    amy_reset_oscs();

    //example_reverb();
    //example_chorus();
    //example_sine(start);
    example_drums(start, 4);
    example_multimbral_fm(start + 0, /* start_osc= */ 6);

    // Now just spin for 10s
    while(amy_sysclock() - start < 40000) {
        if (output_filename) {
            int16_t * frames = amy_simple_fill_buffer();
            int num_frames = AMY_BLOCK_SIZE;
            result = ma_encoder_write_pcm_frames(&encoder, frames, num_frames, NULL);
        }
        usleep(THREAD_USLEEP);
    }
    show_debug(3);

    if (output_filename) {
        ma_encoder_uninit(&encoder);
    } else {
        amy_live_stop();
    }

    return 0;
}

#endif
