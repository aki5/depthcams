#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>
#include <fcntl.h>

#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <linux/uvcvideo.h>
#include <linux/usb/video.h>


#include "x11.h"

#define nelem(x) sizeof(x)/sizeof(x[0])

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
	int fd;

	if((fd = open(name, O_RDWR)) == -1) // O_NONBLOCK
		fatal("open %s", name);

	if(ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1)
		fatal("ioctl %s: VIDIOC_QUERYCAP", name);

	unsigned int needcaps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE;
	if((cap.capabilities & needcaps) != needcaps)
		fatal("%s: misses needed capabilities", name);

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

/*
	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
	if(ioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
		fatal("ioctl %s: VIDIOC_S_FMT", name);
*/
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

	req.count = 4; //16;
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

int
ch(int c)
{
	return isprint(c) ? c : '.';
}

int
parsejpeg(uchar *buf, int len)
{
	int off = 0;
	while(off+2 < len){
		int marker, seglen;
		while(off+2 < len){
			if(buf[off] == 0xff && buf[off+1] != 0xff)
				break;
			off++;
		}
		marker = (buf[off] << 8) | buf[off+1];
		seglen = off+4 <= len ? (buf[off+2] << 8) | buf[off+3] : 0;
		switch(marker){
		default:
			off += 2 + seglen;
			break;
		case 0xff00:
		case 0xff01:
		case 0xffd0:
		case 0xffd1:
		case 0xffd2:
		case 0xffd3:
		case 0xffd4:
		case 0xffd5:
		case 0xffd6:
		case 0xffd7:
		case 0xffd8:
			off += 2;
			break;
		case 0xffd9:
			return off+2;
		}
	}
	return -1;
}

typedef struct Bufwork Bufwork;
struct Bufwork {
	struct v4l2_buffer bufd;
	uchar *buf;
	int len;
	int fd;
};

static pthread_t workthr;
static int workup;

void *
devwork(void *arg)
{
	static uchar buf2[1280*240];
	Bufwork *work;
	struct v4l2_buffer *bufd;
	uchar *buf;
	int i, nnz;
	int fd, len;

	work = (Bufwork*)arg;
	buf = work->buf;//bufs[bufd->index];
	bufd = &work->bufd;
	fd = work->fd;
	len = work->len;

	if(fd == depth_fd){
		memcpy(buf2, buf, 1280*240);
		x11bltdmap(buf2, 1280, 240);
	}
	if(fd == color_fd){
		int buflen, jpeglen;
		buflen = len;
		jpeglen = parsejpeg(buf, buflen);

		if(0)fprintf(stderr,
			"jpeglen %x/%x, past:"
			" %02x %02x %02x %02x %02x %02x %02x %02x"
			" %02x %02x %02x %02x %02x %02x %02x %02x"
			"\n",
			jpeglen, buflen,
			buf[jpeglen+0], buf[jpeglen+1], buf[jpeglen+2], buf[jpeglen+3],
			buf[jpeglen+4], buf[jpeglen+5], buf[jpeglen+6], buf[jpeglen+7],
			buf[jpeglen+8], buf[jpeglen+9], buf[jpeglen+10], buf[jpeglen+11],
			buf[jpeglen+12], buf[jpeglen+13], buf[jpeglen+14], buf[jpeglen+15]
		);
		x11jpegframe(buf, jpeglen);
	}

	if(ioctl(fd, VIDIOC_QBUF, bufd) == -1){
		warn("devinput: VIDIOC_QBUF");
		return NULL;
	}

	free(work);
	workup = 0;
	return NULL;
}

void
devinput(int fd, uchar **bufs, int *lens)
{
	Bufwork *work;
	struct v4l2_buffer *bufd;
	int r;

	work = malloc(sizeof work[0]);
	memset(work, 0, sizeof work[0]);
	bufd = &work->bufd;
	bufd->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufd->memory = V4L2_MEMORY_MMAP;

	if(ioctl(fd, VIDIOC_DQBUF, bufd) == -1){
		warn("devinput: VIDIOC_DQBUF, fd %d", fd);
		return;
	}

	if(workup){
		if(ioctl(fd, VIDIOC_QBUF, bufd) == -1){
			warn("devinput: VIDIOC_QBUF");
			return;
		}
		free(work);
		//fprintf(stderr, "shit\n");
		return;
	}

	work->fd = fd;
	work->buf = bufs[bufd->index];
	work->len = lens[bufd->index];

	//if((r = pthread_join(workthr, NULL)) != 0)
	//	;

	workup = 1;
	//pthread_create(&workthr, NULL, devwork, work);
	devwork(work);
}


int
init_cmd(int fd, int send_num)
{
	struct {
		int query;
		char data[7];
	} cmdtab[] = {
		UVC_SET_CUR, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_GET_CUR, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_GET_CUR, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_GET_CUR, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_GET_CUR, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_GET_CUR, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //optional

		UVC_SET_CUR, 0x92, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_GET_CUR, 0x92, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, //optional

		UVC_SET_CUR, 0x12, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x12, 0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, //optional

		UVC_SET_CUR, 0x12, 0x13, 0x00, 0x04, 0x00, 0x00, 0x00, // blacks out, light turns on
		UVC_SET_CUR, 0x12, 0x14, 0x00, 0x00, 0x2c, 0x00, 0x00, // orig: 0x2c super fast but crazy without
		UVC_SET_CUR, 0x12, 0x15, 0x00, 0x01, 0x00, 0x00, 0x00, // goes nuts without
		UVC_SET_CUR, 0x12, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, // optional

		UVC_SET_CUR, 0x12, 0x17, 0x00, 0xef, 0x00, 0x00, 0x00, // orig: 239 (0xef), last scanline(!?)
		UVC_SET_CUR, 0x12, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, // optional

		UVC_SET_CUR, 0x12, 0x19, 0x00, 0x3f, 0x01, 0x00, 0x00, // orig: 319 (0x3f, 0x01), goes nuts if less than 0x3c, last column(!?)

		UVC_SET_CUR, 0x12, 0x1a, 0x00, 0x00, 0x04, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x1b, 0x00, 0x00, 0x01, 0x00, 0x00, // optional

		UVC_SET_CUR, 0x12, 0x1b, 0x00, 0x00, 0x05, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x1b, 0x00, 0x00, 0x0d, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x1c, 0x00, 0x05, 0x00, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x20, 0x00, 0xb0, 0x04, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x27, 0x00, 0x06, 0x01, 0x00, 0x00, // optional

		UVC_SET_CUR, 0x12, 0x28, 0x00, 0xff, 0x01, 0x00, 0x00, // orig: 0x4d, 0x01, 0x4d, 0x02 reduced funny flickers, lights off without. some kind of initial value. where's the auto tune enable then?
		UVC_SET_CUR, 0x12, 0x29, 0x00, 0xf0, 0x00, 0x00, 0x00, // orig: 0xf0, 0x00. hard to see difference

		UVC_SET_CUR, 0x12, 0x2a, 0x00, 0xff, 0x01, 0x00, 0x00, // orig: 0x4d, 0x01, optional. presumably for the missing second laser?
		UVC_SET_CUR, 0x12, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, // optional

		UVC_SET_CUR, 0x12, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, // optional

		UVC_SET_CUR, 0x12, 0x3c, 0x00, 0x4f, 0x00, 0x00, 0x00, // orig: 0x2f, 0x00, lights off without. affects autoadjust. 0x4f seems brighter?
		UVC_SET_CUR, 0x12, 0x3d, 0x00, 0xe7, 0x03, 0x00, 0x00, // orig: 999 (0xe7, 0x03), lights off without 

		UVC_SET_CUR, 0x12, 0x3e, 0x00, 0x0f, 0x00, 0x00, 0x00, // orig: 0x0f, needs to be >= 0x05, unstable without
		UVC_SET_CUR, 0x12, 0x3f, 0x00, 0x0f, 0x00, 0x00, 0x00, // orig: 0x0f, needs to be >= 0x05, unstable without
		UVC_SET_CUR, 0x12, 0x40, 0x00, 0xe8, 0x03, 0x00, 0x00, // orig: 1000 (0xe8, 0x03) optional

		UVC_SET_CUR, 0x12, 0x43, 0x00, 0x09, 0x01, 0x00, 0x00, // orig: 0x09, 0x01 no light without, bigger number causes longer delay for light to come up

		UVC_SET_CUR, 0x12, 0x1e, 0x00, 0x09, 0x82, 0x00, 0x00, // goes nuts without
		UVC_SET_CUR, 0x12, 0x1d, 0x00, 0x19, 0x01, 0x00, 0x00, // goes nuts without

		UVC_SET_CUR, 0x12, 0x44, 0x00, 0x1e, 0x00, 0x00, 0x00, // optional

		UVC_SET_CUR, 0x12, 0x1b, 0x00, 0x00, 0x0d, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x1b, 0x00, 0x00, 0x4d, 0x00, 0x00, // orig: 0x00, 0x4d complete blackout if disabled. highest bit crashes it. needs at least 8+1 to work.

		UVC_SET_CUR, 0x12, 0x45, 0x00, 0x01, 0x01, 0x00, 0x00, // orig: 0x01, 0x01. hard to see difference
		UVC_SET_CUR, 0x12, 0x46, 0x00, 0x02, 0x00, 0x00, 0x00, // orig: 0x02, 0x00. hard to see difference
		UVC_SET_CUR, 0x12, 0x47, 0x00, 0x32, 0x00, 0x00, 0x00, // orig: 0x32, 0x00. hard to see difference

		UVC_SET_CUR, 0x12, 0x2f, 0x00, 0x60, 0x00, 0x00, 0x00, // orig: 0x60, 0x00. hard to see difference

		// this controls the modulation frequency.
		// formula for frequency is 1/x * 600MHz, ie. the default of 0x0c (12) is 50MHz.
		// usable range seems to be from 25MHz to 66MHz (0x18 to 0x08)
		UVC_SET_CUR, 0x12, 0x00, 0x00, 0x0c, 0x0c, 0x00, 0x00, // orig: 0x0c, 0x0c pairs with below. something to do with frequency?
		UVC_SET_CUR, 0x12, 0x01, 0x00, 0x0c, 0x0c, 0x00, 0x00, // orig: 0x0c, 0x0c something to do with frequency

		UVC_SET_CUR, 0x12, 0x2f, 0x00, 0x60, 0x00, 0x00, 0x00, // default 0x60 optional

		UVC_SET_CUR, 0x12, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x04, 0x00, 0x30, 0x00, 0x00, 0x00, // also changes the data
		UVC_SET_CUR, 0x12, 0x05, 0x00, 0x60, 0x00, 0x00, 0x00, // default 0x60 weird modification of data (blue goes missing in phase plot)
		UVC_SET_CUR, 0x12, 0x06, 0x00, 0x90, 0x00, 0x00, 0x00, // default 0x90, noisier?, some kind of wobble. line noise?

		UVC_SET_CUR, 0x12, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x12, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x12, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x12, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, //optional


		UVC_SET_CUR, 0x12, 0x0b, 0x00, 0x60, 0xea, 0x00, 0x00, // orig: 60000 (0x60, 0xea) slow framerate at zero, seems to work if set >60000, but not below
		UVC_SET_CUR, 0x12, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x0d, 0x00, 0x40, 0x47, 0x00, 0x00, // orig: 0x40, 0x47, runs with 0xff,0x48 but no higher. affects brightness of image.

		UVC_SET_CUR, 0x12, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, // optional

		UVC_SET_CUR, 0x12, 0x11, 0x00, 0xe0, 0x02, 0x00, 0x00, // orig: 480 (0xe0, 0x01) weird shit without. needs to be >= 8. funny specks if very small?
		UVC_SET_CUR, 0x12, 0x12, 0x00, 0x04, 0x00, 0x00, 0x00, // orig: 0x04, 0x00 frame rate select. 120fps/x (?), 4 is 30fps.

		UVC_SET_CUR, 0x12, 0x1a, 0x00, 0x00, 0x14, 0x00, 0x00, // matches the one later? optional

		UVC_SET_CUR, 0x12, 0x33, 0x00, 0xf0, 0x70, 0x00, 0x00, // messes up image, weird ass bands?
		UVC_SET_CUR, 0x12, 0x4a, 0x00, 0x02, 0x00, 0x00, 0x00, // optional

		UVC_SET_CUR, 0x12, 0x1a, 0x00, 0x80, 0x14, 0x00, 0x00, // optional
		UVC_SET_CUR, 0x12, 0x1a, 0x00, 0xc0, 0x14, 0x00, 0x00, // 0xc0, 0x14 default, important

		UVC_SET_CUR, 0x92, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_GET_CUR, 0x92, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x92, 0x35, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_GET_CUR, 0x92, 0x35, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x92, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_GET_CUR, 0x92, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_SET_CUR, 0x92, 0x35, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
		UVC_GET_CUR, 0x92, 0x35, 0x00, 0x00, 0x00, 0x00, 0x00, //optional
	};

	if(send_num >= nelem(cmdtab))
		return -1;

	struct uvc_xu_control_query ctrl;
	ctrl.unit = UVC_PU_HUE_CONTROL;
	ctrl.selector = 2;
	ctrl.query = cmdtab[send_num].query;
	ctrl.size = 7;
	ctrl.data = cmdtab[send_num].data; //data + 7*send_num;

	if(ioctl(fd, UVCIOC_CTRL_QUERY, &ctrl) == -1)
		fatal("init_cmd catastropf");

/*
	fprintf(stderr, "init_cmd %02x %02x %02x %02x %02x %02x %02x\n",
		cmdtab[send_num].data[0],
		cmdtab[send_num].data[1],
		cmdtab[send_num].data[2],
		cmdtab[send_num].data[3],
		cmdtab[send_num].data[4],
		cmdtab[send_num].data[5],
		cmdtab[send_num].data[6]
	);
*/

	return 0;
}

int
main(void)
{
	double st0, et0, fps0;
	double st1, et1, fps1;
	uchar **color_bufs;
	uchar **depth_bufs;
	int *color_buf_lens;
	int *depth_buf_lens;
	int ncolor_bufs;
	int ndepth_bufs;

	x11_fd = x11init();

	color_fd = -1;
	//color_fd = devopen("/dev/video0", 30);
	//devmap(color_fd, &color_bufs, &color_buf_lens, &ncolor_bufs);
	//devstart(color_fd);

	depth_fd = devopen("/dev/video1", 30);
	devmap(depth_fd, &depth_bufs, &depth_buf_lens, &ndepth_bufs);
	devstart(depth_fd);

	//tcpinit();

	st1 = st0 = tsec();	
	fps1 = fps0 = 0.0;
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
		//max_fd = tcpfdset(&rset, &wset);
		//max_fd = color_fd;
		//max_fd = depth_fd;
		max_fd = color_fd > max_fd ? color_fd : max_fd;
		max_fd = depth_fd > max_fd ? depth_fd : max_fd;
		max_fd = x11_fd > max_fd ? x11_fd : max_fd;

		if(select(max_fd+1, &rset, NULL, NULL, &tv) == -1)
			warn("select");

		if(depth_fd != -1 && FD_ISSET(depth_fd, &rset)){
			static int off;
			if(init_cmd(depth_fd, off) == 0){
				off++;
			}
			devinput(depth_fd, depth_bufs, depth_buf_lens);
			et0 = tsec();
			fps0 = 0.95*fps0 + 0.05*(1.0/(et0-st0));
			//fprintf(stderr, "depth fps %f\n", fps0);
			st0 = et0;
		}

		if(color_fd != -1 && FD_ISSET(color_fd, &rset)){
			devinput(color_fd, color_bufs, color_buf_lens);
			et1 = tsec();
			fps1 = 0.95*fps1 + 0.05*(1.0/(et1-st1));
			//fprintf(stderr, "color fps %f\n", fps1);
			st1 = et1;
		}

		//tcpserve(&rset, &wset);
		if(x11_fd != -1 && FD_ISSET(x11_fd, &rset))
			x11serve(x11_fd);

	}

	devstop(color_fd);
	devstop(depth_fd);

	close(color_fd);
	close(depth_fd);

	return 0;
}
