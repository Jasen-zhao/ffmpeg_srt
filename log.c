/*
读取摄像头设备的数据并h256编码
*/

#include <stdio.h>
#include <unistd.h>
#define __STDC_CONSTANT_MACROS
//Linux...
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>

//帧率
int FPS=25;

int main(int argc, char* argv[])
{
	AVFormatContext	*pFormatCtx;
	int				i, videoindex;
	AVCodecContext	*pCodecCtx;//编码
	
	avdevice_register_all();//注册所有libavdevice
	pFormatCtx = avformat_alloc_context();
 
    //Linux下打开输入设备
	AVInputFormat *ifmt=(AVInputFormat *)av_find_input_format("video4linux2");
	if(avformat_open_input(&pFormatCtx,"/dev/video0",ifmt,NULL)!=0){
		printf("Couldn't open input stream.\n");
		return -1;
	}
	
    //检查是否从输入设备获取输入流
	if(avformat_find_stream_info(pFormatCtx,NULL)<0){
		printf("Couldn't find stream information.\n");
		return -1;
	}
	videoindex=-1;
	for(i=0; i<pFormatCtx->nb_streams; i++){
		if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO){
			videoindex=i;
			break;
		}
	}
	if(videoindex==-1){
		printf("Couldn't find a video stream.\n");
		return -1;
	}

    //选择编码器
    AVCodec	*pCodec=(AVCodec *)avcodec_find_encoder(AV_CODEC_ID_H265);
	if(pCodec==NULL){
		printf("Codec not found.\n");
		return -1;
	}
    
    //创建AVCodecContext结构体
	pCodecCtx = avcodec_alloc_context3(NULL);
    if (pCodecCtx == NULL){  
		printf("Could not allocate AVCodecContext \n");  
		return -1;  
    }
    //将AVCodecParameters结构体中码流参数拷贝到AVCodecContext结构体
    avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar); 
    pCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;                 //加上此标识可以从extradata获取sps pps
    pCodecCtx->codec_id = pCodec->id;                                //编码器类型h265
    pCodecCtx->width = pFormatCtx->streams[videoindex]->codecpar->width; //输入图像的宽度
    pCodecCtx->height = pFormatCtx->streams[videoindex]->codecpar->height; //输入图像的高度
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;//输入图像的像素格式AV_PIX_FMT_YUV420P
    pCodecCtx->bit_rate = pFormatCtx->streams[videoindex]->codecpar->bit_rate; //码率
    pCodecCtx->framerate = pFormatCtx->streams[videoindex]->avg_frame_rate; //帧率
    pCodecCtx->time_base = pFormatCtx->streams[videoindex]->time_base; //timebase，pts的单位
    pCodecCtx->max_b_frames = 0;                                     //b帧的数量

    //初始化音视频编解码器pCodecCtx
	if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
		printf("Could not open codec.\n");
		return -1;
	}

    // 指定转换上下文，摄像头采集到的数据是yuyv格式(属于yuv422)，而X265在进行编码的时候需要标准的YUV，
	// 所以需要一个yuv422toyuv420的转换
    struct SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUYV422, //输入
                                    pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, //输出
                                    SWS_BICUBIC, NULL, NULL, NULL); 
	int ret;
	AVPacket *packet=(AVPacket *)av_malloc(sizeof(AVPacket));

    //定义pFrameYUYV422和pFrameYUV420
    AVFrame	*pFrameYUYV422,*pFrameYUV420;
	pFrameYUYV422=av_frame_alloc();
	pFrameYUV420=av_frame_alloc();
	for (;;) {
        if(av_read_frame(pFormatCtx, packet)>=0){
            if(packet->stream_index==videoindex){

                ret = avcodec_send_packet(pCodecCtx, packet);//解码
                if(ret < 0){
                    printf("Decode Error.\n");
                    return -1;
                }
                av_packet_unref(packet);// 解除引用，防止不必要的内存占用
                while(avcodec_receive_frame(pCodecCtx,pFrameYUYV422)==0) { //循环获取AVFrame解码数据
                    // printf("%lu \n",sizeof(pFrameYUYV422->data));//不加\n是不会自动刷新输出流,直至缓存被填满。
                    sws_scale(img_convert_ctx, (uint8_t const **)pFrameYUYV422->data,pFrameYUYV422->linesize,
                                0,pFrameYUYV422->height, pFrameYUV420->data,pFrameYUV420->linesize);
                }
            }
        }
	}

	av_packet_free(&packet);//释放AVPacket
	av_frame_free(&pFrameYUYV422);
	av_frame_free(&pFrameYUV420);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	return 0;
}