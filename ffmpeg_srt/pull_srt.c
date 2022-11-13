/**
 从文件或者服务器拉取流(h264),并解编码显示，代码中只实现了视频流，没有实现音频流。
 目前拉流端基本可以做到实时,推测延时主要产生于推流端。
 */

#include <stdio.h>
#define __STDC_CONSTANT_MACROS
//Linux...
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#include "SDL/SDL.h"  //依赖libSDL.so库

//自定义事件（SDL_USEREVENT+n），只要枚举值不超过0xFFFF即可
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)


//等于1时进程结束，退出进程
int thread_exit=0;

int main(int argc, char* argv[])
{
	AVFormatContext	*pFormatCtx;
	AVCodecContext	*pCodecCtx;
	AVPacket *packet=(AVPacket *)av_malloc(sizeof(AVPacket));
	int videoindex=-1;

    // char inpath[]="/home/zhaofachuan/works/readdemo/data/cuc_ieschool.h264";
    // char inpath[]="/home/zhaofachuan/works/readdemo/data/cuc_ieschool.ts";
	char inpath[] = "srt://127.0.0.1:4201?pkt_size=1316";
	
	avdevice_register_all();//注册libavdevice
    avformat_network_init();//支持网络流

	AVDictionary *options = NULL;
	av_dict_set(&options, "fflags", "nobuffer", 0);//减少缓冲,降低延时
	pFormatCtx = avformat_alloc_context();//初始化AVFormatContext
	if (avformat_open_input(&pFormatCtx,inpath, NULL, &options) != 0){//打开文件或网络流
		printf("Couldn't open input stream.\n");
		return -1;
	}

	//接收网络ts流时，需要对AVFormatContext作设置，一般靠avformat_find_stream_info获取流的信息来给AVFormatContext作设置
	//缺点：在执行avformat_find_stream_info时会出现等待时间过长的情况,读取到非I帧还会出现on-existing PPS 0 referenced错误
	// while(av_read_frame(pFormatCtx, packet)>=0){//跳过非I帧率，以免sps、pps解析错误
	// 	if(packet->flags==0){//找到最后一个非I帧，这样avformat_find_stream_info获取的才是I帧率
	// 		av_packet_unref(packet);
	// 		break;
	// 	}
	// 	av_packet_unref(packet);
	// }

	// 读取媒体文件的音视频包去获取流信息，只有I帧上才有完整信息，所以必须读取I帧以获取pps和sps头；
	if(avformat_find_stream_info(pFormatCtx,NULL)<0){
		printf("Couldn't find stream information.\n");
		return -1;
	}

	//找到视频流
	for(int i=0; i<(int)pFormatCtx->nb_streams; i++){
		if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO)
		{
			videoindex=i;
			break;
		}
	}
	if(videoindex==-1){
		printf("Couldn't find a video stream.\n");
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
	//选择解码器
	AVCodec	*pCodec=(AVCodec *)avcodec_find_decoder(pCodecCtx->codec_id);
	if(pCodec==NULL){
		printf("Codec not found.\n");
		return -1;
	}

	//利用pCodec初始化一个音视频编解码器pCodecCtx（AVCodecContext）
	if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
		printf("Could not open codec.\n");
		return -1;
	}

	//SDL----------------------------
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {  //初始化SDL库
		printf( "Could not initialize SDL - %s\n", SDL_GetError()); 
		return -1;
	}
	int screen_w=0,screen_h=0;
	SDL_Surface *screen;
	screen_w = pCodecCtx->width;
	screen_h = pCodecCtx->height;
	screen = SDL_SetVideoMode(screen_w, screen_h, 0,0);//设置窗口模式

	if(!screen) {
		printf("SDL: could not set video mode - exiting:%s\n",SDL_GetError());  
		return -1;
	}
	
	SDL_Overlay *bmp;
	bmp = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height,SDL_YV12_OVERLAY, screen);
	//三个参数表示创建的overlay将接收 YV12 类型的数据，应通过 FFmpeg 库将源视频数据转换为对应类型
	SDL_Rect rect;
	rect.x = 0;
	rect.y = 0;
	rect.w = screen_w;
	rect.h = screen_h;
	//SDL End------------------------

	struct SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL); 
	//转换成AV_PIX_FMT_YUV420P格式
	//参数1：被转换源的宽、参数2：被转换源的高、参数3：被转换源的格式，eg：YUV、RGB等
	//参数4：转换后指定的宽、参数5：转换后指定的高、参数6：转换后指定的格式同参数3的格式、参数7：转换所使用的算法
	SDL_WM_SetCaption("Read Camera",NULL);//设置窗口的标题和ICON图标

    int ret;
    //定义AVFrame
	AVFrame	*pFrame,*pFrameYUV420;
	pFrame=av_frame_alloc();
	pFrameYUV420=av_frame_alloc();

	for (;;) {
        SDL_Event event;
		SDL_PollEvent(&event);//查看事件队列，如果事件队列中有事件，直接返回并删除,如果没有也直接返回
        if(event.type==SDL_QUIT){
            break;
        }else{
            if(av_read_frame(pFormatCtx, packet)>=0){
				if(packet->stream_index==videoindex){
					ret = avcodec_send_packet(pCodecCtx, packet);//发送packet到解码队列中
					if(ret < 0){
						printf("Decode Error.\n");
						return -1;
					}
					av_packet_unref(packet);// 解除引用，防止不必要的内存占用
					while(avcodec_receive_frame(pCodecCtx,pFrame)==0) { //循环获取AVFrame解码数据
						SDL_LockYUVOverlay(bmp);//对YUV加锁
						pFrameYUV420->data[0]=bmp->pixels[0];//将转码后的图像与画布的像素缓冲器关联
						pFrameYUV420->data[1]=bmp->pixels[2];
						pFrameYUV420->data[2]=bmp->pixels[1];
						pFrameYUV420->linesize[0]=bmp->pitches[0];//将转码后的图像扫描行长度与画布像素缓冲区的扫描行长度相关联
						pFrameYUV420->linesize[1]=bmp->pitches[2];
						pFrameYUV420->linesize[2]=bmp->pitches[1];
						sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV420->data, pFrameYUV420->linesize);
						//sws_scale将图像数据转换为 YUV420P，转换后的数据被塞入pFrameYUV422,pFrameYUV422在前面已经与overlay建立联系。
						SDL_UnlockYUVOverlay(bmp);//对YUV解锁
						SDL_DisplayYUVOverlay(bmp, &rect); //负责显示图片
						// rect 结构体指定播放区域的位置和缩放尺寸
					}
				}
            }
        }
	}

	SDL_Quit();
	av_packet_free(&packet);//释放AVPacket
	sws_freeContext(img_convert_ctx);//释放img_convert_ctx
	av_frame_free(&pFrameYUV420);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	return 0;
}




