#define MINIAUDIO_IMPLEMENTATION

#include "bcm2835.h"
#include "miniaudio.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdbool.h>

// Arrange button between pin 37 and ground (PULL UP)
#define PIN RPI_V2_GPIO_P1_37

// SPACE BAR IS OUR BUTTON
#define BUFFERSIZE 2

// this is a simple input debounce: https://www.e-tinkers.com/2021/05/the-simplest-button-debounce-solution/
bool buttonPressed()  {
  static uint16_t state = 0;
  state = (state<<1) | bcm2835_gpio_lev(PIN) | 0xfe00;
  return (state == 0xff00);
}

struct state;
typedef void state_fn(struct state *);

struct state
{
    state_fn * next;
    ma_decoder * outputDecoder;
    ma_encoder * inputEncoder;
    ma_device * inputDevice;
    ma_device * outputDevice;
};

state_fn enterIdle, enterRecording, recording, leaveRecording, enterLoop, looping, leaveLoop;


void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
  ma_encoder* pEncoder = (ma_encoder*)pDevice->pUserData;
  MA_ASSERT(pEncoder != NULL);
  ma_encoder_write_pcm_frames(pEncoder, pInput, frameCount, NULL);
  (void)pOutput;
}

void data_callbackOutput(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    ma_decoder* pDecoder = (ma_decoder*)pDevice->pUserData;
    if (pDecoder == NULL) {
        return;
    }

    /* Reading PCM frames will loop based on what we specified when called ma_data_source_set_looping(). */
    ma_data_source_read_pcm_frames(pDecoder, pOutput, frameCount, NULL);

    (void)pInput;
}


void enterIdle(struct state * state){
  if(buttonPressed()) {
    state->next = enterRecording;
  }
}

void enterRecording(struct state * state) {
  printf("Entering Recording State\n");
  ma_result result;
  ma_encoder_config inputEncoderConfig;
  inputEncoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, 44100);

  if (ma_encoder_init_file("file.wav", &inputEncoderConfig, state->inputEncoder) != MA_SUCCESS) {
    printf("Failed to initialize output file.\n");
    exit(-1);
  }
  // if the device isn't stopped - the device hasn't been initialized yet
  if(ma_device_get_state(state->inputDevice) != ma_device_state_stopped) {
    ma_device_config inputDeviceConfig;

    // Input Device config
    inputDeviceConfig = ma_device_config_init(ma_device_type_capture);
    inputDeviceConfig.capture.format   = state->inputEncoder->config.format;
    inputDeviceConfig.capture.channels = state->inputEncoder->config.channels;
    // ** Uncomment the Following lines to specify an ALSA sound input device other than the default
    ma_device_id inputDeviceId;
    strcpy(inputDeviceId.alsa, "hw");
    inputDeviceConfig.capture.pDeviceID = &inputDeviceId;
    inputDeviceConfig.sampleRate       = state->inputEncoder->config.sampleRate;
    inputDeviceConfig.dataCallback     = data_callback;
    inputDeviceConfig.pUserData        = state->inputEncoder;

    result = ma_device_init(NULL, &inputDeviceConfig, state->inputDevice);
    if (result != MA_SUCCESS) {
      printf("Failed to initialize capture device.\n");
      exit(-2);
    }

  }

  result = ma_device_start(state->inputDevice);
  if (result != MA_SUCCESS) {
    ma_device_uninit(state->inputDevice);
    printf("Failed to start device.\n");
    exit(-3);
  }
  state->next = recording;
}

void recording(struct state * state) {
  if(buttonPressed()) {
    state->next = leaveRecording;
  }
}

void leaveRecording(struct state * state) {
  ma_device_stop(state->inputDevice);
  ma_encoder_uninit(state->inputEncoder);
  printf("Entering Loop State\n");
  state->next = enterLoop;
}

void enterLoop(struct state * state) {
  ma_device_config outputDeviceConfig;

  if (ma_decoder_init_file("file.wav", NULL, state->outputDecoder) != MA_SUCCESS) {
    printf("Could not load file.wav\n");
    exit(-5);
  }

  ma_data_source_set_next(state->outputDecoder, state->outputDecoder);

  if(ma_device_get_state(state->outputDevice) != ma_device_state_stopped) {
    // Output Device config
    outputDeviceConfig = ma_device_config_init(ma_device_type_playback);
    outputDeviceConfig.playback.format   = state->outputDecoder->outputFormat;
    outputDeviceConfig.playback.channels = state->outputDecoder->outputChannels;
    outputDeviceConfig.sampleRate        = state->outputDecoder->outputSampleRate;
    outputDeviceConfig.dataCallback      = data_callbackOutput;
    outputDeviceConfig.pUserData         = state->outputDecoder;

    if (ma_device_init(NULL, &outputDeviceConfig, state->outputDevice) != MA_SUCCESS) {
      printf("Failed to open playback device.\n");
      ma_decoder_uninit(state->outputDecoder);
      exit(-6);
    }
  }

  if (ma_device_start(state->outputDevice) != MA_SUCCESS) {
      printf("Failed to start playback device.\n");
      ma_device_uninit(state->outputDevice);
      ma_decoder_uninit(state->outputDecoder);
      exit(-7);
  }

  state->next = looping;
}

void looping(struct state * state) {
  if(buttonPressed()) {
    state->next = leaveLoop;
  }
}

void leaveLoop(struct state * state) {
  ma_device_stop(state->outputDevice);
  ma_decoder_uninit(state->outputDecoder);
  printf("Entering Idle State\n");
  state->next = enterIdle;
}

int main(int argc, char** argv)
{
  ma_result result;
  ma_encoder inputEncoder;
  ma_decoder outputDecoder;
  ma_device inputDevice;
  ma_device outputDevice;

  // Init PI Library
  if (!bcm2835_init()) return 1;

  // Set the pin to be an output
  bcm2835_gpio_fsel(PIN, BCM2835_GPIO_FSEL_INPT);
  bcm2835_gpio_set_pud(PIN, BCM2835_GPIO_PUD_UP);

  struct state state = { enterIdle, &outputDecoder, &inputEncoder, &inputDevice, &outputDevice };
  printf("Entering Idle State\n");
  while(state.next) state.next(&state);

  ma_device_uninit(&outputDevice);
  ma_device_uninit(&inputDevice);
  ma_encoder_uninit(&inputEncoder);
  ma_decoder_uninit(&outputDecoder);

  return 0;
}