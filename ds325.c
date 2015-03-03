#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <linux/uvcvideo.h>
#include <linux/usb/video.h>
#include <math.h>


#include "x11.h"

#define nelem(x) (int)(sizeof(x)/sizeof(x[0]))

char *color_dev = "/dev/video0";
char *depth_dev = "/dev/video1";

int color_fd;
int depth_fd;
int x11_fd;


double
tsec(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec + 1e-6*tv.tv_usec;
}

void
fatal(char *fmt, ...)
{
	va_list args;
	int xerrno = errno;
	fprintf(stderr, "error: ");
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	if(xerrno != 0)
		fprintf(stderr, ": %s", strerror(xerrno));
	fprintf(stderr, "\n");
	exit(1);
}

void
warn(char *fmt, ...)
{
	va_list args;
	int xerrno = errno;
	fprintf(stderr, "warn: ");
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	if(xerrno != 0)
		fprintf(stderr, ": %s", strerror(xerrno));
	fprintf(stderr, "\n");
}

int
devopen(char *name, int fps)
{
	struct v4l2_capability cap;
	int i, j;
	int fd;

	if((fd = open(name, O_RDWR)) == -1) // O_NONBLOCK
		fatal("open %s", name);

	if(ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1)
		fatal("ioctl %s: VIDIOC_QUERYCAP", name);

	unsigned int needcaps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE;
	if((cap.capabilities & needcaps) != needcaps)
		fatal("%s: misses needed capabilities", name);

	fprintf(stderr,
		"driver '%s' card '%s' bus_info '%s'\n",
		cap.driver, cap.card, cap.bus_info);

	struct v4l2_fmtdesc fmtdesc;
    struct v4l2_frmsizeenum frmsize;
	for(i = 0;; i++){
		fmtdesc.index = i;
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if(ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == -1)
			break;
		fprintf(stderr, "format desc '%s'\n", fmtdesc.description);
		fprintf(stderr, "format fourcc '%x'\n", fmtdesc.pixelformat);
		fprintf(stderr, "format sizes ", fmtdesc.pixelformat);
		for(j = 0;; j++){
			frmsize.pixel_format = fmtdesc.pixelformat;
			frmsize.index = j;
			if(ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == -1)
				break;
			if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
				fprintf(stderr, " %dx%d", frmsize.discrete.width, frmsize.discrete.height);
			else
				fprintf(stderr, " UNKNOWN");
		}
		fprintf(stderr, "\n");
	}


	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ioctl(fd, VIDIOC_G_FMT, &fmt) == -1)
		warn("get format");
	if(fps == 60){
		fmt.fmt.pix.width = 640;
		fmt.fmt.pix.height = 480;
		fmt.fmt.pix.pixelformat = 0;
	} else {
		fmt.fmt.pix.width = 640;
		fmt.fmt.pix.height = 360;
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	}
	if(ioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
		warn("set format");

	struct v4l2_streamparm sparm;
	memset(&sparm, 0, sizeof sparm);
	sparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	sparm.parm.capture.timeperframe.numerator = 1;
	sparm.parm.capture.timeperframe.denominator = fps;
	fprintf(stderr, "set fps %d", sparm.parm.capture.timeperframe.denominator);
	if(ioctl(fd, VIDIOC_S_PARM, &sparm) == -1)
		warn("set streaming params");

	ioctl(fd, VIDIOC_G_PARM, &sparm);
	fprintf(stderr, "get fps:  %d\n", sparm.parm.capture.timeperframe.denominator);

	return fd;
}

void
devmap(int fd, uchar ***bufp, int **lenp, int *nbufp)
{
	struct v4l2_requestbuffers req;
	uchar **bufs;
	int *lens;
	int nbufs;
	int i;

	req.count = 2; //16;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if(ioctl(fd, VIDIOC_REQBUFS, &req) == -1)
		fatal("devmap: ioctl VIDIOC_REQBUFS");
	if(req.count < 2)
		fatal("devmap: got too few buffers back");
	nbufs = req.count;

	bufs = malloc(nbufs * sizeof bufs[0]);
	lens = malloc(nbufs * sizeof lens[0]);
	for(i = 0; i < nbufs; i++){
		struct v4l2_buffer bufd;

		memset(&bufd, 0, sizeof bufd);
		bufd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		bufd.memory = V4L2_MEMORY_MMAP;
		bufd.index = i;
		if(ioctl(fd, VIDIOC_QUERYBUF, &bufd) == -1)
			fatal("devmap: ioctl VIDIOC_QUERYBUF %d", i);

		lens[i] = bufd.length;
		bufs[i] = mmap(NULL, bufd.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, bufd.m.offset);
		if(bufs[i] == MAP_FAILED)
			fatal("devmap: mmap_failed");

		if(ioctl(fd, VIDIOC_QBUF, &bufd) == -1){
			warn("devmap: VIDIOC_QBUF");
			return;
		}

		fprintf(stderr, "devmap: mapped buffers[%d] len %d\n", i, bufd.length);
	}

	*bufp = bufs;
	*lenp = lens;
	*nbufp = nbufs;

}

void
devstart(int fd)
{
	int cmd;
	cmd = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ioctl(fd, VIDIOC_STREAMON, &cmd) == -1)
		fatal("devstart: ioctl: VIDIOC_STREAMON");
}

void
devstop(int fd)
{
	int cmd;
	cmd = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ioctl(fd, VIDIOC_STREAMOFF, &cmd))
		warn("devstop: ioctl: VIDIOC_STREAMOFF");
}

typedef struct Bufwork Bufwork;
struct Bufwork {
	struct v4l2_buffer bufd;
	uchar *buf;
	int len;
	int fd;
	int badframe;
};

void *
devwork(void *arg)
{
	Bufwork *work;
	struct v4l2_buffer *bufd;
	uchar *buf;
	int fd, len;

	work = (Bufwork*)arg;
	buf = work->buf;
	bufd = &work->bufd;
	fd = work->fd;
	len = work->len;

	if(fd == depth_fd){
		if(!work->badframe)
			x11bltdmap(buf, 640, 480);
	}
	if(fd == color_fd){
		if(!work->badframe)
			x11jpegframe(buf, len);
	}

	if(ioctl(fd, VIDIOC_QBUF, bufd) == -1){
		warn("devinput: VIDIOC_QBUF");
		return NULL;
	}

	free(work);
	return NULL;
}

void
devinput(int fd, uchar **bufs)
{
	Bufwork *work;
	struct v4l2_buffer *bufd;
	int r;

	work = malloc(sizeof work[0]);
	memset(work, 0, sizeof work[0]);
	bufd = &work->bufd;
	bufd->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufd->memory = V4L2_MEMORY_MMAP;

	if((r = ioctl(fd, VIDIOC_DQBUF, bufd)) == -1){
		warn("devinput: VIDIOC_DQBUF, fd %d", fd);
		return;
	}

	work->fd = fd;
	work->buf = bufs[bufd->index];
	work->len = bufd->bytesused;

	devwork(work);
}

/*
	Filter documentation

	0 Skeleton
		Reports the depth data for high fidelity (high confidence) pixels only, and all other pixels as invalid.
	1 Raw
		Raw depth image without any post-processing filters.
	2 Raw + Gradients filter
		Raw depth image  with the gradient filter applied.
	3 Very close range
		Very low smoothing effect with high sharpness, accuracy levels, and low noise artifacts. Good for any distances of up to 350mm.
	4 Close range
		Low smoothing effect with high sharpness and accuracy levels. The noise artifacts are optimized for distances between 350mm to 550mm.
	5 Mid-range [Default]
		Moderate smoothing effect optimized for distances between 550mm to 850mm to balance between good sharpness level, high accuracy and moderate noise artifacts.
	6 Far range
		High smoothing effect for distances between 850mm to 1000mm bringing good accuracy with moderate sharpness level.
	7 Very far range
		Very high smoothing effect to bring moderate accuracy level for distances above 1000mm. Use together with the MotionRangeTradeOff property to increase the depth range.


*/
void
realsense_laserpower(int fd)
{
	struct uvc_xu_control_query ctrl;
	unsigned char val;

	enum {
		PROPERTY_IVCAM_LASER_POWER = 1, // (0-16), default 16
		PROPERTY_IVCAM_ACCURACY = 2, // (1-3), default 2
		PROEPRTY_IVCAM_MOTION_RANGE_TRADE_OFF = 3, // (0-100), default 0
		PROPERTY_IVCAM_FILTER_OPTION = 5,  // (0-7), default 5
		CONFTHR = 6, //(0-15), default 6
	};

	ctrl.unit = 6;
	ctrl.selector = PROPERTY_IVCAM_FILTER_OPTION;
	ctrl.query = UVC_SET_CUR;
	ctrl.size = 1;
	val = 1; /* 0 skel, 1 raw */
	ctrl.data = &val;
	if(ioctl(fd, UVCIOC_CTRL_QUERY, &ctrl) == -1)
		warn("realsense filter fd %d", fd);

	ctrl.unit = 6;
	ctrl.selector = PROPERTY_IVCAM_ACCURACY;
	ctrl.query = UVC_SET_CUR;
	ctrl.size = 1;
	val = 1; /* 1: finest (11 patterns, 50fps), 2: medium (10 patterns, 55fps), 3: coarse (9 patterns, 60fps) */
	ctrl.data = &val;
	if(ioctl(fd, UVCIOC_CTRL_QUERY, &ctrl) == -1)
		warn("realsense accuracy fd %d", fd);

	ctrl.unit = 6;
	ctrl.selector = PROPERTY_IVCAM_LASER_POWER;
	ctrl.query = UVC_SET_CUR;
	ctrl.size = 1;
	val = 7; /* 0 to 16, 7 was reasonable for calib */
	ctrl.data = &val;
	if(ioctl(fd, UVCIOC_CTRL_QUERY, &ctrl) == -1)
		warn("realsense laser power fd %d", fd);

	ctrl.unit = 6;
	ctrl.selector = PROEPRTY_IVCAM_MOTION_RANGE_TRADE_OFF;
	ctrl.query = UVC_SET_CUR;
	ctrl.size = 1;
	val = 0;
	ctrl.data = &val;
	if(ioctl(fd, UVCIOC_CTRL_QUERY, &ctrl) == -1)
		warn("realsense range fd %d", fd);

	ctrl.unit = 6;
	ctrl.selector = CONFTHR;
	ctrl.query = UVC_SET_CUR;
	ctrl.size = 1;
	val = 15;
	ctrl.data = &val;
	if(ioctl(fd, UVCIOC_CTRL_QUERY, &ctrl) == -1)
		warn("realsense confidence threshold fd %d", fd);
}

int
ds325eu_freqdiv(int modfreq_khz)
{
	int fdiv;
	/* 0x18 divider would be 75MHz */
	for(fdiv = 0x08; fdiv < 0x18; fdiv++)
		if(modfreq_khz == 600000/fdiv)
			return fdiv;
	return -1;
}

unsigned int
ds325eu_get(int fd, int ctl, int reg)
{
	struct uvc_xu_control_query ctrl;
	unsigned int val;
	uchar buf[7] = {0};

	buf[0] = 0x80 | ctl;
	buf[1] = reg;

	ctrl.unit = 6;
	ctrl.selector = 2;
	ctrl.query = UVC_SET_CUR;
	ctrl.data = buf;
	ctrl.size = sizeof buf;
	if(ioctl(fd, UVCIOC_CTRL_QUERY, &ctrl) == -1){
		fatal("ds325eu_read: set addr");
		return -1;
	}

	ctrl.unit = 6;
	ctrl.selector = 2;
	ctrl.query = UVC_GET_CUR;
	ctrl.data = buf;
	ctrl.size = sizeof buf;
	if(ioctl(fd, UVCIOC_CTRL_QUERY, &ctrl) == -1){
		fatal("ds325eu_read: get data");
		return -1;
	}
	val = buf[3] | (buf[4]<<8) | (buf[5]<<16) | (buf[6]<<24);

	return val;
}

int
ds325eu_set(int fd, int ctl, int reg, unsigned int val)
{
	struct uvc_xu_control_query ctrl;
	uchar buf[7];

	buf[0] = ctl;
	buf[1] = reg;
	buf[2] = 0;
	buf[3] = val & 0xff;
	buf[4] = (val >> 8) & 0xff;
	buf[5] = (val >> 16) & 0xff;
	buf[6] = (val >> 24) & 0xff;

	ctrl.unit = 6;
	ctrl.selector = 2;
	ctrl.query = UVC_SET_CUR;
	ctrl.data = buf;
	ctrl.size = sizeof buf;
	if(ioctl(fd, UVCIOC_CTRL_QUERY, &ctrl) == -1){
		fatal("ds325eu_set: set addr");
		return -1;
	}

	return 0;
}

/*
 *	laser, fpga, sensor, adc?
 */
void
ds325eu_temps(int fd)
{
	int a, b;

	a = ds325eu_get(fd, 0x12, 0x35);
	b = ds325eu_get(fd, 0x12, 0x36);

	fprintf(stderr, "temps %d %d %d %d\n",
		a & 0xff,
		a >> 8,
		b & 0xff,
		b >> 8
	);
}

/*
 *	not fast updating, maybe twice a second.. might still prove useful
 */
int
ds325eu_accel(int fd)
{
	short x, y, z;

	x = (short)ds325eu_get(fd, 0x12, 0x38);
	y = (short)ds325eu_get(fd, 0x12, 0x39);
	z = (short)ds325eu_get(fd, 0x12, 0x3a);

	fprintf(stderr, "accel %d %d %d, len %d\n", x, y, z, (int)sqrtf(x*x+y*y+z*z));

	return 0;
}

static inline short
getshort(uchar *p)
{
	short val;
	val = (p[1]<<8) | p[0];
	return val;
}

static inline void
get8f(uchar *p, float *r)
{
	int i;
	for(i = 0; i < 8; i++)
		r[i] = (float)getshort(p + 2*i);
}

static inline int
v3normalizef(float *v)
{
	float vlen;
	vlen = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
	if(vlen > 1e-6f){
		v[0] /= vlen;
		v[1] /= vlen;
		v[2] /= vlen;
		return 0;
	}
	return -1;
}

void
ds325_dirs(float *dirs)
{
	/* they say it is 74° x 58° x 87° (w x h x d) */
	float x, y, z;
	float theta;
	int i;

	theta = 74.0f/180.0f*M_PI;
	z = (0.5f * 320.0f) / tanf(0.5f*theta);
	for(y = -120.0f; y < 120.0f; y += 1.0f){
		for(x = -160.0f; x < 320.0f; x += 1.0f){
			dirs[3*i+0] = x;
			dirs[3*i+1] = y;
			dirs[3*i+2] = z;
			v3normalizef(dirs + 3*i);
		}
	}
}

void
ds325_depth(uchar *dmap, float *dirs, float *pos, float *val)
{
	const float phase2dst = 0.5f * 299792458.0f/(2.0f*M_PI*50e6f);
	float I[8], Q[8];
	float th[8];
	float dst;
	int i, j, k;
	float x, y;

	x = 0.0f;
	y = 0.0f;
	for(j = 0; j < 320*240; j += 320, y += 1.0f){
		/* each row has 20 32-byte blocks */
		for(i = 0; i < 320; i += 16, y += 16.0f){
			get8f(dmap + 2*(j+i), I);
			get8f(dmap + 2*(j+i) + 16, Q);
			for(k = 0; k < 8; k++){
				th[k] = atan2f(Q[k], I[k]);
				th[k] = phase2dst * (th[k] < 0.0f ? -th[k] : 2.0f*M_PI-th[k]);
				pos[3*(i+j+k)+0] = dirs[3*(i+j+k)+0] * th[k];
				pos[3*(i+j+k)+1] = dirs[3*(i+j+k)+1] * th[k];
				pos[3*(i+j+k)+2] = dirs[3*(i+j+k)+2] * th[k];
				val[j+i] = sqrtf(Q[k]*Q[k] + I[k]*I[k]);
			}
		}
	}
}

void
ds325eu_init(int fd, int modfreq_khz, int fps, int satur, int dutycycle)
{
	int reg1a, reg1b;
	int fdiv;

	if(fps != 25 && fps != 30 && fps != 50 && fps != 60)
		fatal("ds325_init: fps needs to be 25, 30, 50 or 60");

	if((fdiv = ds325eu_freqdiv(modfreq_khz)) == -1)
		fatal("ds325_init: invalid modfreq_khz %d", modfreq_khz);

	if(dutycycle == -1)
		dutycycle = 240;

	reg1a = 0;
	reg1b = 0;
	ds325eu_set(fd, 0x12, 0x1a, reg1a); /* 0 */
	ds325eu_set(fd, 0x12, 0x1b, reg1b); /* 0 */

	/* number of pulses */
	ds325eu_set(fd, 0x12, 0x13, 0x4); /* 4 */

	/* framing controls, 76800 = 320x240 = 0x12c00 */
	ds325eu_set(fd, 0x12, 0x14, 0x2c00); /* low: 11264 */
	ds325eu_set(fd, 0x12, 0x15, 0x1); /* high: 1 */
	ds325eu_set(fd, 0x12, 0x16, 0); /* 0 */
	ds325eu_set(fd, 0x12, 0x17, 239); /* 239 */
	ds325eu_set(fd, 0x12, 0x18, 0); /* 0 */
	ds325eu_set(fd, 0x12, 0x19, 319); /* 319 */

	reg1a |= 0x0400; // 0x400
	ds325eu_set(fd, 0x12, 0x1a, reg1a);

	reg1b |= 0x0100; // laser power pwm enable
	ds325eu_set(fd, 0x12, 0x1b, reg1b);
	reg1b |= 0x0400; // does something to the laser too?
	ds325eu_set(fd, 0x12, 0x1b, reg1b);
	reg1b |= 0x0800; // laser power switch
	ds325eu_set(fd, 0x12, 0x1b, reg1b);

	ds325eu_set(fd, 0x12, 0x1c, 0x5); /* 5 */
	ds325eu_set(fd, 0x12, 0x20, 1200); /* 0x4b0, 1200 */
	ds325eu_set(fd, 0x12, 0x27, 0x106); /* 0x106, 262 */

	/* laser power (pwm), under <70% the filter inductor starts to spike off like crazy. */
	ds325eu_set(fd, 0x12, 0x28, 333); /* default: 333, laser pwm cycle length, 12MHz clock */
	ds325eu_set(fd, 0x12, 0x29, dutycycle); /* default: 240, laser pwm "off" time, 8bit*/

	/* could be the second laser power (pwm)? */
	ds325eu_set(fd, 0x12, 0x2a, 333); /* 333 */

	ds325eu_set(fd, 0x12, 0x30, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x31, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x32, 0x0); /* 0 */

	ds325eu_set(fd, 0x12, 0x3c, 0x2f); /* 47 */
	ds325eu_set(fd, 0x12, 0x3d, 0x3e7); //0x3e7); /* 999 */ // if this is zero, the laser turns off after startup.
	ds325eu_set(fd, 0x12, 0x3e, 0xf); /* 15 */
	ds325eu_set(fd, 0x12, 0x3f, 0xf); /* 15 */
	ds325eu_set(fd, 0x12, 0x40, 0x3e8); //0x3e8); /* 1000 */

	ds325eu_set(fd, 0x12, 0x43, 0x109); /* 265 */
	ds325eu_set(fd, 0x12, 0x1e, 0x8209); /* 33289 */
	ds325eu_set(fd, 0x12, 0x1d, 0x119); /* 281 */
	ds325eu_set(fd, 0x12, 0x44, 0x1e); /* 30 */
	//ds325eu_set(fd, 0x12, 0x1b, reg1b); /* 3328 why write the same value again? */
	reg1b |= 0x4000; // some kind of saturation?
	ds325eu_set(fd, 0x12, 0x1b, reg1b); /* 19712 */
	ds325eu_set(fd, 0x12, 0x45, 0x101); /* 257 */
	ds325eu_set(fd, 0x12, 0x46, 0x2); /* 2 */
	ds325eu_set(fd, 0x12, 0x47, 0x30); /* 48 */

	/*
	 *	registers not set:
	 *		0x1f - reads zero
	 *		0x21 - some kind of status reg, polled for != 0xffff before init.
	 *		0x22 - reads as 4
	 *		0x23, 0x24, 0x25, 0x26 - reads zero
	 *		0x2b, 0x2c, 0x2d, 0x2e - reads zero
	 *		0x34 - reads zero
	 *		0x35 - temp?
	 *		0x36 - temp?
	 *		0x37 - zero
	 *		0x38, 0x39, 0x3a - accelerometer x y z
	 *		0x3b - reads 181?
	 *		0x41, 0x42
	 *		0x48, 0x49
	 *
	 *	registers set more than once
	 *		0x1a, 0x1b
	 *		0x2f
	 */

	ds325eu_set(fd, 0x12, 0x2f, 0x60);
	fdiv = (fdiv<<8) | fdiv;
	ds325eu_set(fd, 0x12, 0x0, fdiv);
	ds325eu_set(fd, 0x12, 0x1, fdiv);
	ds325eu_set(fd, 0x12, 0x2f, 0x60);

	/* these remain a mystery, and have great impact on the output. */
	ds325eu_set(fd, 0x12, 0x3, 0x00); /* phase adj? */
	ds325eu_set(fd, 0x12, 0x4, 0x30); /* phase adj? */
	ds325eu_set(fd, 0x12, 0x5, 0x60); /* phase adj? */
	ds325eu_set(fd, 0x12, 0x6, 0x90); /* phase adj? */

	/* repeats for a second laser? */
	ds325eu_set(fd, 0x12, 0x7, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x8, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x9, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0xa, 0x0); /* 0 */

	ds325eu_set(fd, 0x12, 0x2, 0x0); /* and why is this one out of order? */

	ds325eu_set(fd, 0x12, 0xb, 0xea60); /* 60000 */
	ds325eu_set(fd, 0x12, 0xc, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0xd, 0x4740); //0x4740); /* 18240 */
	ds325eu_set(fd, 0x12, 0xe, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0xf, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x10, 0x0); /* 0 */

	ds325eu_set(fd, 0x12, 0x11, 480); //0x1e0); /* 480 */

	if(fps == 50 || fps == 60){
		ds325eu_set(fd, 0x12, 0x12, 0x2); /* 2 */
	} else if(fps == 25 || fps == 30){
		ds325eu_set(fd, 0x12, 0x12, 0x4); /* 4 */
	}

	reg1a |= 0x1000;
	ds325eu_set(fd, 0x12, 0x1a, reg1a); /* 5120 */

	if(fps == 25 || fps == 50){
		ds325eu_set(fd, 0x12, 0x33, 0xa980);
		ds325eu_set(fd, 0x12, 0x4a, 0x3);
	} else if(fps == 30 || fps == 60){
		ds325eu_set(fd, 0x12, 0x33, 0x70f0);
		ds325eu_set(fd, 0x12, 0x4a, 0x2);
	}

	if(satur){
		reg1a |= 0x0080;
		ds325eu_set(fd, 0x12, 0x1a, reg1a); /* 5312 */
	}

	reg1a |= 0x0040;
	ds325eu_set(fd, 0x12, 0x1a, reg1a);
}

void
ds325eu_dump(int fd)
{
	int i;
	unsigned int uval;
	short sval;
	for(i = 0; i < 76; i++){
		uval = ds325eu_get(fd, 0x12, i);
		sval = (short)uval;
		printf("ds325eu_set(fd, 0x%x, 0x%x, 0x%x); /* %d, %d */\n",
			0x12, i, uval, uval, sval);
	}
}

int
main(int argc, char *argv[])
{
	uchar **color_bufs;
	uchar **depth_bufs;
	int *color_buf_lens;
	int *depth_buf_lens;
	int ncolor_bufs;
	int ndepth_bufs;
	int i, opt;
	int modfreq = 50000;
	int dutycycle = -1;
	int fps = 60;

	while((opt = getopt(argc, argv, "d:c:m:i:f:")) != -1){
		switch (opt) {
		case 'f':
			fps = strtol(optarg, NULL, 10);
			if(fps != 25 && fps != 30 && fps != 50 && fps != 60)
				goto caseusage;
			break;
		case 'i':
			dutycycle = strtol(optarg, NULL, 10);
			if(dutycycle < 150 || dutycycle > 255)
				goto caseusage;
			break;
		case 'm':
			modfreq = strtol(optarg, NULL, 10);
			if(ds325eu_freqdiv(modfreq) == -1)
				goto caseusage;
			break;
		case 'c':
			if(!strcmp(optarg, "-"))
				color_dev = NULL;
			else
				color_dev = optarg;
			break;
		case 'd':
			if(!strcmp(optarg, "-"))
				depth_dev = NULL;
			else
				depth_dev = optarg;
			break;
		default:
		caseusage:
			fprintf(stderr, "usage: %s [-c /dev/videok] [-d /dev/videoj] [-m modfreq_khz] [-i dutycycle] [-f fps]\n", argv[0]);
			fprintf(stderr, "	default modfreq_khz: 50000, feasible ones:");
			for(i = 0x08; i <= 0x18; i++)
				fprintf(stderr, " %d", 600000/i);
			fprintf(stderr, "\n");
			fprintf(stderr, "	default dutycycle: 240, use not advised, will do 150 to 255\n");
			fprintf(stderr, "	default fps: 25, supported: 25 30 50 60\n");
			exit(1);
		}
	}

	x11_fd = x11init();

	color_fd = -1;
	if(color_dev != NULL){
		color_fd = devopen(color_dev, 30);
		devmap(color_fd, &color_bufs, &color_buf_lens, &ncolor_bufs);
		devstart(color_fd);
	}

	depth_fd = -1;
	if(depth_dev != NULL){
		int r;
		depth_fd = devopen(depth_dev, fps);
		devmap(depth_fd, &depth_bufs, &depth_buf_lens, &ndepth_bufs);
		devstart(depth_fd);
		realsense_laserpower(depth_fd);

/*
		for(;;){
			r = ds325eu_get(depth_fd, 0x12, 0x21);
			if(r == 0xffff)
				continue;
			fprintf(stderr, "got %x, now we go!\n", r);
			ds325eu_init(depth_fd, modfreq, fps, 0, dutycycle);
			break;
		}
*/
	}

#if 0
	ds325eu_dump(depth_fd);
	int frame = 0;
	int reg1a, reg1b;
	reg1a = ds325eu_get(depth_fd, 0x12, 0x1a);
	reg1b = ds325eu_get(depth_fd, 0x12, 0x1b);
#endif

	for(;;){
		struct timeval tv;
		fd_set rset;
		int max_fd;

		FD_ZERO(&rset);
		if(color_fd != -1)
			FD_SET(color_fd, &rset);
		if(depth_fd != -1)
			FD_SET(depth_fd, &rset);
		if(x11_fd != -1)
			FD_SET(x11_fd, &rset);
		tv.tv_sec = 10;
		tv.tv_usec = 0;

		max_fd = -1;
		max_fd = color_fd > max_fd ? color_fd : max_fd;
		max_fd = depth_fd > max_fd ? depth_fd : max_fd;
		max_fd = x11_fd > max_fd ? x11_fd : max_fd;

		if(select(max_fd+1, &rset, NULL, NULL, &tv) == -1)
			warn("select");

		if(depth_fd != -1 && FD_ISSET(depth_fd, &rset)){
			devinput(depth_fd, depth_bufs);


#if 0
			frame++;
			if(0 || (frame & 0x1ff) == 0x1ff){
				reg1b ^= 0x0800;
				fprintf(stderr, "reg1b: %x\n", reg1b);
				ds325eu_set(depth_fd, 0x12, 0x1b, reg1b);
			}
#endif

		}

		if(color_fd != -1 && FD_ISSET(color_fd, &rset)){
			devinput(color_fd, color_bufs);
		}

		if(x11_fd != -1)
			x11serve(x11_fd);


//ds325eu_accel(depth_fd);
//ds325eu_temps(depth_fd);

	}

	if(color_fd != -1){
		devstop(color_fd);
		close(color_fd);
	}

	if(depth_fd != -1){
		devstop(depth_fd);
		close(depth_fd);
	}


	return 0;
}
