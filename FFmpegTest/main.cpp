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
#include "libswresample/swresample.h"
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
#define OUTVIDEO "video.yuv"
#define OUTAUDIO "audio.pcm"
#define OUTRGB "video.rgb"
#define OUTS16 "audios16.pcm"

int main()
{
	int res = 0;
	int videoStream = -1;//标记视频流的编号
	int audioStream = -1;//标记音频流的编号
	char errBuf[BUFSIZ] = { 0 };
	FILE* fp_video = fopen(OUTVIDEO, "wb+");
	FILE* fp_audio = fopen(OUTAUDIO, "wb+");
	FILE* fp_rgb = fopen(OUTRGB, "wb+");
	FILE* fp_s16 = fopen(OUTS16, "wb+");

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
		else if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
			audioStream = i;
	}
	if (videoStream == -1)
	{
		printf("Didn't find a video stream.\n");
		return -1;
	}
	if (audioStream == -1)
	{
		printf("Didn't find a audio stream.\n");
		return -1;
	}

	///查找解码器    
	AVCodecContext* pVCodecCtx = pFormatCtx->streams[videoStream]->codec;
	AVCodec* pVCodec = avcodec_find_decoder(pVCodecCtx->codec_id);
	if (pVCodec == NULL)
	{
		printf("Video Codec not found.\n");
		return -1;
	}
	AVCodecContext* pACodecCtx = pFormatCtx->streams[audioStream]->codec;
	AVCodec* pACodec = avcodec_find_decoder(pACodecCtx->codec_id);
	if (pACodec == NULL)
	{
		printf("Audio Codec not found.\n");
		return -1;
	}

	///打开解码器
	if (avcodec_open2(pVCodecCtx, pVCodec, NULL) < 0)
	{
		printf("Could not open Video codec.\n");
		return -1;
	}
	if (avcodec_open2(pACodecCtx, pACodec, NULL) < 0)
	{
		printf("Could not open Audio codec.\n");
		return -1;
	}

	AVFrame Frame = { 0 };//不初始化，avcodec_decode_video2会报错
	AVFrame rgbFrame;
	AVPacket packet;
	int got_picture;

	int rgbsize = avpicture_get_size(PIX_FMT_RGB24, pVCodecCtx->width, pVCodecCtx->height);//算出该格式和分辨率下一帧图像的数据大小
	//uint8_t* rgbBuffer = (uint8_t *)av_malloc(rgbsize * sizeof(uint8_t));//分配保存图像的内存
	//avpicture_fill((AVPicture *)&rgbFrame, rgbBuffer, PIX_FMT_RGB24, pVCodecCtx->width, pVCodecCtx->height);//将自己分配的内存绑定到rgbFrame的data数据区
	avpicture_alloc((AVPicture *)&rgbFrame, PIX_FMT_RGB24, pVCodecCtx->width, pVCodecCtx->height);//为rgbFrame的data分配内存，不用自己分配
	SwsContext *img_convert_ctx = sws_getContext(pVCodecCtx->width, pVCodecCtx->height, AV_PIX_FMT_YUV420P, pVCodecCtx->width, pVCodecCtx->height, PIX_FMT_RGB24, SWS_FAST_BILINEAR, NULL, NULL, NULL);//转换上下文

	struct SwrContext* swr_covert_ctx = swr_alloc_set_opts(NULL, av_get_default_channel_layout(pACodecCtx->channels), AV_SAMPLE_FMT_S16, pACodecCtx->sample_rate, av_get_default_channel_layout(pACodecCtx->channels), pACodecCtx->sample_fmt, pACodecCtx->sample_rate, 0, NULL);//转换上下文
	swr_init(swr_covert_ctx);//初始化上下文
	int samplessize = av_samples_get_buffer_size(NULL, pACodecCtx->channels, pACodecCtx->sample_rate, AV_SAMPLE_FMT_S16, 1);//计算1s的数据大小，使缓冲区足够大
	uint8_t* sambuf = (uint8_t*)av_mallocz(samplessize);

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
			if (avcodec_decode_video2(pVCodecCtx, &Frame, &got_picture, &packet) < 0)
			{
				printf("decode Video error.\n");
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
					fwrite(Frame.data[0], Frame.linesize[0] * Frame.height, 1, fp_video);
					fwrite(Frame.data[1], Frame.linesize[1] * Frame.height / 2, 1, fp_video);
					fwrite(Frame.data[2], Frame.linesize[2] * Frame.height / 2, 1, fp_video);

					sws_scale(img_convert_ctx, (uint8_t const* const *)Frame.data, Frame.linesize, 0, pVCodecCtx->height, rgbFrame.data, rgbFrame.linesize);//转换
					fwrite(rgbFrame.data[0], rgbsize, 1, fp_rgb);
				}
			}
		}
		else if (packet.stream_index == audioStream)
		{
			//解码音频帧
			if (avcodec_decode_audio4(pACodecCtx, &Frame, &got_picture, &packet) < 0)
			{
				printf("decode Audio error.\n");
				return -1;
			}
			if (got_picture)
			{
				if (Frame.format == AV_SAMPLE_FMT_S16P)//signed 16 bits, planar 16位 平面数据
				{
					//AV_SAMPLE_FMT_S16P
					//代表每个data[]的数据是连续的（planar），每个单位是16bits
					for (int i = 0; i < Frame.linesize[0]; i += 2)
					{
						//如果是多通道的话，保存成c1低位、c1高位、c2低位、c2高位...
						for (int j = 0; j < Frame.channels; ++j)
							fwrite(Frame.data[j] + i, 2, 1, fp_audio);
					}
				}
				else if (Frame.format == AV_SAMPLE_FMT_FLTP)
				{
					for (int i = 0; i < Frame.linesize[0]; i += 4)
					{
						for (int j = 0; j < Frame.channels; ++j)
							fwrite(Frame.data[j] + i, 4, 1, fp_audio);
					}
				}

				int samplenums = swr_convert(swr_covert_ctx, &sambuf, samplessize, (const uint8_t **)Frame.data, Frame.nb_samples);//转换，返回每个通道的样本数
				fwrite(sambuf, av_samples_get_buffer_size(NULL, Frame.channels, samplenums, AV_SAMPLE_FMT_S16, 1), 1, fp_s16);
			}
		}
		av_free_packet(&packet);//清除packet里面指向的缓冲区
	}

	fclose(fp_video);
	fclose(fp_audio);
	fclose(fp_rgb);
	fclose(fp_s16);
	avpicture_free((AVPicture*)&rgbFrame);//释放avpicture_alloc分配的内存
	swr_free(&swr_covert_ctx);//释放swr_alloc_set_opts分配的转换上下文
	av_free(sambuf);
	avcodec_close(pVCodecCtx);//关闭解码器
	avcodec_close(pACodecCtx);
	avformat_close_input(&pFormatCtx);//关闭输入视频文件。avformat_free_context(pFormatCtx);就不需要了
	return 0;
}