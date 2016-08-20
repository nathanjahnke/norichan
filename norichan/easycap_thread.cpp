#include "easycap_thread.h"

#ifdef Q_OS_MAC
static IONotificationPortRef    gNotifyPort;
static io_iterator_t            gRawAddedIter;
static IOUSBDeviceInterface320  **device;
io_service_t service;

void RawDeviceAdded(void *, io_iterator_t iterator) {
	kern_return_t               kr;
	io_service_t                usbDevice;
	IOCFPlugInInterface         **plugInInterface = NULL;
	HRESULT                     result;
	SInt32                      score;
	UInt16                      vendor;
	UInt16                      product;

	IOUSBDeviceInterface320     **new_device;

	while ((usbDevice = IOIteratorNext(iterator))) {
		//Create an intermediate plug-in
		kr = IOCreatePlugInInterfaceForService(usbDevice,
						       kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
						       &plugInInterface, &score);
		//Don't need the device object after intermediate plug-in is created
		kr = IOObjectRelease(usbDevice);
		if ((kIOReturnSuccess != kr) || !plugInInterface) {
			printf("Unable to create a plug-in (%08x)\n", kr);
			continue;
		}
		//Now create the device interface
		result = (*plugInInterface)->QueryInterface(plugInInterface,
							    CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320),
							    (LPVOID *)&new_device);
		//Don't need the intermediate plug-in after device interface
		//is created
		(*plugInInterface)->Release(plugInInterface);

		if (result || !new_device) {
			printf("Couldn't create a device interface (%08x)\n", (int) result);
			continue;
		}

		//Check these values for confirmation
		kr = (*new_device)->GetDeviceVendor(new_device, &vendor);
		kr = (*new_device)->GetDeviceProduct(new_device, &product);
		if ((vendor != kOurVendorID) || (product != kOurProductID)) {
			//printf("Found unwanted device (vendor = %04x, product = %04x)\n", vendor, product);
			(void) (*new_device)->Release(new_device);
			continue;
		}

		//Open the device to change its state
		kr = (*new_device)->USBDeviceOpen(new_device);
		if (kr != kIOReturnSuccess) {
			//printf("Unable to open device: %08x\n", kr);
			(void) (*new_device)->Release(new_device);
			continue;
		}

//		ECVIOReturn((*_deviceInterface)->ResetDevice(_deviceInterface));

//		IOUSBConfigurationDescriptorPtr configurationDescription = NULL;
//		ECVIOReturn((*_deviceInterface)->GetConfigurationDescriptorPtr(_deviceInterface, 0, &configurationDescription));
//		ECVIOReturn((*_deviceInterface)->SetConfiguration(_deviceInterface, configurationDescription->bConfigurationValue));

		service = usbDevice;
		device = new_device;
		break;
		//return new_device;
//		//Configure device
//		kr = ConfigureDevice(device);
//		if (kr != kIOReturnSuccess) {
//			printf("Unable to configure device: %08x\n", kr);
//			(void) (*device)->USBDeviceClose(device);
//			(void) (*device)->Release(device);
//			continue;
//		}

//		//Download firmware to device
//		kr = DownloadToDevice(device);
//		if (kr != kIOReturnSuccess) {
//			printf("Unable to download firmware to device: %08x\n", kr);
//			(void) (*device)->USBDeviceClose(device);
//			(void) (*device)->Release(device);
//			continue;
//		}

		//Close this device and release object
		kr = (*new_device)->USBDeviceClose(new_device);
		kr = (*new_device)->Release(new_device);
	}
}

int easycap_thread::FindInterfaces() {
	IOReturn                    kr;
	IOUSBFindInterfaceRequest   request;
	io_iterator_t               iterator;
	io_service_t                usbInterface;
	IOCFPlugInInterface         **plugInInterface = NULL;
	HRESULT                     result;
	SInt32                      score;
	UInt8                       interfaceClass;
	UInt8                       interfaceSubClass;

	//Placing the constant kIOUSBFindInterfaceDontCare into the following
	//fields of the IOUSBFindInterfaceRequest structure will allow you
	//to find all the interfaces
	request.bInterfaceClass = kIOUSBFindInterfaceDontCare;
	request.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
	request.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
	request.bAlternateSetting = kIOUSBFindInterfaceDontCare;

	//Get an iterator for the interfaces on the device
	kr = (*device)->CreateInterfaceIterator(device, &request, &iterator);
	while ((usbInterface = IOIteratorNext(iterator))) {
		//Create an intermediate plug-in
		kr = IOCreatePlugInInterfaceForService(usbInterface,
						       kIOUSBInterfaceUserClientTypeID,
						       kIOCFPlugInInterfaceID,
						       &plugInInterface, &score);
		//Release the usbInterface object after getting the plug-in
		kr = IOObjectRelease(usbInterface);
		if ((kr != kIOReturnSuccess) || !plugInInterface) {
			printf("Unable to create a plug-in (%08x)\n", kr);
			break;
		}

		//Now create the device interface for the interface
		result = (*plugInInterface)->QueryInterface(plugInInterface,
							    CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),
							    (LPVOID *) &interface);
		//No longer need the intermediate plug-in
		(*plugInInterface)->Release(plugInInterface);

		if (result || !interface) {
			printf("Couldn't create a device interface for the interface (%08x)\n", (int) result);
			break;
		}

		//Get interface class and subclass
		kr = (*interface)->GetInterfaceClass(interface,
						     &interfaceClass);
		kr = (*interface)->GetInterfaceSubClass(interface,
							&interfaceSubClass);

		//printf("Interface class %d, subclass %d\n", interfaceClass, interfaceSubClass);

		//Now open the interface. This will cause the pipes associated with
		//the endpoints in the interface descriptor to be instantiated
		kr = (*interface)->USBInterfaceOpenSeize(interface);
		if (kr != kIOReturnSuccess) {
			printf("Unable to open/seize interface (%08x)\n", kr);
			(void) (*interface)->Release(interface);
			break;
		}

		//service = usbInterface;

		return 1;
	}

	return 0;
}

#endif



easycap_thread::easycap_thread(QObject *parent) : QThread(parent) {
	abort = false;
	restart = true;
	initialized_buffers = false;
	debug_console = false;
	pipeIndex = 2;
	frameRequestSize = 3072;
	millisecondInterval = 1;
	simultaneousTransfers = 32;
	microframesPerTransfer = 16;
	currentFrame = 0;
        channel = 2;
        change_channels = false;

	ntsc = false;
	pal60 = false;
	svideo = false;

	brightness = 0x7F; //from 0x00 (all black) to 0xFF (all white)
	contrast = 0x7F; //default is 0x3F
	saturation = 0x3F; //default is 0x3F
	hue = 0x00; //default is 0x00

	my_easycap_render_thread.save_data = false;

#ifdef Q_OS_MAC
	device = NULL;
#endif
	connect(&my_easycap_render_thread, SIGNAL(frame_ready(bool)), this, SLOT(frame_ready(bool)));
}

void easycap_thread::render() {
	my_easycap_render_thread.colorspace = colorspace;
	my_easycap_render_thread.colorspace_multiplier = colorspace_multiplier;

	my_easycap_render_thread.debug_console = debug_console;

	if (debug_console) {
		connect(&my_easycap_render_thread, SIGNAL(debug_console_set_text(QString)), this, SLOT(set_text(QString)));
	}

	start(TimeCriticalPriority);
}

void easycap_thread::set_text(QString text_to_set) {
	emit(debug_console_set_text(text_to_set));
}

void easycap_thread::set_input(int new_channel) {
        if (channel == new_channel) {
                return;
        }
        change_channels = true;
        channel = new_channel;
        abort = restart = true;
}
void easycap_thread::set_standard() {
	abort = restart = true;
}

uint8_t easycap_thread::readIndex(u_int16_t index) {
	uint8_t retval;
#ifdef Q_OS_MAC
	IOUSBDevRequest request;

	request.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBVendor, kUSBDevice);
	request.bRequest = kUSBRqGetStatus; //does this even matter?
	request.wValue = 0;
	request.wIndex = index;
	request.wLength = sizeof(retval);
	request.pData = &retval;

	IOReturn kr = (*interface)->ControlRequest(interface, 0, &request);
	if (kr != kIOReturnSuccess) {
		qDebug() << "readIndex() error" << index;
	}
#endif
#ifdef Q_OS_WIN32
	if (sizeof(retval) != usb_control_msg(handle, USB_ENDPOINT_IN | USB_TYPE_VENDOR, USB_REQ_GET_STATUS, 0, index, (char*)&retval, sizeof(retval), USB_TIMEOUT)) {
		qDebug() << "readIndex() error" << index;
	}
#endif
	return retval;
}

bool easycap_thread::writeIndex(u_int16_t index, u_int16_t value) {
#ifdef Q_OS_MAC
	IOUSBDevRequest request;

	request.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBVendor, kUSBDevice);
	request.bRequest = kUSBRqClearFeature; //does this even matter?
	request.wValue = value;
	request.wIndex = index;
	request.wLength = 0;
	request.pData = NULL;

	IOReturn kr = (*interface)->ControlRequest(interface, 0, &request);
	if (kr != kIOReturnSuccess) {
		qDebug() << "writeIndex() error" << index;
	}
	return (kr == kIOReturnSuccess);
#endif
#ifdef Q_OS_WIN32
	int err = usb_control_msg(handle, USB_ENDPOINT_OUT | USB_TYPE_VENDOR, USB_REQ_CLEAR_FEATURE, value, index, NULL, 0, USB_TIMEOUT);
	if (err < 0) {
		qDebug() << "writeIndex() error" << err;
		return false;
	}
	return true;
#endif
}

bool easycap_thread::setFeature(u_int16_t index) {
#ifdef Q_OS_MAC
	IOUSBDevRequest     request;

	request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBStandard, kUSBDevice);
	request.bRequest = kUSBRqSetFeature;
	request.wValue = 0;
	request.wIndex = index;
	request.wLength = 0;
	request.pData = NULL;

	IOReturn kr = (*interface)->ControlRequest(interface, 0, &request);
	if (kr == kIOUSBPipeStalled) { //why does the pipe always stall after this request type?
		kr = (*interface)->ClearPipeStall(interface, 0);
	}
	if (kr != kIOReturnSuccess) {
		qDebug() << "setFeature() error" << kr;
	}
	return (kr == kIOReturnSuccess);
#endif
#ifdef Q_OS_WIN32
	int err = usb_control_msg(handle, USB_ENDPOINT_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE, USB_REQ_SET_FEATURE, index, 0, NULL, 0, USB_TIMEOUT);
	if (err < 0) {
		qDebug() << "setFeature() error" << err;
		return false;
	}
	//this doesn't appear to be necessary as it is on the os x side (20110704)
//	err = usb_clear_halt(handle, 0);
//	if (!err) {
//		qDebug() << "usb_clear_halt() error" << err;
//		return false;
//	}
	return true;
#endif
}

void easycap_thread::usb_stk11xx_write_registry(u_int16_t index, u_int16_t value) {
	writeIndex(index, value);
}
void easycap_thread::dev_stk0408_write0(u_int16_t mask, u_int16_t val) {
	//NSCAssert((mask & val) == val, @"Don't set values that will be masked out.");
	usb_stk11xx_write_registry(0x00, val);
	usb_stk11xx_write_registry(0x02, mask);
}
void easycap_thread::writeSAA711XRegister(uint8_t reg, int16_t val) {
	writeIndex(0x204, reg);
	writeIndex(0x205, val);
	writeIndex(0x200, 0x01);
	usleep(500);
	if (readIndex(0x201) != 0x04) {
		qDebug() << "writeSAA711XRegister() error";
	}
}
void easycap_thread::writeVT1612ARegister(uint8_t reg, uint16_t val) {
	union {
		uint16_t v16;
		uint8_t v8[2];
	}  theunion = {};
#ifdef Q_OS_MAC
	theunion.v16 = CFSwapInt16HostToLittle(val);
#else
	theunion.v16 = val;
#endif

	writeIndex(0x504, reg);
	writeIndex(0x502, theunion.v8[0]);
	writeIndex(0x503, theunion.v8[1]);
	writeIndex(0x500, 0x8c);
}
uint16_t easycap_thread::VT1612ABothChannels(uint8_t val) {
	union {
		uint16_t v16;
		uint8_t v8[2];
	}  theunion;
	theunion.v8[0] = theunion.v8[1] = val;

#ifdef Q_OS_MAC
	return CFSwapInt16BigToHost(theunion.v16);
#else
	return theunion.v16;
#endif
}
uint8_t easycap_thread::VT1612AInputGain(double v) {
	return CLAMP(0x0, round((1.0f - v) * 0x1f), 0x1f);
}
uint8_t easycap_thread::VT1612ARecordGain(double v) {
	return CLAMP(0x0, round(v * 0x0f), 0x15);
}


bool easycap_thread::find_easycap() {
#ifdef Q_OS_WIN32
	struct usb_bus *bus;
	struct usb_device *dev;

	usb_init();
	//usb_set_debug(255);
	usb_find_busses();
	usb_find_devices();

	bool foundit = false;
	for (bus = usb_busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next) {
			if (dev->descriptor.idVendor == kOurVendorID && dev->descriptor.idProduct == kOurProductID ) {
				if ((handle = usb_open(dev))) {
					if (!usb_set_configuration(handle, 1)) {
						if (!usb_claim_interface(handle, 0)) {
//							struct usb_device_descriptor device;
//							if ((usb_get_descriptor(handle, USB_DT_DEVICE, 0, &device, USB_DT_DEVICE_SIZE)) < 0) {
//								qDebug() << "usb_get_descriptor() error";
//								return 1;
//							}
//							int i, j, k;
//							for (i = 0; i < device.bNumConfigurations; i++) {
//								qDebug() << "configuration" << i;
//								struct usb_config_descriptor config;
//								if ((usb_get_descriptor(handle, USB_DT_CONFIG, i, &config, USB_DT_CONFIG_SIZE)) < 0) {
//									qDebug() << "usb_get_descriptor() error on" << i;
//									continue;
//								}

//								for (j = 0; j < config.bNumInterfaces; j++) {
//									qDebug() << "interface" << j;
//									struct usb_interface_descriptor interface;
//									if ((usb_get_descriptor(handle, USB_DT_INTERFACE, j, &interface, USB_DT_INTERFACE_SIZE)) < 0)
//									{
//										qDebug() << "usb_get_descriptor() error on" << j;
//										continue;
//									}

//									for (k = 0; k < interface.bNumEndpoints; k++) {
//										qDebug() << "endpoint" << k;
//										struct usb_endpoint_descriptor ep;
//										if ((usb_get_descriptor(handle, USB_DT_ENDPOINT, k, &ep, USB_DT_ENDPOINT_SIZE)) < 0)
//										{
//											qDebug() << "usb_get_descriptor() error on" << k;
//											continue;
//										}
//									}
//								}
//							}

							foundit = true;
						}
					}
				}
			}
		}
	}



	if (!foundit) {
		emit(capture_device_not_found());
		return false;
	}

#endif

#ifdef Q_OS_MAC
//	int r = 1;
//	r = libusb_init(NULL);
//	if (r < 0) {
//		fprintf(stderr, "failed to initialise libusb\n");
//		emit(capture_device_not_found());
//		return -1;
//	}

//	//
//	libusb_set_debug(NULL, 2);
//	//

	//http://developer.apple.com/library/mac/#documentation/DeviceDrivers/Conceptual/USBBook/USBDeviceInterfaces/USBDevInterfaces.html#//apple_ref/doc/uid/TP40002645-TPXREF101
//	mach_port_t             masterPort;
	CFMutableDictionaryRef  matchingDict;
	CFRunLoopSourceRef      runLoopSource;
	kern_return_t           kr;
	SInt32                  usbVendor = kOurVendorID;
	SInt32                  usbProduct = kOurProductID;

	//Create a master port for communication with the I/O Kit
//	kr = IOMasterPort(MACH_PORT_NULL, &masterPort);
//	if (kr || !masterPort) {
//	    printf("ERR: Couldn't create a master I/O Kit port(%08x)\n", kr);
//	    return -1;
//	}

	//Set up matching dictionary for class IOUSBDevice and its subclasses
	matchingDict = IOServiceMatching(kIOUSBDeviceClassName);
	if (!matchingDict) {
	    printf("Couldn't create a USB matching dictionary\n");
	    //mach_port_deallocate(mach_task_self(), masterPort);
	    emit(capture_device_not_found());
	    return false;
	}

	//Add the vendor and product IDs to the matching dictionary.
	//This is the second key in the table of device-matching keys of the
	//USB Common Class Specification
	CFDictionarySetValue(matchingDict, CFSTR(kUSBVendorName),
			    CFNumberCreate(kCFAllocatorDefault,
					 kCFNumberSInt32Type, &usbVendor));

	CFDictionarySetValue(matchingDict, CFSTR(kUSBProductName),
			    CFNumberCreate(kCFAllocatorDefault,
					kCFNumberSInt32Type, &usbProduct));

	//To set up asynchronous notifications, create a notification port and
	//add its run loop event source to the program’s run loop
	gNotifyPort = IONotificationPortCreate(kIOMasterPortDefault);
	runLoopSource = IONotificationPortGetRunLoopSource(gNotifyPort);
	CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource,
			    kCFRunLoopDefaultMode);

	//Retain additional dictionary references because each call to
	//IOServiceAddMatchingNotification consumes one reference
	matchingDict = (CFMutableDictionaryRef) CFRetain(matchingDict);
	matchingDict = (CFMutableDictionaryRef) CFRetain(matchingDict);
	//matchingDict = (CFMutableDictionaryRef) CFRetain(matchingDict);

	//Now set up two notifications: one to be called when a raw device
	//is first matched by the I/O Kit and another to be called when the
	//device is terminated
	//Notification of first match:
	kr = IOServiceAddMatchingNotification(gNotifyPort,
					      kIOFirstMatchNotification, matchingDict,
					      RawDeviceAdded, NULL, &gRawAddedIter);

	//Iterate over set of matching devices to access already-present devices
	//and to arm the notification
	RawDeviceAdded(NULL, gRawAddedIter);
	if (!device) {
		emit(capture_device_not_found());
		return false;
	}
	emit(found_device(device));

	if (!FindInterfaces()) {
		emit(capture_device_not_found());
		return false;
	}

	kr = (*interface)->GetFrameListTime(interface, &frameTime);
	if (kUSBHighSpeedMicrosecondsInFrame != frameTime) {
		emit(capture_device_not_usb_2_0());
		return false;
	}

	kr = (*interface)->CreateInterfaceAsyncEventSource(interface, NULL);
	if (kr != kIOReturnSuccess) {
		qDebug() << "CreateInterfaceAsyncEventSource error" << kr;
		emit(capture_device_not_found());
		return false;
	}

	emit(service_found(service));


//        //Notification of termination:
//        kr = IOServiceAddMatchingNotification(gNotifyPort,
//                        kIOTerminatedNotification, matchingDict,
//                        RawDeviceRemoved, NULL, &gRawRemovedIter);

//        //Iterate over set of matching devices to release each one and to
//        //arm the notification
//        RawDeviceRemoved(NULL, gRawRemovedIter);

	//Finished with master port
//	mach_port_deallocate(mach_task_self(), masterPort);
//	masterPort = 0;

	//Start the run loop so notifications will be received
	//CFRunLoopRun();



//	libusb_device_handle *devh = NULL;
//	devh = libusb_open_device_with_vid_pid(NULL, 0x05e1, 0x0408);
//	if (!devh) {
//		emit(capture_device_not_found());
//		return -1;
//	}

//	r = libusb_claim_interface(devh, 0);
//	if (r < 0) {
//		fprintf(stderr, "usb_claim_interface error %d\n", r);
//		emit(capture_device_not_found());
//		return -1;
//	}

#endif

	return true;
}

bool easycap_thread::initialize_easycap() {
	u_int16_t value;

	//int dev_stk0408_initialize_device(ECVSTK1160Device *dev)
	//{
		usb_stk11xx_write_registry(0x0500, 0x0094);
		usb_stk11xx_write_registry(0x0203, 0x00a0);

		//if (!setFeature(1)) { //this doesn't appear to actually change anything (20110704)
			//qDebug() << "setFeature() error";
			//emit(capture_device_not_found());
			//return -1;
		//}

		usb_stk11xx_write_registry(0x0003, 0x0080);
		usb_stk11xx_write_registry(0x0001, 0x0003);
		dev_stk0408_write0(0x67, 1 << 5 | 1 << 0);
		struct {
			u_int16_t reg;
			u_int16_t val;
		} const stk0408_settings[] = {
			{0x203, 0x04a},
			{0x00d, 0x000},
			{0x00f, 0x002},
			{0x103, 0x000},
			{0x018, 0x000},
			{0x01a, 0x014},
			{0x01b, 0x00e},
			{0x01c, 0x046},
			{0x019, 0x000},
			{0x300, 0x012},
			{0x350, 0x02d},
			{0x351, 0x001},
			{0x352, 0x000},
			{0x353, 0x000},
			{0x300, 0x080},
			{0x018, 0x010},
			{0x202, 0x00f},
		};
		for(unsigned int i = 0; i < numberof(stk0408_settings); i++) usb_stk11xx_write_registry(stk0408_settings[i].reg, stk0408_settings[i].val);
		usb_stk11xx_write_registry(STK0408StatusRegistryIndex, 0x33);
	//}


	//_SAA711XChip initialize
		struct {
                        int reg;
                        int val;
		} const SAA711X_settings[] = {
		{0x01, 0x08},
		{0x02, SAA711XFUSE0Antialias | SAA711XFUSE1Amplifier | (svideo ? SAA711XMODESVideoAI12_YGain : SAA711XMODECompositeAI11)}, //SAA711XFUSE1Amplifier makes it darker for some reason
		{0x03, SAA711XHOLDGAutomaticGainControlEnabled | SAA711XVBSLLongVerticalBlanking}, //SAA711XHOLDGAutomaticGainControlEnabled makes it brighter
		{0x04, 0x90},
		{0x05, 0x90},
		{0x06, 0xeb},
		{0x07, 0xe0},
		{0x08, SAA711XVNOIVerticalNoiseReductionFast
					| SAA711XHTCHorizontalTimeConstantFastLocking
					| SAA711XFOETForcedOddEventToggle
					| (ntsc ? SAA711XFSELManualFieldSelection60Hz : SAA711XFSELManualFieldSelection50Hz)
		},
		//{0x09, (svideo ? SAA711XBYPSChrominanceTrapCombBypass : SAA711XYCOMBAdaptiveLuminanceComb)},
		{0x09, 0},
		{0x0e, (1 << 0) | (pal60 ? (1 << 4) : 0) }, //(1 << 0) is "nominal" chrominance bandwidth
		{0x0f, SAA711XCGAINChromaGainValueNominal | SAA711XACGCAutomaticChromaGainControlEnabled},
		{0x10, 0x06},
		{0x11, SAA711XRTP0OutputPolarityInverted},
		{0x12, 0x00},
		{0x13, 0x00},
		{0x14, 0x01},
		{0x15, 0x11},
		{0x16, 0xfe},
		{0x17, 0xd8},
		{0x18, 0x40},
		{0x19, 0x80},
		{0x1a, 0x77},
		{0x1b, 0x42},
		{0x1c, 0xa9},
		{0x1d, 0x01},
		{0x83, 0x31},
		{0x88, SAA711XSLM1ScalerDisabled | SAA711XSLM3AudioClockGenerationDisabled | (svideo ? (SAA711XCH1ENAD1X | SAA711XCH2ENAD2X) : SAA711XCH1ENAD1X)},
};
	for(unsigned int i = 0; i < numberof(SAA711X_settings); i++) writeSAA711XRegister(SAA711X_settings[i].reg, SAA711X_settings[i].val); //if(![device writeSAA711XRegister:SAA711X_settings[i].reg value:SAA711X_settings[i].val]) return NO;
	for(unsigned int i = 0x41; i <= 0x57; i++) writeSAA711XRegister(i, 0xff); //if(![device writeSAA711XRegister:i value:0xff]) return NO;


	writeSAA711XRegister(0x0a, brightness);
	writeSAA711XRegister(0x0b, contrast);
	writeSAA711XRegister(0x0c, saturation);
	writeSAA711XRegister(0x0d, hue);



	//audio

	//???
	writeVT1612ARegister(0x94, 0x00);

	//writeIndex(0x505, 0x0);
	writeIndex(0x506, 0x1); //0x1
	writeIndex(0x507, 0x0); //0x0
	//writeIndex(0x508, 0x0);
	//writeIndex(0x509, 0x0);

//	VT1612ARecordSourceMic = 0,
//	VT1612ARecordSourceCD = 1,
//	VT1612ARecordSourceVideoIn = 2,
//	VT1612ARecordSourceAuxIn = 3,
//	VT1612ARecordSourceLineIn = 4,
//	VT1612ARecordSourceStereoMix = 5,
//	VT1612ARecordSourceMonoMix = 6,
//	VT1612ARecordSourcePhone = 7,

//	VT1612ARegisterVolumeMic = 0x0e,
//	VT1612ARegisterVolumeLineIn = 0x10,
//	VT1612ARegisterVolumeCD = 0x12,
//	VT1612ARegisterVolumeVideoIn = 0x14,
//	VT1612ARegisterVolumeAuxIn = 0x16,

	struct {
		uint8_t reg;
		u_int16_t val;
	} ac97_settings[] = {
		{VT1612ARegisterRecordSelect, VT1612ABothChannels(VT1612ARecordSourceLineIn)}, //VT1612ARecordSourceLineIn
		{VT1612ARegisterVolumeLineIn, VT1612ABothChannels(VT1612AInputGain(1.0f))}, //VT1612ARegisterVolumeLineIn
		{VT1612ARegisterRecordGain, VT1612ABothChannels(VT1612ARecordGain(0.3f))},
	};
	for(unsigned int i = 0; i < numberof(ac97_settings); i++) writeVT1612ARegister(ac97_settings[i].reg, ac97_settings[i].val);

	//get vendor id - does this code assume we are on a little endian machine? (20111015)
	union {
		uint8_t v8[4];
		uint16_t v16[2];
		uint32_t v32;
	} audio_vendor_id = {};

	writeIndex(0x504, VT1612ARegisterVendorID1);
	writeIndex(0x500, 0x8b);
	audio_vendor_id.v8[1] = readIndex(0x502);
	audio_vendor_id.v8[0] = readIndex(0x503);

	writeIndex(0x504, VT1612ARegisterVendorID2);
	writeIndex(0x500, 0x8b);
	audio_vendor_id.v8[3] = readIndex(0x502);
	audio_vendor_id.v8[2] = readIndex(0x503);

	if (!audio_vendor_id.v32) {
		emit(easycap_audio_not_found());
	} else {
		//qDebug() << QString("%1%2%3-%4")
		//	    .arg((char)audio_vendor_id.v8[0])
		//	    .arg((char)audio_vendor_id.v8[1])
		//	    .arg((char)audio_vendor_id.v8[2])
		//	    .arg(audio_vendor_id.v8[3], 0, 16);
	}




	//- (BOOL)_setVideoSource:(ECVSTK1160VideoSource)source
	//{
	// 	UInt8 val = 0;
	// 	switch(source) {
	// 		case ECVSTK1160SVideoInput:
	// 		case ECVSTK1160Composite1234Input:
	// 			return YES;
	// 		case ECVSTK1160Composite1Input: val = 3; break;
	// 		case ECVSTK1160Composite2Input: val = 2; break;
	// 		case ECVSTK1160Composite3Input: val = 1; break;
	// 		case ECVSTK1160Composite4Input: val = 0; break;
	// 		default:
	// 			return NO;
	// 	}

        if (!change_channels) {
                channel = svideo ? 0 : 2;
        }

        dev_stk0408_write0(1 << 7 | 0x3 << 3, 1 << 7 | channel << 3);
	// 	return YES;
	//}
	//- (BOOL)_initializeResolution
	//{
        const int bpp = 2; //"bytes per pixel"

	int standard_width = 720;
	int standard_height = ntsc ? 486 : 578;

	struct {
                int reg;
                int val;
	} resolution_settings[] = {
//	{0x110, ((initial_width+2) - initial_width) * bpp},
//	{0x111, 0},
//	{0x112, ((initial_height+2) - initial_height) / 2},
//	{0x113, 0},
//	{0x114, (initial_width+2) * bpp},
//	{0x115, 5},
//	{0x116, (initial_height+2) / 2},
//	{0x117, 1},

	{0x110, (standard_width - initial_width) * bpp},
	{0x111, 0},
	{0x112, (standard_height - initial_height) / 2},
	{0x113, 0},
	{0x114, standard_width * bpp},
	{0x115, 5},
	{0x116, standard_height / 2},
	{0x117, (ntsc ? 0 : 1)},
};
	for(unsigned int i = 0; i < numberof(resolution_settings); i++) writeIndex(resolution_settings[i].reg, resolution_settings[i].val); //if(![self writeIndex:resolution_settings[i].reg value:resolution_settings[i].val]) return NO;
	//	return YES;
	//}







#ifdef Q_OS_MAC
	kern_return_t kr = (*interface)->SetAlternateInterface(interface, 5);
	if (kr != kIOReturnSuccess) {
		qDebug() << "SetAlternateInterface error";
		emit(capture_device_not_found());
		return false;
	}
#endif
#ifdef Q_OS_WIN32
	int err = usb_set_altinterface(handle, 5);
	if (err < 0) {
		qDebug() << "usb_set_altinterface() error" << err;
		emit(capture_device_not_found());
		return false;
	}
#endif

	//- (BOOL)_setStreaming:(BOOL)flag
	//{
	value = readIndex(STK0408StatusRegistryIndex); //if(![self readIndex:STK0408StatusRegistryIndex value:&value]) return NO;
	value |= STK0408StatusStreaming;
	writeIndex(STK0408StatusRegistryIndex, value); //return [self writeIndex:STK0408StatusRegistryIndex value:value];
	//}











	if (initialized_buffers) return true;

#ifdef Q_OS_MAC
	UInt8 direction = kUSBNone; UInt8 pipeNumberIgnored = 0; UInt8 transferType = kUSBAnyType;
	(*interface)->GetPipeProperties(interface, pipeIndex, &direction, &pipeNumberIgnored, &transferType, &frameRequestSize, &millisecondInterval);
	transfers = (ECVTransfer *)calloc(simultaneousTransfers, sizeof(ECVTransfer));
	for (int i = 0; i < simultaneousTransfers; ++i) {
		ECVTransfer *const transfer = transfers + i;
		(*interface)->LowLatencyCreateBuffer(interface, (void **)&transfer->list, sizeof(IOUSBLowLatencyIsocFrame) * microframesPerTransfer, kUSBLowLatencyFrameListBuffer);
		(*interface)->LowLatencyCreateBuffer(interface, (void **)&transfer->data, frameRequestSize * microframesPerTransfer, kUSBLowLatencyReadBuffer);
		for (unsigned int j = 0; j < microframesPerTransfer; ++j) {
			transfer->list[j].frStatus = kIOReturnInvalid; // Ignore them to start out.
			transfer->list[j].frReqCount = frameRequestSize;
		}
	}
#endif
#ifdef Q_OS_WIN32
		//why doesn't this work? have to hardcode frameRequestSize and millisecondInterval for windows (20110703)
//	struct usb_endpoint_descriptor pipe_descriptor;
//	int read = usb_get_descriptor(handle, USB_DT_ENDPOINT, pipeIndex, &pipe_descriptor, USB_DT_ENDPOINT_SIZE);
//	if (sizeof(pipe_descriptor) != read) {
//		qDebug() << "usb_get_descriptor_by_endpoint() error" << read;
//		return false;
//	}
//	frameRequestSize = pipe_descriptor.wMaxPacketSize;
//	millisecondInterval = pipe_descriptor.bInterval;
#endif

	initialized_buffers = true;
	return true;
}


void easycap_thread::frame_ready(bool high_field) {
	emit(frame_ready_signal(high_field));
}

void easycap_thread::start_streaming() {
	//qDebug() << "start_streaming()";
	lol_dest_pos = 0;

	my_easycap_render_thread.incoming_image_buffer = incoming_image_buffer;
	my_easycap_render_thread.frame_buffer_for_encoding = frame_buffer_for_encoding;
        my_easycap_render_thread.frame_buffer_for_display = frame_buffer_for_display;
        my_easycap_render_thread.frame_buffer_for_display_1 = frame_buffer_for_display_1;
        my_easycap_render_thread.frame_buffer_for_display_2 = frame_buffer_for_display_2;
        my_easycap_render_thread.initial_width = initial_width;
	my_easycap_render_thread.initial_height = initial_height;
	my_easycap_render_thread.targetwidth = targetwidth;
	my_easycap_render_thread.targetheight = targetheight;
	my_easycap_render_thread.lol_dest_2 = lol_dest_2;

	emit(easycap_streaming());
	timer.start();

#ifdef Q_OS_MAC
	AbsoluteTime atTimeIgnored;
	(*interface)->GetBusFrameNumber(interface, &currentFrame, &atTimeIgnored);
	currentFrame += 10;
	//qDebug() << "start at" << currentFrame;

	while (!abort) {
		for (int i = 0; i < simultaneousTransfers; ++i) {
			ECVTransfer *const transfer = transfers + i;
			for (unsigned int j = 0; j < microframesPerTransfer; j++) {
				if (kUSBLowLatencyIsochTransferKey == transfer->list[j].frStatus && j) {
                                        //Nanoseconds const nextUpdateTime = UInt64ToUnsignedWide(UnsignedWideToUInt64(AbsoluteToNanoseconds(transfer->list[j - 1].frTimeStamp)) + millisecondInterval * ECVNanosecondsPerMillisecond);
                                        auto ts = transfer->list[j - 1].frTimeStamp;
                                        const quint64 nextUpdateTimeNS = (((quint64)ts.hi << 32) + ts.lo) + millisecondInterval*ECVNanosecondsPerMillisecond;

                                        //mach_wait_until(UnsignedWideToUInt64(NanosecondsToAbsolute(nextUpdateTime)));
                                        usleep(nextUpdateTimeNS/1000);
				}
				while(kUSBLowLatencyIsochTransferKey == transfer->list[j].frStatus) usleep(100); // In case we haven't slept long enough already.
				transfer->list[j].frStatus = kUSBLowLatencyIsochTransferKey;

				int length = transfer->list[j].frActCount;
				UInt8 *bytes = transfer->data + j * frameRequestSize;
				int skip = 4;
				if (ECVSTK1160NewImageFlag & bytes[0]) {
					skip = 8;
					if (lol_dest_pos && lol_dest_pos == (ntsc ? 345600 : 414720)) {
						memcpy(lol_dest_2, lol_dest, lol_dest_length); //double buffer the image
						my_easycap_render_thread.render(ECVSTK1160HighFieldFlag & bytes[0]);
					}
					//qDebug() << "read" << lol_dest_pos << "pixels in this frame";
					lol_dest_pos = 0;
				}
				if (length <= skip) {
					continue;
				}
				int realLength = length - skip;
				if (lol_dest_pos + realLength >= initial_width * initial_height * 2) {
					//qDebug() << "discarding data ...";
					continue;
				}
				memcpy(lol_dest + lol_dest_pos, bytes + skip, realLength);
				lol_dest_pos += realLength;
			}
			kern_return_t kr = (*interface)->LowLatencyReadIsochPipeAsync(interface, pipeIndex, transfer->data, currentFrame, microframesPerTransfer, CLAMP(1, millisecondInterval, 8), transfer->list, ECVDoNothing, NULL);
			if (kr != kIOReturnSuccess) {
				qDebug() << QString("LowLatencyReadIsochPipeAsync error %1").arg(kr, 0, 16);
				//if (kIOReturnIsoTooOld == kr) { //we are probably too far behind to catch up (20111014)
					//(*interface)->GetBusFrameNumber(interface, &currentFrame, &atTimeIgnored);
					//qDebug() << "changed currentFrame to" << currentFrame;
					//currentFrame += 10;
				//}
			}
			//qDebug() << "currentFrame " << currentFrame << "+=" << (microframesPerTransfer / (kUSBFullSpeedMicrosecondsInFrame / frameTime));
			//(*interface)->GetBusFrameNumber(interface, &currentFrame, &atTimeIgnored);
			//qDebug() << "actually" << currentFrame;
			currentFrame += microframesPerTransfer / (kUSBFullSpeedMicrosecondsInFrame / frameTime);
		}
	}
#endif

#ifdef Q_OS_WIN32
#define TRANSFER_BUFFER_OFFSET (i * frameRequestSize * microframesPerTransfer + i * (4 + sizeof(USBD_ISO_PACKET_DESCRIPTOR) * microframesPerTransfer))
	void *transfer_contexts[simultaneousTransfers];
	unsigned char *transfer_buffer = new unsigned char[simultaneousTransfers * frameRequestSize * microframesPerTransfer * 2]; //extra for the number-of-frames-at-start hack
	int i, ret;

	//setup (only needs to be run once)
	for (i=0; i < simultaneousTransfers; i++) {
		ret = usb_isochronous_setup_async(handle, &transfer_contexts[i], 0x82, frameRequestSize); //0x82 == input pipe 2
		if (ret < 0) {
			qDebug() << "usb_isochronous_setup_async() error" << ret;
		}
	}

	//submit initial transfers
	for (i=0; i < simultaneousTransfers; i++) {
		//microframesPerTransfer as a safe ceiling for the number of packet descriptors - as of 20110704 there has never been a different number returned as number_of_packets though
		ret = usb_submit_async(transfer_contexts[i], (char*)(transfer_buffer + TRANSFER_BUFFER_OFFSET), frameRequestSize * microframesPerTransfer + 4 + sizeof(USBD_ISO_PACKET_DESCRIPTOR) * microframesPerTransfer);
		if (ret < 0) {
			qDebug() << "usb_submit_async() error" << ret;
		}
	}

	while (!abort) {
		for (i=0; i < simultaneousTransfers; i++) {
			//reap initial transfer (or new transfer that has been waiting since the previous while (!abort) loop)
			int ret2 = usb_reap_async(transfer_contexts[i], USB_TIMEOUT);
			if (debug_console) emit(debug_console_set_text(QString("easycap thread: read usb async at %1").arg(timer.elapsed())));

			//now immediately resubmit the transfer - microframesPerTransfer as a safe ceiling for the number of packet descriptors - as of 20110704 there has never been a different number returned as number_of_packets though
			ret = usb_submit_async(transfer_contexts[i], (char*)(transfer_buffer + TRANSFER_BUFFER_OFFSET), frameRequestSize * microframesPerTransfer + 4 + sizeof(USBD_ISO_PACKET_DESCRIPTOR) * microframesPerTransfer);
			if (ret < 0) qDebug() << "usb_submit_async() error" << ret;

			if (ret2 <= 0) {
				if (ret2 < 0) { //an error
					qDebug() << "usb_reap_async() error" << ret2;
				}
				//0 means we didn't actually read anything so just skip to the next transfer ...
				continue;
			}

			unsigned char *headers = transfer_buffer + TRANSFER_BUFFER_OFFSET;
			ULONG *pnumber_of_packets = reinterpret_cast<ULONG*>(headers);
			ULONG number_of_packets = *pnumber_of_packets;
			if (number_of_packets != microframesPerTransfer) { //to date (20110704) this has never happened - it might be bad if it did
				qDebug() << "number_of_packets" << number_of_packets << "!= microframesPerTransfer";
			}
			unsigned char *bytes;
			headers += 4;
			for (unsigned int j = 0; j < number_of_packets; j++) {
				USBD_ISO_PACKET_DESCRIPTOR *packet_descriptor = reinterpret_cast<USBD_ISO_PACKET_DESCRIPTOR*>(headers);
				headers += sizeof(USBD_ISO_PACKET_DESCRIPTOR);

				int length = packet_descriptor->Length;
				if (!length) {
					continue;
				}

				bytes = transfer_buffer + TRANSFER_BUFFER_OFFSET + packet_descriptor->Offset;
				int skip = 4;
				if (ECVSTK1160NewImageFlag & bytes[0]) {
					//qDebug() << "ECVSTK1160NewImageFlag & bytes[0]";
					skip = 8;
					if (lol_dest_pos && lol_dest_pos == (ntsc ? 345600 : 414720)) {
						memcpy(lol_dest_2, lol_dest, lol_dest_length); //double buffer the image
						my_easycap_render_thread.render(ECVSTK1160HighFieldFlag & bytes[0]);
					}
					if (debug_console) emit(debug_console_set_text(QString("easycap thread: read %1 pixels in this frame in %2").arg(lol_dest_pos).arg(timer.restart())));
					//qDebug() << "read" << lol_dest_pos << "pixels in this frame";
					lol_dest_pos = 0;
				}
				if (length <= skip) {
					continue;
				}
				int realLength = length - skip;
				if (lol_dest_pos + realLength >= initial_width * initial_height * 2) { //don't want to run off the end of the buffer
					//qDebug() << "discarding data ...";
					continue;
				}
				memcpy(lol_dest + lol_dest_pos, bytes + skip, realLength);
				lol_dest_pos += realLength;
			}
			if (debug_console) emit(debug_console_set_text(QString("easycap thread: finished with usb async at %1").arg(timer.elapsed())));

			//the next line does not help as of 20110927 because WaitForSingleObject() is used in usb_reap_async() which hints windows to deprioritize the spinlock
			//__asm__ __volatile__("pause"); //coming up next is usb_reap_async() which is a spinlock - the "pause" instruction tells the cpu to take a break from this thread and is especially important for single core machines (20110926)
		}
	}

	for (i=0; i < simultaneousTransfers; i++) {
		usb_free_async(&transfer_contexts[i]);
	}
	delete[] transfer_buffer;
#endif
}

void easycap_thread::run() {
        //qDebug() << "easycap_thread::run()";

	if (!find_easycap()) return;


	lol_dest_length = initial_width * 576 * 2; //a big enough buffer whether we are streaming ntsc or pal (20111010)

	lol_dest = (unsigned char *)malloc(lol_dest_length);
	memset(lol_dest, 0, lol_dest_length);

	lol_dest_2 = (unsigned char *)malloc(lol_dest_length);
	memset(lol_dest_2, 0, lol_dest_length);


	while (restart) {
		abort = restart = false;
		if (!initialize_easycap()) return;
		start_streaming();
	}

	//qDebug() << "my_easycap_render_thread.abort = true;";
	my_easycap_render_thread.abort = true;
	//qDebug() << "my_easycap_render_thread.render;";
	my_easycap_render_thread.render(true);
	//qDebug() << "my_easycap_render_thread.wait;";
	my_easycap_render_thread.wait();
        //qDebug() << "easycap_thread exited";
}
