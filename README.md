# duo-looper

A State-Machine Based Looper Written in C

## Compilation

on OSX:
`cc looper.c -o looper`

on Linux:
`cc looper.c -ldl -lpthread -lm -o looper`

on RaspberryPi:
`cc looper.c -ldl -lpthread -lm -latomic -o looper`

## Running

`./looper`

## Notes

This is a state machine - the default state is IDLE - on the desktop you use Enter or Spacebar to move through the states in the state machine. The general flow is IDLE -> RECORDING -> LOOPING -> IDLE.

If you are not getting sound capture - you may need to specify your input device, on Linux you can get a list of your input devices using:
`areplay -L`

You can find the commented out code to uncomment and modify in the `enterRecording` method.

## Miniaudio

We use a single header C-library called [miniaudio](https://miniaud.io/index.html) for doing our sound IO. This is an awesome library, and we are only scratching the surface - we can do real-time [duplexing](https://miniaud.io/docs/examples/duplex_effect.html) IN -> OUT (think sound effects) using the [Node Graph](https://miniaud.io/docs/manual/index.html#NodeGraph). We can even create Low/High-pass filters to clean-up the DI sound from our microphone inputs.
