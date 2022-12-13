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

size_t audioBufferSizeInBytes = 0;
char* audioBuffer = NULL;

struct state;
typedef void state_fn(struct state *);

struct state
{
    state_fn * next;
    ma_device * inputDevice;
    ma_device * outputDevice;
};

state_fn enterIdle, enterRecording, recording, leaveRecording, enterLoop, looping, leaveLoop;


void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
  size_t inputBufferSizeInBytes = ma_get_bytes_per_frame(pDevice->capture.format, pDevice->capture.channels) * frameCount;
  size_t newAudioBufferSizeInBytes = audioBufferSizeInBytes + inputBufferSizeInBytes;
  char* newAudioBuffer = (char*)realloc(audioBuffer, newAudioBufferSizeInBytes);
  if (newAudioBuffer == NULL) {
      // Getting here means you're out of memory.
      return;
  }

  // Getting here means buffer expansion was successful.
  memcpy(newAudioBuffer + audioBufferSizeInBytes, pInput, inputBufferSizeInBytes);
  audioBufferSizeInBytes = newAudioBufferSizeInBytes;
  audioBuffer = newAudioBuffer;

  (void)pOutput;
}

void data_callbackOutput(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{

    ma_audio_buffer* pBuffer = (ma_audio_buffer*)pDevice->pUserData;
    if (pBuffer == NULL) {
        return;
    }

    ma_data_source_read_pcm_frames(pBuffer, pOutput, frameCount, NULL);

    (void)pInput;
}


void enterIdle(struct state * state){
  if(_kbhit()) {
    state->next = enterRecording;
  }
}

void enterRecording(struct state * state) {
  printf("Entering Recording State\n");
  ma_result result;

  if(ma_device_get_state(state->inputDevice) != ma_device_state_stopped ) {
    ma_device_config inputDeviceConfig;

    // Input Device config
    inputDeviceConfig = ma_device_config_init(ma_device_type_capture);
    inputDeviceConfig.capture.format   = ma_format_f32;
    inputDeviceConfig.capture.channels = 2;
    // ** Uncomment the Following lines to specify an ALSA sound input device other than the default
    // ma_device_id inputDeviceId;
    // strcpy(inputDeviceId.alsa, "hw");
    // inputDeviceConfig.capture.pDeviceID = &inputDeviceId;
    inputDeviceConfig.sampleRate       = 44100;
    inputDeviceConfig.dataCallback     = data_callback;

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
  if(_kbhit()) {
    state->next = leaveRecording;
  }
}

void leaveRecording(struct state * state) {
  ma_device_stop(state->inputDevice);
  printf("Entering Loop State\n");
  state->next = enterLoop;
}

void enterLoop(struct state * state) {
  ma_device_config outputDeviceConfig;

  if(ma_device_get_state(state->outputDevice) != ma_device_state_stopped ) {

    // Output Device config
    outputDeviceConfig = ma_device_config_init(ma_device_type_playback);
    outputDeviceConfig.playback.format   = ma_format_f32;
    outputDeviceConfig.playback.channels = 2;
    outputDeviceConfig.sampleRate        = 44100;
    outputDeviceConfig.dataCallback      = data_callbackOutput;
    //outputDeviceConfig.pUserData         = audioBuffer;

    if (ma_device_init(NULL, &outputDeviceConfig, state->outputDevice) != MA_SUCCESS) {
      printf("Failed to open playback device.\n");
      exit(-6);
    }
  }

  if (ma_device_start(state->outputDevice) != MA_SUCCESS) {
      printf("Failed to start playback device.\n");
      ma_device_uninit(state->outputDevice);
      exit(-7);
  }

  ma_audio_buffer_config config = ma_audio_buffer_config_init(
      ma_format_f32,
      2,
      audioBufferSizeInBytes / ma_get_bytes_per_frame(ma_format_f32, 2),
      audioBuffer,
      NULL);


  ma_audio_buffer buffer;
  if (ma_audio_buffer_init(&config, &buffer) != MA_SUCCESS) {
    // Error.
    printf("Failed to initialize audio buffer.\n");
    ma_device_uninit(state->outputDevice);
    exit(-7);
  }
  state->outputDevice->pUserData = &buffer;

  ma_data_source_set_next(&buffer, &buffer);

  state->next = looping;
}

void looping(struct state * state) {
  if(_kbhit()) {
    state->next = leaveLoop;
  }
}

void leaveLoop(struct state * state) {
  ma_device_stop(state->outputDevice);
  free(audioBuffer);
  audioBuffer = NULL;
  audioBufferSizeInBytes = 0;
  printf("Entering Idle State\n");
  state->next = enterIdle;
}

int main(int argc, char** argv)
{
  ma_result result;
  ma_device inputDevice;
  ma_device outputDevice;

  struct state state = { enterIdle, &inputDevice, &outputDevice };
  printf("Entering Idle State\n");
  while(state.next) state.next(&state);

  ma_device_uninit(&outputDevice);
  ma_device_uninit(&inputDevice);

  return 0;
}