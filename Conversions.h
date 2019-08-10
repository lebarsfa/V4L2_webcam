#ifndef CONVERSIONS_H
#define CONVERSIONS_H

// Conversions functions from OpenCV, Linux V4L samples...

// LIMIT: convert a 16.16 fixed-point value to a byte, with clipping.
#ifndef LIMIT
#define LIMIT(x) ((x)>0xffffff?0xff: ((x)<=0xffff?0:((x)>>16)))
#endif // LIMIT

#ifndef SAT
#define SAT(c) \
	if ((c) & (~255)) { if ((c) < 0) (c) = 0; else (c) = 255; }
#endif // SAT

#ifndef CLAMP
#define CLAMP(x) ((x)<0?0:((x)>255)?255:(x))
#endif // CLAMP

struct sonix_2_rgb24_code_table_t
{
	int is_abs;
	int len;
	int val;
};
typedef struct sonix_2_rgb24_code_table_t sonix_2_rgb24_code_table_t;

// local storage
static sonix_2_rgb24_code_table_t sonix_2_rgb24_code_table[256];
static int sonix_2_rgb24_init_done = 0;

//  sonix_2_rgb24_init
//  =====================
//    pre-calculates a locally stored table for efficient huffman-decoding.
//
//  Each entry at index x in the table represents the codeword
//  present at the MSB of byte x.
static __inline__ void sonix_2_rgb24_init(void)
{
	int i;
	int is_abs, val, len;

	for (i = 0; i < 256; i++) {
		is_abs = 0;
		val = 0;
		len = 0;
		if ((i & 0x80) == 0) {
			// code 0 
			val = 0;
			len = 1;
		}
		else if ((i & 0xE0) == 0x80) {
			// code 100 
			val = +4;
			len = 3;
		}
		else if ((i & 0xE0) == 0xA0) {
			// code 101 
			val = -4;
			len = 3;
		}
		else if ((i & 0xF0) == 0xD0) {
			// code 1101 
			val = +11;
			len = 4;
		}
		else if ((i & 0xF0) == 0xF0) {
			// code 1111 
			val = -11;
			len = 4;
		}
		else if ((i & 0xF8) == 0xC8) {
			// code 11001 
			val = +20;
			len = 5;
		}
		else if ((i & 0xFC) == 0xC0) {
			// code 110000 
			val = -20;
			len = 6;
		}
		else if ((i & 0xFC) == 0xC4) {
			// code 110001xx: unknown 
			val = 0;
			len = 8;
		}
		else if ((i & 0xF0) == 0xE0) {
			// code 1110xxxx 
			is_abs = 1;
			val = (i & 0x0F) << 4;
			len = 8;
		}
		sonix_2_rgb24_code_table[i].is_abs = is_abs;
		sonix_2_rgb24_code_table[i].val = val;
		sonix_2_rgb24_code_table[i].len = len;
	}

	sonix_2_rgb24_init_done = 1;
}

// FOURCC "S910".
//  sonix_2_rgb24
//  ================
//    decompresses an image encoded by a SN9C101 camera controller chip.
//
//  IN    width
//    height
//    inp         pointer to compressed frame (with header already stripped)
//  OUT   outp    pointer to decompressed frame
//
//  Returns 0 if the operation was successful.
//  Returns <0 if operation failed.
static __inline__ int sonix_2_rgb24(int width, int height, unsigned char *inp, unsigned char *outp)
{
	int row, col;
	int val;
	int bitpos;
	unsigned char code;
	unsigned char *addr;

	if (!sonix_2_rgb24_init_done) sonix_2_rgb24_init();

	bitpos = 0;
	for (row = 0; row < height; row++) 
	{
		col = 0;

		// first two pixels in first two rows are stored as raw 8-bit 
		if (row < 2) {
			addr = inp + (bitpos >> 3);
			code = (addr[0] << (bitpos & 7)) | (addr[1] >> (8 - (bitpos & 7)));
			bitpos += 8;
			*outp++ = code;

			addr = inp + (bitpos >> 3);
			code = (addr[0] << (bitpos & 7)) | (addr[1] >> (8 - (bitpos & 7)));
			bitpos += 8;
			*outp++ = code;

			col += 2;
		}

		while (col < width) {
			// get bitcode from bitstream 
			addr = inp + (bitpos >> 3);
			code = (addr[0] << (bitpos & 7)) | (addr[1] >> (8 - (bitpos & 7)));

			// update bit position 
			bitpos += sonix_2_rgb24_code_table[code].len;

			// calculate pixel value 
			val = sonix_2_rgb24_code_table[code].val;
			if (!sonix_2_rgb24_code_table[code].is_abs) {
				// value is relative to top and left pixel 
				if (col < 2) {
					// left column: relative to top pixel 
					val += outp[-2*width];
				}
				else if (row < 2) {
					// top row: relative to left pixel 
					val += outp[-2];
				}
				else {
					// main area: average of left pixel and top pixel 
					val += (outp[-2] + outp[-2*width]) / 2;
				}
			}

			// store pixel 
			*outp++ = CLAMP(val);
			col++;
		}
	}

	return 0;
}

// FOURCC "BA81".
// BAYER2RGB24 ROUTINE TAKEN FROM:
// Sonix SN9C10x based webcam basic I/F routines
// Takafumi Mizuno <taka-qce@ls-a.jp>
static __inline__ void sbggr8_2_rgb24(long int width, long int height, unsigned char *src, unsigned char *dst)
{
	long int i;
	unsigned char *rawpt, *scanpt;
	long int size;

	rawpt = src;
	scanpt = dst;
	size = width*height;

	for ( i = 0; i < size; i++ ) {
		if ( (i/width) % 2 == 0 ) {
			if ( (i % 2) == 0 ) {
				// B 
				if ( (i > width) && ((i % width) > 0) ) {
					*scanpt++ = (*(rawpt-width-1)+*(rawpt-width+1)+
						*(rawpt+width-1)+*(rawpt+width+1))/4;  // R 
					*scanpt++ = (*(rawpt-1)+*(rawpt+1)+
						*(rawpt+width)+*(rawpt-width))/4;      // G 
					*scanpt++ = *rawpt;                                     // B 
				} else {
					// first line or left column 
					*scanpt++ = *(rawpt+width+1);           // R 
					*scanpt++ = (*(rawpt+1)+*(rawpt+width))/2;      // G 
					*scanpt++ = *rawpt;                             // B 
				}
			} else {
				// (B)G 
				if ( (i > width) && ((i % width) < (width-1)) ) {
					*scanpt++ = (*(rawpt+width)+*(rawpt-width))/2;  // R 
					*scanpt++ = *rawpt;                                     // G 
					*scanpt++ = (*(rawpt-1)+*(rawpt+1))/2;          // B 
				} else {
					// first line or right column 
					*scanpt++ = *(rawpt+width);     // R 
					*scanpt++ = *rawpt;             // G 
					*scanpt++ = *(rawpt-1); // B 
				}
			}
		} else {
			if ( (i % 2) == 0 ) {
				// G(R) 
				if ( (i < (width*(height-1))) && ((i % width) > 0) ) {
					*scanpt++ = (*(rawpt-1)+*(rawpt+1))/2;          // R 
					*scanpt++ = *rawpt;                                     // G 
					*scanpt++ = (*(rawpt+width)+*(rawpt-width))/2;  // B 
				} else {
					// bottom line or left column 
					*scanpt++ = *(rawpt+1);         // R 
					*scanpt++ = *rawpt;                     // G 
					*scanpt++ = *(rawpt-width);             // B 
				}
			} else {
				// R 
				if ( i < (width*(height-1)) && ((i % width) < (width-1)) ) {
					*scanpt++ = *rawpt;                                     // R 
					*scanpt++ = (*(rawpt-1)+*(rawpt+1)+
						*(rawpt-width)+*(rawpt+width))/4;      // G 
					*scanpt++ = (*(rawpt-width-1)+*(rawpt-width+1)+
						*(rawpt+width-1)+*(rawpt+width+1))/4;  // B 
				} else {
					// bottom line or right column
					*scanpt++ = *rawpt;                             // R 
					*scanpt++ = (*(rawpt-1)+*(rawpt-width))/2;      // G 
					*scanpt++ = *(rawpt-width-1);           // B 
				}
			}
		}
		rawpt++;
	}
}

// FOURCC "GBRG".
// SGBRG to RGB24
// for some reason, red and blue needs to be swapped
// at least for  046d:092f Logitech, Inc. QuickCam Express Plus to work
//see: http://www.siliconimaging.com/RGB%20Bayer.htm
//and 4.6 at http://tldp.org/HOWTO/html_single/libdc1394-HOWTO/
static __inline__ void sgbrg8_2_rgb24(long int width, long int height, unsigned char *src, unsigned char *dst)
{
	long int i;
	unsigned char *rawpt, *scanpt;
	long int size;

	rawpt = src;
	scanpt = dst;
	size = width*height;

	for ( i = 0; i < size; i++ )
	{
		if ( (i/width) % 2 == 0 ) //even row
		{
			if ( (i % 2) == 0 ) //even pixel
			{
				if ( (i > width) && ((i % width) > 0) )
				{
					*scanpt++ = (*(rawpt-1)+*(rawpt+1))/2;       // R 
					*scanpt++ = *(rawpt);                        // G 
					*scanpt++ = (*(rawpt-width) + *(rawpt+width))/2;      // B 
				} else
				{
					// first line or left column 

					*scanpt++ = *(rawpt+1);           // R 
					*scanpt++ = *(rawpt);             // G 
					*scanpt++ =  *(rawpt+width);      // B 
				}
			} else //odd pixel
			{
				if ( (i > width) && ((i % width) < (width-1)) )
				{
					*scanpt++ = *(rawpt);       // R 
					*scanpt++ = (*(rawpt-1)+*(rawpt+1)+*(rawpt-width)+*(rawpt+width))/4; // G 
					*scanpt++ = (*(rawpt-width-1) + *(rawpt-width+1) + *(rawpt+width-1) + *(rawpt+width+1))/4;      // B 
				} else
				{
					// first line or right column 

					*scanpt++ = *(rawpt);       // R 
					*scanpt++ = (*(rawpt-1)+*(rawpt+width))/2; // G 
					*scanpt++ = *(rawpt+width-1);      // B 
				}
			}
		} else
		{ //odd row
			if ( (i % 2) == 0 ) //even pixel
			{
				if ( (i < (width*(height-1))) && ((i % width) > 0) )
				{
					*scanpt++ =  (*(rawpt-width-1)+*(rawpt-width+1)+*(rawpt+width-1)+*(rawpt+width+1))/4;          // R 
					*scanpt++ =  (*(rawpt-1)+*(rawpt+1)+*(rawpt-width)+*(rawpt+width))/4;      // G 
					*scanpt++ =  *(rawpt); // B 
				} else
				{
					// bottom line or left column 

					*scanpt++ =  *(rawpt-width+1);          // R 
					*scanpt++ =  (*(rawpt+1)+*(rawpt-width))/2;      // G 
					*scanpt++ =  *(rawpt); // B 
				}
			} else
			{ //odd pixel
				if ( i < (width*(height-1)) && ((i % width) < (width-1)) )
				{
					*scanpt++ = (*(rawpt-width)+*(rawpt+width))/2;  // R 
					*scanpt++ = *(rawpt);      // G 
					*scanpt++ = (*(rawpt-1)+*(rawpt+1))/2; // B 
				} else
				{
					// bottom line or right column 

					*scanpt++ = (*(rawpt-width));  // R 
					*scanpt++ = *(rawpt);      // G 
					*scanpt++ = (*(rawpt-1)); // B 
				}
			}
		}
		rawpt++;
	}
}

// Color space conversion coefficients taken from the excellent
// http://www.inforamp.net/~poynton/ColorFAQ.html
//
// To avoid floating point arithmetic, the color conversion
// coefficients are scaled into 16.16 fixed-point integers.
// They were determined as follows:
//
//  double brightness = 1.0;  (0->black; 1->full scale)
//  double saturation = 1.0;  (0->greyscale; 1->full color)
//  double fixScale = brightness * 256 * 256;
//  int rvScale = (int)(1.402 * saturation * fixScale);
//  int guScale = (int)(-0.344136 * saturation * fixScale);
//  int gvScale = (int)(-0.714136 * saturation * fixScale);
//  int buScale = (int)(1.772 * saturation * fixScale);
//  int yScale = (int)(fixScale);
static __inline__ void yuv_2_rgb_pix(int y, int u, int v, 
	unsigned char* red, unsigned char* green, unsigned char* blue)
{
	int r = 0, g = 0, b = 0, y0 = 0, u0 = 0, v0 = 0, cr = 0, cg = 0, cb = 0;

	y0 = y<<16; u0 = u-128; v0 = v-128; cr = 91881*v0; cg = 22553*u0+46801*v0; cb = 116129*u0;
	r = LIMIT(y0+cr); g = LIMIT(y0-cg); b = LIMIT(y0+cb);
	*red = (unsigned char)r; *green = (unsigned char)g; *blue = (unsigned char)b;
}

static __inline__ void yuv_2_rgb_pix0(int y, int u, int v, 
	unsigned char* red, unsigned char* green, unsigned char* blue)
{
	int r = 0, g = 0, b = 0, u0 = 0, v0 = 0, cr = 0, cg = 0, cb = 0;

	u0 = u-128; v0 = v-128; cr = (359*v0)>>8; cg = (88*u0+183*v0)>>8; cb = (454*u0)>>8;
	r = y+cr; g = y-cg; b = y+cb; SAT(r); SAT(g); SAT(b);
	//r = (int)(y+1.402*(v-128)); g = (int)(y-0.34414*(u-128)-0.71414*(v-128)); b = (int)(y+1.772*(u-128)); 
	//SAT(r); SAT(g); SAT(b);
	*red = (unsigned char)r; *green = (unsigned char)g; *blue = (unsigned char)b;
}

// Turn a YUV4:2:0 block into an RGB block
//
// Video4Linux seems to use the blue, green, red channel
// order convention-- rgb[0] is blue, rgb[1] is green, rgb[2] is red.
//
// Color space conversion coefficients taken from the excellent
// http://www.inforamp.net/~poynton/ColorFAQ.html
// In his terminology, this is a CCIR 601.1 YCbCr -> RGB.
// Y values are given for all 4 pixels, but the U (Pb)
// and V (Pr) are assumed constant over the 2x2 block.
//
// To avoid floating point arithmetic, the color conversion
// coefficients are scaled into 16.16 fixed-point integers.
// They were determined as follows:
//
//  double brightness = 1.0;  (0->black; 1->full scale)
//  double saturation = 1.0;  (0->greyscale; 1->full color)
//  double fixScale = brightness * 256 * 256;
//  int rvScale = (int)(1.402 * saturation * fixScale);
//  int guScale = (int)(-0.344136 * saturation * fixScale);
//  int gvScale = (int)(-0.714136 * saturation * fixScale);
//  int buScale = (int)(1.772 * saturation * fixScale);
//  int yScale = (int)(fixScale);
static __inline__ void move420block(int yTL, int yTR, int yBL, int yBR, 
	int u, int v, int rowPixels, unsigned char * rgb)
{
	const int rvScale = 91881;
	const int guScale = -22553;
	const int gvScale = -46801;
	const int buScale = 116129;
	const int yScale  = 65536;
	int r, g, b;

	g = guScale * u + gvScale * v;
	//  if (force_rgb) {
	//      r = buScale * u;
	//      b = rvScale * v;
	//  } else {
	r = rvScale * v;
	b = buScale * u;
	//  }

	yTL *= yScale; yTR *= yScale;
	yBL *= yScale; yBR *= yScale;

	// Write out top two pixels.
	rgb[0] = LIMIT(r+yTL); rgb[1] = LIMIT(g+yTL); rgb[2] = LIMIT(b+yTL);

	rgb[3] = LIMIT(r+yTR); rgb[4] = LIMIT(g+yTR); rgb[5] = LIMIT(b+yTR);

	// Skip down to next line to write out bottom two pixels.
	rgb += 3 * rowPixels;
	rgb[0] = LIMIT(r+yBL); rgb[1] = LIMIT(g+yBL); rgb[2] = LIMIT(b+yBL);

	rgb[3] = LIMIT(r+yBR); rgb[4] = LIMIT(g+yBR); rgb[5] = LIMIT(b+yBR);
}

static __inline__ void move411block(int yTL, int yTR, int yBL, int yBR, 
	int u, int v, unsigned char * rgb)
{
	const int rvScale = 91881;
	const int guScale = -22553;
	const int gvScale = -46801;
	const int buScale = 116129;
	const int yScale  = 65536;
	int r, g, b;

	g = guScale * u + gvScale * v;
	//  if (force_rgb) {
	//      r = buScale * u;
	//      b = rvScale * v;
	//  } else {
	r = rvScale * v;
	b = buScale * u;
	//  }

	yTL *= yScale; yTR *= yScale;
	yBL *= yScale; yBR *= yScale;

	// Write out top two first pixels.
	rgb[0] = LIMIT(r+yTL); rgb[1] = LIMIT(g+yTL); rgb[2] = LIMIT(b+yTL);

	rgb[3] = LIMIT(r+yTR); rgb[4] = LIMIT(g+yTR); rgb[5] = LIMIT(b+yTR);

	// Write out top two last pixels.
	rgb += 6;
	rgb[0] = LIMIT(r+yBL); rgb[1] = LIMIT(g+yBL); rgb[2] = LIMIT(b+yBL);

	rgb[3] = LIMIT(r+yBR); rgb[4] = LIMIT(g+yBR); rgb[5] = LIMIT(b+yBR);
}

// FOURCC ?.
// Consider a YUV420 image of 6x2 pixels.
//
// A B C D U1 U2
// I J K L V1 V2
//
// The U1/V1 samples correspond to the ABIJ pixels.
//     U2/V2 samples correspond to the CDKL pixels.
//
// Convert from interlaced YUV420 to RGB24.
// [FD] untested...
static __inline__ void yuv420_2_rgb24(int width, int height, unsigned char *pIn0, unsigned char *pOut0)
{
	const int bytes = 24 >> 3;
	int i, j, y00, y01, y10, y11, u, v;
	unsigned char *pY = pIn0;
	unsigned char *pU = pY + 4;
	unsigned char *pV = pU + width;
	unsigned char *pOut = pOut0;

	for (j = 0; j <= height - 2; j += 2) {
		for (i = 0; i <= width - 4; i += 4) {
			y00 = *pY;
			y01 = *(pY + 1);
			y10 = *(pY + width);
			y11 = *(pY + width + 1);
			u = (*pU++) - 128;
			v = (*pV++) - 128;

			move420block(y00, y01, y10, y11, u, v, width, pOut);

			pY += 2;
			pOut += 2 * bytes;

			y00 = *pY;
			y01 = *(pY + 1);
			y10 = *(pY + width);
			y11 = *(pY + width + 1);
			u = (*pU++) - 128;
			v = (*pV++) - 128;

			move420block(y00, y01, y10, y11, u, v, width, pOut);

			pY += 4; // skip UV
			pOut += 2 * bytes;

		}
		pY += width;
		pOut += width * bytes;
	}
}

// FOURCCs "YU12", "I420", "IYUV".
// Consider a YUV420P image of 8x2 pixels.
//
// A plane of Y values    A B C D E F G H
//                        I J K L M N O P
//
// A plane of U values    1   2   3   4
// A plane of V values    1   2   3   4 ....
//
// The U1/V1 samples correspond to the ABIJ pixels.
//     U2/V2 samples correspond to the CDKL pixels.
//
// Convert from planar YUV420P to RGB24.
static __inline__ void yuv420p_2_rgb24(int width, int height, unsigned char *pIn0, unsigned char *pOut0)
{
	const int numpix = width * height;
	const int bytes = 24 >> 3;
	int i, j, y00, y01, y10, y11, u, v;
	unsigned char *pY = pIn0;
	unsigned char *pU = pY + numpix;
	unsigned char *pV = pU + numpix / 4;
	unsigned char *pOut = pOut0;

	for (j = 0; j <= height - 2; j += 2) {
		for (i = 0; i <= width - 2; i += 2) {
			y00 = *pY;
			y01 = *(pY + 1);
			y10 = *(pY + width);
			y11 = *(pY + width + 1);
			u = (*pU++) - 128;
			v = (*pV++) - 128;

			move420block(y00, y01, y10, y11, u, v, width, pOut);

			pY += 2;
			pOut += 2 * bytes;

		}
		pY += width;
		pOut += width * bytes;
	}
}

// FOURCC "YV12".
// To test and optimize...
static __inline__ void yvu420p_2_rgb24(int width, int height, unsigned char *pIn0, unsigned char *pOut0)
{
	const int numpix = width * height;
	const int bytes = 24 >> 3;
	int i, j, y00, y01, y10, y11, u, v;
	unsigned char *pY = pIn0;
	unsigned char *pV = pY + numpix;
	unsigned char *pU = pV + numpix / 4;
	unsigned char *pOut = pOut0;

	for (j = 0; j <= height - 2; j += 2) {
		for (i = 0; i <= width - 2; i += 2) {
			y00 = *pY;
			y01 = *(pY + 1);
			y10 = *(pY + width);
			y11 = *(pY + width + 1);
			v = (*pV++) - 128;
			u = (*pU++) - 128;

			move420block(y00, y01, y10, y11, u, v, width, pOut);

			pY += 2;
			pOut += 2 * bytes;

		}
		pY += width;
		pOut += width * bytes;
	}
}

// FOURCC "411P".
// Consider a YUV411P image of 8x2 pixels.
//
// A plane of Y values    A B C D E F G H
//                        I J K L M N O P
//
// A plane of U values    1       2
//                        3       4
//
// A plane of V values    1       2
//                        3       4
//
// The U1/V1 samples correspond to the ABCD pixels.
//     U2/V2 samples correspond to the EFGH pixels.
//
// Convert from planar YUV411P to RGB24.
// [FD] untested... Does not seem to be OK or maybe bad driver...
static __inline__ void yuv411p_2_rgb24(int width, int height, unsigned char *pIn0, unsigned char *pOut0)
{
	const int numpix = width * height;
	const int bytes = 24 >> 3;
	int i, j, y00, y01, y10, y11, u, v;
	unsigned char *pY = pIn0;
	unsigned char *pU = pY + numpix;
	unsigned char *pV = pU + numpix / 4;
	unsigned char *pOut = pOut0;

	for (j = 0; j <= height; j++) {
		for (i = 0; i <= width - 4; i += 4) {
			y00 = *pY;
			y01 = *(pY + 1);
			y10 = *(pY + 2);
			y11 = *(pY + 3);
			u = (*pU++) - 128;
			v = (*pV++) - 128;

			move411block(y00, y01, y10, y11, u, v, pOut);

			pY += 4;
			pOut += 4 * bytes;

		}
	}
}

// FOURCCs "YUYV", "YUY2", "V422", "YUNV".
// For tests...
static __inline__ void yuy2_2_rgb24(int width, int height, unsigned char *src, unsigned char *dst)
{
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int y1 = 0, y2 = 0, u = 0, v = 0;
	unsigned char r = 0, g = 0, b = 0;
	int k = width*height-1;

	while (k > 0)
	{
		k -= 2;

		y1 = *buf++;
		u = *buf++;
		y2 = *buf++;
		v = *buf++;

		yuv_2_rgb_pix(y1, u, v, &r, &g, &b);

		// Pixel width*height-3-k.
		*rgb++ = r;
		*rgb++ = g;
		*rgb++ = b;

		yuv_2_rgb_pix(y2, u, v, &r, &g, &b);

		// Pixel width*height-2-k.
		*rgb++ = r;
		*rgb++ = g;
		*rgb++ = b;
	}
}

// FOURCCs "YUYV", "YUY2", "V422", "YUNV".
// Convert from 4:2:2 YUYV interlaced to RGB24.
// Based on ccvt_yuyv_bgr32() from camstream.
static __inline__ void yuyv_2_rgb24(int width, int height, unsigned char *src, unsigned char *dst)
{
	unsigned char *s;
	unsigned char *d;
	int l, c;
	int r, g, b, cr, cg, cb, y1, y2;

	l = height;
	s = src;
	d = dst;
	while (l--) {
		c = width >> 1;
		while (c--) {
			y1 = *s++;
			cb = ((*s - 128) * 454) >> 8;
			cg = (*s++ - 128) * 88;
			y2 = *s++;
			cr = ((*s - 128) * 359) >> 8;
			cg = (cg + (*s++ - 128) * 183) >> 8;

			r = y1 + cr;
			g = y1 - cg;
			b = y1 + cb;
			SAT(r);
			SAT(g);
			SAT(b);

			*d++ = r;
			*d++ = g;
			*d++ = b;

			r = y2 + cr;
			g = y2 - cg;
			b = y2 + cb;
			SAT(r);
			SAT(g);
			SAT(b);

			*d++ = r;
			*d++ = g;
			*d++ = b;
		}
	}
}

// FOURCCs "UYVY", "Y422", "UYNV", "HDYC".
static __inline__ void uyvy_2_rgb24(int width, int height, unsigned char *src, unsigned char *dst)
{
	unsigned char *s;
	unsigned char *d;
	int l, c;
	int r, g, b, cr, cg, cb, y1, y2;

	l = height;
	s = src;
	d = dst;
	while (l--) {
		c = width >> 1;
		while (c--) {
			cb = ((*s - 128) * 454) >> 8;
			cg = (*s++ - 128) * 88;
			y1 = *s++;
			cr = ((*s - 128) * 359) >> 8;
			cg = (cg + (*s++ - 128) * 183) >> 8;
			y2 = *s++;

			r = y1 + cr;
			g = y1 - cg;
			b = y1 + cb;
			SAT(r);
			SAT(g);
			SAT(b);

			*d++ = r;
			*d++ = g;
			*d++ = b;

			r = y2 + cr;
			g = y2 - cg;
			b = y2 + cb;
			SAT(r);
			SAT(g);
			SAT(b);

			*d++ = r;
			*d++ = g;
			*d++ = b;
		}
	}
}

// FOURCC "YUV9".
// To test and optimize...
static __inline__ void yuv410p_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	int r = 0, g = 0, b = 0, u = 0, y = 0, v = 0, u0 = 0, v0 = 0, cr = 0, cg = 0, cb = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int i = 0, j = 0, k = 0, n = 0, ki = 0, kj = 0, nbpix = width*height;

	// The Y plane is first. The Y plane has one byte per pixel. 
	// The Cb (U) plane immediately follows the Y plane in memory. 
	// The Cb plane is 1/4 the width ands 1/4 the height of the Y plane (and of the image). 
	// Following the Cb plane is the Cr (V) plane, just like the Cb plane. 
	for (i = 0; i <= height-4; i += 4) 
	{
		for (j = 0; j <= width-4; j += 4) 
		{
			u = buf[nbpix+n];
			v = buf[nbpix+nbpix/16+n];
			u0 = u-128; v0 = v-128; cr = (359*v0)>>8; cg = (88*u0+183*v0)>>8; cb = (454*u0)>>8;
			for (ki = i; ki < i+4; ki++) 
			{
				for (kj = j; kj < j+4; kj++) 
				{
					k = kj+width*ki;
					y = buf[k];

					r = y+cr; g = y-cg;	b = y+cb; SAT(r); SAT(g); SAT(b);

					rgb[3*k] = (unsigned char)r;
					rgb[3*k+1] = (unsigned char)g;
					rgb[3*k+2] = (unsigned char)b;
				}
			}
			n++;
		}
	}
}

// FOURCC "YVU9".
// To test and optimize...
static __inline__ void yvu410p_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	int r = 0, g = 0, b = 0, u = 0, y = 0, v = 0, u0 = 0, v0 = 0, cr = 0, cg = 0, cb = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int i = 0, j = 0, k = 0, n = 0, ki = 0, kj = 0, nbpix = width*height;

	for (i = 0; i <= height-4; i += 4) 
	{
		for (j = 0; j <= width-4; j += 4) 
		{
			v = buf[nbpix+n];
			u = buf[nbpix+nbpix/16+n];
			u0 = u-128; v0 = v-128; cr = (359*v0)>>8; cg = (88*u0+183*v0)>>8; cb = (454*u0)>>8;
			for (ki = i; ki < i+4; ki++) 
			{
				for (kj = j; kj < j+4; kj++) 
				{
					k = kj+width*ki;
					y = buf[k];

					r = y+cr; g = y-cg;	b = y+cb; SAT(r); SAT(g); SAT(b);

					rgb[3*k] = (unsigned char)r;
					rgb[3*k+1] = (unsigned char)g;
					rgb[3*k+2] = (unsigned char)b;
				}
			}
			n++;
		}
	}
}

// FOURCCs "YU12", "I420", "IYUV".
// For tests...
static __inline__ void i420_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	int r = 0, g = 0, b = 0, u = 0, y = 0, v = 0, u0 = 0, v0 = 0, cr = 0, cg = 0, cb = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int i = 0, j = 0, k = 0, n = 0, ki = 0, kj = 0, nbpix = width*height;

	// The Y plane is first. The Y plane has one byte per pixel. 
	// The Cb (U) plane immediately follows the Y plane in memory. 
	// The Cb plane is 1/2 the width ands 1/2 the height of the Y plane (and of the image). 
	// Following the Cb plane is the Cr (V) plane, just like the Cb plane. 
	for (i = 0; i <= height-2; i += 2) 
	{
		for (j = 0; j <= width-2; j += 2) 
		{
			u = buf[nbpix+n];
			v = buf[nbpix+nbpix/4+n];
			u0 = u-128; v0 = v-128; cr = (359*v0)>>8; cg = (88*u0+183*v0)>>8; cb = (454*u0)>>8;
			for (ki = i; ki < i+2; ki++) 
			{
				for (kj = j; kj < j+2; kj++) 
				{
					k = kj+width*ki;
					y = buf[k];

					r = y+cr; g = y-cg;	b = y+cb; SAT(r); SAT(g); SAT(b);

					rgb[3*k] = (unsigned char)r;
					rgb[3*k+1] = (unsigned char)g;
					rgb[3*k+2] = (unsigned char)b;
				}
			}
			n++;
		}
	}
}

// FOURCC "422P".
// To test and optimize...
static __inline__ void yuv422p_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	int r = 0, g = 0, b = 0, u = 0, y = 0, v = 0, u0 = 0, v0 = 0, cr = 0, cg = 0, cb = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int k = 0, n = 0, nbpix = width*height;

	// The Y plane is first. The Y plane has one byte per pixel.
	// The Cb (U) plane immediately follows the Y plane in memory. Each Cb belongs to two pixels.
	// Following the Cb plane is the Cr (V) plane, just like the Cb plane.
	for (k = 0; k <= nbpix-2; k += 2) 
	{
		y = buf[k];
		u = buf[nbpix+n];
		v = buf[nbpix+nbpix/2+n];

		u0 = u-128; v0 = v-128; cr = (359*v0)>>8; cg = (88*u0+183*v0)>>8; cb = (454*u0)>>8;
		r = y+cr; g = y-cg;	b = y+cb; SAT(r); SAT(g); SAT(b);

		rgb[3*k] = (unsigned char)r;
		rgb[3*k+1] = (unsigned char)g;
		rgb[3*k+2] = (unsigned char)b;

		y = buf[k+1];
		//u = buf[nbpix+n];
		//v = buf[nbpix+nbpix/2+n];

		//u0 = u-128; v0 = v-128; cr = (359*v0)>>8; cg = (88*u0+183*v0)>>8; cb = (454*u0)>>8;
		r = y+cr; g = y-cg;	b = y+cb; SAT(r); SAT(g); SAT(b);

		rgb[3*(k+1)] = (unsigned char)r;
		rgb[3*(k+1)+1] = (unsigned char)g;
		rgb[3*(k+1)+2] = (unsigned char)b;

		n++;
	}
}

// FOURCC "Y41P".
// To test and optimize...
static __inline__ void yuv411_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	int r = 0, g = 0, b = 0, u = 0, y = 0, v = 0, u0 = 0, v0 = 0, cr = 0, cg = 0, cb = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int k = 0, nbpix = width*height;

	for (k = 0; k <= nbpix-4; k += 4) 
	{
		u = buf[0];
		y = buf[1];
		v = buf[3];

		u0 = u-128; v0 = v-128; cr = (359*v0)>>8; cg = (88*u0+183*v0)>>8; cb = (454*u0)>>8;
		r = y+cr; g = y-cg;	b = y+cb; SAT(r); SAT(g); SAT(b);

		*rgb = (unsigned char)r; rgb++;
		*rgb = (unsigned char)g; rgb++;
		*rgb = (unsigned char)b; rgb++;

		//u = buf[0];
		y = buf[2];
		//v = buf[3];

		//u0 = u-128; v0 = v-128; cr = (359*v0)>>8; cg = (88*u0+183*v0)>>8; cb = (454*u0)>>8;
		r = y+cr; g = y-cg;	b = y+cb; SAT(r); SAT(g); SAT(b);

		*rgb = (unsigned char)r; rgb++;
		*rgb = (unsigned char)g; rgb++;
		*rgb = (unsigned char)b; rgb++;

		//u = buf[0];
		y = buf[4];
		//v = buf[3];

		//u0 = u-128; v0 = v-128; cr = (359*v0)>>8; cg = (88*u0+183*v0)>>8; cb = (454*u0)>>8;
		r = y+cr; g = y-cg;	b = y+cb; SAT(r); SAT(g); SAT(b);

		*rgb = (unsigned char)r; rgb++;
		*rgb = (unsigned char)g; rgb++;
		*rgb = (unsigned char)b; rgb++;

		//u = buf[0];
		y = buf[5];
		//v = buf[3];

		//u0 = u-128; v0 = v-128; cr = (359*v0)>>8; cg = (88*u0+183*v0)>>8; cb = (454*u0)>>8;
		r = y+cr; g = y-cg;	b = y+cb; SAT(r); SAT(g); SAT(b);

		*rgb = (unsigned char)r; rgb++;
		*rgb = (unsigned char)g; rgb++;
		*rgb = (unsigned char)b; rgb++;

		buf += 6;                                       
	}
}

// FOURCC "RGBO".
// To test and optimize...
static __inline__ void rgb555_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	int r = 0, g = 0, b = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int k = width*height;

	while (k)
	{
		k--;

		r = (buf[1]&0x7C)<<1;                         
		g = (((buf[0]&0xE0)>>2)|((buf[1]&0x03)<<6));
		b = (buf[0]&0x1F)<<3;               
		buf += 2;                                       

		// Pixel width*height-1-k.
		*rgb = (unsigned char)r; rgb++;
		*rgb = (unsigned char)g; rgb++;
		*rgb = (unsigned char)b; rgb++;
	}
}

// FOURCC "RGBP".
static __inline__ void rgb565_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	unsigned short tmp = 0;
	int r = 0, g = 0, b = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int k = width*height;

	while (k)
	{
		k--;

		tmp = *(unsigned short*)buf;    
		r = (tmp&0xF800)>>8;                               
		g = ((tmp<<5)&0xFC00)>>8;                          
		b = ((tmp<<11)&0xF800)>>8;                         
		buf += 2;                                       

		// Pixel width*height-1-k.
		*rgb = (unsigned char)r; rgb++;
		*rgb = (unsigned char)g; rgb++;
		*rgb = (unsigned char)b; rgb++;
	}
}

// FOURCC "RGB4".
static __inline__ void rgb32_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	unsigned char r = 0, g = 0, b = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int k = width*height;

	while (k)
	{
		k--;

		r = *buf; buf++;
		g = *buf; buf++;        
		b = *buf; buf++;    
		buf++;                                       

		// Pixel width*height-1-k.
		*rgb = r; rgb++;
		*rgb = g; rgb++;   
		*rgb = b; rgb++;                           
	}
}

// FOURCC "BGR4".
static __inline__ void bgr32_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	unsigned char r = 0, g = 0, b = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int k = width*height;

	while (k)
	{
		k--;

		b = *buf; buf++;
		g = *buf; buf++;        
		r = *buf; buf++;    
		buf++;                                       

		// Pixel width*height-1-k.
		*rgb = r; rgb++;
		*rgb = g; rgb++;   
		*rgb = b; rgb++;                           
	}
}

// FOURCC "RGB3".
static __inline__ void rgb24_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	unsigned char r = 0, g = 0, b = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int k = width*height;

	while (k)
	{
		k--;

		r = *buf; buf++;
		g = *buf; buf++;        
		b = *buf; buf++;    

		// Pixel width*height-1-k.
		*rgb = r; rgb++;
		*rgb = g; rgb++;   
		*rgb = b; rgb++;                           
	}
}

// FOURCC "BGR3".
static __inline__ void bgr24_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	unsigned char r = 0, g = 0, b = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int k = width*height;

	while (k)
	{
		k--;

		b = *buf; buf++;
		g = *buf; buf++;        
		r = *buf; buf++;    

		// Pixel width*height-1-k.
		*rgb = r; rgb++;
		*rgb = g; rgb++;   
		*rgb = b; rgb++;                           
	}
}

// FOURCCs "GREY", "Y800", "Y8  ".
// To test and optimize...
static __inline__ void grey8_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	unsigned char r = 0, g = 0, b = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int k = width*height;

	while (k)
	{
		k--;

		r = g = b = (*buf++);

		// Pixel width*height-1-k.
		*rgb = r; rgb++;
		*rgb = g; rgb++;   
		*rgb = b; rgb++;                           
	}
}

// FOURCC "Y16 ".
// To test and optimize...
static __inline__ void grey16_2_rgb24(int width, int height, unsigned char* src, unsigned char* dst)
{
	int r = 0, g = 0, b = 0;
	unsigned char* buf = src;
	unsigned char* rgb = dst;
	int k = width*height;

	while (k)
	{
		k--;

		r = g = b = ((*(unsigned short*)buf)>>8);      
		buf += 2;                       

		// Pixel width*height-1-k.
		*rgb = (unsigned char)r; rgb++;
		*rgb = (unsigned char)g; rgb++;
		*rgb = (unsigned char)b; rgb++;
	}
}

static __inline__ void SwapRedBluePPM(unsigned char* ppmimgdata, int width, int height)
{
	unsigned char c = 0;
	unsigned int i = 0;
	unsigned int size = width*height*3;

	while (i <= size-3)
	{
		c = ppmimgdata[i]; ppmimgdata[i] = ppmimgdata[i+2]; ppmimgdata[i+2] = c;
		i += 3;
	}
}

static __inline__ void ConvertV4L2FormatToRGB24(
	unsigned char* webcamimgdata, unsigned char* ppmimgdata, 
	unsigned int width, unsigned int height, unsigned int pixfmt, unsigned int minbuffersize)
{
	unsigned char* buf = webcamimgdata;
	unsigned char* rgb = ppmimgdata;

	switch (pixfmt)
	{ 
	case V4L2_PIX_FMT_PAL8: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_RGB332: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_RGB444: 
		grey16_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_RGB555: 
		rgb555_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_RGB565: 
		rgb565_2_rgb24(width, height, buf, rgb);
		break;
	case V4L2_PIX_FMT_RGB555X: 
		grey16_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_RGB565X: 
		grey16_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_BGR24: 
		bgr24_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_RGB24: 
		rgb24_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_BGR32: 
		bgr32_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_RGB32:                               
		rgb32_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_SBGGR8: 
		sbggr8_2_rgb24(width, height, buf, rgb);
		break;
	case V4L2_PIX_FMT_SGBRG8: 
		sgbrg8_2_rgb24(width, height, buf, rgb); // Some perturbations but maybe bad driver...
		SwapRedBluePPM(rgb, width, height); // Need R and B swapped?
		break;
	case V4L2_PIX_FMT_SGRBG8: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_SRGGB8: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_SBGGR10: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_SGBRG10: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_SGRBG10: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_SRGGB10: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_SGRBG10DPCM8: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_SBGGR16: 
		grey16_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_YUV444: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_YUV555: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_YUV565: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_YUV32: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_GREY:
		grey8_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_Y4:
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_Y6:
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_Y10:
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_Y16:
		grey8_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_YUYV: 
		yuyv_2_rgb24(width, height, buf, rgb);
		//yuy2_2_rgb24(width, height, buf, rgb); // For tests...
		break;
	case V4L2_PIX_FMT_UYVY: 
		uyvy_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_Y41P: 
		yuv411_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_YVU420: 
		yvu420p_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_YUV420: 
		yuv420p_2_rgb24(width, height, buf, rgb); // Not tested...
		//i420_2_rgb24(width, height, buf, rgb); // For tests...
		break;
	case V4L2_PIX_FMT_YVU410: 
		yvu410p_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_YUV410: 
		yuv410p_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_YUV422P: 
		yuv422p_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_YUV411P: 
		yuv411p_2_rgb24(width, height, buf, rgb); // Does not seem to be OK but maybe bad driver...
		break;
	case V4L2_PIX_FMT_NV12: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_NV21: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_NV16: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_NV61: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_JPEG: 
		// Important...
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_MPEG: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_DV: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_ET61X251: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_HI240: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_HM12: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_MJPEG: 
		// Important...
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_PWC1: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_PWC2: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_SN9C10X: 
		sonix_2_rgb24(width, height, buf, rgb); // Not tested...
		break;
	case V4L2_PIX_FMT_SN9C20X_I420: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_CPIA1: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_WNVA: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_YYUV: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_SPCA501: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_SPCA505: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_SPCA508: 
		grey8_2_rgb24(width, height, buf, rgb); // Might get better than a black image...
		break;
	case V4L2_PIX_FMT_SPCA561: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_PAC207: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_MR97310A: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_SN9C2028: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_SQ905C: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_OV511: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_OV518: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_STV0680: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	case V4L2_PIX_FMT_TM6000: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	default: 
		memcpy(rgb, buf, minbuffersize); // To try get something... 
		break;
	}
}

#endif // CONVERSIONS_H
