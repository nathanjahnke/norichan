bool PutHintingData(unsigned char *video, unsigned int hint);
bool GetHintingData(const unsigned char *video, unsigned int *hint);

#define HINT_INVALID 0x80000000
#define HINT_PROGRESSIVE  0x00000001
#define HINT_IN_PATTERN   0x00000002
