# norichan

based on easycapviewer by ben trask.

requirements:
* qt 5
* ffmpeg
* lame
* opengl
* libusb (windows only)

i made sure that the code compiles with qt 5.7 and runs under os x 10.11.6, but i got a kernel panic when i quit the app. also the audio was garbled which made me think the buffer size needs to be adjusted. finally, the ffmpeg code will need to be modernized.
