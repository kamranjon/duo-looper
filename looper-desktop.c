#define MINIAUDIO_IMPLEMENTATION

#include "miniaudio.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <stdbool.h>

// SPACE BAR IS OUR BUTTON
#define BUFFERSIZE 2
// we are using _kbhit here to mimic a GPIO signal on PI
// it's just nice to develop most the functionality on your computer first
// -- https://www.flipcode.com/archives/_kbhit_for_Linux.shtml
int _kbhit() {
  char buffer[BUFFERSIZE];
  static const int STDIN = 0;
  static bool initialized = false;

  if (! initialized) {
    // Use termios to turn off line buffering
    struct termios term;
    tcgetattr(STDIN, &term);
    term.c_lflag &= ~ICANON;
    tcsetattr(STDIN, TCSANOW, &term);
    setbuf(stdin, NULL);
    initialized = true;
  }

  int bytesWaiting;
  ioctl(STDIN, FIONREAD, &bytesWaiting);
  if(bytesWaiting == true) {
    fgets(buffer, BUFFERSIZE , stdin);
  }
  return bytesWaiting;
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
    ma_device * device;
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
  if(_kbhit()) {
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
  if(_kbhit()) {
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
  if(_kbhit()) {
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
  ma_device device;
  ma_device outputDevice;

  ma_device_config deviceConfig;

  ma_encoder_config inputEncoderConfig;
  inputEncoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, 44100);


  if (ma_encoder_init_file("file.wav", &inputEncoderConfig, &inputEncoder) != MA_SUCCESS) {
    printf("Failed to initialize output file.\n");
    exit(-1);
  }

  // Input Device config
  deviceConfig = ma_device_config_init(ma_device_type_duplex);
  deviceConfig.capture.format   = inputEncoderConfig.format;
  deviceConfig.capture.channels = inputEncoderConfig.channels;
  deviceConfig.capture.pDeviceID = NULL;

  deviceConfig.playback.format   = inputEncoderConfig.format;
  deviceConfig.playback.channels = inputEncoderConfig.channels;

  // ** Uncomment the Following lines to specify an ALSA sound input device other than the default
  // ma_device_id deviceId;
  // strcpy(deviceId.alsa, "hw");
  deviceConfig.playback.pDeviceID = NULL;
  deviceConfig.sampleRate       = inputEncoderConfig.sampleRate;
  deviceConfig.dataCallback     = data_callback;
  isLooping        = FALSE;
  isRecording = FALSE;

  result = ma_device_init(NULL, &deviceConfig, &device);
  if (result != MA_SUCCESS) {
    printf("Failed to initialize capture device.\n");
    exit(-2);
  }

  result = ma_device_start(&device);
  if (result != MA_SUCCESS) {
    ma_device_uninit(&device);
    printf("Failed to start device.\n");
    exit(-3);
  }

  struct state state = { enterIdle, &device, &inputEncoderConfig };
  printf("Entering Idle State\n");
  while(state.next) state.next(&state);

  ma_device_uninit(&outputDevice);
  ma_device_uninit(&device);
  ma_encoder_uninit(&inputEncoder);
  ma_decoder_uninit(&outputDecoder);

  return 0;
}