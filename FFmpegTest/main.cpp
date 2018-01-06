#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

/*
#define __STDC_CONSTANT_MACROS
#ifndef INT64_C
#define INT64_C(c) (c ## LL)
#define UINT64_C(c) (c ## ULL)
#endif
*/

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
}

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avdevice.lib")
#pragma comment(lib, "avfilter.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "postproc.lib")
#pragma comment(lib, "swresample.lib")
#pragma comment(lib, "swscale.lib")

#define INPUT "in.flv"
#define OUTPUT "out.yuv"

int main()
{
	int res = 0;
	int videoStream = -1;//标记视频流的编号
	char errBuf[BUFSIZ] = { 0 };
	FILE* fp_out = fopen(OUTPUT, "wb+");

	//初始化FFMPEG  调用了这个才能正常适用编码器和解码器
	av_register_all();
	printf("FFmpeg's version is: %d\n", avcodec_version());

	//FFMPEG所有的操作都要通过这个AVFormatContext来进行
	AVFormatContext* pFormatCtx = NULL;

	//打开输入视频文件
	//Open an input stream and read the header. The codecs are not opened.
	if ((res = avformat_open_input(&pFormatCtx, INPUT, NULL, NULL)) < 0)
	{
		av_strerror(res, errBuf, sizeof(errBuf));
		printf("%s\n", errBuf);
		return -1;
	}
	//Read packets of a media file to get stream information. This is useful for file formats with no headers such as MPEG.
	//相当于对输入进行 “预处理”
	avformat_find_stream_info(pFormatCtx, NULL);
	av_dump_format(pFormatCtx, 0, NULL, 0); //输出视频流的信息

	//查找流
	for (int i = 0; i < pFormatCtx->nb_streams; ++i)
	{
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			videoStream = i;
	}
	if (videoStream == -1)
	{
		printf("Didn't find a video stream.\n");
		return -1;
	}

	///查找解码器    
	AVCodecContext* pCodecCtx = pFormatCtx->streams[videoStream]->codec;
	AVCodec* pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL)
	{
		printf("Codec not found.\n");
		return -1;
	}

	///打开解码器
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		printf("Could not open codec.");
		return -1;
	}

	AVFrame Frame = { 0 };//不初始化，avcodec_decode_video2会报错
	AVPacket packet;
	int got_picture;
	while (1)
	{
		//读取视频帧
		//return 0 if OK, < 0 on error or end of file
		if (av_read_frame(pFormatCtx, &packet) < 0)
		{
			break; //这里认为视频读取完了
		}
		if (packet.stream_index == videoStream)
		{
			//解码视频帧
			if (avcodec_decode_video2(pCodecCtx, &Frame, &got_picture, &packet) < 0)
			{
				printf("decode error.\n");
				return -1;
			}
			if (got_picture)
			{
				if (Frame.format == PIX_FMT_YUV420P)
				{
					//解码后YUV格式的视频像素数据保存在AVFrame的data[0]、data[1]、data[2]中。
					//但是这些像素值并不是连续存储的，每行有效像素之后存储了一些无效像素。
					//以亮度Y数据为例，data[0]中一共包含了linesize[0] * height个数据。
					//但是出于优化等方面的考虑，linesize[0]实际上并不等于宽度width，而是一个比宽度大一些的值。
					fwrite(Frame.data[0], Frame.linesize[0] * Frame.height, 1, fp_out);
					fwrite(Frame.data[1], Frame.linesize[1] * Frame.height / 2, 1, fp_out);
					fwrite(Frame.data[2], Frame.linesize[2] * Frame.height / 2, 1, fp_out);
				}
			}
		}
		av_free_packet(&packet);//清除packet里面指向的缓冲区
	}

	fclose(fp_out);
	avcodec_close(pCodecCtx);//关闭解码器
	avformat_close_input(&pFormatCtx);//关闭输入视频文件。avformat_free_context(pFormatCtx);就不需要了
	return 0;
}