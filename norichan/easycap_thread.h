#ifndef EASYCAP_THREAD_H
#define EASYCAP_THREAD_H

#include <QThread>
#include <QtGui>

#include "easycap_render_thread.h"
#include "gtimer.h"

#ifdef Q_OS_WIN32
#include <windows.h>
#include <usb.h>
#endif
#ifdef Q_OS_MAC
extern "C" {
#include <IOKit/IOCFBundle.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <mach/mach_time.h>
}
//typedef UnsignedWide Nanoseconds;
#endif

//common macros
#ifndef MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#endif
#ifndef CLAMP
#define CLAMP(min, x, max) MIN(MAX((min), (x)), (max))
#endif

//http://stackoverflow.com/questions/4275921/getting-the-number-of-elements-in-a-struct
#ifndef numberof
#define numberof(keys) (sizeof(keys)/sizeof(*keys))
#endif

#define kOurVendorID  0x05e1
#define kOurProductID 0x0408
#define USB_TIMEOUT 5000
#define ECVNanosecondsPerMillisecond 1e6


class easycap_thread : public QThread
{
	Q_OBJECT

public:
	easycap_thread(QObject *parent = 0);
	easycap_render_thread my_easycap_render_thread;
	void render();
	bool abort, restart;
	int initial_width, initial_height, targetwidth, targetheight;
        unsigned char *incoming_image_buffer, *lol_dest_2, *frame_buffer_for_encoding, *frame_buffer_for_display, *frame_buffer_for_display_1, *frame_buffer_for_display_2;
	int brightness, contrast, saturation, hue;
	bool ntsc, pal60, svideo;
	bool debug_console;
	QByteArray *audio_buffer;
	int colorspace;
	double colorspace_multiplier;
        int channel;
        bool change_channels;

public slots:
	void frame_ready(bool);
	void set_text(QString);
        void set_input(int);
	void set_standard();

protected:
	void run();

signals:
	void capture_device_not_found();
	void capture_device_not_usb_2_0();
	void easycap_audio_not_found();
#ifdef Q_OS_MAC
	void service_found(io_service_t);
	void found_device(IOUSBDeviceInterface320 **);
#endif
	void easycap_streaming();
	void frame_ready_signal(bool);
	void debug_console_set_text(QString);



private:
	unsigned char *lol_dest, *transfer_buffer;
	unsigned int lol_dest_length;
	bool find_easycap();
	void start_streaming();
	bool initialize_easycap();
        GTimer timer;
	int lol_dest_pos;
	bool initialized_buffers;

#ifdef Q_OS_WIN32
	usb_dev_handle *handle;
#endif
#ifdef Q_OS_MAC
	IOUSBInterfaceInterface300     **interface;
	static char                    gBuffer[64];
	int FindInterfaces();
	static void ECVDoNothing(void *, IOReturn, void *) {}

	typedef struct {
		IOUSBLowLatencyIsocFrame *list;
		UInt8 *data;
	} ECVTransfer;
	ECVTransfer *transfers;
	UInt32 frameTime;
#endif

	//typedef uint8_t u_int8_t;
	typedef uint16_t u_int16_t;
	typedef uint64_t UInt64;
	//typedef uint32_t UInt32;
	typedef uint16_t UInt16;
	typedef uint8_t UInt8;

	UInt8 pipeIndex;
	UInt16 frameRequestSize;
	UInt8 millisecondInterval;
	int simultaneousTransfers;
	unsigned int microframesPerTransfer;
	UInt64 currentFrame;

	uint8_t readIndex(u_int16_t);
	bool writeIndex(u_int16_t, u_int16_t);
	bool setFeature(u_int16_t);
	void usb_stk11xx_write_registry(u_int16_t, u_int16_t);
	void dev_stk0408_write0(u_int16_t, u_int16_t);
	void writeSAA711XRegister(uint8_t, int16_t);
	void writeVT1612ARegister(uint8_t, uint16_t);
	uint16_t VT1612ABothChannels(uint8_t);
	uint8_t VT1612AInputGain(double);
	uint8_t VT1612ARecordGain(double);

	enum {
		ECVSTK1160HighFieldFlag = 1 << 6,
		ECVSTK1160NewImageFlag = 1 << 7,
	};
	enum {
		SAA711XFUSE0Antialias = 1 << 6,
		SAA711XFUSE1Amplifier = 1 << 7,
	};
	enum {
		SAA711XGAI18StaticGainControl1 = 1 << 0,
		SAA711XGAI28StaticGainControl2 = 1 << 1,
		SAA711XGAFIXGainControlUserProgrammable = 1 << 2,
		SAA711XHOLDGAutomaticGainControlEnabled = 0 << 3,
		SAA711XHOLDGAutomaticGainControlDisabled = 1 << 3,
		SAA711XCPOFFColorPeakControlDisabled = 1 << 4,
		SAA711XVBSLLongVerticalBlanking = 1 << 5,
		SAA711XHLNRSReferenceSelect = 1 << 6,
	};
	enum {
		SAA711XYCOMBAdaptiveLuminanceComb = 1 << 6,
		SAA711XBYPSChrominanceTrapCombBypass = 1 << 7,
	};
	enum {
		SAA711XVNOIVerticalNoiseReductionNormal = 0 << 0,
		SAA711XVNOIVerticalNoiseReductionFast = 1 << 0,
		SAA711XVNOIVerticalNoiseReductionFree = 2 << 0,
		SAA711XVNOIVerticalNoiseReductionBypass = 3 << 0,
		SAA711XHTCHorizontalTimeConstantTVMode = 0 << 3,
		SAA711XHTCHorizontalTimeConstantVTRMode = 1 << 3,
		SAA711XHTCHorizontalTimeConstantAutomatic = 2 << 3,
		SAA711XHTCHorizontalTimeConstantFastLocking = 3 << 3,
		SAA711XFOETForcedOddEventToggle = 1 << 5,
		SAA711XFSELManualFieldSelection50Hz = 0 << 6,
		SAA711XFSELManualFieldSelection60Hz = 1 << 6,
		SAA711XAUFDAutomaticFieldDetection = 1 << 7,
	};
	enum {
		SAA711XCGAINChromaGainValueMinimum = 0x00,
		SAA711XCGAINChromaGainValueNominal = 0x2a,
		SAA711XCGAINChromaGainValueMaximum = 0x7f,
		SAA711XACGCAutomaticChromaGainControlEnabled = 0 << 7,
		SAA711XACGCAutomaticChromaGainControlDisabled = 1 << 7,
	};
	enum {
		SAA711XRTP0OutputPolarityInverted = 1 << 3,
	};
	enum {
		SAA711XSLM1ScalerDisabled = 1 << 1,
		SAA711XSLM3AudioClockGenerationDisabled = 1 << 3,
		SAA711XCH1ENAD1X = 1 << 6,
		SAA711XCH2ENAD2X = 1 << 7,
	};
	enum {
		SAA711XCCOMBAdaptiveChrominanceComb = 1 << 0,
		SAA711XFCTCFastColorTimeConstant = 1 << 2,
	};

	enum {
		STK0408StatusRegistryIndex = 0x100,
	};
	enum {
		STK0408StatusStreaming = 1 << 7,
	};

	enum {
		SAA711XMODECompositeAI11 = 0,
		SAA711XMODECompositeAI12 = 1,
		SAA711XMODECompositeAI21 = 2,
		SAA711XMODECompositeAI22 = 3,
		SAA711XMODECompositeAI23 = 4,
		SAA711XMODECompositeAI24 = 5,
		SAA711XMODESVideoAI11_GAI2 = 6,
		SAA711XMODESVideoAI12_GAI2 = 7,
		SAA711XMODESVideoAI11_YGain = 8,
		SAA711XMODESVideoAI12_YGain = 9,
	};

	//audio
	enum {
		VT1612ARegisterVolumeStereoOut = 0x02,

		VT1612ARegisterVolumeMic = 0x0e,
		VT1612ARegisterVolumeLineIn = 0x10,
		VT1612ARegisterVolumeCD = 0x12,
		VT1612ARegisterVolumeVideoIn = 0x14,
		VT1612ARegisterVolumeAuxIn = 0x16,

		VT1612ARegisterRecordSelect = 0x1a,
		VT1612ARegisterRecordGain = 0x1c,

		VT1612ARegisterVendorID1 = 0x7c,
		VT1612ARegisterVendorID2 = 0x7e,
	};
	enum {
		VT1612AMute = 1 << 15,
	};
	enum {
		VT1612ARecordSourceMic = 0,
		VT1612ARecordSourceCD = 1,
		VT1612ARecordSourceVideoIn = 2,
		VT1612ARecordSourceAuxIn = 3,
		VT1612ARecordSourceLineIn = 4,
		VT1612ARecordSourceStereoMix = 5,
		VT1612ARecordSourceMonoMix = 6,
		VT1612ARecordSourcePhone = 7,
	};


};

#endif // EASYCAP_THREAD_H
