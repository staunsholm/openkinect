/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2010 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <libusb.h>
#include "libfreenect.h"

#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>

#include <math.h>

int g_argc;
char **g_argv;

freenect_context *f_ctx;
freenect_device *f_dev;

int got_frames = 0;

#define AS3_BITMAPDATA_LEN 640 * 480 * 4

struct sockaddr_in si_depth, si_rgb;
pthread_t depth_thread, rgb_thread;
pthread_mutex_t depth_mutex	= PTHREAD_MUTEX_INITIALIZER,
rgb_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t depth_cond = PTHREAD_COND_INITIALIZER,
rgb_cond = PTHREAD_COND_INITIALIZER;
char *conf_ip		= "127.0.0.1";
int s_depth			= -1,
s_rgb			= -1,
conf_port_depth	= 6001,
conf_port_rgb	= 6002;

uint8_t buf_depth[AS3_BITMAPDATA_LEN];
uint8_t	buf_rgb[AS3_BITMAPDATA_LEN];

int die = 0;
int depth_child;
int depth_connected = 0;
int rgb_child;
int rgb_connected = 0;

void send_policy_file(int child){
	int n;
	char * str = "<?xml version='1.0'?><!DOCTYPE cross-domain-policy SYSTEM '/xml/dtds/cross-domain-policy.dtd'><cross-domain-policy><site-control permitted-cross-domain-policies='all'/><allow-access-from domain='*' to-ports='*'/></cross-domain-policy>\n";
	n = write(child,str , 237);
	if ( n < 0 || n != 237)
	{
		fprintf(stderr, "Error on write() for depth (%d instead of %d)\n",n, 237);
		//break;
	}
}

void *network_depth(void *arg)
{
	int childlen;
	struct sockaddr_in childaddr;
	
	childlen = sizeof(childaddr);
	while ( !die )
	{
		printf("### Wait depth client\n");
		depth_child = accept(s_depth, (struct sockaddr *)&childaddr, (unsigned int *)&childlen);
		if ( network_depth < 0 )
		{
			fprintf(stderr, "Error on accept() for depth, exit depth thread.\n");
			break;
		}
		
		printf("### Got depth client\n");
		send_policy_file(depth_child);
		freenect_start_depth(f_dev);
		depth_connected = 1;
	}
	
	return NULL;
}

void *network_rgb(void *arg)
{
	int childlen;
	struct sockaddr_in childaddr;
	
	childlen = sizeof(childaddr);
	while ( !die )
	{
		printf("### Wait rgb client\n");
		rgb_child = accept(s_rgb, (struct sockaddr *)&childaddr, (unsigned int *)&childlen);
		if ( rgb_child < 0 )
		{
			fprintf(stderr, "Error on accept() for rgb, exit rgb thread.\n");
			break;
		}
		
		printf("### Got rgb client\n");
		send_policy_file(rgb_child);
		freenect_start_rgb(f_dev);
		rgb_connected = 1;
	}
	
	return NULL;
}

int network_init()
{
	int optval = 1;
	
	signal(SIGPIPE, SIG_IGN);
	
	if ( (s_depth = socket(AF_INET, SOCK_STREAM, 0)) == -1 )
	{
		fprintf(stderr, "Unable to create depth socket\n");
		return -1;
	}
	
	if ( (s_rgb = socket(AF_INET, SOCK_STREAM, 0)) == -1 )
	{
		fprintf(stderr, "Unable to create rgb socket\n");
		return -1;
	}
	
	setsockopt(s_depth, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(optval));
	setsockopt(s_rgb, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(optval));
	
	memset((char *) &si_depth, 0, sizeof(si_depth));
	memset((char *) &si_rgb, 0, sizeof(si_rgb));
	
	si_depth.sin_family			= AF_INET;
	si_depth.sin_port			= htons(conf_port_depth);
	si_depth.sin_addr.s_addr	= inet_addr(conf_ip);
	
	si_rgb.sin_family			= AF_INET;
	si_rgb.sin_port				= htons(conf_port_rgb);
	si_rgb.sin_addr.s_addr		= inet_addr(conf_ip);
	
	if ( bind(s_depth, (struct sockaddr *)&si_depth,
			  sizeof(si_depth)) < 0 )
	{
		fprintf(stderr, "Error at bind() for depth\n");
		return -1;
	}
	
	if ( bind(s_rgb, (struct sockaddr *)&si_rgb,
			  sizeof(si_rgb)) < 0 )
	{
		fprintf(stderr, "Error at bind() for rgb\n");
		return -1;
	}
	
	if ( listen(s_depth, 1) < 0 )
	{
		fprintf(stderr, "Error on listen() for depth\n");
		return -1;
	}
	
	if ( listen(s_rgb, 1) < 0 )
	{
		fprintf(stderr, "Error on listen() for rgb\n");
		return -1;
	}
	
	/* launch 2 thread for each images
	 */
	
	if ( pthread_create(&depth_thread, NULL, network_depth, NULL) )
	{
		fprintf(stderr, "Error on pthread_create() for depth\n");
		return -1;
	}
	
	if ( pthread_create(&rgb_thread, NULL, network_rgb, NULL) )
	{
		fprintf(stderr, "Error on pthread_create() for rgb\n");
		return -1;
	}
	
	return 0;
}

void network_close()
{
	die = 1;
	if ( s_depth != -1 )
		close(s_depth), s_depth = -1;
	if ( s_rgb != -1 )
		close(s_rgb), s_rgb = -1;
}

uint16_t t_gamma[2048];

void depthimg(freenect_device *dev, freenect_depth *depth, uint32_t timestamp)
{
	int i, n;
	
	pthread_mutex_lock(&depth_mutex);
	for (i=0; i<FREENECT_FRAME_PIX; i++) {
		int pval = t_gamma[depth[i]];
		int lb = pval & 0xff;
		switch (pval>>8) {
			case 0:
				buf_depth[4 * i + 0] = 255;
				buf_depth[4 * i + 1] = 255-lb;
				buf_depth[4 * i + 2] = 255-lb;
				break;
			case 1:
				buf_depth[4 * i + 0] = 255;
				buf_depth[4 * i + 1] = lb;
				buf_depth[4 * i + 2] = 0;
				break;
			case 2:
				buf_depth[4 * i + 0] = 255-lb;
				buf_depth[4 * i + 1] = 255;
				buf_depth[4 * i + 2] = 0;
				break;
			case 3:
				buf_depth[4 * i + 0] = 0;
				buf_depth[4 * i + 1] = 255;
				buf_depth[4 * i + 2] = lb;
				break;
			case 4:
				buf_depth[4 * i + 0] = 0;
				buf_depth[4 * i + 1] = 255-lb;
				buf_depth[4 * i + 2] = 255;
				break;
			case 5:
				buf_depth[4 * i + 0] = 0;
				buf_depth[4 * i + 1] = 0;
				buf_depth[4 * i + 2] = 255-lb;
				break;
			default:
				buf_depth[4 * i + 0] = 0;
				buf_depth[4 * i + 1] = 0;
				buf_depth[4 * i + 2] = 0;
				break;
		}
		buf_depth[4 * i + 3] = 0x00;
	}
	got_frames++;

	if ( depth_connected == 1 )
	{
		n = write(depth_child, buf_depth, AS3_BITMAPDATA_LEN);
		if ( n < 0 || n != AS3_BITMAPDATA_LEN )
		{
			fprintf(stderr, "Error on write() for depth (%d instead of %d)\n",n, AS3_BITMAPDATA_LEN);
			//break;
		}
	}
	pthread_cond_signal(&depth_cond);
	pthread_mutex_unlock(&depth_mutex);
}

void rgbimg(freenect_device *dev, freenect_pixel *rgb, uint32_t timestamp)
{
	int n;
	pthread_mutex_lock(&depth_mutex);
	got_frames++;
	//memcpy(buf_rgb, rgb, FREENECT_RGB_SIZE);
	int x;
	for (x=0; x<640 * 480; x++) {
				buf_rgb[4 * x + 0] = rgb[3 * x + 0];
				buf_rgb[4 * x + 1] = rgb[3 * x + 1];
				buf_rgb[4 * x + 2] = rgb[3 * x + 2];
				buf_rgb[4 * x + 3] = 0x00;
	}
	
	if ( rgb_connected == 1 )
	{
		n = write(rgb_child, buf_rgb, AS3_BITMAPDATA_LEN);
		if ( n < 0 || n != AS3_BITMAPDATA_LEN )
		{
			fprintf(stderr, "Error on write() for rgb (%d instead of %d)\n",n, AS3_BITMAPDATA_LEN);
			//break;
		}
	}
	
	pthread_cond_signal(&depth_cond);
	pthread_mutex_unlock(&depth_mutex);
}

int main(int argc, char **argv)
{
	//int res;
	
	printf("Kinect camera test\n");
	
	int i;
	for (i=0; i<2048; i++) {
		float v = i/2048.0;
		v = powf(v, 3)* 6;
		t_gamma[i] = v*6*256;
	}
	
	g_argc = argc;
	g_argv = argv;
	
	if (freenect_init(&f_ctx, NULL) < 0) {
		printf("freenect_init() failed\n");
		return 1;
	}
	
	if (freenect_open_device(f_ctx, &f_dev, 0) < 0) {
		printf("Could not open device\n");
		return 1;
	}
	
	if ( network_init() < 0 )
		return -1;
	
	freenect_set_depth_callback(f_dev, depthimg);
	freenect_set_rgb_callback(f_dev, rgbimg);
	freenect_set_rgb_format(f_dev, FREENECT_FORMAT_RGB);
	
	//res = pthread_create(&gl_thread, NULL, gl_threadfunc, NULL);
	//if (res) {
	//	printf("pthread_create failed\n");
	//	return 1;
	//}
	
	//freenect_start_depth(f_dev);
	//freenect_start_rgb(f_dev);
	
	while(!die && freenect_process_events(f_ctx) >= 0 );
	
	network_close();
	
	printf("-- done!\n");
	
	pthread_exit(NULL);
}
