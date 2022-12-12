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

ma_decoder outputDecoder;
ma_encoder inputEncoder;

bool isLooping;
bool isRecording;

struct state
{
    state_fn * next;
    ma_device * inputDevice;
    ma_encoder_config * inputEncoderConfig;
};

state_fn enterIdle, enterRecording, recording, leaveRecording, enterLoop, looping, leaveLoop;


void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
  if(isLooping) {
    ma_data_source_read_pcm_frames(&outputDecoder, pOutput, frameCount, NULL);
    (void)pInput;
  } else if(isRecording) {
    ma_encoder_write_pcm_frames(&inputEncoder, pInput, frameCount, NULL);
    (void)pOutput;
  }
}

void enterIdle(struct state * state){
  if(buttonPressed()) {
    state->next = enterRecording;
  }
}

void enterRecording(struct state * state) {
  printf("Entering Recording State\n");


  if (ma_encoder_init_file("file.wav", state->inputEncoderConfig, &inputEncoder) != MA_SUCCESS) {
    printf("Failed to initialize output file.\n");
    exit(-1);
  }
  isRecording = TRUE;

  state->next = recording;
}

void recording(struct state * state) {
  if(buttonPressed()) {
    state->next = leaveRecording;
  }
}

void leaveRecording(struct state * state) {
  ma_encoder_uninit(&inputEncoder);
  isRecording = FALSE;
  printf("Entering Loop State\n");
  state->next = enterLoop;
}

void enterLoop(struct state * state) {

  if (ma_decoder_init_file("file.wav", NULL, &outputDecoder) != MA_SUCCESS) {
    printf("Could not load file.wav\n");
    exit(-5);
  }
  isLooping = TRUE;
  ma_data_source_set_next(&outputDecoder, &outputDecoder);
  state->next = looping;
}

void looping(struct state * state) {
  if(buttonPressed()) {
    state->next = leaveLoop;
  }
}

void leaveLoop(struct state * state) {
  ma_decoder_uninit(&outputDecoder);
  isLooping = FALSE;
  printf("Entering Idle State\n");
  state->next = enterIdle;
}


int main(int argc, char** argv)
{
  ma_result result;
  ma_device inputDevice;
  ma_device outputDevice;


    ma_device_config inputDeviceConfig;

  ma_encoder_config inputEncoderConfig;
  inputEncoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, 44100);


  if (ma_encoder_init_file("file.wav", &inputEncoderConfig, &inputEncoder) != MA_SUCCESS) {
    printf("Failed to initialize output file.\n");
    exit(-1);
  }

  // ** Uncomment the Following lines to specify an ALSA sound input device other than the default
  ma_device_id inputDeviceId;
  strcpy(inputDeviceId.alsa, "hw");

  // Input Device config
  inputDeviceConfig = ma_device_config_init(ma_device_type_duplex);
  inputDeviceConfig.capture.format   = inputEncoderConfig.format;
  inputDeviceConfig.capture.channels = inputEncoderConfig.channels;

  inputDeviceConfig.capture.pDeviceID = &inputDeviceId;

  inputDeviceConfig.playback.format   = inputEncoderConfig.format;
  inputDeviceConfig.playback.channels = inputEncoderConfig.channels;


  inputDeviceConfig.playback.pDeviceID = &inputDeviceId;
  inputDeviceConfig.sampleRate       = inputEncoderConfig.sampleRate;
  inputDeviceConfig.dataCallback     = data_callback;
  isLooping        = FALSE;
  isRecording = FALSE;

  result = ma_device_init(NULL, &inputDeviceConfig, &inputDevice);
  if (result != MA_SUCCESS) {
    printf("Failed to initialize capture device.\n");
    exit(-2);
  }

  result = ma_device_start(&inputDevice);
  if (result != MA_SUCCESS) {
    ma_device_uninit(&inputDevice);
    printf("Failed to start device.\n");
    exit(-3);
  }

  // Init PI Library
  if (!bcm2835_init()) return 1;

  // Set the pin to be an output
  bcm2835_gpio_fsel(PIN, BCM2835_GPIO_FSEL_INPT);
  bcm2835_gpio_set_pud(PIN, BCM2835_GPIO_PUD_UP);

  struct state state = { enterIdle, &inputDevice, &inputEncoderConfig };
  printf("Entering Idle State\n");
  while(state.next) state.next(&state);

  ma_device_uninit(&outputDevice);
  ma_device_uninit(&inputDevice);
  ma_encoder_uninit(&inputEncoder);
  ma_decoder_uninit(&outputDecoder);

  return 0;
}