#include <stdio.h>

extern "C"
{
#include "libavformat\avformat.h"
#include "libavutil\time.h"
}

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")

int Error(int res)
{
	char buf[1024] = { 0 };
	av_strerror(res, buf, sizeof(buf));
	printf("error : %s.\n", buf);
	return res;
}

int main(int argc, char* argv[])
{
	char* inUrl = "rtsp://184.72.239.149/vod/mp4://BigBuckBunny_175k.mov";//可以是本地文件
	char* outUrl = "rtmp://123.207.71.137/live/test";

	//初始化所有封装器
	av_register_all();
	
	//初始化网络库
	avformat_network_init();
	
	int res = 0;
	//打开文件，解封装文件头
	//输入封装上下文
	AVFormatContext* ictx = NULL;
	//设置rtsp协议延时最大值
	AVDictionary *opts = NULL;
	av_dict_set(&opts, "max_delay", "500", 0);
	if ((res = avformat_open_input(&ictx, inUrl, NULL, &opts)) != 0)
		return Error(res);

	//获取音视频流信息
	if ((res = avformat_find_stream_info(ictx, NULL)) < 0)
		return Error(res);
	av_dump_format(ictx, 0, inUrl, 0);

	//创建输出上下文
	AVFormatContext* octx = NULL;
	if ((res = avformat_alloc_output_context2(&octx, NULL, "flv", outUrl) < 0))
		return Error(res);

	//配置输出流
	//遍历输入的AVStream
	for (int i = 0; i < ictx->nb_streams; ++i)
	{
		//创建输出流
		AVStream* out = avformat_new_stream(octx, ictx->streams[i]->codec->codec);
		if (out == NULL)
		{
			printf("new stream error.\n");
			return -1;
		}
		//复制配置信息
		if ((res = avcodec_copy_context(out->codec, ictx->streams[i]->codec)) != 0)
			return Error(res);
		//out->codec->codec_tag = 0;//标记不需要重新编解码
	}
	av_dump_format(octx, 0, outUrl, 1);

	//rtmp推流
	//打开io
	//@param s Used to return the pointer to the created AVIOContext.In case of failure the pointed to value is set to NULL.
	res = avio_open(&octx->pb, outUrl, AVIO_FLAG_WRITE);
	if (octx->pb == NULL)
		return Error(res);

	//写入头信息
	//avformat_write_header可能会改变流的timebase
	if ((res = avformat_write_header(octx, NULL)) < 0)
		return Error(res);

	long long  begintime = av_gettime();
	long long  realdts = 0;
	long long  caldts = 0;
	AVPacket pkt;
	while (true)
	{
		if ((res = av_read_frame(ictx, &pkt)) != 0)
			break;
		if(pkt.size <= 0)//读取rtsp时pkt.size可能会等于0
			continue;
		//转换pts、dts、duration
		pkt.pts = pkt.pts * av_q2d(ictx->streams[pkt.stream_index]->time_base) / av_q2d(octx->streams[pkt.stream_index]->time_base);
		pkt.dts = pkt.dts * av_q2d(ictx->streams[pkt.stream_index]->time_base) / av_q2d(octx->streams[pkt.stream_index]->time_base);
		pkt.duration = pkt.duration * av_q2d(ictx->streams[pkt.stream_index]->time_base) / av_q2d(octx->streams[pkt.stream_index]->time_base);
		pkt.pos = -1;//byte position in stream, -1 if unknown

		//文件推流计算延时
		//av_usleep(30 * 1000);
		/*realdts = av_gettime() - begintime;
		caldts = 1000 * 1000 * pkt.pts * av_q2d(octx->streams[pkt.stream_index]->time_base);
		if (caldts > realdts)
			av_usleep(caldts - realdts);*/

		if ((res = av_interleaved_write_frame(octx, &pkt)) < 0)//推流,推完之后pkt的pts，dts竟然都被重置了！而且前面几帧还因为dts没有增长而返回-22错误
			Error(res);

		av_free_packet(&pkt);//回收pkt内部分配的内存
	}
	av_write_trailer(octx);//写文件尾

	system("pause");
	return 0;
}