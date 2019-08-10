// C Standard Library headers.
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <time.h> 
#include <errno.h> 

// Linux headers.
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include "Conversions.h"

union uInt
{
	unsigned int v;  
	unsigned char c[4];
};
typedef union uInt uInt;

// Globals.
int fd = 0;
struct v4l2_capability v_cap;
enum v4l2_priority v_prio;
struct v4l2_output v_out;
int videooutputarg = 0;
struct v4l2_input v_in;
int videoinputarg = 0;
v4l2_std_id v_std_id;
struct v4l2_cropcap v_cropcap;
struct v4l2_crop v_crop;
struct v4l2_format v_fmt;
struct v4l2_fmtdesc v_fmtdesc;
struct v4l2_frmsizeenum v_frmsize;
struct v4l2_frmivalenum v_frmival;
struct v4l2_queryctrl v_queryctrl;
struct v4l2_querymenu v_querymenu;
struct v4l2_control v_ctrl;
struct v4l2_requestbuffers v_reqbuf;
struct v4l2_buffer v_buf;
void* mem = NULL;
struct buffer_t
{
	void* start;
	size_t length;
};
struct buffer_t* buffers;
int buffertypearg = 0;

// PPM format is usually RGB 24 bits.
// minbuffersize should only be used in case the application cannot convert the data from the webcam.
void ConvertAndSaveToPPM(char* szFilename, unsigned char* webcamimgdata, 
	unsigned int width, unsigned int height, unsigned int pixfmt, unsigned int minbuffersize)
{
	FILE* fp = NULL;
	unsigned char* ppmimgdata = NULL;

	fp = fopen(szFilename, "wb");
	if (fp)
	{
		ppmimgdata = (unsigned char*)calloc(width*height*3, sizeof(unsigned char));
		if (ppmimgdata)
		{
			fprintf(fp, "P6\n%d %d 255\n", width, height);
			ConvertV4L2FormatToRGB24(webcamimgdata, ppmimgdata, width, height, pixfmt, minbuffersize);
			fwrite(ppmimgdata, sizeof(unsigned char), width*height*3, fp);
			free(ppmimgdata);
		}
		fclose(fp);
	}
	else
	{
		printf("fopen() warning : %s. \n", strerror(errno));
	}
}

void print_control_info(void)
{
	if (v_queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) return;
	printf("     Control number : %u, name : %s", v_queryctrl.id, v_queryctrl.name);
	// Select control for which we want to get information.
	memset(&v_ctrl, 0, sizeof(v_ctrl));
	v_ctrl.id = v_queryctrl.id;
	if (ioctl(fd, VIDIOC_G_CTRL, &v_ctrl) != -1)
	{
		printf(", value : %d", v_ctrl.value);
	}
	switch (v_queryctrl.type)
	{	
	case V4L2_CTRL_TYPE_INTEGER:	
		printf(", type : integer, default value : %d, range : {%d:%d:%d}\n", 
			v_queryctrl.default_value, v_queryctrl.minimum, v_queryctrl.step, v_queryctrl.maximum);
		break;
	case V4L2_CTRL_TYPE_BOOLEAN:
		printf(", type : boolean, default value : %d\n", v_queryctrl.default_value);
		break;
	case V4L2_CTRL_TYPE_MENU:
		printf(", type : menu, default value : %d, menu items : \n", v_queryctrl.default_value);
		// Select control for which we want to get information.
		memset(&v_querymenu, 0, sizeof(v_querymenu));
		v_querymenu.id = v_queryctrl.id;
		for (v_querymenu.index = v_queryctrl.minimum; (int)v_querymenu.index <= v_queryctrl.maximum; v_querymenu.index++) 
		{
			v_querymenu.id = v_queryctrl.id;
			if (ioctl(fd, VIDIOC_QUERYMENU, &v_querymenu) != -1) 
			{
				printf("          [%u] %s\n", v_querymenu.index, v_querymenu.name);
			} 
		}
		break;
	default:
		break;
	}
}

void set_control_value(unsigned int id, int value)
{
	// Select the desired control and set a new value.
	memset(&v_ctrl, 0, sizeof(v_ctrl));
	v_ctrl.id = id;
	v_ctrl.value = value;
	if (ioctl(fd, VIDIOC_S_CTRL, &v_ctrl) == -1)
	{
		printf("ioctl() VIDIOC_S_CTRL warning : %s. \n", strerror(errno));
	}
	usleep(100000);
	// Check if the settings requested were accepted.
	memset(&v_ctrl, 0, sizeof(v_ctrl));
	v_ctrl.id = id;
	if (ioctl(fd, VIDIOC_G_CTRL, &v_ctrl) == -1)
	{
		printf("ioctl() VIDIOC_G_CTRL warning : %s. \n", strerror(errno));
	}
	else
	{
		if (v_ctrl.value != value)
		{
			printf("An unsupported value was specified. \n");
		}
	}
	usleep(100000);
}

void print_video_standards(v4l2_std_id stds)
{
	struct v4l2_standard v_std;
	int j = 0;

	j = 0;
	for (;;)
	{
		memset(&v_std, 0, sizeof(v_std));
		v_std.index = j;
		if (ioctl(fd, VIDIOC_ENUMSTD, &v_std) == -1) break;
		// v4l2_std_id stds is a bitfield indicating all the standards supported by an input or output, 
		// or the current selected standard.
		// VIDIOC_ENUMSTD enumerates standards supported by the driver with simplifications
		// to take into account automatic switches?
		if (stds & v_std.id) printf ("     %s (identifier : %llu, frame period : %u/%us (%dHz), total lines per frame including blanking : %u)\n", 
			v_std.name, v_std.id, v_std.frameperiod.numerator, v_std.frameperiod.denominator, 
			(int)((double)v_std.frameperiod.denominator/(double)v_std.frameperiod.numerator), 
			v_std.framelines);
		j++;
	}
}

void print_video_format()
{
	uInt fourcc;

	if (v_fmt.type == V4L2_BUF_TYPE_VIDEO_CAPTURE) printf(", video capture type");
	if (v_fmt.type == V4L2_BUF_TYPE_VIDEO_OUTPUT) printf(", video output type");
	if (v_fmt.type == V4L2_BUF_TYPE_VIDEO_OVERLAY) printf(", video overlay type");
	if (v_fmt.type == V4L2_BUF_TYPE_VBI_CAPTURE) printf(", VBI capture type");
	if (v_fmt.type == V4L2_BUF_TYPE_VBI_OUTPUT) printf(", VBI output type");
	if (v_fmt.type == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE) printf(", sliced VBI capture type");
	if (v_fmt.type == V4L2_BUF_TYPE_SLICED_VBI_OUTPUT) printf(", sliced VBI output type");
	if (v_fmt.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY) printf(", video output overlay type");
	if (v_fmt.type >= V4L2_BUF_TYPE_PRIVATE) printf(", custom type");

	if ((v_fmt.type == V4L2_BUF_TYPE_VIDEO_CAPTURE)||(v_fmt.type == V4L2_BUF_TYPE_VIDEO_OUTPUT))
	{
		fourcc.v = v_fmt.fmt.pix.pixelformat;
		printf(", image width : %u, image height : %u, FOURCC identifier : %u, FOURCC code : %c%c%c%c, "
			"bytes per line : %u, image size : %u bytes, color space identifier : %d", 
			v_fmt.fmt.pix.width, v_fmt.fmt.pix.height, v_fmt.fmt.pix.pixelformat, 
			fourcc.c[0], fourcc.c[1], fourcc.c[2], fourcc.c[3], 
			v_fmt.fmt.pix.bytesperline, v_fmt.fmt.pix.sizeimage, (int)v_fmt.fmt.pix.colorspace);

		switch ((int)v_fmt.fmt.pix.colorspace)
		{
		case V4L2_COLORSPACE_SMPTE170M:
			printf(", color space : NTSC/PAL SMPTE170M");
			break;
		case V4L2_COLORSPACE_SMPTE240M:
			printf(", color space : HDTV SMPTE240M");
			break;
		case V4L2_COLORSPACE_REC709:
			printf(", color space : HDTV REC709");
			break;
		case V4L2_COLORSPACE_BT878:
			printf(", color space : BT878");
			break;
		case V4L2_COLORSPACE_470_SYSTEM_M:
			printf(", color space : M/NTSC");
			break;
		case V4L2_COLORSPACE_470_SYSTEM_BG:
			printf(", color space : PAL and SECAM BG");
			break;
		case V4L2_COLORSPACE_JPEG:
			printf(", color space : JPEG");
			break;
		case V4L2_COLORSPACE_SRGB:
			printf(", color space : SRGB");
			break;
		default:
			printf(", color space : unknown");
			break;
		}

		printf(", field identifier : %d", (int)v_fmt.fmt.pix.field);

		switch ((int)v_fmt.fmt.pix.field)
		{
		case V4L2_FIELD_ANY:
			printf(", field : unknown field order");
			break;
		case V4L2_FIELD_NONE:
			printf(", field : progressive format");
			break;
		case V4L2_FIELD_TOP:
			printf(", field : top field only");
			break;
		case V4L2_FIELD_BOTTOM:
			printf(", field : bottom field only");
			break;
		case V4L2_FIELD_INTERLACED:
			printf(", field : images contain both fields interleaved line by line (M/NTSC transmits "
				"the bottom field first, all other standards the top field first)");
			break;
		case V4L2_FIELD_SEQ_TB:
			printf(", field : images contain both fields the top field lines stored first immediately "
				"followed by the bottom field lines (the older one first in memory)");
			break;
		case V4L2_FIELD_SEQ_BT:
			printf(", field : images contain both fields the bottom field lines stored first immediately "
				"followed by the top field lines (the older one first in memory)");
			break;
		case V4L2_FIELD_ALTERNATE:
			printf(", field : 2 fields of a frame are passed in separate buffers (the older one first)");
			break;
		case V4L2_FIELD_INTERLACED_TB:
			printf(", field : images contain both fields interleaved line by line top field first (top field transmitted first)");
			break;
		case V4L2_FIELD_INTERLACED_BT:
			printf(", field : images contain both fields interleaved line by line top field first (bottom field transmitted first)");
			break;
		default:
			printf(", field : unknown");
			break;
		}
	}

	if ((v_fmt.type == V4L2_BUF_TYPE_VIDEO_OVERLAY)||(v_fmt.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY))
	{
		printf(", window x coordinate : %u, y coordinate : %u, width : %u, height : %u, pixel value for the chroma key : %u", 
			v_fmt.fmt.win.w.left, v_fmt.fmt.win.w.top, v_fmt.fmt.win.w.width, v_fmt.fmt.win.w.height, v_fmt.fmt.win.chromakey);
	}
}

void CleanUp(void)
{
	free(buffers);
	close(fd);
}

void CleanUpWithoutMem(void)
{
	close(fd);
}

int main()
{
	int i = 0, j = 0, k = 0, ki = 0, kj = 0;
	int bStop = 0;
	unsigned int count = 0;
	size_t size = 0, page_size = 0;
	char szFilename[256];
	uInt fourcc;
	struct v4l2_format v_fmt_saved;

	// Parameters.
	char szDevice[256];
	int buffertype = 0;
	int bEnumCapabilities = 0;
	int bEnableShowPriority = 0;
	int bEnablePriorityChoice = 0;
	int priority = 0;
	int bEnableShowCurrentVideoOutput = 0;
	int bEnumVideoOutputs = 0;
	int bEnumAllSupportedVideoOutputsStandards = 0;
	int bEnableVideoOutputChoice = 0;
	int videooutput = 0;
	int bEnableShowCurrentVideoInput = 0;
	int bEnumVideoInputs = 0;
	int bEnumAllSupportedVideoInputsStandards = 0;
	int bEnableVideoInputChoice = 0;
	int videoinput = 0;
	int bEnableShowSensedVideoStandard = 0;
	int bEnableShowCurrentVideoStandard = 0;
	int bEnableVideoStandardChoice = 0;
	unsigned long long standard = 0;
	int bResetCrop = 0;
	int bEnableShowCurrentVideoDataFormat = 0;
	int bEnumSupportedVideoDataFormats = 0;
	int bEnumSupportedVideoFrameSizes = 0;
	int enum_resolutions_method = 0;
	int min_resolution_width = 0;
	int max_resolution_width = 0;
	int min_resolution_height = 0;
	int max_resolution_height = 0;
	int step = 0;
	int bEnumSupportedVideoFrameIntervals = 0;
	int bEnableVideoDataFormatChoice = 0;
	char szFourCC[5];
	int width = 0;
	int height = 0;
	int field = 0;
	int bEnumStandardControls = 0;
	int bEnumSpecificControls = 0;
	int bEnableStandardControlsChanges = 0;
	int brightness = 0;
	int contrast = 0;
	int saturation = 0;
	int hue = 0;
	int whitebalancetemperature = 0;
	int redbalance = 0;
	int bluebalance = 0;
	int gamma = 0;
	int gain = 0;
	int sharpness = 0;
	int backlight_compensation = 0;
	int powerfreqfilter = 0;
	int bHorizontalFlip = 0;
	int bVerticalFlip = 0;
	int bEnableAutoHue = 0;
	int bEnableAutoWhiteBalance = 0;
	int bEnableAutoGain = 0;
	int bUseOptimizedCaptureLoop = 0;
	unsigned int maxcount = 0;
	int memorytype = 0;
	int nbframes = 0;
	int bForceUserValues = 0;
	unsigned int minbuffersize = 0;

	// Options to be tweaked by user : 
	// _ szDevice = /dev/video0 ([IMPORTANT] from /dev/video0 to /dev/video63 for a webcam)
	// _ buffertype = V4L2_BUF_TYPE_VIDEO_CAPTURE (usually V4L2_BUF_TYPE_VIDEO_CAPTURE for a webcam, possible values 
	// are V4L2_BUF_TYPE_VIDEO_CAPTURE (1), V4L2_BUF_TYPE_VIDEO_OUTPUT (2), V4L2_BUF_TYPE_VIDEO_OVERLAY (3), 
	// V4L2_BUF_TYPE_VBI_CAPTURE (4), V4L2_BUF_TYPE_VBI_OUTPUT (5), V4L2_BUF_TYPE_SLICED_VBI_CAPTURE (6), 
	// V4L2_BUF_TYPE_SLICED_VBI_OUTPUT (7), V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY (8) or >= V4L2_BUF_TYPE_PRIVATE (128)
	// for specific values, depending on the capabilities of the device)
	// _ bEnumCapabilities = 1 (1 to enable, 0 to disable)
	// _ bEnableShowPriority = 0 (1 to enable, 0 to disable)
	// _ bEnablePriorityChoice = 0 (1 to enable, 0 to disable)
	// _ priority = V4L2_PRIORITY_DEFAULT (V4L2_PRIORITY_UNSET (0), V4L2_PRIORITY_BACKGROUND (1, lowest priority), 
	// V4L2_PRIORITY_INTERACTIVE (2, medium priority), V4L2_PRIORITY_DEFAULT (2, medium priority) or V4L2_PRIORITY_RECORD 
	// (3, highest priority), valid only if bEnablePriorityChoice is enabled)
	// _ bEnableShowCurrentVideoOutput = 0 (1 to enable, 0 to disable)
	// _ bEnumVideoOutputs = 0 (1 to enable, 0 to disable)
	// _ bEnumAllSupportedVideoOutputsStandards = 0 (1 to enable, 0 to disable)
	// _ bEnableVideoOutputChoice = 0 (1 to enable, 0 to disable)
	// _ videooutput = 0 (usually not used for a webcam, valid only if bEnableVideoOutputChoice is enabled)
	// _ bEnableShowCurrentVideoInput = 1 (1 to enable, 0 to disable)
	// _ bEnumVideoInputs = 1 (1 to enable, 0 to disable)
	// _ bEnumAllSupportedVideoInputsStandards = 1 (1 to enable, 0 to disable)
	// _ bEnableVideoInputChoice = 1 (1 to enable, 0 to disable)
	// _ videoinput = 1 ([IMPORTANT FOR TV CARDS] usually not used for a webcam, depends on the device if it has several inputs, 
	// starts at 0, valid only if bEnableVideoInputChoice is enabled)
	// _ bEnableShowSensedVideoStandard = 1 (1 to enable, 0 to disable)
	// _ bEnableShowCurrentVideoStandard = 1 (1 to enable, 0 to disable)
	// _ bEnableVideoStandardChoice = 1 (1 to enable, 0 to disable)
	// _ standard = V4L2_STD_PAL (usually not used for a webcam, enable the enumeration of supported standards to see a 
	// list of values, you should tweak this setting if you have a TV card and the image has colors or synchronization problems, 
	// valid only if bEnableVideoStandardChoice is enabled)
	// _ bResetCrop = 1 (1 to enable, 0 to disable)
	// _ bEnableShowCurrentVideoDataFormat = 1 (1 to enable, 0 to disable)
	// _ bEnumSupportedVideoDataFormats = 1 (1 to enable, 0 to disable, valid only if bEnableShowCurrentVideoDataFormat is enabled)
	// _ bEnumSupportedVideoFrameSizes = 0 (1 to enable, 0 to disable, valid only if bEnumSupportedVideoDataFormats is enabled)
	// _ enum_resolutions_method = 1 (3 methods are possible : 0, 1 or 2, valid only if bEnumSupportedVideoFrameSizes is enabled)
	// _ min_resolution_width = 32 (change in case you suspect your device could use resolutions beyond these bounds, 
	// only used if enum_resolutions_method = 0 or 2)
	// _ max_resolution_width = 2048 (change in case you suspect your device could use resolutions beyond these bounds, 
	// only used if enum_resolutions_method = 0 or 2)
	// _ min_resolution_height = 32 (change in case you suspect your device could use resolutions beyond these bounds, 
	// only used if enum_resolutions_method = 0 or 2)
	// _ max_resolution_height = 2048 (change in case you suspect your device could use resolutions beyond these bounds, 
	// only used if enum_resolutions_method = 0 or 2)
	// _ step = 1 (width and height steps for the resolution enumeration, lower until you find a supported resolution if needed, 
	// or increase if the enumeration is too long, only used if enum_resolutions_method = 0 or 2)
	// _ bEnumSupportedVideoFrameIntervals = 0 (1 to enable, 0 to disable, valid only if bEnumSupportedVideoFrameSizes is 
	// enabled and enum_resolutions_method = 1)
	// _ bEnableVideoDataFormatChoice = 1 (1 to enable, 0 to disable)
	// _ szFourCC = YUYV ([IMPORTANT] four character code (include space if needed) : PAL8, RGB1, R444, RGBO, RGBP, RGBQ, 
	// RGBR, BGR3, RGB3, BGR4, RGB4, BA81, BA82, Y444, YUVO, YUVP, YUV4, GREY, Y16 , YUYV, UYVY, Y41P, YV12, YU12, YVU9, 
	// YUV9, 422P, 411P, NV12, NV21, JPEG, MPEG, dvsd, E625, HI24, HM12, MJPG, PWC1, PWC2, S910, WNVA or YYUV, 
	// enable the enumeration of supported video data formats to see a list of values, you should tweak this setting 
	// if you get black images, valid only if bEnableVideoDataFormatChoice is enabled)
	// _ width = 320 (one of the most common resolution is 320x240 pixels, might be 160x120, 320x240, 640x480, 800x600..., 
	// the driver should adjust it automatically to match device capabilities, valid only if bEnableResolutionChoice is enabled)
	// _ height = 240 (one of the most common resolution is 320x240 pixels, might be 160x120, 320x240, 640x480, 800x600..., 
	// the driver should adjust it automatically to match device capabilities, valid only if bEnableResolutionChoice is enabled)
	// _ field = V4L2_FIELD_ANY (V4L2_FIELD_ANY (0), V4L2_FIELD_NONE (1), V4L2_FIELD_TOP (2), V4L2_FIELD_BOTTOM (3), 
	// V4L2_FIELD_INTERLACED (4), V4L2_FIELD_SEQ_TB (5), V4L2_FIELD_SEQ_BT (6), V4L2_FIELD_ALTERNATE (7), 
	// V4L2_FIELD_INTERLACED_TB (8) or V4L2_FIELD_INTERLACED_BT (9), valid only if bEnableVideoDataFormatChoice is enabled 
	// and used only when different from V4L2_FIELD_ANY)
	// _ bEnumStandardControls = 1 (1 to enable, 0 to disable)
	// _ bEnumSpecificControls = 0 (1 to enable, 0 to disable)
	// _ bEnableStandardControlsChanges = 0 (1 to enable, 0 to disable)
	// _ brightness = 128 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ contrast = 128 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ saturation = 128 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ hue = 128 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ whitebalancetemperature = 128 (enable the enumeration of standard controls to see possible values, 
	// valid only if bEnableStandardControlsChanges is enabled)
	// _ redbalance = 128 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ bluebalance = 128 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ gamma = 128 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ gain = 128 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ sharpness = 128 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ backlight_compensation = 0 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ powerfreqfilter = 0 (enable the enumeration of standard controls to see possible values, valid only if 
	// bEnableStandardControlsChanges is enabled)
	// _ bHorizontalFlip = 0 (1 to enable, 0 to disable, valid only if bEnableStandardControlsChanges is enabled)
	// _ bVerticalFlip = 0 (1 to enable, 0 to disable, valid only if bEnableStandardControlsChanges is enabled)
	// _ bEnableAutoHue = 1 (1 to enable, 0 to disable, valid only if bEnableStandardControlsChanges is enabled)
	// _ bEnableAutoWhiteBalance = 1 (1 to enable, 0 to disable, valid only if bEnableStandardControlsChanges is enabled)
	// _ bEnableAutoGain = 1 (1 to enable, 0 to disable, valid only if bEnableStandardControlsChanges is enabled)
	// _ bUseOptimizedCaptureLoop = 1 (1 to enable, 0 to disable, change if you do not get any image)
	// _ maxcount = 100 (number of frames to capture)
	// _ memorytype = V4L2_MEMORY_MMAP (2 modes are available if bUseOptimizedCaptureLoop = 1 : V4L2_MEMORY_MMAP (1) 
	// or V4L2_MEMORY_USERPTR (2), change if you do not get any image)
	// _ nbframes = 4 (number of frames to buffer)
	// _ bForceUserValues = 0 (1 to enable, 0 to disable, might be useful if the webcam driver is not responding correctly)
	// _ minbuffersize = 0 (used if the application cannot convert the data from the camera and if different from 0, otherwise 
	// a default value is tried)
	sprintf(szDevice, "/dev/video0");
	buffertype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bEnumCapabilities = 1;
	bEnableShowPriority = 0;
	bEnablePriorityChoice = 0;
	priority = V4L2_PRIORITY_DEFAULT;
	bEnableShowCurrentVideoOutput = 0;
	bEnumVideoOutputs = 0;
	bEnumAllSupportedVideoOutputsStandards = 0;
	bEnableVideoOutputChoice = 0;
	videooutput = 0;
	bEnableShowCurrentVideoInput = 1;
	bEnumVideoInputs = 1;
	bEnumAllSupportedVideoInputsStandards = 1;
	bEnableVideoInputChoice = 1;
	videoinput = 1;
	bEnableShowSensedVideoStandard = 1;
	bEnableShowCurrentVideoStandard = 1;
	bEnableVideoStandardChoice = 1;
	standard = V4L2_STD_PAL;
	bResetCrop = 1;
	bEnableShowCurrentVideoDataFormat = 1;
	bEnumSupportedVideoDataFormats = 1;
	bEnumSupportedVideoFrameSizes = 0;
	enum_resolutions_method = 1;
	min_resolution_width = 32;
	max_resolution_width = 2048;
	min_resolution_height = 32;
	max_resolution_height = 2048;
	step = 1;
	bEnumSupportedVideoFrameIntervals = 0;
	bEnableVideoDataFormatChoice = 1;
	sprintf(szFourCC, "YUYV");
	width = 320;
	height = 240;
	field = V4L2_FIELD_ANY;
	bEnumStandardControls = 1;
	bEnumSpecificControls = 0;
	bEnableStandardControlsChanges = 0;
	brightness = 128;
	contrast = 128;
	saturation = 128;
	hue = 128;
	whitebalancetemperature = 128;
	redbalance = 128;
	bluebalance = 128;
	gamma = 128;
	gain = 128;
	sharpness = 128;
	backlight_compensation = 0;
	powerfreqfilter = 0;
	bHorizontalFlip = 0;
	bVerticalFlip = 0;
	bEnableAutoHue = 1;
	bEnableAutoWhiteBalance = 1;
	bEnableAutoGain = 1;
	bUseOptimizedCaptureLoop = 1;
	maxcount = 100;
	memorytype = V4L2_MEMORY_MMAP;
	nbframes = 4;
	bForceUserValues = 0;
	minbuffersize = 0;

	// Open the desired device.
	fd = open(szDevice, O_RDWR); // O_NONBLOCK is possible if we do not want read() or the ioctl() VIDIOC_DQBUF to block.
	if (fd == -1)
	{
		printf("open() error : %s. \n", strerror(errno));
		return EXIT_FAILURE;
	}	
	usleep(100000);

	if (bEnumCapabilities)
	{
		// Query device capabilities.
		memset(&v_cap, 0, sizeof(v_cap));
		if (ioctl(fd, VIDIOC_QUERYCAP, &v_cap) == -1)
		{
			printf("ioctl() VIDIOC_QUERYCAP error : %s. \n", strerror(errno));
			return EXIT_FAILURE;
		}
		printf("Video device name : %s, bus info : %s, driver name : %s, driver version : %u, capabilities identifier : %u", 
			v_cap.card, v_cap.bus_info, v_cap.driver, v_cap.version, v_cap.capabilities);
		if (v_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) printf(", is a video capture device");
		if (v_cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) printf(", is a video output device");
		if (v_cap.capabilities & V4L2_CAP_VIDEO_OVERLAY) printf(", can do video overlay");
		if (v_cap.capabilities & V4L2_CAP_VBI_CAPTURE) printf(", is a raw VBI capture device (Teletext and Closed Caption data)");
		if (v_cap.capabilities & V4L2_CAP_VBI_OUTPUT) printf(", is a raw VBI output device");
		if (v_cap.capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) printf(", is a sliced VBI capture device");
		if (v_cap.capabilities & V4L2_CAP_SLICED_VBI_OUTPUT) printf(", is a sliced VBI output device");
		if (v_cap.capabilities & V4L2_CAP_RDS_CAPTURE) printf(", supports RDS data capture");
		if (v_cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_OVERLAY) printf(", can do video output overlay (OSD)");
		if (v_cap.capabilities & V4L2_CAP_HW_FREQ_SEEK) printf(", can do hardware frequency seek");
		if (v_cap.capabilities & V4L2_CAP_RDS_OUTPUT) printf(", is an RDS encoder");
		if (v_cap.capabilities & V4L2_CAP_TUNER) printf(", has a tuner");
		if (v_cap.capabilities & V4L2_CAP_AUDIO) printf(", has audio support");
		if (v_cap.capabilities & V4L2_CAP_RADIO) printf(", is a radio device");
		if (v_cap.capabilities & V4L2_CAP_MODULATOR) printf(", has a modulator");
		if (v_cap.capabilities & V4L2_CAP_READWRITE) printf(", supports the read() and/or write() I/O methods");
		if (v_cap.capabilities & V4L2_CAP_ASYNCIO) printf(", supports the asynchronous I/O methods");
		if (v_cap.capabilities & V4L2_CAP_STREAMING) printf(", supports the streaming I/O methods");
		printf(". \n");
		usleep(100000);
	}

	if (bEnableShowPriority)
	{
		// Application access priority.
		if (ioctl(fd, VIDIOC_G_PRIORITY, &v_prio) == -1)
		{
			printf("ioctl() VIDIOC_G_PRIORITY warning : %s. \n", strerror(errno));
		}
		else
		{
			printf("Video device application current priority : %d. \n", (int)v_prio);
		}
		usleep(100000);
	}

	if (bEnablePriorityChoice)
	{
		// Set the desired priority.
		v_prio = (enum v4l2_priority)priority;
		if (ioctl(fd, VIDIOC_S_PRIORITY, &v_prio) == -1)
		{
			printf("ioctl() VIDIOC_S_PRIORITY warning : %s. \n", strerror(errno));
		}
		usleep(100000);
		// Check if the priority requested was accepted.
		if (ioctl(fd, VIDIOC_G_PRIORITY, &v_prio) == -1)
		{
			printf("ioctl() VIDIOC_G_PRIORITY warning : %s. \n", strerror(errno));

			// Values that will be used in what follows.
			v_prio = (enum v4l2_priority)priority;
		}
		else
		{
			if ((int)v_prio != priority)
			{
				printf("An unsupported or currently unavailable priority was specified. \n");
			}
		}
		usleep(100000);
		printf("Priority used : %d. \n", (int)v_prio);
	}

	if (bEnableShowCurrentVideoOutput)
	{
		// Query the current video output.
		videooutputarg = 0;
		if (ioctl(fd, VIDIOC_G_OUTPUT, &videooutputarg) == -1)
		{
			printf("ioctl() VIDIOC_G_OUTPUT warning : %s. \n", strerror(errno));
		}
		else
		{
			printf("Video device current output number : %u. \n", videooutputarg);
		}
		usleep(100000);
	}

	if (bEnumVideoOutputs)
	{
		// Enumerate video outputs.
		i = 0;
		for (;;)
		{
			// Select video output for which we want to get information.
			memset(&v_out, 0, sizeof(v_out));
			v_out.index = i;
			if (ioctl(fd, VIDIOC_ENUMOUTPUT, &v_out) == -1) break;
			usleep(100000);
			printf("Device video output number : %u, name : %s, type identifier : %u, associated modulator number (if modulator) : %u", 
				v_out.index, v_out.name, v_out.type, v_out.modulator);
			if (v_out.type == V4L2_OUTPUT_TYPE_MODULATOR) printf(", is an analog TV modulator");
			if (v_out.type == V4L2_OUTPUT_TYPE_ANALOG) printf(", is analog baseband");
			if (v_out.type == V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY) printf(", is analog with VGA overlay");
			if (v_out.capabilities & V4L2_OUT_CAP_NATIVE_SIZE) printf(", supports setting native size");
			if (v_out.capabilities & V4L2_OUT_CAP_DV_TIMINGS) printf(", supports S_DV_TIMINGS");
			if (v_out.capabilities & V4L2_OUT_CAP_STD) printf(", supports S_STD (standard choice)");
			if (bEnumAllSupportedVideoOutputsStandards)
			{
				printf(", supported standards : \n");
				print_video_standards(v_out.std);
			}
			else
			{
				printf(". \n");
			}
			i++;
		}
		usleep(100000);
		printf("\n");
	}

	if (bEnableVideoOutputChoice)
	{
		// Choose a video output.
		videooutputarg = videooutput;
		if (ioctl(fd, VIDIOC_S_OUTPUT, &videooutputarg) == -1)
		{
			printf("ioctl() VIDIOC_S_OUTPUT warning : %s. \n", strerror(errno));
		}
		usleep(100000);
		// Check if the settings requested were accepted.
		videooutputarg = 0;
		if (ioctl(fd, VIDIOC_G_OUTPUT, &videooutputarg) == -1)
		{
			printf("ioctl() VIDIOC_G_OUTPUT warning : %s. \n", strerror(errno));

			// Values that will be used in what follows.
			videooutputarg = videooutput;
		}
		else
		{
			if (videooutputarg != videooutput)
			{
				printf("An unsupported video output setting was specified. \n");
			}
		}
		usleep(100000);
		printf("Video output used : %d. \n", videooutputarg);
	}

	if (bEnableShowCurrentVideoInput)
	{
		// Query the current video input.
		videoinputarg = 0;
		if (ioctl(fd, VIDIOC_G_INPUT, &videoinputarg) == -1)
		{
			printf("ioctl() VIDIOC_G_INPUT warning : %s. \n", strerror(errno));
		}
		else
		{
			printf("Video device current input number : %u. \n", videoinputarg);
		}
		usleep(100000);
	}

	if (bEnumVideoInputs)
	{
		// Enumerate video inputs.
		i = 0;
		for (;;)
		{
			// Select video input for which we want to get information.
			memset(&v_in, 0, sizeof(v_in));
			v_in.index = i;
			if (ioctl(fd, VIDIOC_ENUMINPUT, &v_in) == -1) break;
			usleep(100000);
			printf("Device video input number : %u, name : %s, type identifier : %u, associated tuner number (if tuner) : %u", 
				v_in.index, v_in.name, v_in.type, v_in.tuner);
			if (v_in.type & V4L2_INPUT_TYPE_TUNER) printf(", use a tuner");
			if (v_in.type & V4L2_INPUT_TYPE_CAMERA) printf(", is analog baseband");
			if (v_in.capabilities & V4L2_IN_CAP_NATIVE_SIZE) printf(", supports setting native size");
			if (v_in.capabilities & V4L2_IN_CAP_DV_TIMINGS) printf(", supports S_DV_TIMINGS");
			if (v_in.capabilities & V4L2_IN_CAP_STD) printf(", supports S_STD (standard choice)");
			if (bEnumAllSupportedVideoInputsStandards)
			{
				printf(", supported standards : \n");
				print_video_standards(v_in.std);
			}
			else
			{
				printf(". \n");
			}
			i++;
		}
		usleep(100000);
		printf("\n");
	}

	if (bEnableVideoInputChoice)
	{
		// Choose a video input (usually 0 for a webcam).
		videoinputarg = videoinput;
		if (ioctl(fd, VIDIOC_S_INPUT, &videoinputarg) == -1)
		{
			printf("ioctl() VIDIOC_S_INPUT warning : %s. \n", strerror(errno));
		}
		usleep(100000);
		// Check if the settings requested were accepted.
		videoinputarg = 0;
		if (ioctl(fd, VIDIOC_G_INPUT, &videoinputarg) == -1)
		{
			printf("ioctl() VIDIOC_G_INPUT warning : %s. \n", strerror(errno));

			// Values that will be used in what follows.
			videoinputarg = videoinput;
		}
		else
		{
			if (videoinputarg != videoinput)
			{
				printf("An unsupported video input setting was specified. \n");
			}
		}
		usleep(100000);
		printf("Video input used : %d. \n", videoinputarg);
		// Get current video input additional information.
		memset(&v_in, 0, sizeof(v_in));
		v_in.index = videoinputarg;
		if (ioctl(fd, VIDIOC_ENUMINPUT, &v_in) == -1)
		{
			printf("ioctl() VIDIOC_ENUMINPUT warning : %s. \n", strerror(errno));
		}
		else
		{
			printf("Device video input number : %u, name : %s, type identifier : %u, associated tuner number (if tuner) : %u", 
				v_in.index, v_in.name, v_in.type, v_in.tuner);
			if (v_in.type & V4L2_INPUT_TYPE_TUNER) printf(", use a tuner");
			if (v_in.type & V4L2_INPUT_TYPE_CAMERA) printf(", is analog baseband");
			// The following information should only be valid if it is the current input.
			if (v_in.status & V4L2_IN_ST_NO_POWER) printf(", has attached device off");
			if (v_in.status & V4L2_IN_ST_NO_SIGNAL) printf(", does not detect the signal");
			if (v_in.status & V4L2_IN_ST_NO_COLOR) printf(", supports color decoding, but does not detect color modulation in the signal");
			if (v_in.status & V4L2_IN_ST_HFLIP) printf(", has frames flipped horizontally");
			if (v_in.status & V4L2_IN_ST_VFLIP) printf(", has frames flipped vertically");
			if (v_in.status & V4L2_IN_ST_NO_H_LOCK) printf(", has no horizontal sync lock");
			if (v_in.status & V4L2_IN_ST_COLOR_KILL) printf(", has color killer circuit enabled that has shut off color decoding");
			if (v_in.status & V4L2_IN_ST_NO_SYNC) printf(", has no synchronization lock");
			if (v_in.status & V4L2_IN_ST_NO_EQU) printf(", has no equalizer lock");
			if (v_in.status & V4L2_IN_ST_NO_CARRIER) printf(", carrier recovery failed");
			if (v_in.status & V4L2_IN_ST_MACROVISION) printf(", has Macrovision (copy prevention system) detected");
			if (v_in.status & V4L2_IN_ST_NO_ACCESS) printf(", conditional access denied");
			if (v_in.status & V4L2_IN_ST_VTR) printf(", VTR time constant");
			printf(". \n");
		}
		usleep(100000);
	}

	if (bEnableShowSensedVideoStandard)
	{
		// Try to sense the video standard received by the current input.
		v_std_id = 0;
		if (ioctl(fd, VIDIOC_QUERYSTD, &v_std_id) == -1)
		{
			printf("ioctl() VIDIOC_QUERYSTD warning : %s. \n", strerror(errno));
		}
		else
		{
			printf("Video device possible sensed standards : \n");
			print_video_standards(v_std_id);
		}
		usleep(100000);
	}

	if (bEnableShowCurrentVideoStandard)
	{
		// Query the current video standard.
		v_std_id = 0;
		if (ioctl(fd, VIDIOC_G_STD, &v_std_id) == -1)
		{
			printf("ioctl() VIDIOC_G_STD warning : %s. \n", strerror(errno));
		}
		else
		{
			printf("Video device current standard : \n");
			print_video_standards(v_std_id);
		}
		usleep(100000);
	}

	if (bEnableVideoStandardChoice)
	{
		// Choose a video standard.
		v_std_id = standard;
		if (ioctl(fd, VIDIOC_S_STD, &v_std_id) == -1)
		{
			printf("ioctl() VIDIOC_S_STD warning : %s. \n", strerror(errno));
		}
		usleep(100000);
		// Check if the settings requested were accepted.
		v_std_id = 0;
		if (ioctl(fd, VIDIOC_G_STD, &v_std_id) == -1)
		{
			printf("ioctl() VIDIOC_G_STD warning : %s. \n", strerror(errno));

			// Values that will be used in what follows.
			v_std_id = standard;
		}
		else
		{
			if (v_std_id != standard)
			{
				printf("An unsupported video standard setting was specified. \n");
			}
		}
		usleep(100000);
		printf("Video standard used : \n");
		print_video_standards(v_std_id);
	}

	// Should also add user parameters for cropping settings...

	if (bResetCrop)
	{
		memset(&v_cropcap, 0, sizeof(v_cropcap));
		v_cropcap.type = (enum v4l2_buf_type)buffertype;
		if (ioctl(fd, VIDIOC_CROPCAP, &v_cropcap) == -1)
		{
			printf("ioctl() VIDIOC_CROPCAP warning : %s. \n", strerror(errno));
		}
		else
		{
			usleep(100000);
			// Reset cropping settings to default.
			memset(&v_crop, 0, sizeof(v_crop));
			v_crop.type = (enum v4l2_buf_type)buffertype;
			v_crop.c = v_cropcap.defrect; 
			if (ioctl(fd, VIDIOC_S_CROP, &v_crop) == -1)
			{
				printf("ioctl() VIDIOC_S_CROP warning : %s. \n", strerror(errno));
			}
			usleep(100000);
			// Check if the settings requested were accepted.
			memset(&v_crop, 0, sizeof(v_crop));
			if (ioctl(fd, VIDIOC_G_CROP, &v_crop) == -1)
			{
				printf("ioctl() VIDIOC_G_CROP warning : %s. \n", strerror(errno));
			}
			else
			{
				if ((v_crop.c.top != v_cropcap.defrect.top)||(v_crop.c.left != v_cropcap.defrect.left)||
					(v_crop.c.width != v_cropcap.defrect.width)||(v_crop.c.height != v_cropcap.defrect.height)
					)
				{
					printf("Cannot reset cropping settings. \n");
				}
			}
			usleep(100000);
		}
		usleep(100000);
	}

	if (bEnableShowCurrentVideoDataFormat)
	{
		// Query the current video data format.
		memset(&v_fmt, 0, sizeof(v_fmt));
		v_fmt.type = (enum v4l2_buf_type)buffertype;
		if (ioctl(fd, VIDIOC_G_FMT, &v_fmt) == -1)
		{
			printf("ioctl() VIDIOC_G_FMT warning : %s. \n", strerror(errno));
		}
		else
		{
			usleep(100000);
			printf("Video device current data format type identifier : %d", (int)v_fmt.type);
			print_video_format();
			printf(". \n");

			// ioctl() VIDIOC_ENUM_FRAMESIZES and VIDIOC_ENUM_FRAMEINTERVALS are indicated as experimental...
			// VIDIOC_TRY_FMT can be used instead. However, VIDIOC_TRY_FMT is an optional ioctl()...
			// We can call VIDIOC_S_FMT, but it is not clear whether it is good to call it
			// several times without reopening the device...

			if (bEnumSupportedVideoDataFormats)
			{
				if ((bEnumSupportedVideoFrameSizes)&&(enum_resolutions_method != 1))
				{
					// Save the current settings because the enumeration might change them.
					memcpy(&v_fmt_saved, &v_fmt, sizeof(v_fmt));
				}
				// Enumerate supported video data formats.
				printf("Video device supported data formats : \n");
				i = 0;
				for (;;)
				{
					// Select video data format for which we want to get information.
					memset(&v_fmtdesc, 0, sizeof(v_fmtdesc));
					v_fmtdesc.type = (enum v4l2_buf_type)buffertype;
					v_fmtdesc.index = i;
					if (ioctl(fd, VIDIOC_ENUM_FMT, &v_fmtdesc) == -1) break;
					fourcc.v = v_fmtdesc.pixelformat;
					printf("     %s (format number : %u, type identifier : %d, FOURCC identifier : %u, FOURCC code : %c%c%c%c", 
						v_fmtdesc.description, v_fmtdesc.index, (int)v_fmtdesc.type, 
						v_fmtdesc.pixelformat, fourcc.c[0], fourcc.c[1], fourcc.c[2], fourcc.c[3]);
					if (v_fmtdesc.type == V4L2_BUF_TYPE_VIDEO_CAPTURE) printf(", video capture type");
					if (v_fmtdesc.type == V4L2_BUF_TYPE_VIDEO_OUTPUT) printf(", video output type");
					if (v_fmtdesc.type == V4L2_BUF_TYPE_VIDEO_OVERLAY) printf(", video overlay type");
					if (v_fmtdesc.type == V4L2_BUF_TYPE_VBI_CAPTURE) printf(", VBI capture type");
					if (v_fmtdesc.type == V4L2_BUF_TYPE_VBI_OUTPUT) printf(", VBI output type");
					if (v_fmtdesc.type == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE) printf(", sliced VBI capture type");
					if (v_fmtdesc.type == V4L2_BUF_TYPE_SLICED_VBI_OUTPUT) printf(", sliced VBI output type");
					if (v_fmtdesc.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY) printf(", video output overlay type");
					if (v_fmtdesc.type >= V4L2_BUF_TYPE_PRIVATE) printf(", custom type");
					if (v_fmtdesc.flags & V4L2_FMT_FLAG_COMPRESSED) printf(", compressed format");
					if (v_fmtdesc.flags & V4L2_FMT_FLAG_EMULATED) printf(", emulated format");
					printf(")\n");
					if (bEnumSupportedVideoFrameSizes)
					{
						// Enumerate supported video frame sizes.
						printf("          Supported resolutions : \n");
						if (enum_resolutions_method == 1)
						{
							bStop = 0;
							j = 0;
							for (;;)
							{
								// Select video frame format and size for which we want to get information.
								memset(&v_frmsize, 0, sizeof(v_frmsize));
								v_frmsize.pixel_format = v_fmtdesc.pixelformat;
								v_frmsize.index = j;
								if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &v_frmsize) == -1) break;
								if (v_frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) 
								{
									printf("               %ux%u", v_frmsize.discrete.width, v_frmsize.discrete.height);
								}
								if (v_frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS)
								{
									printf("               %u..%ux%u..%u", 
										v_frmsize.stepwise.min_width, v_frmsize.stepwise.max_width, 
										v_frmsize.stepwise.min_height, v_frmsize.stepwise.max_height);
									// The enumeration should be finished.
									bStop = 1;
								}
								if (v_frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE)
								{
									printf("               {%u:%u:%u}x{%u:%u:%u}", 
										v_frmsize.stepwise.min_width, v_frmsize.stepwise.step_width, v_frmsize.stepwise.max_width, 
										v_frmsize.stepwise.min_height, v_frmsize.stepwise.step_height, v_frmsize.stepwise.max_height);
									// The enumeration should be finished.
									bStop = 1;
								}
								if (bEnumSupportedVideoFrameIntervals)
								{
									// Enumerate supported video frame intervals.
									k = 0;
									for (;;)
									{
										// Select video frame format, size and interval for which we want to get information.
										memset(&v_frmival, 0, sizeof(v_frmival));
										if (v_frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) 
										{
											v_frmival.width = v_frmsize.discrete.width;
											v_frmival.height = v_frmsize.discrete.height;
										}
										if (v_frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS)
										{
											v_frmival.width = v_frmsize.stepwise.max_width;
											v_frmival.height = v_frmsize.stepwise.max_height;
										}
										if (v_frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE)
										{
											v_frmival.width = v_frmsize.stepwise.max_width;
											v_frmival.height = v_frmsize.stepwise.max_height;
										}
										v_frmival.pixel_format = v_fmtdesc.pixelformat;
										v_frmival.index = k;
										if (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &v_frmival) == -1) break;
										if (k == 0) printf("@"); else printf("|"); 
										// The frame rate is displayed (1/frame_interval).
										if (v_frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) 
										{
											printf("%dHz", (int)((double)v_frmival.discrete.denominator/(double)v_frmival.discrete.numerator));
										}
										if (v_frmival.type == V4L2_FRMIVAL_TYPE_CONTINUOUS)
										{
											printf("%d..%dHz", 
												(int)((double)v_frmival.stepwise.max.denominator/(double)v_frmival.stepwise.max.numerator), 
												(int)((double)v_frmival.stepwise.min.denominator/(double)v_frmival.stepwise.min.numerator));
											break; // The enumeration should be finished.
										}
										if (v_frmival.type == V4L2_FRMIVAL_TYPE_STEPWISE) 
										{
											printf("{%dHz:%u/%us:%dHz}", 
												(int)((double)v_frmival.stepwise.max.denominator/(double)v_frmival.stepwise.max.numerator), 
												v_frmival.stepwise.step.numerator, v_frmival.stepwise.step.denominator, 
												(int)((double)v_frmival.stepwise.min.denominator/(double)v_frmival.stepwise.min.numerator));
											break; // The enumeration should be finished.
										}
										k++;
									}
								}
								printf("\n");
								if (bStop) break;
								j++;
							}
						}
						else
						{
							// Try all resolutions.
							for (ki = min_resolution_height; ki <= max_resolution_height; ki += step)
							{
								for (kj = min_resolution_width; kj <= max_resolution_width; kj += step)
								{
									memset(&v_fmt, 0, sizeof(v_fmt));
									v_fmt.type = (enum v4l2_buf_type)buffertype;
									v_fmt.fmt.pix.width = (__u32)kj;
									v_fmt.fmt.pix.height = (__u32)ki; 
									v_fmt.fmt.pix.pixelformat = v_fmtdesc.pixelformat;
									v_fmt.fmt.pix.field = (enum v4l2_field)field;
									// Should not use VIDIOC_G_FMT to check after a VIDIOC_TRY_FMT and VIDIOC_S_FMT 
									// should do the same as VIDIOC_G_FMT if success...
									if ((ioctl(fd, enum_resolutions_method==2?VIDIOC_TRY_FMT:VIDIOC_S_FMT, &v_fmt) != -1)&&
										((int)v_fmt.fmt.pix.width == kj)&&((int)v_fmt.fmt.pix.height == ki)&&
										((int)v_fmt.type == buffertype)&&(v_fmt.fmt.pix.pixelformat == v_fmtdesc.pixelformat)&&
										(((int)v_fmt.fmt.pix.field == field)||(field == (int)V4L2_FIELD_ANY)))
									{
										printf("               %ux%u\n", v_fmt.fmt.pix.width, v_fmt.fmt.pix.height);
									}
								}
							}
						}
					}
					i++;
				}
				usleep(100000);
				printf("\n");
				if ((bEnumSupportedVideoFrameSizes)&&(enum_resolutions_method != 1))
				{
					// Try to restore the previous settings.
					memcpy(&v_fmt, &v_fmt_saved, sizeof(v_fmt));
					ioctl(fd, VIDIOC_S_FMT, &v_fmt);
				}
			}
		}
		usleep(100000);
	}

	if (bEnableVideoDataFormatChoice)
	{
		// Query the current video data format.
		memset(&v_fmt, 0, sizeof(v_fmt));
		v_fmt.type = (enum v4l2_buf_type)buffertype;
		if (ioctl(fd, VIDIOC_G_FMT, &v_fmt) == -1)
		{
			printf("ioctl() VIDIOC_G_FMT warning : %s. \n", strerror(errno));

			memset(&v_fmt, 0, sizeof(v_fmt));
		}
		// Choose a video data format.
		v_fmt.type = (enum v4l2_buf_type)buffertype;
		v_fmt.fmt.pix.width = (__u32)width;
		v_fmt.fmt.pix.height = (__u32)height; 
		v_fmt.fmt.pix.pixelformat = v4l2_fourcc(szFourCC[0], szFourCC[1], szFourCC[2], szFourCC[3]);
		if (field != (int)V4L2_FIELD_ANY) v_fmt.fmt.pix.field = (enum v4l2_field)field;
		if (ioctl(fd, VIDIOC_S_FMT, &v_fmt) == -1)
		{
			printf("ioctl() VIDIOC_S_FMT warning : %s. \n", strerror(errno));
		}
		usleep(100000);
		// Check if the settings requested were accepted.
		memset(&v_fmt, 0, sizeof(v_fmt));
		v_fmt.type = (enum v4l2_buf_type)buffertype;
		if (ioctl(fd, VIDIOC_G_FMT, &v_fmt) == -1)
		{
			printf("ioctl() VIDIOC_G_FMT warning : %s. \n", strerror(errno));

			// Values that will be used in what follows.
			v_fmt.type = (enum v4l2_buf_type)buffertype;
			v_fmt.fmt.pix.width = (__u32)width;
			v_fmt.fmt.pix.height = (__u32)height; 
			v_fmt.fmt.pix.pixelformat = v4l2_fourcc(szFourCC[0], szFourCC[1], szFourCC[2], szFourCC[3]);
			v_fmt.fmt.pix.field = (enum v4l2_field)field;
		}
		else
		{
			if (((int)v_fmt.type != buffertype)||
				((int)v_fmt.fmt.pix.width != width)||((int)v_fmt.fmt.pix.height != height)||
				(v_fmt.fmt.pix.pixelformat != v4l2_fourcc(szFourCC[0], szFourCC[1], szFourCC[2], szFourCC[3]))||
				(((int)v_fmt.fmt.pix.field != field)&&(field != (int)V4L2_FIELD_ANY)))
			{
				printf("An unsupported video data format setting was specified. \n");
			}
		}
		usleep(100000);
		printf("Video data format used : %d", (int)v_fmt.type);
		print_video_format();
		printf(". \n");
	}

	if (bEnumStandardControls)
	{
		printf("Video device standard controls : \n");
		// Enumerate standard controls.
		memset(&v_queryctrl, 0, sizeof(v_queryctrl));
		for (v_queryctrl.id = V4L2_CID_BASE; v_queryctrl.id < V4L2_CID_LASTP1; v_queryctrl.id++) 
		{
			if (ioctl(fd, VIDIOC_QUERYCTRL, &v_queryctrl) != -1)
			{
				print_control_info();
			} 
		}
		usleep(100000);
		printf("\n");
	}

	if (bEnumSpecificControls)
	{
		printf("Video device specific controls : \n");
		// Enumerate specific controls.
		memset(&v_queryctrl, 0, sizeof(v_queryctrl));
		for (v_queryctrl.id = V4L2_CID_PRIVATE_BASE;; v_queryctrl.id++) 
		{
			if (ioctl(fd, VIDIOC_QUERYCTRL, &v_queryctrl) == -1)
			{
				if (errno == EINVAL) break;
				else
				{
					printf("ioctl() VIDIOC_QUERYCTRL warning : %s. \n", strerror(errno));
					break;
				}						
			}
			else 
			{
				print_control_info();
			} 
		}
		usleep(100000);
		printf("\n");
	}

	if (bEnableStandardControlsChanges)
	{
		set_control_value(V4L2_CID_BRIGHTNESS, brightness);
		printf("Brightness : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_CONTRAST, contrast);
		printf("Contrast : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_SATURATION, saturation);
		printf("Saturation : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_HUE, hue);
		printf("Hue : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_WHITE_BALANCE_TEMPERATURE, whitebalancetemperature);
		printf("White balance temperature : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_RED_BALANCE, redbalance);
		printf("Red balance : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_BLUE_BALANCE, bluebalance);
		printf("Blue balance : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_GAMMA, gamma);
		printf("Gamma : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_GAIN, gain);
		printf("Gain : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_SHARPNESS, sharpness);
		printf("Sharpness : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_BACKLIGHT_COMPENSATION, backlight_compensation);
		printf("Backlight compensation : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_POWER_LINE_FREQUENCY, powerfreqfilter);
		printf("Power line frequency filter : ");
		switch (v_ctrl.value)
		{
		case V4L2_CID_POWER_LINE_FREQUENCY_50HZ: 
			printf("50Hz");
			break;
		case V4L2_CID_POWER_LINE_FREQUENCY_60HZ: 
			printf("60Hz");
			break;
		default: 
			printf("disabled");
			break;
		}
		printf(". \n");
		set_control_value(V4L2_CID_HFLIP, bHorizontalFlip);
		printf("Horizontal flip : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_VFLIP, bVerticalFlip);
		printf("Vertical flip : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_HUE_AUTO, bEnableAutoHue);
		printf("Auto hue : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_AUTO_WHITE_BALANCE, bEnableAutoWhiteBalance);
		printf("Auto white balance : %d. \n", v_ctrl.value);
		set_control_value(V4L2_CID_AUTOGAIN, bEnableAutoGain);
		printf("Auto gain : %d. \n", v_ctrl.value);
	}

	if (bForceUserValues)
	{
		v_prio = (enum v4l2_priority)priority;
		videooutputarg = videooutput;
		videoinputarg = videoinput;
		v_std_id = standard;
		v_fmt.type = (enum v4l2_buf_type)buffertype;
		v_fmt.fmt.pix.width = (__u32)width;
		v_fmt.fmt.pix.height = (__u32)height; 
		v_fmt.fmt.pix.pixelformat = v4l2_fourcc(szFourCC[0], szFourCC[1], szFourCC[2], szFourCC[3]);
		v_fmt.fmt.pix.field = (enum v4l2_field)field;
		v_fmt.fmt.pix.sizeimage = v_fmt.fmt.pix.width*v_fmt.fmt.pix.height*4; // This should be an upper bound.
		printf("Because you selected to force user parameters instead of driver values, the following values will be used \n"
			"priority : %d, video output number : %d, video input number : %d, standard identifier : %llu, "
			"format type identifier : %d, width : %u, height : %u, FOURCC identifier : %u, FOURCC code : %c%c%c%c, "
			"field identifier : %d, image size : %u"
			//"brightness : %d, hue : %d, colour : %d, contrast : %d, whiteness : %d"
			". \n\n", 
			(int)v_prio, videooutputarg, videoinputarg, v_std_id, 
			(int)v_fmt.type, v_fmt.fmt.pix.width, v_fmt.fmt.pix.height, 
			v_fmt.fmt.pix.pixelformat, fourcc.c[0], fourcc.c[1], fourcc.c[2], fourcc.c[3], 
			(int)field, v_fmt.fmt.pix.sizeimage);
	}

	if (!bUseOptimizedCaptureLoop)
	{
		// Try the simple method with the read() function.
		size = v_fmt.fmt.pix.sizeimage;
		mem = calloc(size, sizeof(unsigned char));
		if (mem == NULL)
		{
			printf("calloc() error : %s. \n", strerror(errno));
			CleanUpWithoutMem();
			return EXIT_FAILURE;
		}

		// Simple capture loop.
		count = 0;
		while (count < maxcount)
		{
			// Maybe we should try to continue to fill the buffer with other calls to read() 
			// if the returned value is lower than size but we must be sure of the size...
			if (read(fd, mem, size) == -1)
			{
				printf("read() error : %s. \n", strerror(errno));
				free(mem);
				CleanUpWithoutMem();
				return EXIT_FAILURE;
			}
			sprintf(szFilename, "%.8d.ppm", count);
			ConvertAndSaveToPPM(szFilename, (unsigned char*)mem, 
				v_fmt.fmt.pix.width, v_fmt.fmt.pix.height, v_fmt.fmt.pix.pixelformat, 
				minbuffersize?minbuffersize:size/256);
			count++;
		}

		free(mem);
	}
	else
	{
		// Allocate device buffers or switch to user pointer I/O mode (depending on memorytype).
		memset(&v_reqbuf, 0, sizeof(v_reqbuf));
		v_reqbuf.type = (enum v4l2_buf_type)buffertype;
		v_reqbuf.memory = (enum v4l2_memory)memorytype;
		v_reqbuf.count = (__u32)nbframes; // Number of device buffers requested (ignored in user pointer I/O mode).
		if (ioctl(fd, VIDIOC_REQBUFS, &v_reqbuf) == -1)
		{
			printf("ioctl() VIDIOC_REQBUFS warning : %s. \n", strerror(errno));
		}
		usleep(100000);

		if (memorytype == V4L2_MEMORY_MMAP)
		{
			printf("Video device buffers allocated : %u. \n", v_reqbuf.count);

			buffers = (struct buffer_t*)calloc(v_reqbuf.count, sizeof(struct buffer_t));
			if (buffers == NULL)
			{
				printf("calloc() error : %s. \n", strerror(errno));
				CleanUpWithoutMem();
				return EXIT_FAILURE;
			}

			// Set memory mappings between device buffers and this program buffers, where the image data should be available.
			// PROT_READ specifies that the memory mapped will only be read from, MAP_SHARED specifies that a 
			// change to the video memory will change the user space memory.
			for (i = 0; i < (int)v_reqbuf.count; i++) 
			{
				memset(&v_buf, 0, sizeof(v_buf));
				v_buf.type = (enum v4l2_buf_type)buffertype;
				v_buf.memory = (enum v4l2_memory)memorytype;
				v_buf.index = i;
				if (ioctl(fd, VIDIOC_QUERYBUF, &v_buf) == -1)
				{
					printf("ioctl() VIDIOC_QUERYBUF warning : %s. \n", strerror(errno));
				}

				buffers[i].length = v_buf.length;
				buffers[i].start = mmap(NULL, v_buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, v_buf.m.offset);
				if (buffers[i].start == MAP_FAILED)
				{
					printf("mmap() warning : %s. \n", strerror(errno));
				}

				// Enqueue the buffer so that the device can fill it.
				memset(&v_buf, 0, sizeof(v_buf));
				v_buf.type = (enum v4l2_buf_type)buffertype;
				v_buf.memory = (enum v4l2_memory)memorytype;
				v_buf.index = i;
				if (ioctl(fd, VIDIOC_QBUF, &v_buf) == -1)
				{
					printf("ioctl() VIDIOC_QBUF warning : %s. \n", strerror(errno));
				}
			}

			// Start buffer filling process for queued buffers.
			buffertypearg = buffertype;
			if (ioctl(fd, VIDIOC_STREAMON, &buffertypearg) == -1)
			{
				printf("ioctl() VIDIOC_STREAMON warning : %s. \n", strerror(errno));
			}
			usleep(100000);

			// Capture loop.
			while (count < maxcount)
			{
				// Wait for and dequeue a buffer filled by the device.
				memset(&v_buf, 0, sizeof(v_buf));
				v_buf.type = (enum v4l2_buf_type)buffertype;
				v_buf.memory = (enum v4l2_memory)memorytype;
				if (ioctl(fd, VIDIOC_DQBUF, &v_buf) == -1)
				{
					printf("ioctl() VIDIOC_DQBUF warning : %s. \n", strerror(errno));
				}

				// Image data are available at buffers[v_buf.index].start.
				sprintf(szFilename, "%.8d.ppm", count);
				ConvertAndSaveToPPM(szFilename, (unsigned char*)buffers[v_buf.index].start, 
					v_fmt.fmt.pix.width, v_fmt.fmt.pix.height, v_fmt.fmt.pix.pixelformat, 
					minbuffersize?minbuffersize:v_buf.bytesused);
				count++;

				// Enqueue the buffer just dequeued so that the device can fill it again.
				if (ioctl(fd, VIDIOC_QBUF, &v_buf) == -1)
				{
					printf("ioctl() VIDIOC_QBUF warning : %s. \n", strerror(errno));
				}
			}

			// Stop buffer filling process and dequeue all buffers.
			buffertypearg = buffertype;
			if (ioctl(fd, VIDIOC_STREAMOFF, &buffertypearg) == -1)
			{
				printf("ioctl() VIDIOC_STREAMOFF warning : %s. \n", strerror(errno));
			}
			usleep(100000);

			for (i = 0; i < (int)v_reqbuf.count; i++)
			{
				if (munmap(buffers[i].start, buffers[i].length) != EXIT_SUCCESS)
				{
					printf("munmap() warning : %s. \n", strerror(errno));
				}
			}

			free(buffers);
		}

		if (memorytype == V4L2_MEMORY_USERPTR)
		{
			buffers = (struct buffer_t*)calloc(nbframes, sizeof(struct buffer_t));
			if (buffers == NULL)
			{
				printf("calloc() error : %s. \n", strerror(errno));
				CleanUpWithoutMem();
				return EXIT_FAILURE;
			}

			// ???
			page_size = getpagesize();
			size = (v_fmt.fmt.pix.sizeimage+page_size-1)&~(page_size-1);

			for (i = 0; i < nbframes; i++) 
			{
				// Allocate user buffers?
				buffers[i].length = size;
				buffers[i].start = memalign(page_size, size);
				if (buffers[i].start == NULL)
				{
					printf("memalign() warning : %s. \n", strerror(errno));
				}

				// Enqueue the buffer so that the device can fill it.
				memset(&v_buf, 0, sizeof(v_buf));
				v_buf.type = (enum v4l2_buf_type)buffertype;
				v_buf.memory = (enum v4l2_memory)memorytype;
				v_buf.index = i;
				v_buf.m.userptr = (unsigned long)buffers[i].start;
				v_buf.length = buffers[i].length;
				if (ioctl(fd, VIDIOC_QBUF, &v_buf) == -1)
				{
					printf("ioctl() VIDIOC_QBUF warning : %s. \n", strerror(errno));
				}
			}

			printf("Video user buffers allocated : %d. \n", nbframes);

			// Start buffer filling process for queued buffers.
			buffertypearg = buffertype;
			if (ioctl(fd, VIDIOC_STREAMON, &buffertypearg) == -1)
			{
				printf("ioctl() VIDIOC_STREAMON warning : %s. \n", strerror(errno));
			}
			usleep(100000);

			// Capture loop.
			while (count < maxcount)
			{
				// Wait for and dequeue a buffer filled by the device.
				memset(&v_buf, 0, sizeof(v_buf));
				v_buf.type = (enum v4l2_buf_type)buffertype;
				v_buf.memory = (enum v4l2_memory)memorytype;
				if (ioctl(fd, VIDIOC_DQBUF, &v_buf) == -1)
				{
					printf("ioctl() VIDIOC_DQBUF warning : %s. \n", strerror(errno));
				}

				// Image data are available at v_buf.m.userptr.
				sprintf(szFilename, "%.8d.ppm", count);
				ConvertAndSaveToPPM(szFilename, (unsigned char*)v_buf.m.userptr, 
					v_fmt.fmt.pix.width, v_fmt.fmt.pix.height, v_fmt.fmt.pix.pixelformat, 
					minbuffersize?minbuffersize:v_buf.bytesused);
				count++;

				// Enqueue the buffer just dequeued so that the device can fill it again.
				if (ioctl(fd, VIDIOC_QBUF, &v_buf) == -1)
				{
					printf("ioctl() VIDIOC_QBUF warning : %s. \n", strerror(errno));
				}
			}

			// Stop buffer filling process and dequeue all buffers.
			buffertypearg = buffertype;
			if (ioctl(fd, VIDIOC_STREAMOFF, &buffertypearg) == -1)
			{
				printf("ioctl() VIDIOC_STREAMOFF warning : %s. \n", strerror(errno));
			}
			usleep(100000);

			for (i = 0; i < nbframes; i++)
			{
				free(buffers[i].start);
			}

			free(buffers);
		}

		// Free device buffers (if needed).
		//memset(&v_reqbuf, 0, sizeof(v_reqbuf));
		//v_reqbuf.type = (enum v4l2_buf_type)buffertype;
		//v_reqbuf.memory = (enum v4l2_memory)memorytype;
		//v_reqbuf.count = 0;
		//if (ioctl(fd, VIDIOC_REQBUFS, &v_reqbuf) == -1)
		//{
		//	printf("ioctl() VIDIOC_REQBUFS warning : %s. \n", strerror(errno));
		//}
		//usleep(100000);
	}

	// Close the device.
	if (close(fd) != EXIT_SUCCESS)
	{
		printf("close() error : %s. \n", strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
