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

int color_fd;
int depth_fd;
int x11_fd;

int dflag;
int cflag;

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

	struct v4l2_streamparm sparm;
	memset(&sparm, 0, sizeof sparm);
	sparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	sparm.parm.capture.timeperframe.numerator = 1;
	sparm.parm.capture.timeperframe.denominator = fps;
	fprintf(stderr, "set fps %d", sparm.parm.capture.timeperframe.denominator);
	if(ioctl(fd, VIDIOC_S_PARM, &sparm) == -1)
		fprintf(stderr, "set frame rate error %d, %s\n", errno, strerror(errno));
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
	static uchar buf2[1280*240];
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
		memcpy(buf2, buf, 1280*240);
		if(!work->badframe)
			x11bltdmap(buf2, 1280, 240);
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
int
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

int
init_cmd(int fd, int send_num)
{
	struct {
		int query;
		unsigned char data[7];
	} cmdtab[] = {
		{UVC_SET_CUR, {0x12, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00}}, //optional
		{UVC_SET_CUR, {0x12, 0x1b, 0x00, 0x00, 0x00, 0x00, 0x00}}, //optional

		{UVC_SET_CUR, {0x12, 0x13, 0x00, 0x04, 0x00, 0x00, 0x00}}, // blacks out, light turns on
		{UVC_SET_CUR, {0x12, 0x14, 0x00, 0x00, 0x2c, 0x00, 0x00}}, // orig: 0x2c super fast but crazy without
		{UVC_SET_CUR, {0x12, 0x15, 0x00, 0x01, 0x00, 0x00, 0x00}}, // goes nuts without
		{UVC_SET_CUR, {0x12, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional

		{UVC_SET_CUR, {0x12, 0x17, 0x00, 0xef, 0x00, 0x00, 0x00}}, // orig: 239 (0xef), last scanline(!?)
		{UVC_SET_CUR, {0x12, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional

		{UVC_SET_CUR, {0x12, 0x19, 0x00, 0x3f, 0x01, 0x00, 0x00}}, // orig: 319 (0x3f, 0x01), goes nuts if less than 0x3c, last column(!?)

		{UVC_SET_CUR, {0x12, 0x1a, 0x00, 0x00, 0x04, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x1b, 0x00, 0x00, 0x01, 0x00, 0x00}}, // optional

		{UVC_SET_CUR, {0x12, 0x1b, 0x00, 0x00, 0x05, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x1b, 0x00, 0x00, 0x0d, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x1c, 0x00, 0x05, 0x00, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x20, 0x00, 0xb0, 0x04, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x27, 0x00, 0x06, 0x01, 0x00, 0x00}}, // optional

		{UVC_SET_CUR, {0x12, 0x28, 0x00, 0xff, 0x01, 0x00, 0x00}}, // orig: 0x4d, 0x01, 0x4d, 0x02 reduced funny flickers, lights off without. some kind of initial value. where's the auto tune enable then?
		{UVC_SET_CUR, {0x12, 0x29, 0x00, 0xf0, 0x00, 0x00, 0x00}}, // orig: 0xf0, 0x00. hard to see difference

		{UVC_SET_CUR, {0x12, 0x2a, 0x00, 0xff, 0x01, 0x00, 0x00}}, // orig: 0x4d, 0x01, optional. presumably for the missing second laser?
		{UVC_SET_CUR, {0x12, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional

		{UVC_SET_CUR, {0x12, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional

		{UVC_SET_CUR, {0x12, 0x3c, 0x00, 0x4f, 0x00, 0x00, 0x00}}, // orig: 0x2f, 0x00, lights off without. affects autoadjust. 0x4f seems brighter?
		{UVC_SET_CUR, {0x12, 0x3d, 0x00, 0xe7, 0x03, 0x00, 0x00}}, // orig: 999 (0xe7, 0x03), lights off without 

		{UVC_SET_CUR, {0x12, 0x3e, 0x00, 0x0f, 0x00, 0x00, 0x00}}, // orig: 0x0f, needs to be >= 0x05, unstable without
		{UVC_SET_CUR, {0x12, 0x3f, 0x00, 0x0f, 0x00, 0x00, 0x00}}, // orig: 0x0f, needs to be >= 0x05, unstable without
		{UVC_SET_CUR, {0x12, 0x40, 0x00, 0xe8, 0x03, 0x00, 0x00}}, // orig: 1000 (0xe8, 0x03) optional

		{UVC_SET_CUR, {0x12, 0x43, 0x00, 0x09, 0x01, 0x00, 0x00}}, // orig: 0x09, 0x01 no light without, bigger number causes longer delay for light to come up

		{UVC_SET_CUR, {0x12, 0x1e, 0x00, 0x09, 0x82, 0x00, 0x00}}, // goes nuts without
		{UVC_SET_CUR, {0x12, 0x1d, 0x00, 0x19, 0x01, 0x00, 0x00}}, // goes nuts without

		{UVC_SET_CUR, {0x12, 0x44, 0x00, 0x1e, 0x00, 0x00, 0x00}}, // optional

		{UVC_SET_CUR, {0x12, 0x1b, 0x00, 0x00, 0x0d, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x1b, 0x00, 0x00, 0x4d, 0x00, 0x00}}, // orig: 0x00, 0x4d complete blackout if disabled. highest bit crashes it. needs at least 8+1 to work.

		{UVC_SET_CUR, {0x12, 0x45, 0x00, 0x01, 0x01, 0x00, 0x00}}, // orig: 0x01, 0x01. hard to see difference
		{UVC_SET_CUR, {0x12, 0x46, 0x00, 0x02, 0x00, 0x00, 0x00}}, // orig: 0x02, 0x00. hard to see difference
		{UVC_SET_CUR, {0x12, 0x47, 0x00, 0x32, 0x00, 0x00, 0x00}}, // orig: 0x32, 0x00. hard to see difference

		{UVC_SET_CUR, {0x12, 0x2f, 0x00, 0x60, 0x00, 0x00, 0x00}}, // orig: 0x60, 0x00. hard to see difference

		// this controls the modulation frequency.
		// formula for frequency is 1/x * 600MHz, ie. the default of 0x0c (12) is 50MHz.
		// usable range seems to be from 25MHz to 66MHz (0x18 to 0x08)
		{UVC_SET_CUR, {0x12, 0x00, 0x00, 0x0c, 0x0c, 0x00, 0x00}}, // orig: 0x0c, 0x0c pairs with below. something to do with frequency?
		{UVC_SET_CUR, {0x12, 0x01, 0x00, 0x0c, 0x0c, 0x00, 0x00}}, // orig: 0x0c, 0x0c something to do with frequency

		{UVC_SET_CUR, {0x12, 0x2f, 0x00, 0x60, 0x00, 0x00, 0x00}}, // default 0x60 optional

		{UVC_SET_CUR, {0x12, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x04, 0x00, 0x30, 0x00, 0x00, 0x00}}, // also changes the data
		{UVC_SET_CUR, {0x12, 0x05, 0x00, 0x60, 0x00, 0x00, 0x00}}, // default 0x60 weird modification of data (blue goes missing in phase plot)
		{UVC_SET_CUR, {0x12, 0x06, 0x00, 0x90, 0x00, 0x00, 0x00}}, // default 0x90, noisier?, some kind of wobble. line noise?

		{UVC_SET_CUR, {0x12, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00}}, //optional
		{UVC_SET_CUR, {0x12, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00}}, //optional
		{UVC_SET_CUR, {0x12, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00}}, //optional
		{UVC_SET_CUR, {0x12, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}}, //optional


		{UVC_SET_CUR, {0x12, 0x0b, 0x00, 0x60, 0xea, 0x00, 0x00}}, // orig: 60000 (0x60, 0xea) slow framerate at zero, seems to work if set >60000, but not below
		{UVC_SET_CUR, {0x12, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x0d, 0x00, 0x40, 0x47, 0x00, 0x00}}, // orig: 0x40, 0x47, runs with 0xff,0x48 but no higher. affects brightness of image.

		{UVC_SET_CUR, {0x12, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00}}, // optional

		{UVC_SET_CUR, {0x12, 0x11, 0x00, 0xe0, 0x02, 0x00, 0x00}}, // orig: 480 (0xe0, 0x01) weird shit without. needs to be >= 8. funny specks if very small?
		{UVC_SET_CUR, {0x12, 0x12, 0x00, 0x02, 0x00, 0x00, 0x00}}, // orig: 0x02, 0x00 frame rate select. 120fps/x (?), 4 is 30fps.

		{UVC_SET_CUR, {0x12, 0x1a, 0x00, 0x00, 0x14, 0x00, 0x00}}, // matches the one later? optional

		{UVC_SET_CUR, {0x12, 0x33, 0x00, 0xf0, 0x70, 0x00, 0x00}}, // messes up image, weird ass bands?
		{UVC_SET_CUR, {0x12, 0x4a, 0x00, 0x02, 0x00, 0x00, 0x00}}, // ??

		{UVC_SET_CUR, {0x12, 0x1a, 0x00, 0x80, 0x14, 0x00, 0x00}}, // optional
		{UVC_SET_CUR, {0x12, 0x1a, 0x00, 0xc0, 0x14, 0x00, 0x00}}, // 0xc0, 0x14 default, important
	};

	if(send_num >= nelem(cmdtab))
		return -1;

	struct uvc_xu_control_query ctrl;
	ctrl.unit = UVC_PU_HUE_CONTROL;
	ctrl.selector = 2;
	ctrl.query = cmdtab[send_num].query;
	ctrl.size = 7;
	ctrl.data = cmdtab[send_num].data;

	if(ioctl(fd, UVCIOC_CTRL_QUERY, &ctrl) == -1)
		fatal("init_cmd catastropf");

	return 0;
}

void
ds325eu_init(int fd, int modf_khz, int fps, int satur)
{
	int reg1a, reg1b;
	int fdiv;

	if(fps != 25 && fps != 30 && fps != 50 && fps != 60)
		fatal("ds325_init: fps needs to be 25, 30, 50 or 60");

	/* 0x18 divider would be 75MHz */
	for(fdiv = 0x08; fdiv < 0x18; fdiv++)
		if(modf_khz == 600000/fdiv)
			break;

	if(fdiv < 0x08 || fdiv >= 0x18){
		fprintf(stderr, "ds325_init: modf_khz %d is not feasible, use one of", modf_khz);
		for(fdiv = 0x08; fdiv <= 0x18; fdiv++)
			fprintf(stderr, " %d", 600000/fdiv);
		fprintf(stderr, "\n");
		errno=0;
		fatal("ds325_init");
	}

	reg1a = 0;
	reg1b = 0;
	ds325eu_set(fd, 0x12, 0x1a, reg1a); /* 0 */
	ds325eu_set(fd, 0x12, 0x1b, reg1b); /* 0 */
	ds325eu_set(fd, 0x12, 0x13, 0x4); /* 4 */
	ds325eu_set(fd, 0x12, 0x14, 0x2c00); /* 11264 */
	ds325eu_set(fd, 0x12, 0x15, 0x1); /* 1 */
	ds325eu_set(fd, 0x12, 0x16, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x17, 0xef); /* 239 */
	ds325eu_set(fd, 0x12, 0x18, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x19, 0x13f); /* 319 */
	reg1a |= 0x0400;
	ds325eu_set(fd, 0x12, 0x1a, reg1a);
	reg1b |= 0x0100; // turns laser on quickly! TODO: see what these do with a scope.
	ds325eu_set(fd, 0x12, 0x1b, reg1b);
	reg1b |= 0x0400; // some kind of auto-adjust for laser
	ds325eu_set(fd, 0x12, 0x1b, reg1b);
	reg1b |= 0x0800; // turns laser on sloooowly TODO: see what these do with a scope.
	ds325eu_set(fd, 0x12, 0x1b, reg1b);
	ds325eu_set(fd, 0x12, 0x1c, 0x5); /* 5 */

	ds325eu_set(fd, 0x12, 0x20, 0x4b0); /* 0x4b0, 1200 */
	ds325eu_set(fd, 0x12, 0x27, 0x106); /* 0x106, 262 */

	ds325eu_set(fd, 0x12, 0x28, 0x14d); /* laser intensity */
	ds325eu_set(fd, 0x12, 0x29, 0xf0); /* 0xf0, 240 */

	ds325eu_set(fd, 0x12, 0x2a, 0x14d); /* 333, seems to make no difference, is this the other laser? */
	ds325eu_set(fd, 0x12, 0x30, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x31, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x32, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x3c, 0x2f); /* 47 */
	ds325eu_set(fd, 0x12, 0x3d, 0x3e7); /* 999 */
	ds325eu_set(fd, 0x12, 0x3e, 0xf); /* 15 */
	ds325eu_set(fd, 0x12, 0x3f, 0xf); /* 15 */
	ds325eu_set(fd, 0x12, 0x40, 0x3e8); /* 1000 */
	ds325eu_set(fd, 0x12, 0x43, 0x109); /* 265 */
	ds325eu_set(fd, 0x12, 0x1e, 0x8209); /* 33289 */
	ds325eu_set(fd, 0x12, 0x1d, 0x119); /* 281 */
	ds325eu_set(fd, 0x12, 0x44, 0x1e); /* 30 */
	ds325eu_set(fd, 0x12, 0x1b, reg1b); /* 3328 */
	reg1b |= 0x4000;
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

	/*
	 *	why 4 repeats of the clock divider? theory:
	 *		a clock for laser
	 *		a delayed one for sensor integrator toggle
	 *		a third one to clock sensor readout
	 *		a fourth one to clock ADC
	 *	TODO: verify with a scope
	 *	we should probably not overclock the sensor and ADC
	 *	what's the role of register 0x2f here?
	 */
	ds325eu_set(fd, 0x12, 0x2f, 0x60);
	fdiv = (fdiv<<8) | fdiv;
	ds325eu_set(fd, 0x12, 0x0, fdiv);
	ds325eu_set(fd, 0x12, 0x1, fdiv);
	ds325eu_set(fd, 0x12, 0x2f, 0x60);

	ds325eu_set(fd, 0x12, 0x3, 0x0); /* phase adj? */
	ds325eu_set(fd, 0x12, 0x4, 0x30); /* phase adj? */
	ds325eu_set(fd, 0x12, 0x5, 0x60); /* phase adj? */
	ds325eu_set(fd, 0x12, 0x6, 0x90); /* phase adj? */
	ds325eu_set(fd, 0x12, 0x7, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x8, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x9, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0xa, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x2, 0x0); /* why is this one out of order? */
	ds325eu_set(fd, 0x12, 0xb, 0xea60); /* 60000 */
	ds325eu_set(fd, 0x12, 0xc, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0xd, 0x4740); /* 18240 */
	ds325eu_set(fd, 0x12, 0xe, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0xf, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x10, 0x0); /* 0 */
	ds325eu_set(fd, 0x12, 0x11, 0x1e0); /* 480 */

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
	int opt;

	while((opt = getopt(argc, argv, "dc")) != -1){
		switch (opt) {
		case 'c':
			cflag = 1;
			break;
		case 'd':
			dflag = 1;
			break;
		default:
			fprintf(stderr, "usage: %s [-dc]\nn", argv[0]);
			exit(1);
		}
	}

	x11_fd = x11init();

	color_fd = -1;
	if(!cflag){
		color_fd = devopen("/dev/video0", 30);
		devmap(color_fd, &color_bufs, &color_buf_lens, &ncolor_bufs);
		devstart(color_fd);
	}

	depth_fd = -1;
	if(!dflag){
		int r, fps;
		fps = 25;
		depth_fd = devopen("/dev/video1", fps);
		devmap(depth_fd, &depth_bufs, &depth_buf_lens, &ndepth_bufs);
		devstart(depth_fd);
		for(;;){
			r = ds325eu_get(depth_fd, 0x12, 0x21);
			if(r == 0xffff)
				continue;
			fprintf(stderr, "got %x, now we go!\n", r);
			ds325eu_init(depth_fd, 50000, fps, 0);
			break;
		}
	}

	ds325eu_dump(depth_fd);

	int frame = 0;
	int reg1a, reg1b;
	reg1a = ds325eu_get(depth_fd, 0x12, 0x1a);
	reg1b = ds325eu_get(depth_fd, 0x12, 0x1b);

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
			frame++;
			devinput(depth_fd, depth_bufs);
			if(0 || (frame & 0x3f) == 0x3f){
				reg1b ^= 0x0100;
				fprintf(stderr, "reg1b: %x\n", reg1b);
				ds325eu_set(depth_fd, 0x12, 0x1b, reg1b);
			}
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
